/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "synthetic_class_format_util.h"

#include <gtest/gtest.h>

namespace art {

TEST(SyntheticClassFormatUtil, RewriteSyntheticProfileClassIfNeeded_Minimization) {
  // Lambda
  EXPECT_EQ(RewriteSyntheticProfileClassIfNeeded("LMyClass$$ExternalSyntheticLambda0;"),
            "LMyClass$$0;");

  // Outline
  EXPECT_EQ(RewriteSyntheticProfileClassIfNeeded("LMyClass$$ExternalSyntheticOutline33;"),
            "LMyClass$$33;");

  // Backport
  EXPECT_EQ(
      RewriteSyntheticProfileClassIfNeeded("LMyClass$$ExternalSyntheticBackportWithForwarding7;"),
      "LMyClass$$7;");

  // With Package
  EXPECT_EQ(RewriteSyntheticProfileClassIfNeeded("Lcom/foo/MyClass$$ExternalSyntheticLambda0;"),
            "Lcom/foo/MyClass$$0;");

  // As an array (not expected, but we should handle it gracefully)
  EXPECT_EQ(RewriteSyntheticProfileClassIfNeeded("[[LMyClass$$ExternalSyntheticLambda0;"),
            "[[LMyClass$$0;");
}

TEST(SyntheticClassFormatUtil, RewriteSyntheticProfileClassIfNeeded_Expansion) {
  // Assumed Lambda
  EXPECT_EQ(RewriteSyntheticProfileClassIfNeeded("LMyClass$$0;"),
            "LMyClass$$ExternalSyntheticLambda0;");

  // With Package
  EXPECT_EQ(RewriteSyntheticProfileClassIfNeeded("Lcom/foo/MyClass$$737;"),
            "Lcom/foo/MyClass$$ExternalSyntheticLambda737;");

  // As an array (not expected, but we should handle it gracefully)
  EXPECT_EQ(RewriteSyntheticProfileClassIfNeeded("[[Lcom/foo/MyClass$$737;"),
            "[[Lcom/foo/MyClass$$ExternalSyntheticLambda737;");
}

TEST(SyntheticClassFormatUtil, RewriteSyntheticProfileClassIfNeeded_Noop) {
  // No change (synthetic not at the end)
  EXPECT_EQ(RewriteSyntheticProfileClassIfNeeded("LMyClass$$ExternalSyntheticLambda0$Extra;"),
            std::nullopt);
  EXPECT_EQ(RewriteSyntheticProfileClassIfNeeded("LMyClass$$ExternalSyntheticLambda77Extra;"),
            std::nullopt);
  EXPECT_EQ(RewriteSyntheticProfileClassIfNeeded("LMyClass$$77$Extra;"), std::nullopt);
  EXPECT_EQ(RewriteSyntheticProfileClassIfNeeded("LMyClass$$Extra77;"), std::nullopt);
  EXPECT_EQ(RewriteSyntheticProfileClassIfNeeded("LMyClass$$77Extra;"), std::nullopt);

  // No change (not an R8 synthetic)
  EXPECT_EQ(RewriteSyntheticProfileClassIfNeeded("LMyClass$1;"), std::nullopt);

  // No change (standard inner class)
  EXPECT_EQ(RewriteSyntheticProfileClassIfNeeded("LMyClass$$Inner;"), std::nullopt);

  // No change (regular class)
  EXPECT_EQ(RewriteSyntheticProfileClassIfNeeded("LMyClass;"), std::nullopt);

  // No change (primitive type or primitive array)
  EXPECT_EQ(RewriteSyntheticProfileClassIfNeeded("[[B"), std::nullopt);
  EXPECT_EQ(RewriteSyntheticProfileClassIfNeeded("J"), std::nullopt);
}

}  // namespace art
