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

#include "madvise_utils.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <ranges>
#include <set>
#include <tuple>
#include <vector>

#include "base/logging.h"

namespace art {

namespace {

struct DexFileClassTraits {
  static constexpr const char* kName = "classes";
  static size_t NumStartup(const DexFileMadviseMetadata& m) { return m.num_startup_classes; }
  // The relative startup class density within the given dex file.
  static float StartupDensity(const DexFileMadviseMetadata& m) {
    return m.num_classes > 0 ? static_cast<float>(m.num_startup_classes) / m.num_classes : 0.0f;
  }
};

struct DexFileMethodTraits {
  static constexpr const char* kName = "methods";
  static size_t NumStartup(const DexFileMadviseMetadata& m) { return m.num_startup_methods; }
  // The relative startup method density within the given dex file.
  static float StartupDensity(const DexFileMadviseMetadata& m) {
    return m.num_methods > 0 ? static_cast<float>(m.num_startup_methods) / m.num_methods : 0.0f;
  }
};

// Ranks the candidate `dex_files` based on the startup density heuristic for the given traits,
// then appends to the existing `selected_file_indices` if not already present in the selection.
template <typename Traits>
void AppendDexFileSelection(std::vector<DexFileMadviseMetadata>& dex_files,
                            std::vector<size_t>& selected_file_indices) {
  // 1) Compute aggregate startup item count for use with accumulation logic.
  uint64_t total_startup_items = std::transform_reduce(
      dex_files.begin(), dex_files.end(), 0, std::plus<>(), Traits::NumStartup);
  if (total_startup_items == 0) {
    // As a minor optimization, short-circuit selection logic if no startup items were found.
    return;
  }

  // 2) Sort by descending importance on the tuple: (is_high_density, num_startup)
  constexpr float kHighStartupDensityThreshold = 0.50f;
  std::ranges::stable_sort(dex_files, std::greater{}, [](const auto& dex_file) {
    return std::make_tuple(Traits::StartupDensity(dex_file) >= kHighStartupDensityThreshold,
                           Traits::NumStartup(dex_file));
  });

  // If we're appending to an existing selection, we'll first check if new candidates have already
  // been selected. Otherwise, we can skip the check as a (minor) optimization.
  const bool check_before_append = !selected_file_indices.empty();
  selected_file_indices.reserve(dex_files.size());

  // Select if high startup density OR we haven't reached target accumulated startup coverage yet.
  constexpr double kAccumulatedStartupThreshold = 0.90;
  uint64_t accumulated_startup_items = 0;
  const uint64_t target_startup_items =
      static_cast<uint64_t>(std::ceil(kAccumulatedStartupThreshold * total_startup_items));
  for (const auto& dex_file : dex_files) {
    if (Traits::StartupDensity(dex_file) >= kHighStartupDensityThreshold) {
      VLOG(oat) << "Madvise - Selecting dex (>=50% startup " << Traits::kName
                << " density): " << dex_file.index;
    } else if (accumulated_startup_items < target_startup_items) {
      VLOG(oat) << "Madvise - Selecting dex (<90% overall startup " << Traits::kName
                << " accumulated): " << dex_file.index;
    } else {
      break;
    }
    accumulated_startup_items += Traits::NumStartup(dex_file);
    // While `std::find` on the vector can be inefficient, the range of dex file counts is small
    // enough that this shouldn't be a bottleneck.
    if (check_before_append &&
        std::find(selected_file_indices.begin(), selected_file_indices.end(), dex_file.index) !=
            selected_file_indices.end()) {
      continue;
    }
    selected_file_indices.push_back(dex_file.index);
  }
}

}  // namespace

std::vector<size_t> SelectDexFilesToMadvise(std::vector<DexFileMadviseMetadata> dex_files) {
  // Apply selection to classes then methods; startup class loads are more impactful to start times.
  std::vector<size_t> selected_file_indices;
  AppendDexFileSelection<DexFileClassTraits>(dex_files, selected_file_indices);
  AppendDexFileSelection<DexFileMethodTraits>(dex_files, selected_file_indices);
  if (selected_file_indices.size() < dex_files.size()) {
    VLOG(oat) << "Madvise - Skipping for " << dex_files.size() - selected_file_indices.size()
              << " non-startup dex files.";
  }
  // TODO(b/358590127): Tune the logic to accommodate DEX V41 string pooling. In particular, we
  // may want to explicitly handle common string pools placed in the *final* dex. The current
  // logic might short-circuit that madvise, and the impact on app startup is unclear.
  return selected_file_indices;
}

}  // namespace art
