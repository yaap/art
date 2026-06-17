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

import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;

/**
 * Regression test for b/479959182.
 *
 * This test exercises a bug in the ARM64 compiler's implementation of VarHandle CAS
 * intrinsics when Large System Extensions (LSE) are enabled and Concurrent Copying (CC)
 * read barriers are involved.
 *
 * THE BUG:
 * An optimization (ag/38504543) moved the CSEL instruction (which finalizes the boolean
 * result of a weak CAS) from the main path to the slow path for LSE-enabled systems.
 * However, the slow path for reference-type CAS has an "early failure" path: if the
 * loaded from-space reference is marked but still doesn't match the expected value, it
 * branches back to the main path's exit label. If that exit label is missing the CSEL,
 * the result register incorrectly retains the non-zero (true) unmarked reference,
 * causing the CAS to incorrectly report success.
 *
 * THE STRESS TEST:
 * When run as a program, this test acts as a stress test for this interaction:
 * 1. Multiple worker threads concurrently attempt to push nodes onto a stack using
 *    weakCompareAndSetRelease.
 * 2. A separate daemon thread continuously triggers System.gc() to force the Concurrent
 *    Copying collector to move objects and enable read barriers.
 * 3. High concurrency increases the probability of one thread seeing a from-space
 *    reference that has been moved by the GC thread, thus triggering the read barrier
 *    slow path during the CAS operation.
 * 4. If the bug is present, weakCompareAndSetRelease will eventually return 'true'
 *    even though it failed to update the field (because the value was changed by another
 *    thread). This leads to nodes being lost from the stack.
 * 5. After all workers finish, we count the nodes in the stack and compare with the
 *    total number of successful pushes reported.
 *
 * THE CHECKER TEST:
 * The CHECKER instructions below verify that the CSEL instruction is present in the
 * final disassembly of the testWeakCas method, ensuring that all failure paths
 * (including those from the slow path) correctly finalize the boolean result to 'false' (0).
 */
public class Main {
    public static VarHandle sTop;
    public volatile Object mTopValue = null;

    static class Node {
        Node next;
    }

    /// CHECK-START-ARM64: boolean Main.testWeakCas(java.lang.Object, java.lang.Object) disassembly (after)
    /// CHECK-IF: hasIsaFeature("lse") and os.environ.get('ART_USE_READ_BARRIER') == 'true'
    /// CHECK:      casl w{{[0-9]+}}, w{{[0-9]+}}, [x{{[0-9]+}}]
    /// CHECK-IF: os.environ.get('ART_HEAP_POISONING') == 'true'
    /// CHECK-NEXT: neg
    /// CHECK-NEXT: neg
    /// CHECK-FI:
    /// CHECK-NEXT: cmp w{{[0-9]+}}, w{{[0-9]+}}
    // The following B.NE might branch to success_csel_label (fixed) or exit_loop (buggy).
    /// CHECK-NEXT: b.ne #+{{0x[0-9a-f]+}}
    /// CHECK-NEXT: cset w{{[0-9]+}}, eq
    // In the revised fix, the CSEL is emitted in the slow path to finalize the result
    // register (setting it to 0 on failure) before returning to the main path.
    /// CHECK:      ReadBarrierCasSlowPathARM64
    /// CHECK:      csel w{{[0-9]+}}, w{{[0-9]+}}, wzr, eq
    /// CHECK-FI:
    public boolean testWeakCas(Object expected, Object newValue) {
        return sTop.weakCompareAndSetRelease(this, expected, newValue);
    }

    public void push(Node n) {
        Object current;
        do {
            current = mTopValue;
            n.next = (Node) current;
        } while (!testWeakCas(current, n));
    }

    static class Worker extends Thread {
        Main test;
        int iterations;

        Worker(Main test, int iterations) {
            this.test = test;
            this.iterations = iterations;
        }

        @Override
        public void run() {
            for (int i = 0; i < iterations; i++) {
                test.push(new Node());
            }
        }
    }

    public static void main(String[] args) throws Exception {
        try {
            sTop = MethodHandles.lookup().findVarHandle(Main.class, "mTopValue", Object.class);
        } catch (ReflectiveOperationException e) {
            throw new ExceptionInInitializerError(e);
        }

        final Main test = new Main();
        final int NUM_WORKERS = 4;
        final int ITERATIONS = 100000;

        Thread gcThread = new Thread(() -> {
            while (!Thread.interrupted()) {
                Runtime.getRuntime().gc();
                try {
                    Thread.sleep(1);
                } catch (InterruptedException e) {
                    break;
                }
            }
        });
        gcThread.start();

        Worker[] workers = new Worker[NUM_WORKERS];
        for (int i = 0; i < NUM_WORKERS; i++) {
            workers[i] = new Worker(test, ITERATIONS);
            workers[i].start();
        }

        for (int i = 0; i < NUM_WORKERS; i++) {
            workers[i].join();
        }

        gcThread.interrupt();
        gcThread.join();

        // Count nodes
        int count = 0;
        Node curr = (Node) test.mTopValue;
        while (curr != null) {
            count++;
            curr = curr.next;
        }

        int expectedCount = NUM_WORKERS * ITERATIONS;
        if (count != expectedCount) {
            System.err.println("Error: Expected " + expectedCount + " nodes, but found " + count);
            System.exit(1);
        }

        System.out.println("Test PASSED");
    }
}
