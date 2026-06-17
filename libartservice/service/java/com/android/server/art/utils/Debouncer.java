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

package com.android.server.art.utils;

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.os.Build;

import androidx.annotation.RequiresApi;

import com.android.internal.annotations.GuardedBy;

import java.util.concurrent.CompletableFuture;

/**
 * A class that executes commands with a minimum interval.
 *
 * @hide
 */
@RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
public class Debouncer {
    @NonNull private final AsyncExecutor mAsyncExecutor;
    private final long mIntervalMs;
    @GuardedBy("this") @Nullable private CompletableFuture<?> mCurrentTask = null;

    public Debouncer(long intervalMs, @NonNull AsyncExecutor asyncExecutor) {
        mAsyncExecutor = asyncExecutor;
        mIntervalMs = intervalMs;
    }

    /**
     * Runs the given command after the interval has passed. If another command comes in during
     * this interval, the previous one will never run.
     */
    synchronized public void maybeRunAsync(@NonNull Runnable command) {
        if (mCurrentTask != null) {
            mAsyncExecutor.cancelTask(mCurrentTask);
        }
        mCurrentTask = mAsyncExecutor.executeDelayed(command, mIntervalMs);
    }

    synchronized public void cancel() {
        if (mCurrentTask != null) {
            mAsyncExecutor.cancelTask(mCurrentTask);
            mCurrentTask = null;
        }
    }
}
