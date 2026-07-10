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

#include <dlfcn.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
constexpr uint32_t kProtectedTransportAuthenticated = 0x1u;
constexpr uint32_t kProtectedTransportEncrypted = 0x2u;

using BridgeInit = int32_t (*)();
using BridgeShutdown = void (*)();
using BridgeActivateProtectedTransport = int32_t (*)(uint32_t);

std::string DefaultBridgeLibrary()
{
  const char * env = std::getenv("RMW_MDDS_BRIDGE_LIBRARY");
  if (env != nullptr && env[0] != '\0') {
    return env;
  }
  return "libmdds_bridge_shared.z.so";
}

template<typename T>
T LoadSymbol(void * library, const char * name)
{
  dlerror();
  void * symbol = dlsym(library, name);
  const char * error = dlerror();
  if (error != nullptr || symbol == nullptr) {
    std::cerr << "BRIDGE_PROTECTED_TRANSPORT_MISSING_SYMBOL=" << name << std::endl;
    if (error != nullptr) {
      std::cerr << "BRIDGE_PROTECTED_TRANSPORT_DLSYM_ERROR=" << error << std::endl;
    }
    return nullptr;
  }
  return reinterpret_cast<T>(symbol);
}
}  // namespace

int main(int argc, char ** argv)
{
  const std::string library_path = argc > 1 ? argv[1] : DefaultBridgeLibrary();
  std::cout << "BRIDGE_PROTECTED_TRANSPORT_LIBRARY=" << library_path << std::endl;

  void * library = dlopen(library_path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (library == nullptr) {
    const char * error = dlerror();
    std::cerr << "BRIDGE_PROTECTED_TRANSPORT_DLOPEN_STATUS=1" << std::endl;
    std::cerr << "BRIDGE_PROTECTED_TRANSPORT_DLOPEN_ERROR="
              << (error == nullptr ? "unknown" : error) << std::endl;
    return 10;
  }

  BridgeInit init = LoadSymbol<BridgeInit>(library, "MddsBridgeInit");
  BridgeShutdown shutdown = LoadSymbol<BridgeShutdown>(library, "MddsBridgeShutdown");
  BridgeActivateProtectedTransport activate =
    LoadSymbol<BridgeActivateProtectedTransport>(library, "MddsBridgeActivateProtectedTransport");
  if (init == nullptr || shutdown == nullptr || activate == nullptr) {
    dlclose(library);
    return 11;
  }

  const int32_t init_ret = init();
  std::cout << "BRIDGE_PROTECTED_TRANSPORT_INIT_STATUS=" << init_ret << std::endl;
  if (init_ret != 0) {
    shutdown();
    dlclose(library);
    return 12;
  }

  const uint32_t flags = kProtectedTransportAuthenticated | kProtectedTransportEncrypted;
  const int32_t activation_ret = activate(flags);
  std::cout << "BRIDGE_PROTECTED_TRANSPORT_FLAGS=" << flags << std::endl;
  std::cout << "BRIDGE_PROTECTED_TRANSPORT_ACTIVATE_RET=" << activation_ret << std::endl;
  shutdown();
  dlclose(library);

  return activation_ret == 0 ? 0 : 13;
}
