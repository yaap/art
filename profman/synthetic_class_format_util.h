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

#ifndef ART_PROFMAN_SYNTHETIC_CLASS_FORMAT_UTIL_H_
#define ART_PROFMAN_SYNTHETIC_CLASS_FORMAT_UTIL_H_

#include <optional>
#include <string>

namespace art {

// Helper to rewrite `$$ExternalSynthetic`... to `$...` or vice versa for class descriptors.
// Returns true if any rewriting was performed.
// TODO(b/185560004): Remove after fully migrating to streamlined synthetic names and ramping the
// underlying RELEASE_R8_MINIMIZE_SYNTHETIC_NAMES build flag.
std::optional<std::string> RewriteSyntheticProfileClassIfNeeded(std::string_view klass);

}  // namespace art

#endif  // ART_PROFMAN_SYNTHETIC_CLASS_FORMAT_UTIL_H_
