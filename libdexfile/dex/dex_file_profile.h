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

#ifndef ART_LIBDEXFILE_DEX_DEX_FILE_PROFILE_H_
#define ART_LIBDEXFILE_DEX_DEX_FILE_PROFILE_H_

#include <cstdint>
#include <type_traits>

namespace art {

// Optional per-dex profile summary metadata.
class DexProfileMetadata {
 public:
  uint32_t num_startup_classes = 0;
  uint32_t num_startup_methods = 0;
};

static_assert(std::is_trivially_copyable_v<DexProfileMetadata>,
              "DexProfileMetadata should be trivially copyable for binary dumping.");

}  // namespace art

#endif  // ART_LIBDEXFILE_DEX_DEX_FILE_PROFILE_H_
