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

package com.android.server.art.utils;

import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Process;

import androidx.annotation.RequiresApi;

import com.android.internal.annotations.VisibleForTesting;

import java.util.concurrent.Callable;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.Executors;

/**
 * A background thread that executes tasks asynchronously.
 *
 * @hide
 */
@RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
public class AsyncExecutor {
    private final AsyncExecutorImpl mImpl;

    /** @hide */
    @VisibleForTesting
    public AsyncExecutor(AsyncExecutorImpl impl) {
        mImpl = impl;
    }

    /** Thread-safe static singleton. */
    private static class InstanceHolder {
        private static final AsyncExecutor INSTANCE =
                new AsyncExecutor(new DefaultAsyncExecutorImpl());
    }

    public static AsyncExecutor getInstance() {
        return InstanceHolder.INSTANCE;
    }

    /** Executes a task asynchronously. */
    public CompletableFuture<Void> executeAsync(Runnable runnable) {
        return executeDelayed(runnable, 0 /* delayMillis */);
    }

    /** Executes a task asynchronously. */
    public <T> CompletableFuture<T> executeAsync(Callable<T> callable) {
        return executeDelayed(callable, 0 /* delayMillis */);
    }

    /** Executes a task asynchronously with a delay. */
    public CompletableFuture<Void> executeDelayed(Runnable runnable, long delayMillis) {
        return executeDelayed(Executors.callable(runnable, null /* result */), delayMillis);
    }

    /** Executes a task asynchronously with a delay. */
    public <T> CompletableFuture<T> executeDelayed(Callable<T> callable, long delayMillis) {
        CompletableFuture<T> future = new CompletableFuture<>();
        boolean accepted = mImpl.executeDelayed(() -> {
            try {
                future.complete(callable.call());
            } catch (Exception e) {
                future.completeExceptionally(e);
            }
        }, future, delayMillis);
        Utils.check(accepted);
        return future;
    }

    /** Cancels a task. */
    public void cancelTask(CompletableFuture<?> future) {
        mImpl.cancelTask(future);
    }

    /**
     * Shuts down the executor. No more tasks can be scheduled after this call. This will cancel
     * all pending tasks but not the ones that are already running.
     */
    public void shutdown() {
        mImpl.shutdown();
    }

    public void awaitTermination(long timeoutMs) throws InterruptedException {
        mImpl.awaitTermination(timeoutMs);
    }

    public interface AsyncExecutorImpl {
        boolean executeDelayed(Runnable runnable, Object token, long delayMillis);
        void cancelTask(Object token);
        void shutdown();
        void awaitTermination(long timeoutMs) throws InterruptedException;
    }

    private static class DefaultAsyncExecutorImpl implements AsyncExecutorImpl {
        private final HandlerThread mHandlerThread;
        private final Handler mHandler;

        private DefaultAsyncExecutorImpl() {
            mHandlerThread =
                    new HandlerThread("ArtService-BgThread", Process.THREAD_PRIORITY_BACKGROUND);
            mHandlerThread.start();
            mHandler = new Handler(mHandlerThread.getLooper());
        }

        @Override
        public boolean executeDelayed(Runnable runnable, Object token, long delayMillis) {
            return mHandler.postDelayed(runnable, token, delayMillis);
        }

        @Override
        public void cancelTask(Object token) {
            mHandler.removeCallbacksAndMessages(token);
        }

        @Override
        public void shutdown() {
            // Stop the OS thread and drop pending runnables from the queue.
            mHandlerThread.quitSafely();
        }

        @Override
        public void awaitTermination(long timeoutMs) throws InterruptedException {
            mHandlerThread.join(timeoutMs);
        }
    }
}
