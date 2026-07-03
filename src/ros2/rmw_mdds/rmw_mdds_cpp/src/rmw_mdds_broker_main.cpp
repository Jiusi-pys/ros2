// Copyright 2026 Kaihong Digital Industry Development Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <signal.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "ipc_broker.hpp"

namespace
{
std::atomic<bool> g_running{true};

void HandleSignal(int)
{
  g_running.store(false);
}

void PrintUsage(const char * program)
{
  std::cerr << "usage: " << (program == nullptr ? "rmw_mdds_broker" : program)
            << " --socket <path>\n";
}
}  // namespace

int main(int argc, char ** argv)
{
  std::string socket_path;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
      socket_path = argv[++i];
    } else {
      PrintUsage(argv[0]);
      return 64;
    }
  }

  if (socket_path.empty()) {
    PrintUsage(argv[0]);
    return 64;
  }

  signal(SIGINT, HandleSignal);
  signal(SIGTERM, HandleSignal);

  std::string error;
  rmw_mdds_cpp::ipc::IpcBroker broker;
  if (!broker.Start(socket_path, &error)) {
    std::cerr << "failed to start rmw_mdds_broker: " << error << "\n";
    return 2;
  }

  while (g_running.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  broker.Stop();
  return 0;
}
