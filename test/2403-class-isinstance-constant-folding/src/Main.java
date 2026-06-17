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

import java.util.concurrent.atomic.AtomicReferenceFieldUpdater;

public class Main {
    volatile byte[] bytes;
    volatile Main[] mains;
    volatile Void[][] voids;

    private static final AtomicReferenceFieldUpdater<Main, byte[]> BYTES_UPDATER =
        AtomicReferenceFieldUpdater.newUpdater(Main.class, byte[].class, "bytes");
    private static final AtomicReferenceFieldUpdater<Main, Main[]> MAINS_UPDATER =
        AtomicReferenceFieldUpdater.newUpdater(Main.class, Main[].class, "mains");
    private static final AtomicReferenceFieldUpdater<Main, Void[][]> VOIDS_UPDATER =
        AtomicReferenceFieldUpdater.newUpdater(Main.class,
                                               Void[][].class,
                                               "voids");

    private static final Main MAIN = new Main();
    private static final Class<?> STRING_CLASS = String.class;
    private static Class<?> NULL = null;

    public static void main(String[] args) {
        System.loadLibrary(args[0]);

        ensureJitCompiled(Main.class, "$noinline$fieldupdaters");
        $noinline$fieldupdaters(new byte[0]);

        ensureJitCompiled(Main.class, "$noinline$fieldupdatersCCE");
        $noinline$fieldupdatersCCE();

        ensureJitCompiled(Main.class, "$noinline$classInStaticField");
        $noinline$classInStaticField(new Object());

        ensureJitCompiled(Main.class, "$noinline$npe");
        $noinline$npe();

        ensureJitCompiled(Main.class, "$noinline$arrays");
        $noinline$arrays(new String[0]);

        ensureJitCompiled(Main.class, "$noinline$primitives");
        $noinline$primitives(new Object());
    }

    private static void $noinline$fieldupdaters(Object obj) {
        byte[] newBytes = new byte[0];
        assertTrue(BYTES_UPDATER.compareAndSet(MAIN, null, newBytes));
        assertTrue(BYTES_UPDATER.compareAndSet(MAIN, newBytes, new byte[1]));
        assertFalse(BYTES_UPDATER.compareAndSet(MAIN, null, newBytes));
        assertFalse(((AtomicReferenceFieldUpdater) BYTES_UPDATER).compareAndSet(MAIN, null, obj));

        Main[] newMains = new Main[0];
        assertTrue(MAINS_UPDATER.compareAndSet(MAIN, null, newMains));
        assertTrue(MAINS_UPDATER.compareAndSet(MAIN, newMains, new Main[1]));
        assertFalse(MAINS_UPDATER.compareAndSet(MAIN, null, new Main[0]));
    }

    private static void $noinline$fieldupdatersCCE() {
        final AtomicReferenceFieldUpdater rawUpdater = (AtomicReferenceFieldUpdater) BYTES_UPDATER;
        try {
            byte[] newBytes = new byte[10];
            Main[] newMains = new Main[0];
            rawUpdater.compareAndSet(MAIN, newBytes, newMains);
            fail("CCE is expected");
        } catch (ClassCastException expected) {}

        try {
            byte[] newBytes = new byte[10];
            rawUpdater.compareAndSet("", newBytes, newBytes);
            fail("CCE is expected");
        } catch (ClassCastException expected) {}

        {
            byte[] newBytes = new byte[10];
            Main[] newMains = new Main[0];
            // `expect` parameter's type is not checked.
            assertFalse(rawUpdater.compareAndSet(MAIN, newMains, newBytes));
        }
    }

    private static void $noinline$classInStaticField(Object obj) {
        assertFalse(STRING_CLASS.isInstance(obj));
    }

    private static void $noinline$npe() {
        try {
            ((Class) null).isInstance(BYTES_UPDATER);
            fail("NPE is expected");
        } catch (NullPointerException expected) {}

        try {
            String.class.isAssignableFrom(NULL);
            fail("NPE is expected");
        } catch (NullPointerException expected) {}
    }

    private static void $noinline$arrays(Object obj) {
        assertTrue(Object[].class.isInstance(obj));
        assertTrue(String[].class.isInstance(obj));
        assertTrue(CharSequence[].class.isInstance(obj));
        assertFalse(Object[][].class.isInstance(obj));
        assertFalse(String[][].class.isInstance(obj));
        assertFalse(int[].class.isInstance(obj));
    }

    private static void $noinline$primitives(Object obj) {
        assertFalse(int.class.isInstance(obj));
        assertFalse(void.class.isInstance(obj));

        assertFalse(int.class.isAssignableFrom(float.class));
        assertTrue(int.class.isAssignableFrom(int.class));
        assertFalse(int.class.isAssignableFrom(long.class));
        assertFalse(long.class.isAssignableFrom(int.class));
        assertFalse(void.class.isAssignableFrom(double.class));
    }

    private static void assertTrue(boolean result) {
        if (!result) {
            fail();
        }
    }

    private static void assertFalse(boolean result) {
        if (result) {
            fail();
        }
    }

    private static void fail() {
        fail("");
    }

    private static void fail(String msg) {
        throw new AssertionError(msg);
    }

    private static native void ensureJitCompiled(Class<?> clazz, String method);
}
