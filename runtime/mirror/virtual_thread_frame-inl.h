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

#ifndef ART_RUNTIME_MIRROR_VIRTUAL_THREAD_FRAME_INL_H_
#define ART_RUNTIME_MIRROR_VIRTUAL_THREAD_FRAME_INL_H_

#include "class_root-inl.h"
#include "class_root.h"
#include "obj_ptr.h"
#include "object-inl.h"
#include "object.h"
#include "verify_object.h"
#include "virtual_thread_frame.h"
#include "well_known_classes.h"

namespace art HIDDEN {
namespace mirror {

inline ObjPtr<VirtualThreadFrame> VirtualThreadFrame::Alloc(Thread* self) {
  ObjPtr<Class> clazz = GetClassRoot<VirtualThreadFrame>();
  return ObjPtr<VirtualThreadFrame>::DownCast(clazz->AllocObject(self));
}

inline ObjPtr<Class> VirtualThreadFrame::GetDeclaringClass() {
  return GetFieldObject<Class>(DeclaringClassOffset());
}

template <bool kTransactionActive>
inline void VirtualThreadFrame::SetDeclaringClass(ObjPtr<Class> clazz) {
  SetFieldObject<kTransactionActive>(DeclaringClassOffset(), clazz);
}

inline ObjPtr<ByteArray> VirtualThreadFrame::GetFrame() {
  return GetFieldObject<ByteArray>(FrameOffset());
}

template <bool kTransactionActive>
inline void VirtualThreadFrame::SetFrame(ObjPtr<ByteArray> frame) {
  SetFieldObject<kTransactionActive>(FrameOffset(), frame);
}

inline ObjPtr<ObjectArray<Object>> VirtualThreadFrame::GetRefs() {
  return GetFieldObject<ObjectArray<Object>>(RefsOffset());
}

template <bool kTransactionActive>
inline void VirtualThreadFrame::SetRefs(ObjPtr<ObjectArray<Object>> refs) {
  SetFieldObject<kTransactionActive>(RefsOffset(), refs);
}

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_VIRTUAL_THREAD_FRAME_INL_H_
