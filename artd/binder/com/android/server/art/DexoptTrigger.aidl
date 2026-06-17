/*
 * Copyright (C) 2022 The Android Open Source Project
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

package com.android.server.art;

/**
 * Represents the conditions where dexopt should be performed.
 *
 * @hide
 */
parcelable DexoptTrigger {
    @Backing(type="int")
    enum DexoptComparator {
        /**
         * Compares the compiler filter of the current and target dexopt state. Higher is better.
         *
         * This is a primary comparator.
         */
        COMPARING_COMPILER_FILTER = 0,
        /**
         * Compares the compiler filter of the current and target dexopt state. Lower is better.
         *
         * This is a primary comparator.
         */
        COMPARING_COMPILER_FILTER_REVERSED = 1,
        /**
         * Compares the primary boot image availability at the time of current dexopt state creation
         * against its projected availability for the target state. Only relevant for compiler
         * filters that involves compilation (e.g. `speed` and `speed-profile`).
         *
         * This is a secondary comparator.
         */
        COMPARING_PRIMARY_BOOT_IMAGE_STATUS = 2,
        /**
         * Compares the presence of extracted DEX code in the current dexopt state against its
         * projected presence in the target state. Only relevant for compressed DEX files.
         *
         * This is a secondary comparator.
         */
        COMPARING_EXTRACTION_STATUS = 3,
        /**
         * A custom comparator indicating that the target dexopt state is better than the current.
         *
         * This is a primary comparator.
         */
        CUSTOM_TARGET_IS_BETTER_THAN_CURRENT = 4,
        /**
         * A custom comparator indicating that the target dexopt state is worse than the current.
         *
         * This is a primary comparator.
         */
        CUSTOM_TARGET_IS_WORSE_THAN_CURRENT = 5,
    }

    /**
     * A list of `DexoptComparator`s used to compare the current dexopt state with the target
     * dexopt state, in the order of precedence. The dexopt is needed if the target dexopt state is
     * better than the current.
     *
     * The list must contain a primary comparator as the first element, followed by any number of
     * additional primary or secondary comparators.
     */
    DexoptComparator[] dexoptComparators;

    /**
     * A string that describes the reason for using a custom comparator. This is only used when
     * `dexoptComparators` contains `CUSTOM_TARGET_IS_BETTER_THAN_CURRENT` or
     * `CUSTOM_TARGET_IS_WORSE_THAN_CURRENT`.
     */
    @nullable @utf8InCpp String customComparatorReason;
}

