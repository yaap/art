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

import static art.NonStandardExit.popFrame;
import static art.SuspendEvents.setupMethodExitTest;
import static art.SuspendEvents.setupSuspendMethodEventWithCallback;
import static art.SuspendEvents.setupSuspendPopFrameEvent;

import java.lang.reflect.Executable;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.Base64;
import java.util.EnumSet;
import java.util.concurrent.CountDownLatch;
import java.util.function.Consumer;

public class Main {
    private static void SafePrintStackTrace(StackTraceElement st[]) {
        for (StackTraceElement e : st) {
            System.out.println("\t" + e.getClassName() + "." + e.getMethodName() + "("
                               + (e.isNativeMethod() ? "Native Method" : e.getFileName()) + ")");
            if (e.getClassName().equals("art.Test1953") && e.getMethodName().equals("runTests")) {
                System.out.println("\t<Additional frames hidden>");
                break;
            }
        }
    }

    public void runTestOn(TestObject testObj, Method method) throws Exception {
        System.out.println("Single call with PopFrame on " + testObj);

        final Method methodExitCallback =
                TestObject.class.getDeclaredMethod("methodExitCallback", Object.class);
        final CountDownLatch continue_latch = new CountDownLatch(1);
        final CountDownLatch startup_latch = new CountDownLatch(1);
        Runnable await = () -> {
            try {
                startup_latch.countDown();
                continue_latch.await();
            } catch (Exception e) {
                throw new Error("Failed to await latch", e);
            }
        };
        Thread thr = new Thread(() -> {
            await.run();
            testObj.run();
        });
        thr.start();

        // Wait until the other thread is started.
        startup_latch.await();

        // Setup suspension method on the thread.
        setupSuspendMethodEventWithCallback(method, /*enter*/ false, thr, TestObject.class,
                                            methodExitCallback);

        // Let the other thread go.
        continue_latch.countDown();

        // Wait for the other thread to hit the breakpoint/watchpoint/whatever and suspend itself
        // (without re-entering java)
        art.SuspendEvents.waitForSuspendHit(thr);

        try {
            // Pop the frame.
            popFrame(thr);
        } catch (Exception e) {
            System.out.println("Failed to pop frame due to " + e);
            SafePrintStackTrace(e.getStackTrace());
        }

        // Start the other thread going again.
        art.Suspension.resume(thr);

        // Wait for the other thread to finish.
        thr.join();

        // See how many times calledFunction was called.
        System.out.println("result is " + testObj);
    }

    public static class TestObject implements Runnable {
        public int cnt;
        public int callerCnt;
        public boolean isJit;

        public TestObject(boolean isJit) {
            cnt = 0;
            callerCnt = 0;
            this.isJit = isJit;
        }

        public void run() {
            callerFunction();
        }

        public void callerFunction() {
            callerCnt++;
            if (isJit) {
                // This function should be re-executed by the popFrame.
                calledFunctionJit();
            } else {
                // This function should be re-executed by the popFrame.
                calledFunction();
            }
            if (cnt == 1) {
                System.out.println("FAILED: No pop on first call!");
            }
        }

        public void calledFunction() {
            cnt++;
        }

        public void calledFunctionJit() {
            cnt++;
        }

        public String toString() {
            return "TestObject { cnt: " + cnt + " callerCnt: " + callerCnt + " }";
        }

        public static void methodExitCallback(Object m) {
            System.out.println("MethodExitCallback " + m.toString());
        }
    }

    public void runTests() throws Exception {
        setupMethodExitTest();

        final Method calledFunction = TestObject.class.getDeclaredMethod("calledFunction");
        final Method calledFunctionJit = TestObject.class.getDeclaredMethod("calledFunctionJit");
        Main.ensureJitCompiled(TestObject.class, "calledFunctionJit");

        runTestOn(new TestObject(false), calledFunction);
        runTestOn(new TestObject(true), calledFunctionJit);
    }

    public static void main(String[] args) throws Exception {
        System.loadLibrary(args[0]);
        new Main().runTests();
    }

    private static native void ensureJitCompiled(Class<?> c, String name);
}
