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

#ifndef ART_RUNTIME_MONITOR_INL_H_
#define ART_RUNTIME_MONITOR_INL_H_

#include "android-base/macros.h"
#include "gc_root-inl.h"
#include "monitor.h"
#include "obj_ptr-inl.h"
#include "thread.h"
#include "thread_list.h"

namespace art HIDDEN {

template<ReadBarrierOption kReadBarrierOption>
inline ObjPtr<mirror::Object> Monitor::GetObject() REQUIRES_SHARED(Locks::mutator_lock_) {
  return obj_.Read<kReadBarrierOption>();
}

// Check for request to set lock owner info.
inline void Monitor::CheckLockOwnerRequest(Thread* self) {
  DCHECK(self != nullptr);
  MonitorOwner request_thread = lock_owner_request_.load(std::memory_order_relaxed);
  if (request_thread == self) {
    SetLockingMethod(self);
    // Only do this the first time after a request.
    lock_owner_request_.store(MonitorOwner(), std::memory_order_relaxed);
  }
}

inline uintptr_t Monitor::LockOwnerInfoChecksum(ArtMethod* m, uint32_t dex_pc, MonitorOwner owner) {
  uintptr_t dpc_and_thread = static_cast<uintptr_t>(dex_pc << 8) ^ owner.getStorageValue();
  return reinterpret_cast<uintptr_t>(m) ^ dpc_and_thread
      ^ (dpc_and_thread << (/* ptr_size / 2 */ (sizeof m) << 2));
}

inline void Monitor::SetLockOwnerInfo(ArtMethod* method, uint32_t dex_pc, MonitorOwner owner) {
  lock_owner_method_.store(method, std::memory_order_relaxed);
  lock_owner_dex_pc_.store(dex_pc, std::memory_order_relaxed);
  lock_owner_.store(owner, std::memory_order_relaxed);
  uintptr_t sum = LockOwnerInfoChecksum(method, dex_pc, owner);
  lock_owner_sum_.store(sum, std::memory_order_relaxed);
}

inline void Monitor::GetLockOwnerInfo(/*out*/ ArtMethod** method,
                                      /*out*/ uint32_t* dex_pc,
                                      MonitorOwner t) {
  ArtMethod* owners_method;
  uint32_t owners_dex_pc;
  MonitorOwner owner;
  uintptr_t owners_sum;
  DCHECK(t != nullptr);
  do {
    owner = lock_owner_.load(std::memory_order_relaxed);
    if (owner == nullptr) {
      break;
    }
    owners_method = lock_owner_method_.load(std::memory_order_relaxed);
    owners_dex_pc = lock_owner_dex_pc_.load(std::memory_order_relaxed);
    owners_sum = lock_owner_sum_.load(std::memory_order_relaxed);
  } while (owners_sum != LockOwnerInfoChecksum(owners_method, owners_dex_pc, owner));
  if (owner == t) {
    *method = owners_method;
    *dex_pc = owners_dex_pc;
  } else {
    *method = nullptr;
    *dex_pc = 0;
  }
}

inline bool MonitorOwner::IsOwner(const Thread* t) const {
  if (t == nullptr) {
    return false;
  }

  if (IsVirtualThread()) {
    DCHECK_EQ(Thread::Current(), t) << "Must be null or self to avoid data race of "
        "reading the virtual thread id";
    return t->IsVirtualThreadMounted() && GetVirtualThreadId() == t->GetVirtualThreadId();
  }

  return storage_ == reinterpret_cast<uintptr_t>(t);
}

inline MonitorOwner MonitorOwner::FromThread(const Thread* self) {
  DCHECK_EQ(self, Thread::Current())
      << "MonitorOwner::FromThread should only be called on the current thread. "
      << "Current tid: " << Thread::Current()->GetTid()
      << ", expected tid: " << self->GetTid();
  if (UNLIKELY(self != nullptr && self->IsVirtualThreadMounted())) {
    return FromVirtualThreadId(self->GetVirtualThreadId());
  }

  return MonitorOwner(reinterpret_cast<uintptr_t>(self));
}

inline MonitorOwner MonitorOwner::FromVirtualThreadId(int32_t id) {
  DCHECK(kIsVirtualThreadEnabled);
  DCHECK_GT(id, 0);
  return MonitorOwner(id << 1 | 1);
}

inline bool MonitorOwner::operator==(const Thread* t) const {
  if (t != nullptr && IsVirtualThread()) {
    DCHECK_EQ(Thread::Current(), t) << "Must be null or self to avoid data race of "
        "reading the virtual thread id";
    return t->IsVirtualThreadMounted() && GetVirtualThreadId() == t->GetVirtualThreadId();
  }

  return storage_ == reinterpret_cast<uintptr_t>(t);
}

inline bool MonitorOwner::IsVirtualThread() const {
  bool is_virtual = storage_ & 1;
  DCHECK(kIsVirtualThreadEnabled || !is_virtual);
  return is_virtual;
}

inline uint32_t MonitorOwner::GetVirtualThreadId() const {
  DCHECK(kIsVirtualThreadEnabled);
  DCHECK(IsVirtualThread());
  return storage_ >> 1;
}

inline uint32_t MonitorOwner::GetThreadId() const {
  if (IsNull()) {
    return ThreadList::kInvalidThreadId;
  }

  if (IsVirtualThread()) {
    return GetVirtualThreadId();
  }

  // Return GetThreadId(), not GetMonitorThreadId() because this monitor
  // is owned by the carrier thread when Thread::IsVirtualThreadMounted() is false,
  // regardless whether a virtual thread is mounted on this thread or not.
  return reinterpret_cast<Thread*>(storage_)->GetThreadId();
}

inline pid_t MonitorOwner::GetMutexOwnerId() const {
  if (IsNull()) {
    return ThreadList::kInvalidThreadId;
  }

  if (IsVirtualThread()) {
    return GetVirtualThreadId() | MonitorMutex::kVTFlag;
  }

  return GetThreadPtr()->GetTid();
}

}  // namespace art

#endif  // ART_RUNTIME_MONITOR_INL_H_
