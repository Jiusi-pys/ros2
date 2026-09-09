#include "contracts.hpp"
#include <cassert>
#include <iostream>
int main() {
  using namespace bench;
  auto bytes = payload(1024, 42);
  stamp(bytes, 17, 42);
  assert(valid(bytes, 1024, 42));
  assert(!valid(bytes, 1024, 43));
  assert(!valid(bytes, 2048, 42));
  assert(sequence(bytes) == 17);
  bytes.back() ^= 1;
  assert(!valid(bytes, 1024, 42));
  assert(!valid(std::vector<uint8_t>(3), 1024, 42));
  bool rejected = false;
  try { payload(MAX_BYTES + 1, 42); } catch (const std::exception &) { rejected = true; }
  assert(rejected);
  assert(peer_topic("/run_ab")=="/run_ba");
  assert(peer_topic(peer_topic("/run_ba"))=="/run_ba");
  auto sent=std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(990);
  assert(sample_deadline(sent,100)==std::chrono::steady_clock::time_point{}+std::chrono::milliseconds(1090));
  std::cout << "CONTRACT_PASS\n";
}
