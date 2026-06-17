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

import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.lang.invoke.VarHandle;
import java.lang.invoke.WrongMethodTypeException;
import java.util.Objects;

public class Main {

    private static final int SIZE = 64;

    private static final VarHandle vhArray = MethodHandles.arrayElementVarHandle(boolean[].class);

    private static final MethodHandle setter = vhArray.toMethodHandle(VarHandle.AccessMode.SET);
    private static final MethodHandle getter = vhArray.toMethodHandle(VarHandle.AccessMode.GET);

    private static final MethodHandle NULL_MH = null;

    public static void main(String[] args) throws Throwable {
        System.loadLibrary(args[0]);

        $noinline$testOsr();

        ensureJitBaselineCompiled(Main.class, "$noinline$testInvokeExact");
        $noinline$testInvokeExact(/*useCombinator=*/ true);

        ensureJitCompiled(Main.class, "$noinline$testInvokeExact");
        $noinline$testInvokeExact(/*useCombinator=*/ false);

        ensureJitCompiled(Main.class, "$noinline$testInvokeExactWithKnownMethodHandle");
        $noinline$testInvokeExactWithKnownMethodHandle();

        testNull();
        testWrongCallSite();
    }

    private static void $noinline$testOsr() throws Throwable {
        boolean[] array = new boolean[SIZE];

        boolean interpreting = isInInterpreter("$noinline$testOsr");

        int i;
        // Straight from 570-checker-osr.
        for (i = 0; i < 300000; ++i) {}

        if (interpreting) {
            while (!isInOsrCode("$noinline$testOsr")) {}
        }

        for (i = 0; i < SIZE; ++i) {
            setter.invokeExact(array, i, true);
            boolean x = (boolean) getter.invokeExact(array, i);

            assertEquals(x, true, "get boolean value");
        }
    }

    private static void $noinline$testInvokeExact(boolean useCombinator) throws Throwable {
        MethodHandle identity = MethodHandles.identity(Object.class);
        if (useCombinator) {
            MethodHandle callee = MethodHandles.identity(Object.class);
            identity = MethodHandles.filterArguments(callee, 0, identity);
            identity = identity.asType(MethodType.genericMethodType(1));
        }

        Object x = 42;
        Object y = identity.invokeExact(x);
        assertEquals(x, y);
    }

    private static void $noinline$testInvokeExactWithKnownMethodHandle() throws Throwable {
        boolean[] array = new boolean[SIZE];
        boolean ignored = (boolean) getter.invokeExact(array, 0);
        setter.invokeExact(array, 0, true);

        try {
            setter.invokeExact();
            fail("WMTE is expected");
        } catch (WrongMethodTypeException expected) {}
    }

    private static void testNull() throws Throwable {
        try {
            NULL_MH.invokeExact("");
        } catch (NullPointerException npe) {
            StackTraceElement[] trace = npe.getStackTrace();
            assertEquals(2, trace.length);
            assertEquals("testNull", trace[0].getMethodName());
            assertEquals("main", trace[1].getMethodName());
        }

        try {
            $noinline$nullMH().invokeExact("");
        } catch (NullPointerException npe) {
            StackTraceElement[] trace = npe.getStackTrace();
            assertEquals(2, trace.length);
            assertEquals("testNull", trace[0].getMethodName());
            assertEquals("main", trace[1].getMethodName());
        }
    }

    private static MethodHandle $noinline$nullMH() {
        return null;
    }

    private static void testWrongCallSite() throws Throwable {
        try {
            setter.invokeExact();
            fail("Unreachable: WMTE should be thrown");
        } catch (WrongMethodTypeException expected) {}
    }

    private static void assertEquals(Object lhs, Object rhs) {
        assertEquals(lhs, rhs, null);
    }

    private static void assertEquals(Object lhs, Object rhs, String msg) {
        if (!Objects.equals(lhs, rhs)) {
            if (msg != null) {
                throw new AssertionError(msg);
            } else {
                throw new AssertionError("expected: " + lhs + ", got: " + rhs);
            }
        }
    }

    private static void assertEquals(boolean lhs, boolean rhs, String msg) {
        if (lhs != rhs) {
            throw new AssertionError(msg);
        }
    }

    private static void fail(String message) {
        throw new AssertionError(message);
    }

    public static native boolean isInOsrCode(String methodName);
    public static native boolean isInInterpreter(String methodName);
    public static native boolean ensureJitCompiled(Class<?> clazz, String methodName);
    public static native boolean ensureJitBaselineCompiled(Class<?> clazz, String methodName);
}
