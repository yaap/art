/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_RUNTIME_BASE_QUASI_ATOMIC_H_
#define ART_RUNTIME_BASE_QUASI_ATOMIC_H_

#include <stdint.h>
#include <atomic>
#include <limits>
#include <vector>

#include <android-base/logging.h>

#include "arch/instruction_set.h"
#include "base/macros.h"

namespace art HIDDEN {

class Mutex;

// QuasiAtomic encapsulates two separate facilities that we are
// trying to move away from:  "quasiatomic" 64 bit operations
// and custom memory fences.  For the time being, they remain
// exposed.  Clients should be converted to use either class Atomic
// below whenever possible, and should eventually use C++11 atomics.
// The two facilities that do not have a good C++11 analog are
// ThreadFenceForConstructor and Atomic::*JavaData.
//
// NOTE: Two "quasiatomic" operations on the exact same memory address
// are guaranteed to operate atomically with respect to each other,
// but no guarantees are made about quasiatomic operations mixed with
// non-quasiatomic operations on the same address, nor about
// quasiatomic operations that are performed on partially-overlapping
// memory.
class QuasiAtomic {
 public:
  static void ThreadFenceForConstructor() {
    #if defined(__aarch64__)
      __asm__ __volatile__("dmb ishst" : : : "memory");
    #else
      std::atomic_thread_fence(std::memory_order_release);
    #endif
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(QuasiAtomic);
};

}  // namespace art

#endif  // ART_RUNTIME_BASE_QUASI_ATOMIC_H_
