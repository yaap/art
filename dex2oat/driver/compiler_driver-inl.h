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

#ifndef ART_DEX2OAT_DRIVER_COMPILER_DRIVER_INL_H_
#define ART_DEX2OAT_DRIVER_COMPILER_DRIVER_INL_H_

#include "compiler_driver.h"

#include "art_field-inl.h"
#include "art_method-inl.h"
#include "base/pointer_size.h"
#include "class_linker-inl.h"
#include "driver/dex_compilation_unit.h"
#include "handle_scope-inl.h"
#include "mirror/class_loader.h"
#include "mirror/dex_cache-inl.h"
#include "runtime.h"
#include "scoped_thread_state_change-inl.h"
#include "utils/atomic_dex_ref_map-inl.h"

namespace art {

inline ObjPtr<mirror::Class> CompilerDriver::ResolveClass(
    const ScopedObjectAccess& soa,
    Handle<mirror::DexCache> dex_cache,
    Handle<mirror::ClassLoader> class_loader,
    dex::TypeIndex cls_index,
    const DexCompilationUnit* mUnit) {
  DCHECK_EQ(dex_cache->GetDexFile(), mUnit->GetDexFile());
  DCHECK_EQ(class_loader.Get(), mUnit->GetClassLoader().Get());
  ObjPtr<mirror::Class> cls =
      mUnit->GetClassLinker()->ResolveType(cls_index, dex_cache, class_loader);
  DCHECK_EQ(cls == nullptr, soa.Self()->IsExceptionPending());
  if (UNLIKELY(cls == nullptr)) {
    // Clean up any exception left by type resolution.
    soa.Self()->ClearException();
  }
  return cls;
}

inline ObjPtr<mirror::Class> CompilerDriver::ResolveCompilingMethodsClass(
    const ScopedObjectAccess& soa,
    Handle<mirror::DexCache> dex_cache,
    Handle<mirror::ClassLoader> class_loader,
    const DexCompilationUnit* mUnit) {
  DCHECK_EQ(dex_cache->GetDexFile(), mUnit->GetDexFile());
  DCHECK_EQ(class_loader.Get(), mUnit->GetClassLoader().Get());
  const dex::MethodId& referrer_method_id =
      mUnit->GetDexFile()->GetMethodId(mUnit->GetDexMethodIndex());
  return ResolveClass(soa, dex_cache, class_loader, referrer_method_id.class_idx_, mUnit);
}

inline const CompilerDriver::CompiledMethodArray* CompilerDriver::GetCompiledMethods(
    const DexFile* dex_file) const {
  return compiled_methods_.GetArray(dex_file);
}

}  // namespace art

#endif  // ART_DEX2OAT_DRIVER_COMPILER_DRIVER_INL_H_
