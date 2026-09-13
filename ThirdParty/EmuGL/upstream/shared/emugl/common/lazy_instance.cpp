// Copyright (C) 2014 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "emugl/common/lazy_instance.h"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN 1
#  include <windows.h>
#else
#  include <sched.h>
#endif

namespace emugl {
namespace internal {

typedef LazyInstanceState::AtomicType AtomicType;

// Acquire/release must synchronize the constructed object's contents on ARM,
// where compiler-only barriers do not order CPU loads and stores.
static inline AtomicType loadAcquire(AtomicType volatile* ptr) {
#ifdef _WIN32
    return InterlockedCompareExchange(ptr, 0, 0);
#else
    return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
#endif
}

static inline void storeRelease(AtomicType volatile* ptr, AtomicType value) {
#ifdef _WIN32
    InterlockedExchange(ptr, value);
#else
    __atomic_store_n(ptr, value, __ATOMIC_RELEASE);
#endif
}

static int atomicCompareAndSwap(AtomicType volatile* ptr,
                                int expected,
                                int value) {
#ifdef _WIN32
    return InterlockedCompareExchange(ptr, value, expected);
#elif defined(__GNUC__)
    return __sync_val_compare_and_swap(ptr, expected, value);
#else
#error "Your compiler is not supported"
#endif
}

static void yieldThread() {
#ifdef _WIN32
    ::Sleep(0);
#else
    sched_yield();
#endif
}

bool LazyInstanceState::inInitState() {
    return loadAcquire(&mState) == STATE_INIT;
}

bool LazyInstanceState::needConstruction() {
    AtomicType state = loadAcquire(&mState);
    if (state == STATE_DONE)
        return false;

    state = atomicCompareAndSwap(&mState, STATE_INIT, STATE_CONSTRUCTING);
    if (state == STATE_INIT)
        return true;

    do {
        yieldThread();
        state = loadAcquire(&mState);
    } while (state != STATE_DONE);

    return false;
}

void LazyInstanceState::doneConstructing() {
    storeRelease(&mState, STATE_DONE);
}

}  // namespace internal
}  // namespace emugl
