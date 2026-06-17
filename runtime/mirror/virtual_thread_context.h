/*
 * Copyright (C) 2025 The Android Open Source Project
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

#ifndef ART_RUNTIME_MIRROR_VIRTUAL_THREAD_CONTEXT_H_
#define ART_RUNTIME_MIRROR_VIRTUAL_THREAD_CONTEXT_H_

#include "base/macros.h"
#include "obj_ptr.h"
#include "object.h"
#include "string.h"
#include "virtual_thread_frame.h"

namespace art HIDDEN {

struct VirtualThreadContextOffsets;

namespace mirror {

// C++ mirror of dalvik.system.VirtualThreadContext
class MANAGED VirtualThreadContext final : public Object {
 public:
  MIRROR_CLASS("Ldalvik/system/VirtualThreadContext;");

  int64_t GetId() REQUIRES_SHARED(Locks::mutator_lock_);
  template <bool kTransactionActive = false>
  void SetId(int64_t id) REQUIRES_SHARED(Locks::mutator_lock_);

  uint32_t GetMonitorThreadId() REQUIRES_SHARED(Locks::mutator_lock_);

  ObjPtr<String> GetCarrierName() REQUIRES_SHARED(Locks::mutator_lock_);
  template <bool kTransactionActive = false>
  void SetCarrierName(ObjPtr<String> name) REQUIRES_SHARED(Locks::mutator_lock_);

  ObjPtr<Object> GetTarget() REQUIRES_SHARED(Locks::mutator_lock_);
  template <bool kTransactionActive = false>
  void SetTarget(ObjPtr<Object> target) REQUIRES_SHARED(Locks::mutator_lock_);

  ObjPtr<Object> GetParkedStates() REQUIRES_SHARED(Locks::mutator_lock_);
  template <bool kTransactionActive = false>
  void SetParkedStates(ObjPtr<Object> states) REQUIRES_SHARED(Locks::mutator_lock_);

  ObjPtr<Object> GetPinnedCarrierThread() REQUIRES_SHARED(Locks::mutator_lock_);
  template <bool kTransactionActive = false>
  void SetPinnedCarrierThread(ObjPtr<Object> thread) REQUIRES_SHARED(Locks::mutator_lock_);

  ObjPtr<ObjectArray<VirtualThreadFrame>> GetFramesArray() REQUIRES_SHARED(Locks::mutator_lock_);
  template <bool kTransactionActive = false>
  void SetFramesArray(ObjPtr<Object> parked_states, ObjPtr<ObjectArray<VirtualThreadFrame>> frames)
      REQUIRES_SHARED(Locks::mutator_lock_);

 private:
  HeapReference<String> carrier_name_;
  HeapReference<Object> monitor_thread_id_cleanable_;
  HeapReference<Object> parked_states_;
  HeapReference<Object> pinned_carrier_thread_;
  HeapReference<Object> target_;
  uint32_t monitor_thread_id_;
  int64_t id_;

  static constexpr MemberOffset IdOffset() {
    return MemberOffset(OFFSETOF_MEMBER(VirtualThreadContext, id_));
  }
  static constexpr MemberOffset MonitorThreadIdOffset() {
    return MemberOffset(OFFSETOF_MEMBER(VirtualThreadContext, monitor_thread_id_));
  }
  static constexpr MemberOffset CarrierNameOffset() {
    return MemberOffset(OFFSETOF_MEMBER(VirtualThreadContext, carrier_name_));
  }
  static constexpr MemberOffset ParkedStatesOffset() {
    return MemberOffset(OFFSETOF_MEMBER(VirtualThreadContext, parked_states_));
  }
  static constexpr MemberOffset PinnedCarrierThreadOffset() {
    return MemberOffset(OFFSETOF_MEMBER(VirtualThreadContext, pinned_carrier_thread_));
  }
  static constexpr MemberOffset TargetOffset() {
    return MemberOffset(OFFSETOF_MEMBER(VirtualThreadContext, target_));
  }

  friend struct art::VirtualThreadContextOffsets;  // For VisitReferences
  DISALLOW_IMPLICIT_CONSTRUCTORS(VirtualThreadContext);
};

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_VIRTUAL_THREAD_CONTEXT_H_
