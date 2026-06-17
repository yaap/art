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

#ifndef ART_RUNTIME_OAT_MADVISE_UTILS_H_
#define ART_RUNTIME_OAT_MADVISE_UTILS_H_

#include <cstdint>
#include <vector>

namespace art {

// Metadata for a DexFile to help decide whether to madvise it.
struct DexFileMadviseMetadata {
  size_t index;
  size_t num_startup_classes;
  size_t num_classes;
  size_t num_startup_methods;
  size_t num_methods;
};

// Selects which dex files to madvise based on a list of per-dex profile metadata.
// Returns a vector of (unique) dex indices corresponding to the selected dex files.
//
// Note: The current logic returns indices in ascending order, but this is not a strict guarantee,
// and should not be assumed.
std::vector<size_t> SelectDexFilesToMadvise(std::vector<DexFileMadviseMetadata> dex_files);

}  // namespace art

#endif  // ART_RUNTIME_OAT_MADVISE_UTILS_H_
