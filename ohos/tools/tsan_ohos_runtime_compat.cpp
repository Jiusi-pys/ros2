// Copyright (c) 2026 OpenHarmony
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

#include <stdint.h>

namespace {
constexpr int TSAN_REPORT_EXIT_CODE = 66;

__attribute__((no_sanitize("thread"))) long RawWrite(const char *data, uintptr_t size)
{
#if defined(__aarch64__)
  register long x0 asm("x0") = 2;
  register long x1 asm("x1") = reinterpret_cast<long>(data);
  register long x2 asm("x2") = static_cast<long>(size);
  register long x8 asm("x8") = 64;
  asm volatile("svc 0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
  return x0;
#else
  (void)data;
  (void)size;
  return -1;
#endif
}

[[noreturn]] __attribute__((no_sanitize("thread"))) void RawExit(int status)
{
#if defined(__aarch64__)
  register long x0 asm("x0") = status;
  register long x8 asm("x8") = 94;
  asm volatile("svc 0" : : "r"(x0), "r"(x8) : "memory");
#endif
  __builtin_trap();
}
}  // namespace

namespace __tsan {
struct ReportDesc;

// The OHOS clang 15 runtime hangs after formatting a report. Preserve the
// detector result as a stable marker and nonzero board-side exit status.
__attribute__((no_sanitize("thread"))) bool OnReport(const ReportDesc *, bool)
{
  static constexpr char TSAN_REPORT_MARKER[] = "RMW_MDDS_TSAN_REPORT\n";
  RawWrite(TSAN_REPORT_MARKER, sizeof(TSAN_REPORT_MARKER) - 1);
  RawExit(TSAN_REPORT_EXIT_CODE);
}

// The same runtime faults after clean Finalize(). Exit only after TSan has
// decided whether the process produced a report.
__attribute__((no_sanitize("thread"))) bool OnFinalize(bool failed)
{
  RawExit(failed ? TSAN_REPORT_EXIT_CODE : 0);
}
}  // namespace __tsan

extern "C" __attribute__((no_sanitize("thread"))) int __tsan_on_finalize(int failed)
{
  RawExit(failed ? TSAN_REPORT_EXIT_CODE : 0);
}
