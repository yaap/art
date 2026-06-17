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

import android.system.Os;

import dalvik.system.VirtualThreadContext;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * Verify that a virtual thread isn't pinned on a carrier thread when parking in
 * synchronized block.
 */
public class Main {

    private static final Object MONITOR = new Object();

    public static void main(String[] args) throws InterruptedException {
        if (!com.android.art.flags.Flags.virtualThreadImplV1()) {
            return;
        }
        // Exit if the thread throws any exception.
        Thread.setDefaultUncaughtExceptionHandler((t, e) -> {
            System.err.println("thread: " + t.getName());
            e.printStackTrace(System.err);
            System.exit(1);
        });

        VirtualThreadContext context = startVirtualThreadAndVerifyNoPinning();
        Thread carrier2 = Thread.unparkVirtual(context);
        carrier2.join();

        runContendedMonitor();
        runContendedMonitor2();
    }

    private static void runContendedMonitor() throws InterruptedException {
        CountDownLatch contendedThreadStartLatch = new CountDownLatch(1);
        CountDownLatch releaseLatch = new CountDownLatch(1);
        Thread thread1 = Thread.ofVirtual().unstarted(() -> {
            synchronized (MONITOR) {
                contendedThreadStartLatch.countDown();;
                try {
                    if (!releaseLatch.await(1, TimeUnit.SECONDS)) {
                        throw new RuntimeException("Time out!");
                    }
                    Thread.sleep(100);
                } catch (InterruptedException e) {
                    throw new RuntimeException(e);
                }
            }
        });

        Thread thread2 = new Thread(() -> {
            try {
                releaseLatch.countDown();
                synchronized (MONITOR) {
                    Thread.sleep(10);
                }
            } catch (InterruptedException e) {
                throw new RuntimeException(e);
            }
        });

        thread1.start();
        contendedThreadStartLatch.await(1, TimeUnit.SECONDS);
        thread2.start();
        thread1.join();
        thread2.join();
    }


    private static void runContendedMonitor2() throws InterruptedException {
        CountDownLatch contendedThreadStartLatch = new CountDownLatch(1);
        CountDownLatch releaseLatch = new CountDownLatch(1);
        Thread thread1 = new Thread(() -> {
            synchronized (MONITOR) {
                try {
                    contendedThreadStartLatch.countDown();;
                    if (!releaseLatch.await(1, TimeUnit.SECONDS)) {
                        throw new RuntimeException("Time out!");
                    }
                    Thread.sleep(100);
                } catch (InterruptedException e) {
                    throw new RuntimeException(e);
                }
            }
        });

        Thread thread2 = Thread.ofVirtual().unstarted(() -> {
            try {
                releaseLatch.countDown();
                synchronized (MONITOR) {
                    Thread.sleep(10);
                }
            } catch (InterruptedException e) {
                throw new RuntimeException(e);
            }
        });

        thread1.start();
        contendedThreadStartLatch.await(1, TimeUnit.SECONDS);
        thread2.start();
        thread1.join();
        thread2.join();
    }

    private static VirtualThreadContext startVirtualThreadAndVerifyNoPinning()
            throws InterruptedException {
        VirtualThreadContext context = startVirtualThreadAndGetParkedContext();
        if (context.parkedStates == null) {
            throw new AssertionError("virtual thread should be unmounted");
        }
        return context;
    }

    private static VirtualThreadContext startVirtualThreadAndGetParkedContext()
            throws InterruptedException {
        Thread carrier = Thread.startVirtual(Main::task);
        VirtualThreadContext context = carrier.getVirtualThreadContext();
        carrier.join();
        if (!context.isParked()) {
            throw new IllegalStateException("Expect a parked virtual thread context.");
        }
        return context;
    }

    private static void task() {
        int tid1 = Os.gettid();
        long threadId1 = getCarrierThreadId();
        synchronized (MONITOR) {
            if (!Thread.holdsLock(MONITOR)) {
                throw new AssertionError("Lock should be held");
            }
            Thread.parkVirtual();
            if (!Thread.holdsLock(MONITOR)) {
                throw new AssertionError("Lock should be held");
            }
        }

        int tid2 = Os.gettid();
        long threadId2 = getCarrierThreadId();
        // Verify that the 2 carrier threads are identical.
        if (tid1 == tid2) {
            throw new AssertionError("tid shouldn't be the same: "
                    + tid1 + " != " + tid2);
        }
        if (threadId1 == threadId2) {
            throw new AssertionError("tid shouldn't be the same: "
                    + threadId1 + " != " + threadId2);
        }
    }

    /**
     * This method is extracted to avoid holding a reference to the carrier thread.
     */
    private static long getCarrierThreadId() {
        return Thread.currentThread().threadId();
    }
}
