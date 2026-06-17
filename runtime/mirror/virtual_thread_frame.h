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

#ifndef ART_RUNTIME_MIRROR_VIRTUAL_THREAD_FRAME_H_
#define ART_RUNTIME_MIRROR_VIRTUAL_THREAD_FRAME_H_

#include "base/macros.h"
#include "mirror/object_array.h"
#include "obj_ptr.h"
#include "object.h"
#include "string.h"

namespace art HIDDEN {

struct VirtualThreadFrameOffsets;

namespace mirror {

// C++ mirror of dalvik.system.VirtualThreadFrame
class MANAGED VirtualThreadFrame final : public Object {
 public:
  MIRROR_CLASS("Ldalvik/system/VirtualThreadFrame;");

  static ObjPtr<VirtualThreadFrame> Alloc(Thread* self) REQUIRES_SHARED(Locks::mutator_lock_);

  ObjPtr<Class> GetDeclaringClass() REQUIRES_SHARED(Locks::mutator_lock_);
  template <bool kTransactionActive = false>
  void SetDeclaringClass(ObjPtr<Class> clazz) REQUIRES_SHARED(Locks::mutator_lock_);

  ObjPtr<ByteArray> GetFrame() REQUIRES_SHARED(Locks::mutator_lock_);
  template <bool kTransactionActive = false>
  void SetFrame(ObjPtr<ByteArray> frame) REQUIRES_SHARED(Locks::mutator_lock_);

  ObjPtr<ObjectArray<Object>> GetRefs() REQUIRES_SHARED(Locks::mutator_lock_);
  template <bool kTransactionActive = false>
  void SetRefs(ObjPtr<ObjectArray<Object>> refs) REQUIRES_SHARED(Locks::mutator_lock_);

 private:
  HeapReference<Class> declaring_class_;
  HeapReference<ByteArray> frame_;
  HeapReference<ObjectArray<Object>> refs_;

  static constexpr MemberOffset DeclaringClassOffset() {
    return MemberOffset(OFFSETOF_MEMBER(VirtualThreadFrame, declaring_class_));
  }
  static constexpr MemberOffset FrameOffset() {
    return MemberOffset(OFFSETOF_MEMBER(VirtualThreadFrame, frame_));
  }
  static constexpr MemberOffset RefsOffset() {
    return MemberOffset(OFFSETOF_MEMBER(VirtualThreadFrame, refs_));
  }

  friend struct art::VirtualThreadFrameOffsets;  // For VisitReferences
  DISALLOW_IMPLICIT_CONSTRUCTORS(VirtualThreadFrame);
};

}  // namespace mirror
}  // namespace art

#endif  // ART_RUNTIME_MIRROR_VIRTUAL_THREAD_FRAME_H_
