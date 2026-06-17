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

#ifndef ART_RUNTIME_MIRROR_VIRTUAL_THREAD_CONTEXT_INL_H_
#define ART_RUNTIME_MIRROR_VIRTUAL_THREAD_CONTEXT_INL_H_

#include "mirror/virtual_thread_frame.h"
#include "obj_ptr.h"
#include "object-inl.h"
#include "object.h"
#include "verify_object.h"
#include "virtual_thread_context.h"
#include "well_known_classes.h"

namespace art HIDDEN {
namespace mirror {

inline int64_t VirtualThreadContext::GetId() { return GetField64(IdOffset()); }

template <bool kTransactionActive>
inline void VirtualThreadContext::SetId(int64_t id) {
  SetField64<kTransactionActive>(IdOffset(), id);
}

inline uint32_t VirtualThreadContext::GetMonitorThreadId() {
  return GetField32(MonitorThreadIdOffset());
}

inline ObjPtr<String> VirtualThreadContext::GetCarrierName() {
  return GetFieldObject<String>(CarrierNameOffset());
}
template <bool kTransactionActive>
inline void VirtualThreadContext::SetCarrierName(ObjPtr<String> name) {
  SetFieldObject<kTransactionActive>(CarrierNameOffset(), name);
}

inline ObjPtr<Object> VirtualThreadContext::GetTarget() {
  return GetFieldObject<Object>(TargetOffset());
}
template <bool kTransactionActive>
inline void VirtualThreadContext::SetTarget(ObjPtr<Object> target) {
  SetFieldObject<kTransactionActive>(TargetOffset(), target);
}

inline ObjPtr<Object> VirtualThreadContext::GetParkedStates() {
  return GetFieldObjectVolatile<Object>(ParkedStatesOffset());
}
template <bool kTransactionActive>
void VirtualThreadContext::SetParkedStates(ObjPtr<Object> states) {
  SetFieldObjectVolatile<kTransactionActive>(ParkedStatesOffset(), states);
}

inline ObjPtr<Object> VirtualThreadContext::GetPinnedCarrierThread() {
  return GetFieldObjectVolatile<Object>(PinnedCarrierThreadOffset());
}
template <bool kTransactionActive>
inline void VirtualThreadContext::SetPinnedCarrierThread(ObjPtr<Object> thread) {
  SetFieldObjectVolatile<kTransactionActive>(PinnedCarrierThreadOffset(), thread);
}

inline ObjPtr<ObjectArray<VirtualThreadFrame>> VirtualThreadContext::GetFramesArray() {
  ObjPtr<Object> parked_states = GetParkedStates();
  if (parked_states.IsNull()) {
    return nullptr;
  }
  return WellKnownClasses::dalvik_system_VirtualThreadParkedStates_frames->GetObject(parked_states)
      ->AsObjectArray<VirtualThreadFrame>();
}
template <bool kTransactionActive>
inline void VirtualThreadContext::SetFramesArray(
    ObjPtr<Object> parked_states, ObjPtr<ObjectArray<mirror::VirtualThreadFrame>> frames) {
  DCHECK(!parked_states.IsNull());
  WellKnownClasses::dalvik_system_VirtualThreadParkedStates_frames->SetObject<kTransactionActive>(
      parked_states, frames);
  SetParkedStates<kTransactionActive>(parked_states);
}

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_VIRTUAL_THREAD_CONTEXT_INL_H_
