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

import static com.google.common.truth.Truth.assertThat;

import static org.junit.Assert.assertThrows;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.reset;

import android.os.IBinder;

import androidx.test.filters.SmallTest;
import androidx.test.runner.AndroidJUnit4;

import com.android.server.art.IArtd;
import com.android.server.art.testing.MockClock;
import com.android.server.art.utils.ArtdRefCache;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.lang.ref.PhantomReference;
import java.lang.ref.ReferenceQueue;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;

@SmallTest
@RunWith(AndroidJUnit4.class)
public class AsyncExecutorTest {
    private MockClock mMockClock;
    private AsyncExecutor mExecutor;

    @Before
    public void setUp() {
        mMockClock = new MockClock();
        mExecutor = mMockClock.getAsyncExecutor();
    }

    @Test
    public void testExecuteAsyncVoid() {
        List<Integer> list = new ArrayList<>();
        CompletableFuture<Void> future = mExecutor.executeAsync(() -> { list.add(1); });
        assertThat(future.isDone()).isFalse();
        mMockClock.advanceTime(0);
        assertThat(future.isDone()).isTrue();
        assertThat(list).containsExactly(1);
    }

    @Test
    public void testExecuteAsyncWithResult() {
        CompletableFuture<String> future = mExecutor.executeAsync(() -> "foo");
        assertThat(future.isDone()).isFalse();
        mMockClock.advanceTime(0);
        assertThat(future.getNow(null)).isEqualTo("foo");
    }

    @Test
    public void testExecuteAsyncException() {
        CompletableFuture<String> future =
                mExecutor.executeAsync(() -> { throw new IllegalArgumentException("bar"); });
        mMockClock.advanceTime(0);
        assertThat(future.isCompletedExceptionally()).isTrue();
        ExecutionException e = assertThrows(ExecutionException.class, future::get);
        assertThat(e).hasCauseThat().isInstanceOf(IllegalArgumentException.class);
        assertThat(e).hasCauseThat().hasMessageThat().isEqualTo("bar");
    }

    @Test
    public void testExecuteDelayedVoid() {
        List<Integer> list = new ArrayList<>();
        CompletableFuture<Void> future = mExecutor.executeDelayed(() -> { list.add(1); }, 100);
        assertThat(future.isDone()).isFalse();
        assertThat(list).isEmpty();
        mMockClock.advanceTime(99);
        assertThat(future.isDone()).isFalse();
        assertThat(list).isEmpty();
        mMockClock.advanceTime(1);
        assertThat(future.isDone()).isTrue();
        assertThat(list).containsExactly(1);
    }

    @Test
    public void testExecuteDelayedWithResult() {
        CompletableFuture<String> future = mExecutor.executeDelayed(() -> "foo", 100);
        mMockClock.advanceTime(99);
        assertThat(future.isDone()).isFalse();
        mMockClock.advanceTime(1);
        assertThat(future.getNow(null)).isEqualTo("foo");
    }

    @Test
    public void testExecuteDelayedMultiple() {
        CompletableFuture<String> future1 = mExecutor.executeDelayed(() -> "foo", 100);
        CompletableFuture<String> future2 = mExecutor.executeDelayed(() -> "bar", 50);
        assertThat(future1.isDone()).isFalse();
        assertThat(future2.isDone()).isFalse();
        mMockClock.advanceTime(50);
        assertThat(future1.isDone()).isFalse();
        assertThat(future2.getNow(null)).isEqualTo("bar");
        mMockClock.advanceTime(50);
        assertThat(future1.getNow(null)).isEqualTo("foo");
        assertThat(future2.getNow(null)).isEqualTo("bar");
    }

    @Test
    public void testExecuteDelayedCancel() {
        List<Integer> list = new ArrayList<>();
        CompletableFuture<Void> future1 = mExecutor.executeDelayed(() -> { list.add(1); }, 100);
        mExecutor.cancelTask(future1);
        mMockClock.advanceTime(100);
        assertThat(future1.isDone()).isFalse();
        assertThat(list).isEmpty();
    }

    @Test
    public void testShutdown() {
        List<Integer> list = new ArrayList<>();
        CompletableFuture<Void> future1 = mExecutor.executeDelayed(() -> { list.add(1); }, 100);
        CompletableFuture<Void> future2 = mExecutor.executeDelayed(() -> { list.add(2); }, 50);
        mExecutor.shutdown();
        assertThat(future1.isDone()).isFalse();
        assertThat(future2.isDone()).isFalse();
        assertThat(list).isEmpty();

        // MockClock asserts that we don't post tasks after shutdown.
        assertThrows(AssertionError.class, () -> mExecutor.executeAsync(() -> {}));
    }

    // A complex case to test that things are GC-able after AsyncExecutor is shut down.
    //
    // `ArtdRefCache` internally uses `Debouncer` which uses `AsyncExecutor`. If `AsyncExecutor`
    // somehow holds an unexpected reference to `ArtdRefCache` after the shutdown, then
    // `ArtdRefCache` will not be GC-able.
    @Test
    public void testShutdownGcable() throws Exception {
        var injector = mock(ArtdRefCache.Injector.class);
        var artd = mock(IArtd.class);
        var binder = mock(IBinder.class);
        lenient().when(injector.getAsyncExecutor()).thenReturn(mMockClock.getAsyncExecutor());
        lenient().when(injector.getArtd()).thenReturn(artd);
        lenient().when(artd.asBinder()).thenReturn(binder);
        var artdRefCache = new ArtdRefCache(injector);

        var queue = new ReferenceQueue<ArtdRefCache>();
        var phantomRef = new PhantomReference(artdRefCache, queue);

        try (var pin = artdRefCache.new Pin()) {
            artdRefCache.getArtd();
        }

        mMockClock.getAsyncExecutor().shutdown();
        artdRefCache = null;

        // Mockito mocks hold the arguments of historical calls. `reset` removes them.
        reset(binder);

        Runtime.getRuntime().gc();
        Runtime.getRuntime().runFinalization();

        // The reference is enqueued if it's GC-able.
        assertThat(phantomRef.isEnqueued()).isTrue();
    }
}
