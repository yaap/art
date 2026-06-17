/*
 * Copyright (C) 2012 The Android Open Source Project
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

#include "base/mutex.h"
#include "base/locks.h"
#include "mutex-inl.h"

#include "common_runtime_test.h"
#include "thread-current-inl.h"
#include "thread.h"
#include "thread_list.h"

// The standard ASSERT macros don't play correctly with thread-safety analysis, in that they
// apparently do not always execute the rest of the function, and can thus result in function
// returns with different lock states. They should only be used when no locks are held.
//
// In other contexts, we replace assertions by macros that record failures as we go, but only
// reporting them at the end.  We invoke CHECK_DEFERRED_FAILURE_STATE only when no locks are held.
#define DECLARE_DEFERRED_FAILURE_STATE \
  int fail_line = 0;                   \
  const char* fail_cond = nullptr;
#define DEFERRED_ASSERT_TRUE(p) \
  if (!(p) && fail_line == 0) { \
    fail_line = __LINE__;       \
    fail_cond = #p;             \
  }
#define DEFERRED_ASSERT_FALSE(p)     DEFERRED_ASSERT_TRUE(!p)
#define CHECK_DEFERRED_FAILURE_STATE CHECK_EQ(fail_line, 0) << fail_cond

namespace art HIDDEN {

class MutexTest : public CommonRuntimeTest {
 protected:
  MutexTest() {
    use_boot_image_ = true;  // Make the Runtime creation cheaper.
  }
};

// TODO: The use of Assert...Held() here is not optimal, since that tells the thread checker
// to assume it is held, which is not great in a test, and it actually performs the dynamic
// check only in debug builds, which is also not great in a test. Use ASSERT_TRUE(...Is...Held())
// or DEFERRED_ASSERT_TRUE(...Is...Held()) instead, as we've started to do below.

struct MutexTester {
  static void AssertDepth(Mutex& mu, uint32_t expected_depth) {
    ASSERT_EQ(expected_depth, mu.GetDepth());

    // This test is single-threaded, so we also know _who_ should hold the lock.
    if (expected_depth == 0) {
      mu.AssertNotHeld(Thread::Current());
    } else {
      mu.AssertHeld(Thread::Current());
    }
  }
};

TEST_F(MutexTest, LockUnlock) {
  // TODO: Remove `Mutex` dependency on `Runtime` or at least make sure it works
  // without a `Runtime` with reasonable defaults (and without dumping stack for timeout).
  ASSERT_TRUE(Runtime::Current() != nullptr);
  Mutex mu("test mutex");
  MutexTester::AssertDepth(mu, 0U);
  mu.Lock(Thread::Current());
  MutexTester::AssertDepth(mu, 1U);
  mu.Unlock(Thread::Current());
  MutexTester::AssertDepth(mu, 0U);
}

// TODO: NO_THREAD_SAFETY_ANALYSIS because ASSERT_TRUE is not yet DEFERRED.
static void TryLockUnlockTest() NO_THREAD_SAFETY_ANALYSIS {
  Mutex mu("test mutex");
  MutexTester::AssertDepth(mu, 0U);
  ASSERT_TRUE(mu.TryLock(Thread::Current()));
  MutexTester::AssertDepth(mu, 1U);
  mu.Unlock(Thread::Current());
  MutexTester::AssertDepth(mu, 0U);
}

TEST_F(MutexTest, TryLockUnlock) {
  TryLockUnlockTest();
}

// Assertions here don't play with thread safety analysis.
static void RecursiveLockUnlockTest() NO_THREAD_SAFETY_ANALYSIS {
  Mutex mu("test mutex", kDefaultMutexLevel, true);
  MutexTester::AssertDepth(mu, 0U);
  mu.Lock(Thread::Current());
  MutexTester::AssertDepth(mu, 1U);
  mu.Lock(Thread::Current());
  MutexTester::AssertDepth(mu, 2U);
  mu.Unlock(Thread::Current());
  MutexTester::AssertDepth(mu, 1U);
  mu.Unlock(Thread::Current());
  MutexTester::AssertDepth(mu, 0U);
}

TEST_F(MutexTest, RecursiveLockUnlock) {
  RecursiveLockUnlockTest();
}

static void RecursiveTryLockUnlockTest() NO_THREAD_SAFETY_ANALYSIS {
  Mutex mu("test mutex", kDefaultMutexLevel, true);
  MutexTester::AssertDepth(mu, 0U);
  ASSERT_TRUE(mu.TryLock(Thread::Current()));
  MutexTester::AssertDepth(mu, 1U);
  ASSERT_TRUE(mu.TryLock(Thread::Current()));
  MutexTester::AssertDepth(mu, 2U);
  mu.Unlock(Thread::Current());
  MutexTester::AssertDepth(mu, 1U);
  mu.Unlock(Thread::Current());
  MutexTester::AssertDepth(mu, 0U);
}

TEST_F(MutexTest, RecursiveTryLockUnlock) {
  RecursiveTryLockUnlockTest();
}


struct RecursiveLockWait {
  RecursiveLockWait()
      : mu("test mutex", kDefaultMutexLevel, true), cv("test condition variable", mu) {
  }

  Mutex mu;
  ConditionVariable cv;
};

static void* RecursiveLockWaitCallback(void* arg) {
  RecursiveLockWait* state = reinterpret_cast<RecursiveLockWait*>(arg);
  state->mu.Lock(Thread::Current());
  state->cv.Signal(Thread::Current());
  state->mu.Unlock(Thread::Current());
  return nullptr;
}

// Recursive lock acquisition is not supported by our current thread-safety annotations, which do
// not mention REENTRANT_CAPABILITY. This may not be worth fixing, since reentrant Mutexes should
// probably be deprecated.
static void RecursiveLockWaitTest() NO_THREAD_SAFETY_ANALYSIS {
  DECLARE_DEFERRED_FAILURE_STATE;
  RecursiveLockWait state;
  state.mu.Lock(Thread::Current());
  state.mu.Lock(Thread::Current());

  pthread_t pthread;
  int pthread_create_result = pthread_create(&pthread, nullptr, RecursiveLockWaitCallback, &state);
  DEFERRED_ASSERT_TRUE(pthread_create_result == 0);

  state.cv.Wait(Thread::Current());

  state.mu.Unlock(Thread::Current());
  state.mu.Unlock(Thread::Current());
  CHECK_DEFERRED_FAILURE_STATE;
  EXPECT_EQ(pthread_join(pthread, nullptr), 0);
}

// This ensures we don't hang when waiting on a recursively locked mutex,
// which is not supported with bare pthread_mutex_t.
TEST_F(MutexTest, RecursiveLockWait) {
  RecursiveLockWaitTest();
}

TEST_F(MutexTest, SharedLockUnlock) {
  DECLARE_DEFERRED_FAILURE_STATE;
  ReaderWriterMutex mu("test rwmutex");
  DEFERRED_ASSERT_FALSE(mu.IsSharedHeld(Thread::Current()));
  DEFERRED_ASSERT_FALSE(mu.IsExclusiveHeld(Thread::Current()));
  mu.SharedLock(Thread::Current());
  DEFERRED_ASSERT_TRUE(mu.IsSharedHeld(Thread::Current()));
  DEFERRED_ASSERT_FALSE(mu.IsExclusiveHeld(Thread::Current()));
  mu.SharedUnlock(Thread::Current());
  DEFERRED_ASSERT_FALSE(mu.IsSharedHeld(Thread::Current()));
  DEFERRED_ASSERT_FALSE(mu.IsExclusiveHeld(Thread::Current()));
  CHECK_DEFERRED_FAILURE_STATE;
}

TEST_F(MutexTest, ExclusiveLockUnlock) {
  DECLARE_DEFERRED_FAILURE_STATE;
  ReaderWriterMutex mu("test rwmutex");
  mu.AssertNotHeld(Thread::Current());
  mu.AssertNotExclusiveHeld(Thread::Current());
  mu.ExclusiveLock(Thread::Current());
  DEFERRED_ASSERT_TRUE(mu.IsSharedHeld(Thread::Current()));
  DEFERRED_ASSERT_TRUE(mu.IsExclusiveHeld(Thread::Current()));
  mu.ExclusiveUnlock(Thread::Current());
  DEFERRED_ASSERT_FALSE(mu.IsSharedHeld(Thread::Current()));
  DEFERRED_ASSERT_FALSE(mu.IsExclusiveHeld(Thread::Current()));
  CHECK_DEFERRED_FAILURE_STATE;

  // This is technically not guaranteed, but it should always succeed. We try to keep the control
  // flow in the failure case simple enough so that thread-safety analysis can understand it.
  if (!mu.ExclusiveTryLock(Thread::Current())) {
    abort();
  }
  DEFERRED_ASSERT_TRUE(mu.IsSharedHeld(Thread::Current()));
  DEFERRED_ASSERT_TRUE(mu.IsExclusiveHeld(Thread::Current()));
  mu.ExclusiveUnlock(Thread::Current());
  ASSERT_FALSE(mu.IsExclusiveHeld(Thread::Current()));
  ASSERT_FALSE(mu.IsSharedHeld(Thread::Current()));
  CHECK_DEFERRED_FAILURE_STATE;

  mu.ExclusiveLock(Thread::Current());
  DEFERRED_ASSERT_TRUE(mu.IsSharedHeld(Thread::Current()));
  DEFERRED_ASSERT_TRUE(mu.IsExclusiveHeld(Thread::Current()));
  mu.Downgrade(Thread::Current());
  DEFERRED_ASSERT_TRUE(mu.IsSharedHeld(Thread::Current()));
  DEFERRED_ASSERT_FALSE(mu.IsExclusiveHeld(Thread::Current()));
  mu.SharedUnlock(Thread::Current());
  DEFERRED_ASSERT_FALSE(mu.IsSharedHeld(Thread::Current()));
  DEFERRED_ASSERT_FALSE(mu.IsExclusiveHeld(Thread::Current()));
  CHECK_DEFERRED_FAILURE_STATE;
}

static void SharedTryLockUnlockTest() {
  ReaderWriterMutex mu("test rwmutex");
  mu.AssertNotHeld(Thread::Current());
  ASSERT_TRUE(mu.SharedTryLock(Thread::Current()));
  mu.AssertSharedHeld(Thread::Current());
  mu.SharedUnlock(Thread::Current());
  mu.AssertNotHeld(Thread::Current());
}

TEST_F(MutexTest, SharedTryLockUnlock) {
  SharedTryLockUnlockTest();
}

class ScopedVirtualThreadId {
 public:
  ScopedVirtualThreadId(): id_(AllocThreadId()) {}
  ~ScopedVirtualThreadId() {
    ThreadList* thread_list = Runtime::Current()->GetThreadList();
    thread_list->ReleaseVirtualThreadSuspendCount(id_);
    thread_list->ReleaseThreadId(Thread::Current(), id_);
  }
  uint32_t GetId() const {
    return id_;
  }
 private:
  static uint32_t AllocThreadId() {
    ThreadList* thread_list = Runtime::Current()->GetThreadList();
    Thread* self = Thread::Current();
    uint32_t id = thread_list->AllocThreadId(self);
    thread_list->AllocVirtualThreadSuspendCount(id);
    return id;
  }
 private:
  const uint32_t id_;
};

class VirtualThreadMounter {
 public:
  explicit VirtualThreadMounter(MountedVirtualThreadData* mounted_data)
      REQUIRES_SHARED(Locks::mutator_lock_) {
    Thread* self = Thread::Current();
    self->TrySetMountedVirtualThreadData(mounted_data);
  }
  ~VirtualThreadMounter() {
    Thread* self = Thread::Current();
    self->TryClearMountedVirtualThreadData();
  }
};

TEST_F(MutexTest, MonitorLockUnlock) {
  ASSERT_TRUE(Runtime::Current() != nullptr);
  Thread* self = Thread::Current();
  pid_t tid = self->GetTid();
  uint32_t carrier_id = self->GetThreadId();
  MonitorMutex mu;

  ASSERT_EQ(tid, mu.GetSelfId(self));

  MutexTester::AssertDepth(mu, 0U);
  mu.Lock(self);
  MutexTester::AssertDepth(mu, 1U);
  mu.Unlock(self);
  MutexTester::AssertDepth(mu, 0U);

  if (!kIsVirtualThreadEnabled) {
    return;
  }

  ScopedVirtualThreadId virtual_thread_id_holder;
  uint32_t virtual_thread_id = virtual_thread_id_holder.GetId();
  ASSERT_NE(virtual_thread_id, carrier_id);

  MountedVirtualThreadData mounted_data(virtual_thread_id, carrier_id, 0);
  ScopedObjectAccess soa(self);
  VirtualThreadMounter mounter(&mounted_data);
  ASSERT_EQ(virtual_thread_id, self->GetVirtualThreadId());
  ASSERT_EQ(virtual_thread_id, self->GetMonitorThreadId());
  ASSERT_EQ(virtual_thread_id | MonitorMutex::kVTFlag, mu.GetSelfId(self));

  MutexTester::AssertDepth(mu, 0U);
  mu.Lock(self);
  MutexTester::AssertDepth(mu, 1U);
  mu.Unlock(self);
  MutexTester::AssertDepth(mu, 0U);
}

}  // namespace art
