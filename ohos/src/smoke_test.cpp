/*
 * Copyright (c) 2026
 * Licensed under the Apache License, Version 2.0
 */

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include "rcpputils/process.hpp"
#include "rcpputils/shared_library.hpp"
#include "rcutils/logging_macros.h"
#include "rcutils/process.h"
#include "rcutils/time.h"

namespace OHOS {
namespace Ros2Port {

using DummyValueFunction = int (*)();

int32_t runSmoke(const std::string & libraryPath)
{
    rcutils_ret_t ret = rcutils_logging_initialize();
    if (ret != RCUTILS_RET_OK) {
        std::cerr << "rcutils_logging_initialize failed: " <<
            rcutils_get_error_string().str << std::endl;
        rcutils_reset_error();
        return 1;
    }

    rcutils_time_point_value_t systemTime = 0;
    rcutils_time_point_value_t steadyTime = 0;

    ret = rcutils_system_time_now(&systemTime);
    if (ret != RCUTILS_RET_OK) {
        std::cerr << "rcutils_system_time_now failed: " <<
            rcutils_get_error_string().str << std::endl;
        rcutils_reset_error();
        return 2;
    }

    ret = rcutils_steady_time_now(&steadyTime);
    if (ret != RCUTILS_RET_OK) {
        std::cerr << "rcutils_steady_time_now failed: " <<
            rcutils_get_error_string().str << std::endl;
        rcutils_reset_error();
        return 3;
    }

    const int pid = rcutils_get_pid();
    const std::string executableName = rcpputils::get_executable_name();

    rcpputils::SharedLibrary library(libraryPath);
    auto symbol = reinterpret_cast<DummyValueFunction>(library.get_symbol("Ros2OhosDummyValue"));
    const int dummyValue = symbol();

    RCUTILS_LOG_INFO_NAMED(
        "ros2_ohos_smoke",
        "smoke test running on KaihongOS pid=%d executable=%s",
        pid,
        executableName.c_str());

    std::cout << "smoke_ok" << std::endl;
    std::cout << "pid=" << pid << std::endl;
    std::cout << "executable=" << executableName << std::endl;
    std::cout << "system_time_ns=" << systemTime << std::endl;
    std::cout << "steady_time_ns=" << steadyTime << std::endl;
    std::cout << "dummy_value=" << dummyValue << std::endl;
    std::cout << "library_path=" << library.get_library_path() << std::endl;

    const rcutils_ret_t shutdownRet = rcutils_logging_shutdown();
    if (shutdownRet != RCUTILS_RET_OK) {
        std::cerr << "rcutils_logging_shutdown failed: " <<
            rcutils_get_error_string().str << std::endl;
        rcutils_reset_error();
        return 4;
    }
    return 0;
}

}  // namespace Ros2Port
}  // namespace OHOS

int main(int argc, char ** argv)
{
    if (argc != 2) {
        std::cerr << "Usage: ros2_ohos_smoke <path-to-dummy-library>" << std::endl;
        return 64;
    }

    try {
        return OHOS::Ros2Port::runSmoke(argv[1]);
    } catch (const std::exception & error) {
        std::cerr << "smoke test failed: " << error.what() << std::endl;
        return 65;
    }
}
