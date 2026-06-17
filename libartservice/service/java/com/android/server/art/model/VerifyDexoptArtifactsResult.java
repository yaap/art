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

package com.android.server.art.model;

import android.annotation.FlaggedApi;
import android.annotation.NonNull;
import android.annotation.SystemApi;
import android.os.Build;

import androidx.annotation.RequiresApi;

import com.android.art.rw.flags.Flags;
import com.android.internal.annotations.Immutable;

/**
 * Result of {@link ArtManagerLocal#verifyDexoptArtifacts}.
 *
 * @hide
 */
// TODO(b/419024976): when available: @RequiresApi(Build.VERSION_CODES.CINNAMON_BUN_1)
@SystemApi(client = SystemApi.Client.SYSTEM_SERVER)
@FlaggedApi(Flags.FLAG_SECURE_COMPILATION)
@RequiresApi(Build.VERSION_CODES.CUR_DEVELOPMENT)
@Immutable
public final class VerifyDexoptArtifactsResult {
    private final boolean mIsVerified;

    /** @hide */
    public VerifyDexoptArtifactsResult(boolean isVerified) {
        mIsVerified = isVerified;
    }

    /** Returns whether the artifacts are verified. */
    @FlaggedApi(Flags.FLAG_SECURE_COMPILATION)
    public boolean isVerified() {
        return mIsVerified;
    }
}
