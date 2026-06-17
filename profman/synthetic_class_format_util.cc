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

#include <format>
#include <string_view>

#include "dex/descriptors_names.h"

namespace art {

// The expanded separator used historically by R8 for verbose synthetic class names.
static constexpr std::string_view kVerboseSyntheticSeparator = "$$ExternalSynthetic";

// The custom minimized separator used by R8 in the platform for minimal synthetic class names.
// We define this separately from the verbose separator for clarity and safety.
// See also the RELEASE_R8_MINIMIZE_SYNTHETIC_NAMES build flag.
// NOTE: This should be kept in sync with related references in the `prebuilts/r8/` repo.
// Unfortunately, `IfChange` lint rules don't support cross project references.
static constexpr std::string_view kMinimizedSyntheticSeparator = "$$";

std::optional<std::string> RewriteSyntheticProfileClassIfNeeded(std::string_view klass) {
  if (klass.empty()) {
    return std::nullopt;
  }

  DCHECK(IsValidDescriptor(std::string(klass).c_str()));

  // Ignore primitives or primitive arrays.
  if (!klass.ends_with(';')) {
    return {};
  }

  // Strip the trailing semicolon.
  std::string_view body = klass.substr(0, klass.size() - 1);

  // Find the split point where digits start at the end (e.g., "LBar$$1" -> "LBar$$"+"1").
  size_t last_non_digit_idx = body.find_last_not_of("0123456789");
  if (last_non_digit_idx == std::string::npos) {
    return std::nullopt;
  }

  size_t digit_idx = last_non_digit_idx + 1;
  if (digit_idx >= body.size()) {
    return std::nullopt;
  }

  std::string_view prefix = body.substr(0, digit_idx);
  std::string_view digits = body.substr(digit_idx);

  // Case 1: Minimization (e.g., $$ExternalSyntheticLambda7 -> $$7)
  if (size_t pos = prefix.find(kVerboseSyntheticSeparator); pos != std::string_view::npos) {
    return std::format("{}{}{};", prefix.substr(0, pos), kMinimizedSyntheticSeparator, digits);
  }

  // Case 2: Expansion (e.g., $$7 -> $$ExternalSyntheticLambda7)
  // Note: This rewrite is purely *speculative*. We assume Lambda only because that has historically
  // been the most represented synthetic present in platform profiles.
  if (prefix.ends_with(kMinimizedSyntheticSeparator)) {
    // Explicitly remove the minimized separator from the end of the prefix before appending the
    // full expanded separator. This avoids assuming any relationship between the two.
    std::string_view base_prefix =
        prefix.substr(0, prefix.size() - kMinimizedSyntheticSeparator.size());
    return std::format("{}{}Lambda{};", base_prefix, kVerboseSyntheticSeparator, digits);
  }

  return std::nullopt;
}

}  // namespace art
