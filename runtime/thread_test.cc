/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "thread.h"

#include <sys/resource.h>
#include <atomic>

#include "android-base/logging.h"
#include "base/locks.h"
#include "base/mutex.h"
#include "base/time_utils.h"
#include "common_runtime_test.h"
#include "mirror/object.h"
#include "runtime.h"
#include "scoped_thread_priority_change.h"
#include "suspend_reason.h"
#include "thread-current-inl.h"
#include "thread-inl.h"
#include "thread_list.h"
#include "well_known_classes-inl.h"

namespace art HIDDEN {

class ThreadTest : public CommonRuntimeTest {};

// Ensure that basic list operations on ThreadExitFlags work. These are rarely
// exercised in practice, since normally only one flag is registered at a time.

TEST_F(ThreadTest, ThreadExitFlagTest) {
  Thread* self = Thread::Current();
  ThreadExitFlag tefs[3];
  {
    MutexLock mu(self, *Locks::thread_list_lock_);
    self->NotifyOnThreadExit(&tefs[2]);
    ASSERT_TRUE(self->IsRegistered(&tefs[2]));
    ASSERT_FALSE(tefs[2].HasExited());
    ASSERT_FALSE(self->IsRegistered(&tefs[1]));
    self->NotifyOnThreadExit(&tefs[1]);
    self->NotifyOnThreadExit(&tefs[0]);
    ASSERT_TRUE(self->IsRegistered(&tefs[0]));
    ASSERT_TRUE(self->IsRegistered(&tefs[1]));
    ASSERT_TRUE(self->IsRegistered(&tefs[2]));
    self->UnregisterThreadExitFlag(&tefs[1]);
    ASSERT_TRUE(self->IsRegistered(&tefs[0]));
    ASSERT_FALSE(self->IsRegistered(&tefs[1]));
    ASSERT_TRUE(self->IsRegistered(&tefs[2]));
    self->UnregisterThreadExitFlag(&tefs[2]);
    ASSERT_TRUE(self->IsRegistered(&tefs[0]));
    ASSERT_FALSE(self->IsRegistered(&tefs[1]));
    ASSERT_FALSE(self->IsRegistered(&tefs[2]));
  }
  Thread::DCheckUnregisteredEverywhere(&tefs[1], &tefs[2]);
  {
    MutexLock mu(self, *Locks::thread_list_lock_);
    self->UnregisterThreadExitFlag(&tefs[0]);
    ASSERT_FALSE(self->IsRegistered(&tefs[0]));
    ASSERT_FALSE(self->IsRegistered(&tefs[1]));
    ASSERT_FALSE(self->IsRegistered(&tefs[2]));
  }
  Thread::DCheckUnregisteredEverywhere(&tefs[0], &tefs[2]);
}

TEST_F(ThreadTest, ThreadExitSignalTest) {
  Thread* self = Thread::Current();
  ThreadExitFlag tefs[3];
  {
    MutexLock mu(self, *Locks::thread_list_lock_);
    self->NotifyOnThreadExit(&tefs[2]);
    ASSERT_TRUE(self->IsRegistered(&tefs[2]));
    ASSERT_FALSE(self->IsRegistered(&tefs[1]));
    self->NotifyOnThreadExit(&tefs[1]);
    ASSERT_TRUE(self->IsRegistered(&tefs[1]));
    self->SignalExitFlags();
    ASSERT_TRUE(tefs[1].HasExited());
    ASSERT_TRUE(tefs[2].HasExited());
  }
  Thread::DCheckUnregisteredEverywhere(&tefs[1], &tefs[2]);
  {
    MutexLock mu(self, *Locks::thread_list_lock_);
    self->NotifyOnThreadExit(&tefs[0]);
    tefs[2].~ThreadExitFlag();  //  Destroy and reinitialize.
    new (&tefs[2]) ThreadExitFlag();
    self->NotifyOnThreadExit(&tefs[2]);
    ASSERT_FALSE(tefs[0].HasExited());
    ASSERT_TRUE(tefs[1].HasExited());
    ASSERT_FALSE(tefs[2].HasExited());
    self->SignalExitFlags();
    ASSERT_TRUE(tefs[0].HasExited());
    ASSERT_TRUE(tefs[1].HasExited());
    ASSERT_TRUE(tefs[2].HasExited());
  }
  Thread::DCheckUnregisteredEverywhere(&tefs[0], &tefs[2]);
}

// Ensure that ScopedPriorityChange works correctly and interacts properly with
// GetNicenessBeforeBoost().
TEST_F(ThreadTest, ScopedPriorityChangeTest) {
  // This is disabled on VM and SBC because they do not emulate Android's more generous
  // setpriority() handling.
  TEST_DISABLED_ON_VM();
  TEST_DISABLED_ON_SBC();
#if defined(ART_TARGET_ANDROID)
  Thread* self = Thread::Current();
  ASSERT_FALSE(self == nullptr);
  self->TransitionFromSuspendedToRunnable();  // Start() releases mutator lock.
  bool started = Runtime::Current()->Start();
  CHECK(started);
  ScopedObjectAccess soa(self);
  mirror::Object* peer = self->GetPeer();
  ASSERT_FALSE(peer == nullptr);
  int initial_niceness = self->GetCachedNiceness();
  // Set java.lang.Thread cached niceness and Linux niceness to match.
  // O.w. ScopedPriorityChage does nothing.
  WellKnownClasses::java_lang_Thread_niceness->SetInt<false>(peer, 5);
  int ret = setpriority(PRIO_PROCESS, 0, 5);
  ASSERT_EQ(ret, 0);
  ASSERT_EQ(getpriority(PRIO_PROCESS, 0), 5);
  ASSERT_EQ(self->GetCachedNiceness(), 5);
  ASSERT_EQ(self->GetNicenessBeforeBoost(), Thread::kNotBoosted);
  {
    ScopedPriorityChange spc(self);
    ASSERT_EQ(getpriority(PRIO_PROCESS, 0), 5);
    ASSERT_EQ(self->GetNicenessBeforeBoost(), Thread::kNotBoosted);
    spc.SetToNormalOrBetter();
    ASSERT_EQ(getpriority(PRIO_PROCESS, 0), 0);
    ASSERT_EQ(self->GetNicenessBeforeBoost(), 5);
    {
      // Nested invocations have no effect.
      ScopedPriorityChange spc2(self);
      spc2.SetToNormalOrBetter();
      ASSERT_EQ(getpriority(PRIO_PROCESS, 0), 0);
      ASSERT_EQ(self->GetNicenessBeforeBoost(), 5);
    }
    ASSERT_EQ(getpriority(PRIO_PROCESS, 0), 0);
    ASSERT_EQ(self->GetNicenessBeforeBoost(), 5);
  }
  ASSERT_EQ(getpriority(PRIO_PROCESS, 0), 5);
  ASSERT_EQ(self->GetNicenessBeforeBoost(), Thread::kNotBoosted);
  WellKnownClasses::java_lang_Thread_niceness->SetInt<false>(peer, initial_niceness);
  ret = setpriority(PRIO_PROCESS, 0, initial_niceness);
  ASSERT_EQ(ret, 0);
#endif  // ART_TARGET_ANDROID
  // Else we are on host where we don't have permission to decrease niceness,
  // and thus can't effectively test.
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

class ThreadSuspendResumeTask : public Task {
 public:
  enum class Result : uint8_t {
    kStarted,
    kSuspended,
    kResumed,
    kSuspensionFailure,
    kResumptionFailure,
  };
  explicit ThreadSuspendResumeTask(uint32_t thread_id, Thread* expected_carrier, bool is_virtual,
    std::atomic<Result>* result)
      : thread_id_(thread_id), expected_carrier_(expected_carrier), is_virtual_(is_virtual),
      result_(result) {}

  void Run(Thread*) override {
    ThreadList* thread_list = Runtime::Current()->GetThreadList();
    Thread* carrier;
    SuspendReason reason = SuspendReason::kInternal;
    ThreadSuspensionResult r = thread_list->SuspendPlatformOrVirtualThread(thread_id_,
      reason, &carrier);
    if (r == ThreadSuspensionResult::kResultFailure) {
      result_->store(Result::kSuspensionFailure);
      return;
    }
    result_->store(Result::kSuspended);
    ThreadSuspensionResult expected = is_virtual_ ? ThreadSuspensionResult::kResultSuccessVirtual
        : ThreadSuspensionResult::kResultSuccessPlatform;
    EXPECT_EQ(expected, r);
    EXPECT_EQ(carrier, expected_carrier_);

    bool r2 = thread_list->ResumePlatformOrVirtualThread(thread_id_, carrier,
      r == ThreadSuspensionResult::kResultSuccessVirtual, reason);
    Result result = r2 ? Result::kResumed : Result::kResumptionFailure;
    result_->store(result);
  }

  void Finalize() override {
    delete this;
  }

 private:
  const uint32_t thread_id_;
  const Thread* expected_carrier_;
  bool is_virtual_;
  std::atomic<Result>* result_;
};

static void AssertThreadSuccessfulSuspension(Thread* self, uint32_t thread_id,
  Thread* expected_carrier,
  bool is_virtual) {
  std::unique_ptr<ThreadPool> thread_pool(ThreadPool::Create("the pool", 1));
  std::atomic<ThreadSuspendResumeTask::Result> result(ThreadSuspendResumeTask::Result::kStarted);
  ScopedObjectAccess soa(self);
  ThreadSuspendResumeTask* task = new ThreadSuspendResumeTask(thread_id, expected_carrier,
    is_virtual, &result);
  thread_pool->AddTask(self, task);
  thread_pool->StartWorkers(self);
  ScopedThreadSuspension sts(self, ThreadState::kSuspended);
  uint64_t start = MilliTime();
  const uint64_t timeout = 3 * 1000;  // 2s
  while (MilliTime() - start < timeout) {
    ThreadSuspendResumeTask::Result r = result.load();
    if (r != ThreadSuspendResumeTask::Result::kStarted &&
      r != ThreadSuspendResumeTask::Result::kSuspended) {
      break;
    }
  }
  EXPECT_EQ(ThreadSuspendResumeTask::Result::kResumed, result.load());
  thread_pool->Wait(self, /*do_work=*/false, /*may_hold_locks=*/false);
}

TEST_F(ThreadTest, TestSuspendResume) {
  ASSERT_TRUE(Runtime::Current() != nullptr);
  Thread* self = Thread::Current();
  ThreadList* thread_list = Runtime::Current()->GetThreadList();
  pid_t tid = self->GetTid();
  uint32_t carrier_id = self->GetThreadId();

  // Case 1: platform thread
  AssertThreadSuccessfulSuspension(self, carrier_id, self, false);

  if (!kIsVirtualThreadEnabled) {
    return;
  }

  // Case 2: unmounted virtual thread
  ScopedVirtualThreadId virtual_thread_id_holder;
  uint32_t virtual_thread_id = virtual_thread_id_holder.GetId();
  EXPECT_NE(virtual_thread_id, carrier_id);
  AssertThreadSuccessfulSuspension(self, virtual_thread_id, nullptr, true);

  // Case 3: mounted virtual thread
  MountedVirtualThreadData mounted_data(virtual_thread_id, carrier_id, 0);
  ScopedObjectAccess soa(self);
  VirtualThreadMounter virtual_thread_mounter(&mounted_data);
  AssertThreadSuccessfulSuspension(self, virtual_thread_id, self, true);
}

}  // namespace art
