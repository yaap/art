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

package com.android.server.art.testing;

import static com.google.common.truth.Truth.assertThat;

import static org.mockito.Mockito.any;
import static org.mockito.Mockito.anyLong;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.reset;

import android.os.Handler;
import android.os.HandlerThread;

import com.android.internal.annotations.GuardedBy;
import com.android.server.art.utils.AsyncExecutor;
import com.android.server.art.utils.AsyncExecutor.AsyncExecutorImpl;
import com.android.server.art.utils.Utils.Clock;
import com.android.server.art.utils.Utils.Sleeper;

import java.util.Comparator;
import java.util.PriorityQueue;

public class MockClock implements Clock, Sleeper {
    private final AsyncExecutor mExecutor;

    @GuardedBy("this") private volatile long mCurrentTimeMillis = 1000000000000L;
    @GuardedBy("this")
    private PriorityQueue<Task> mTasks =
            new PriorityQueue<>(Comparator.comparingLong(Task::scheduledTimeMillis));
    @GuardedBy("this") private boolean mIsShutdown = false;
    @GuardedBy("this") private int mSleeperCount = 0;

    public MockClock() {
        mExecutor = new AsyncExecutor(new MockAsyncExecutorImpl());
    }

    public AsyncExecutor getAsyncExecutor() {
        return mExecutor;
    }

    @Override
    public long currentTimeMillis() {
        return mCurrentTimeMillis;
    }

    @Override
    public synchronized void sleep(long durationMillis) throws InterruptedException {
        long targetTimeMillis = mCurrentTimeMillis + durationMillis;
        mSleeperCount++;
        this.notifyAll();
        try {
            while (mCurrentTimeMillis < targetTimeMillis) {
                this.wait();
            }
        } finally {
            mSleeperCount--;
        }
    }

    public synchronized void waitForSleepers(int expectedCount) throws InterruptedException {
        while (mSleeperCount < expectedCount) {
            this.wait();
        }
    }

    public synchronized void advanceTime(long timeMillis) {
        mCurrentTimeMillis += timeMillis;
        onUpdate();
    }

    public synchronized void setCurrentTimeMillis(long currentTimeMillis) {
        mCurrentTimeMillis = currentTimeMillis;
        onUpdate();
    }

    @GuardedBy("this")
    private void onUpdate() {
        while (!mTasks.isEmpty() && mTasks.peek().scheduledTimeMillis <= mCurrentTimeMillis) {
            mTasks.poll().runnable.run();
        }
        this.notifyAll();
    }

    private class MockAsyncExecutorImpl implements AsyncExecutorImpl {
        @Override
        public boolean executeDelayed(Runnable runnable, Object token, long delayMillis) {
            synchronized (MockClock.this) {
                assertThat(mIsShutdown).isFalse();
                mTasks.add(new Task(runnable, token, mCurrentTimeMillis + delayMillis));
            }
            return true;
        }

        @Override
        public void cancelTask(Object token) {
            synchronized (MockClock.this) {
                mTasks.removeIf(task -> task.token == token);
            }
        }

        @Override
        public void shutdown() {
            synchronized (MockClock.this) {
                mTasks.clear();
                mIsShutdown = true;
            }
        }

        @Override
        public void awaitTermination(long timeoutMs) throws InterruptedException {
            // No-op for mock clock.
        }
    }

    private record Task(Runnable runnable, Object token, long scheduledTimeMillis) {}
}
