// Copyright 2026 Yusheng Peng. SPDX-License-Identifier: Apache-2.0
#include "contracts.hpp"
#include "minimal_node.hpp"
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/u_int8_multi_array.hpp>
#include <chrono>
#include <csignal>
#include <dlfcn.h>
#include <execinfo.h>
#include <fstream>
#include <exception>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_set>
#include <rmw/rmw.h>
using Clock = std::chrono::steady_clock;
using Msg = std_msgs::msg::UInt8MultiArray;
static volatile std::sig_atomic_t stop_requested=0;
static std::weak_ptr<rclcpp::Context> test_context;
static void on_signal(int) { stop_requested=1; }
static void crash_trace(int signal_number) {
  void *frames[48];
  int count=backtrace(frames,48);
  backtrace_symbols_fd(frames,count,2);
  std::signal(signal_number,SIG_DFL);
  std::raise(signal_number);
}
static bool active() { auto context=test_context.lock(); return !stop_requested && context && context->is_valid(); }
struct ShutdownAfterEntities {
  rclcpp::Context::SharedPtr context;
  ~ShutdownAfterEntities() { if(context->is_valid()) context->shutdown("benchmark completed"); }
};
struct JoinReceiver {
  std::thread &thread;
  rclcpp::executors::SingleThreadedExecutor &executor;
  ~JoinReceiver() { if(thread.joinable()) {executor.cancel();thread.join();} }
};
static double us(Clock::duration d) { return std::chrono::duration<double, std::micro>(d).count(); }
static uint64_t number(const char *s) {
  const std::string v(s); size_t end=0;
  if (v.empty() || v[0]=='-') throw std::invalid_argument("unsigned argument");
  auto n=std::stoull(v, &end); if(end!=v.size()) throw std::invalid_argument("numeric argument"); return n;
}
int main(int argc, char **argv) {
  try {
    if (argc != 15) { std::cerr << "role topic bytes qos depth count warmup seconds timeout_ms rate slow_ms run output ready_file\n"; return 2; }
    std::string role=argv[1], topic=argv[2], qos_name=argv[4];
    bool duplex=std::getenv("DDS_BENCH_DUPLEX")!=nullptr;
    if(duplex && role!="ping" && role!="source") throw std::invalid_argument("duplex requires a sender role");
    auto other_topic=duplex?bench::peer_topic(topic):topic;
    uint64_t size=number(argv[3]), depth=number(argv[5]), count=number(argv[6]), warm=number(argv[7]);
    uint64_t seconds=number(argv[8]), timeout=number(argv[9]), rate=number(argv[10]), slow=number(argv[11]), run=number(argv[12]);
    if ((role!="ping" && role!="pong" && role!="source" && role!="sink") ||
        size==0 || size>bench::MAX_BYTES || depth==0 || depth>100 || count==0 || count>1000000 ||
        warm>10000 || !seconds || seconds>7200 || !timeout || timeout>120000 || rate>1000000 || slow>10000 ||
        (qos_name!="reliable" && qos_name!="best_effort")) throw std::invalid_argument("configuration bounds");
    std::ofstream out(argv[13]); if(!out) throw std::runtime_error("output open");
    std::ofstream receive_out;
    if(duplex && role=="source") {
      std::string path=argv[13];
      path=path.substr(0,path.find_last_of('/')+1)+"sink_"+other_topic.substr(other_topic.size()-2)+".jsonl";
      receive_out.open(path); if(!receive_out) throw std::runtime_error("receive output open");
    }
    if(role=="ping" || role=="pong") out << std::unitbuf;
    auto context=std::make_shared<rclcpp::Context>();
    context->init(0,nullptr);
    test_context=context;
    ShutdownAfterEntities shutdown_after_entities{context};
    std::signal(SIGTERM, on_signal); std::signal(SIGINT, on_signal);
    if(std::getenv("DDS_BENCH_CRASH_TRACE")) {
      struct sigaction action{};
      action.sa_handler=crash_trace; action.sa_flags=SA_RESETHAND;
      sigemptyset(&action.sa_mask); sigaction(SIGSEGV,&action,nullptr);
    }
    auto node=std::make_shared<bench::MinimalNode>("dds_bench_"+role,context);
    rclcpp::ExecutorOptions executor_options;
    executor_options.context=context;
    rclcpp::executors::SingleThreadedExecutor executor(executor_options);
    executor.add_node(node->get_node_base_interface());
    auto qos=rclcpp::QoS(rclcpp::KeepLast(depth)).durability_volatile();
    if(qos_name=="reliable") qos.reliable(); else qos.best_effort();
    bool sender=role=="ping" || role=="source";
    auto pub=node->create_publisher<Msg>(topic+(role=="pong" ? "/reply" : "/request"),qos);
    uint64_t received=0, invalid=0, duplicate=0, reordered=0, late=0, errors=0, highest=0, pending=0;
    bool waiting=false, echoed=false, started=false;
    double rtt=0; Clock::time_point sent, first, last;
    std::unordered_set<uint64_t> seen;
    auto sub=node->create_subscription<Msg>((duplex && role=="source"?other_topic:topic)+(role=="ping" ? "/reply" : "/request"),qos,
      [&](Msg::ConstSharedPtr msg) {
        const auto arrival=Clock::now();
        if(!bench::valid(msg->data,size,run)) { ++invalid; return; }
        auto seq=bench::sequence(msg->data);
        if (seq >= count+warm) { ++invalid; return; }
        if(!seen.insert(seq).second) { ++duplicate; return; }
        if(received && seq<highest) ++reordered;
        highest=std::max(highest,seq); ++received;
        if(!started) {first=arrival; started=true;} last=arrival;
        if(role=="ping") {
          if(waiting && seq==pending) {rtt=us(arrival-sent); echoed=true;}
          else ++late;
        } else if(role=="pong") {
          if(slow) std::this_thread::sleep_for(std::chrono::milliseconds(slow));
          try {pub->publish(*msg);} catch(const std::exception &) {++errors;}
        } else if(role=="sink" || (duplex && role=="source")) {
          auto &receiver=duplex?receive_out:out;
          receiver << "{\"event\":\"receive\",\"seq\":" << seq << ",\"arrival_ns\":" << std::chrono::duration_cast<std::chrono::nanoseconds>(arrival.time_since_epoch()).count() << "}\n";
          if(slow) std::this_thread::sleep_for(std::chrono::milliseconds(slow));
        }
      });
    // Source must not subscribe to itself; pong/sink publisher is unused for sink.
    if(role=="source" && !duplex) sub.reset();
    if(role=="sink") pub.reset();
    uint64_t echo_received=0,echo_invalid=0,echo_errors=0;
    std::unordered_set<uint64_t> echo_seen;
    rclcpp::Publisher<Msg>::SharedPtr echo_pub;
    rclcpp::Subscription<Msg>::SharedPtr echo_sub;
    if(duplex && role=="ping") {
      echo_pub=node->create_publisher<Msg>(other_topic+"/reply",qos);
      echo_sub=node->create_subscription<Msg>(other_topic+"/request",qos,[&](Msg::ConstSharedPtr msg) {
        if(!bench::valid(msg->data,size,run)) {++echo_invalid;return;}
        auto seq=bench::sequence(msg->data);
        if(seq>=count+warm) {++echo_invalid;return;}
        if(!echo_seen.insert(seq).second) return;
        ++echo_received;
        try {echo_pub->publish(*msg);} catch(const std::exception &) {++echo_errors;}
      });
    }
    auto start=Clock::now();
    double publish_window_us=0;
    Dl_info provider_info{};
    auto provider_symbol=dlsym(RTLD_DEFAULT,"rmw_get_implementation_identifier");
    const char *provider_path=(provider_symbol && dladdr(provider_symbol,&provider_info))?provider_info.dli_fname:"unknown";
    out << "{\"event\":\"config\",\"role\":\""<<role<<"\",\"rmw\":\""<<rmw_get_implementation_identifier()<<"\",\"rmw_library\":\""<<provider_path<<"\",\"bytes\":"<<size<<",\"header_bytes\":40,\"qos\":\""<<qos_name<<"\",\"depth\":"<<depth<<",\"run\":"<<run<<"}\n";
    if(receive_out.is_open()) receive_out << "{\"event\":\"config\",\"role\":\"sink\",\"rmw\":\""<<rmw_get_implementation_identifier()<<"\",\"rmw_library\":\""<<provider_path<<"\",\"bytes\":"<<size<<",\"header_bytes\":40,\"qos\":\""<<qos_name<<"\",\"depth\":"<<depth<<",\"run\":"<<run<<"}\n";
    {std::ofstream ready(argv[14]); ready << "READY\n";}
    auto receiver_start=Clock::now();
    std::exception_ptr receiver_error;
    std::thread receiver_thread;
    JoinReceiver join_receiver{receiver_thread,executor};
    if(sender) {
      while(active() && (pub->get_subscription_count()==0 || (role=="ping" && sub->get_publisher_count()==0)) && Clock::now()-start<std::chrono::seconds(15)) {
        executor.spin_some(); std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      if(pub->get_subscription_count()==0 || (role=="ping" && sub->get_publisher_count()==0)) {
        out << "{\"event\":\"terminal\",\"status\":\"discovery_timeout\"}\n"; out.flush(); return 3;
      }
      out << "{\"event\":\"discovery\",\"elapsed_us\":"<<us(Clock::now()-start)<<"}\n";
      Msg msg; msg.data=bench::payload(size,run);
      if(duplex && role=="source") receiver_thread=std::thread([&]{try {executor.spin();} catch(...) {receiver_error=std::current_exception();}});
      start=Clock::now(); auto deadline=start+std::chrono::seconds(seconds); auto next=start;
      for(uint64_t seq=0; active() && seq<count+warm && Clock::now()<deadline; ++seq) {
        bench::stamp(msg.data,seq,run); pending=seq; echoed=false; waiting=true;
        sent=Clock::now(); bool failed=false;
        try {pub->publish(msg);} catch(const std::exception &e) {
          failed=true; if(errors++==0) std::cerr << "PUBLISH_ERROR " << e.what() << '\n';
        }
        double publish_us=us(Clock::now()-sent);
        if(role=="ping" && !failed) {
          auto until=bench::sample_deadline(sent,timeout);
          while(active() && !echoed && Clock::now()<until) {executor.spin_some(); std::this_thread::sleep_for(std::chrono::microseconds(50));}
        }
        waiting=false;
        out << "{\"event\":\"sample\",\"seq\":"<<seq<<",\"warmup\":"<<(seq<warm?"true":"false")<<",\"outcome\":\""<<(failed?"publish_error":role=="source"?"published":echoed?"ok":"timeout")<<"\",\"publish_us\":"<<publish_us;
        if(role=="ping" && echoed && !failed) out << ",\"rtt_us\":"<<rtt;
        out << "}\n";
        if(rate) { next += std::chrono::nanoseconds(1000000000ULL/rate); std::this_thread::sleep_until(next); }
      }
      publish_window_us=us(Clock::now()-start);
      if(role=="source") {
        out << "{\"event\":\"send_complete\",\"elapsed_us\":"<<publish_window_us<<"}\n";
        const auto drain_start=Clock::now();
        while(active() && Clock::now()-drain_start<std::chrono::seconds(2)) {
          if(!receiver_thread.joinable()) executor.spin_some();
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        out << "{\"event\":\"publisher_alive_drain\",\"elapsed_us\":"<<us(Clock::now()-drain_start)<<"}\n";
      }
      if(duplex) {
        {std::ofstream done(std::string(argv[14])+".done");done<<"DONE\n";}
        while(active() && Clock::now()-start<std::chrono::seconds(seconds+22)) {
          if(!receiver_thread.joinable()) executor.spin_some();
          std::this_thread::sleep_for(std::chrono::microseconds(role=="ping"?50:1000));
        }
      }
    } else {
      while(active() && Clock::now()-start<std::chrono::seconds(seconds)) {
        if(role=="sink") executor.spin_once(std::chrono::milliseconds(20));
        else {executor.spin_some(); std::this_thread::sleep_for(std::chrono::microseconds(50));}
      }
    }
    if(receiver_thread.joinable()) {executor.cancel();receiver_thread.join();}
    if(receiver_error) std::rethrow_exception(receiver_error);
    if(receive_out.is_open()) {
      receive_out << "{\"event\":\"terminal\",\"status\":\"complete\",\"received\":"<<received<<",\"invalid\":"<<invalid<<",\"duplicate\":"<<duplicate<<",\"reordered\":"<<reordered<<",\"late\":0,\"publish_errors\":0,\"elapsed_us\":"<<us(Clock::now()-receiver_start)<<",\"arrival_span_us\":"<<(received>1?us(last-first):0)<<"}\n";
      receive_out.flush();
    }
    out << "{\"event\":\"terminal\",\"status\":\"complete\",\"received\":"<<received<<",\"invalid\":"<<invalid+echo_invalid<<",\"duplicate\":"<<duplicate<<",\"reordered\":"<<reordered<<",\"late\":"<<late<<",\"publish_errors\":"<<errors<<",\"echo_received\":"<<echo_received<<",\"echo_errors\":"<<echo_errors<<",\"elapsed_us\":"<<(sender?publish_window_us:us(Clock::now()-start))<<",\"arrival_span_us\":"<<(received>1?us(last-first):0)<<"}\n";
    out.flush(); return 0;
  } catch(const std::exception &e) {std::cerr << "BENCH_EXCEPTION " << e.what() << '\n'; return 4;}
}
