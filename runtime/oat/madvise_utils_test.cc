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

#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace art {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

TEST(MadviseUtilsTest, SelectDexFilesToMadvise_Empty) {
  std::vector<DexFileMadviseMetadata> dex_files;
  std::vector<size_t> selected = SelectDexFilesToMadvise(dex_files);
  EXPECT_THAT(selected, IsEmpty());
}

TEST(MadviseUtilsTest, SelectDexFilesToMadvise_NoStartupInfo) {
  std::vector<DexFileMadviseMetadata> dex_files = {
      {0, 0, 100, 0, 100},
      {1, 0, 100, 0, 100},
  };
  std::vector<size_t> selected = SelectDexFilesToMadvise(dex_files);
  EXPECT_THAT(selected, IsEmpty());
}

TEST(MadviseUtilsTest, SelectDexFilesToMadvise_StartupMethodsPrioritized) {
  // Dex 0: 95 methods, 95 startup (100%)
  // Dex 1: 10 methods, 5 startup   (50%)
  // Total startup: 100.
  // Dex 0 covers >90% of total startup.
  // Dex 1 has >50% density.

  std::vector<DexFileMadviseMetadata> dex_files = {
      {0, 0, 0, 95, 95},
      {1, 0, 0, 5, 10},
  };
  std::vector<size_t> selected = SelectDexFilesToMadvise(dex_files);

  // Both should be selected, as both satisfy minimum criteria.
  ASSERT_THAT(selected, ElementsAre(0u, 1u));
}

TEST(MadviseUtilsTest, SelectDexFilesToMadvise_StartupClassesPrioritized) {
  // Dex 0: 100 classes, 90 startup (90%)
  // Dex 1: 100 classes, 10 startup (10%)

  std::vector<DexFileMadviseMetadata> dex_files = {
      {0, 90, 100, 0, 0},
      {1, 10, 100, 0, 0},
  };
  std::vector<size_t> selected = SelectDexFilesToMadvise(dex_files);

  ASSERT_THAT(selected, ElementsAre(0u));
}

TEST(MadviseUtilsTest, SelectDexFilesToMadvise_MixedPriorities) {
  // Dex 0: Low class startup, High method startup
  // Dex 1: Med class startup, High method startup
  // Dex 2: High class startup, High method startup
  // Dex 3: Low class startup, Med method startup
  // Dex 4: Med class startup, Med method startup
  // Dex 5: High class startup, Med method startup
  // Dex 6: Low class startup, Low method startup
  // Dex 7: Med class startup, Low method startup
  // Dex 8: High class startup, Low method startup

  std::vector<DexFileMadviseMetadata> dex_files = {
      {0, 0, 100, 90, 100},
      {1, 50, 100, 90, 100},
      {2, 90, 100, 90, 100},
      {3, 0, 100, 50, 100},
      {4, 50, 100, 50, 100},
      {5, 90, 100, 50, 100},
      {6, 0, 100, 0, 100},
      {7, 50, 100, 0, 100},
      {8, 90, 100, 0, 100},
  };
  std::vector<size_t> selected = SelectDexFilesToMadvise(dex_files);

  // 1) Class startup density takes precedence over method startup density.
  // 2) High startup density takes precedence over medium and low density.
  // 3) Medium density still qualifies.
  // High class (2,5,8) -> med class (1,4,7) -> remaining high/med method (0,3)
  ASSERT_THAT(selected, ElementsAre(2u, 5u, 8u, 1u, 4u, 7u, 0u, 3u));
}

TEST(MadviseUtilsTest, SelectDexFilesToMadvise_LowDensityButNeededForTotal) {
  // Dex 0: 100 methods, 40 startup (40%).
  // Dex 1: 100 methods,  0 startup ( 0%).
  // Dex 2: 100 methods, 40 startup (40%).
  // Dex 3: 100 methods, 10 startup (10%).
  // Dex 4: 100 methods, 10 startup (10%).
  // Total startup: 100. Target: 90.

  std::vector<DexFileMadviseMetadata> dex_files = {
      {0, 0, 0, 40, 100},
      {1, 0, 0, 0, 100},
      {2, 0, 0, 40, 100},
      {3, 0, 0, 10, 100},
      {4, 0, 0, 10, 100},
  };
  std::vector<size_t> selected = SelectDexFilesToMadvise(dex_files);

  ASSERT_THAT(selected, ElementsAre(0u, 2u, 3u));
}

}  // namespace art
