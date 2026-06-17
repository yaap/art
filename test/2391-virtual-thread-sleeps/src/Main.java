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

import dalvik.system.VirtualThreadContext;

import java.text.DateFormat;
import java.util.Date;
import java.util.Timer;
import java.util.TimerTask;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.ForkJoinPool;
import java.util.concurrent.atomic.AtomicInteger;

import jdk.internal.access.SharedSecrets;

/**
 * Implement a thread sleeping for virtual thread on top of a single-threaded {@link Timer}.
 * Each sleeping virtual thread should take ~1 KB or less memory. It doesn't use
 * {@link ForkJoinPool} to avoid the fixed memory cost of cached platform threads and
 * doesn't allocate {@link VirtualThread} objects to avoid additional heap usage, though it takes
 * extra time to start and unpark a virtual thread.
 */
public class Main {

    private static final boolean DEBUG = false;
    private static final DateFormat TIME_FORMAT = DateFormat.getTimeInstance();

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

        testNSleepingThreads(2);
        testNSleepingThreads(10);
        testNSleepingThreads(100);
        // TODO: Re-enable this test when this test is stable enough in the CI.
        // testNSleepingThreads(500);
        // testNSleepingThreads(1000);
        // testNSleepingThreads(3000);
    }

    /**
     * Start {@code numOfThreads} virtual threads sleeping for the given duration and wait until
     * all threads join or time out.
     * For the heap size limit, one virtual thread in the parked state takes ~1kB heap. Too many
     * virtual threads can cause {@link OutOfMemoryError}.
     * @param numOfThreads number of concurrent virtual threads sleeping
     */
    private static void testNSleepingThreads(int numOfThreads) throws InterruptedException {
        long sleepDurationMs = Math.max(numOfThreads / 10, 100);

        debugPrintln("Start creating test case for " + numOfThreads + " threads sleeping for " +
                sleepDurationMs + "ms at " + timeNow());
        try (SleepingVirtualThreadTestCase test =
                     new SleepingVirtualThreadTestCase(numOfThreads, sleepDurationMs)) {
            debugPrintln("End creating test case for " + numOfThreads + " at " + timeNow());
            test.run();
        }
    }

    private static void debugPrintln(String msg) {
        if (DEBUG) {
             System.out.println(msg);
        }
    }

    private static String timeNow() {
        return TIME_FORMAT.format(new Date(System.currentTimeMillis()));
    }

    /**
     * Starts the given number of virtual threads which sleeps for the given duration concurrently.
     * Instead of backed by {@link ForkJoinPool}, this virtual thread scheduler always polls and
     * determines the next task from a FIFO queue in the main thread before starting a
     * carrier thread. It keeps tracking the number of running carrier threads to avoid exceed the
     * specific limit CARRIER_THREADS_LIMIT. Too many running carrier threads may
     * exhaust the virtual memory address or cause memory fragmentation on 32-bit ASAN build
     * http://b/435082121.
     */
    private static class SleepingVirtualThreadTestCase implements AutoCloseable {
        private static final JavaLangAccess JLA = SharedSecrets.getJavaLangAccess();
        private static final int CARRIER_THREADS_LIMIT = 32;
        private static final int JOIN_TIMEOUT_MS = 2 * 1000;
        private static final int TIMEOUT_MULTIPLIER = 200;
        private final Timer mTimer = new Timer();
        private final int mNumOfThreads;
        private final long mSleepDurationMs;
        private final long mTimeOutMs;

        private final AtomicInteger mRunningTasksCounter;

        private final ConcurrentLinkedQueue<Runnable> mPendingTasks;

        private final ConcurrentLinkedQueue<Thread> mFinishingCarriers;

        // Use a separate counter because mFinishingCarriers.size() is slow with
        // --gcstress and --gcverify.
        private final AtomicInteger mFinishingCarriersCounter;

        public SleepingVirtualThreadTestCase(int numOfThreads, long sleepDurationMs) {
            mNumOfThreads = numOfThreads;
            mSleepDurationMs = sleepDurationMs;
            mTimeOutMs = mSleepDurationMs * TIMEOUT_MULTIPLIER;
            mRunningTasksCounter = new AtomicInteger(0);
            mPendingTasks = new ConcurrentLinkedQueue<>();
            mFinishingCarriers = new ConcurrentLinkedQueue<>();
            mFinishingCarriersCounter = new AtomicInteger(0);
        }

        public void run() throws InterruptedException {
            debugPrintln("Start inserting tasks at " + timeNow());
            for (int i = 0; i < mNumOfThreads; i++) {
                Runnable task = () -> {
                    Thread t = startSleepingThread(mSleepDurationMs, mTimer,
                            mRunningTasksCounter, mPendingTasks, mFinishingCarriers,
                            mFinishingCarriersCounter);
                    VirtualThreadContext vtContext = t.getVirtualThreadContext();
                    debugPrintln("Thread " + vtContext.id + " started at " + timeNow());
                };
                mPendingTasks.add(task);
            }

            debugPrintln("End inserting tasks at " + timeNow());

            long startTime = nowForElapsedTimeMillis();
            debugPrintln("Started at " + timeNow());

            int iterations = 0;
            while (true) {
                int finishCarriersCount = mFinishingCarriersCounter.get();
                if (finishCarriersCount >= mNumOfThreads) {
                    break;
                }
                int runningTaskCount = mRunningTasksCounter.get();
                long duration = nowForElapsedTimeMillis() - startTime;
                if (duration > mTimeOutMs) {
                    throw new IllegalStateException("Timeout : " + duration + " ms, " +
                            "Expect " + mNumOfThreads + " threads joining, but only " +
                            finishCarriersCount + " threads joined. " +
                            "Num of running tasks: " + runningTaskCount +
                            ", Num of pending tasks: " + mPendingTasks.size() +
                            ", iterations: " + iterations + ", at: " + timeNow());
                }
                // It's okay to check and increment the threshold non-atomically because
                // the counter is incremented only on this thread.
                while (runningTaskCount < CARRIER_THREADS_LIMIT) {
                    Runnable task = mPendingTasks.poll();
                    if (task == null) {
                        break;
                    }
                    runningTaskCount = mRunningTasksCounter.incrementAndGet();
                    task.run();
                }
                iterations++;
            }

            long duration = nowForElapsedTimeMillis() - startTime;
            debugPrintln("Waiting for " + mNumOfThreads + " threads to join after " +
                    duration + "ms at " + timeNow());
            while (!mFinishingCarriers.isEmpty()) {
                Thread t = mFinishingCarriers.poll();
                if (t == null) {
                    continue;
                }
                t.join(JOIN_TIMEOUT_MS);
            }

            int pendingCount = mPendingTasks.size();
            if (pendingCount != 0) {
                throw new IllegalStateException("Expected zero pending tasks, but got " +
                        pendingCount);
            }

            duration = nowForElapsedTimeMillis() - startTime;
            debugPrintln(mNumOfThreads + " threads joined after " + duration + "ms at " +
                    timeNow());
        }

        private static Thread startSleepingThread(long sleepDurationMs, Timer timer,
                AtomicInteger runningTasksCounter, ConcurrentLinkedQueue<Runnable> pendingTasks,
                ConcurrentLinkedQueue<Thread> finishingCarriers,
                AtomicInteger finishingCarriersCounter) {
            return Thread.startVirtual(() ->
                sleepingTask(sleepDurationMs, timer, runningTasksCounter, pendingTasks,
                        finishingCarriers, finishingCarriersCounter));
        }

        private static void sleepingTask(long sleepDurationMs, Timer timer,
                AtomicInteger runningTasksCounter, ConcurrentLinkedQueue<Runnable> pendingTasks,
                ConcurrentLinkedQueue<Thread> finishingCarriers,
                AtomicInteger finishingCarriersCounter) {
            long tid1 = getCarrierThreadId();
            parkVirtual(sleepDurationMs, timer, runningTasksCounter, pendingTasks, finishingCarriers,
                finishingCarriersCounter);
            long tid2 = getCarrierThreadId();
            if (tid1 == tid2) {
                throw new RuntimeException("thread id shouldn't be the same: "
                        + tid1 + " != " + tid2);
            }

            Thread carrier = JLA.currentCarrierThread();
            long virtualThreadId = carrier.getVirtualThreadContext().id;
            timer.schedule(new TimerTask() {
                @Override
                public void run() {
                    try {
                        carrier.join(JOIN_TIMEOUT_MS);
                    } catch (InterruptedException e) {
                        throw new IllegalStateException("virtual thread id: " + virtualThreadId, e);
                    }
                    runningTasksCounter.decrementAndGet();
                }
            }, 0);
        }

        private static void parkVirtual(long sleepDurationMs, Timer timer,
                AtomicInteger runningTasksCounter, ConcurrentLinkedQueue<Runnable> pendingTasks,
                ConcurrentLinkedQueue<Thread> finishingCarriers,
                AtomicInteger finishingCarriersCounter) {
            long startTime = nowForElapsedTimeMillis();
            Thread carrier = JLA.currentCarrierThread();
            VirtualThreadContext vtContext = carrier.getVirtualThreadContext();
            long virtualThreadId = vtContext.id;
            // The following runs the 3 tasks
            // 1. Wait for this carrier thread to join
            //    (and this virtual thread to park successfully) on the timer thread.
            // 2. After a delay, mark this virtual thread ready to be unparked by the scheduler.
            // 3. When capacity is available, the main thread unparks this virtual thread.
            TimerTask unparkingTask = new TimerTask() {
                @Override
                public void run() {
                    pendingTasks.add(() -> {
                        Thread th = Thread.unparkVirtual(vtContext);
                        finishingCarriers.add(th);
                        finishingCarriersCounter.incrementAndGet();
                        debugPrintln("Virtual thread is unparked: " + virtualThreadId +
                                " at " + timeNow());
                    });
                }
            };

            TimerTask joiningTask = new TimerTask() {
                @Override
                public void run() {
                    try {
                        carrier.join(JOIN_TIMEOUT_MS);
                    } catch (InterruptedException e) {
                        throw new IllegalStateException("virtual thread id: " + virtualThreadId, e);
                    }
                    runningTasksCounter.decrementAndGet();

                    if (!vtContext.isParked()) {
                        throw new IllegalStateException("Virtual thread is expected to be parked: "
                                + vtContext.id);
                    }

                    long delay = sleepDurationMs + startTime - nowForElapsedTimeMillis();
                    debugPrintln("Thread " + vtContext.id + " is scheduled to be unparked "
                                    + "after " + (delay <= 0 ? 0 : delay)  + "ms.");
                    if (delay <= 0) {
                        unparkingTask.run();
                    } else {
                        timer.schedule(unparkingTask, delay);
                    }

                }
            };
            timer.schedule(joiningTask, 0);
            Thread.parkVirtual();
        }

        private static long nowForElapsedTimeMillis() {
            return System.nanoTime() / 1_000_000;
        }

        private static long getCarrierThreadId() {
            return JLA.currentCarrierThread().threadId();
        }

        @Override
        public void close() {
            // Timer thread isn't a daemon. Canceling the timer allows process termination.
            mTimer.cancel();
        }
    }
}
