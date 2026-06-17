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

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

public final class Main {

    private static final boolean BOOLEAN = Values.BOOLEAN;
    private static final byte BYTE = Values.BYTE;
    private static final char CHAR = Values.CHAR;
    private static final short SHORT = Values.SHORT;
    private static final int INT = Values.INT;
    private static final long LONG = Values.LONG;

    private static final float FLOAT_PI = FloatValues.PI;
    private static final float FLOAT_PLUS_INF = FloatValues.PLUS_INF;
    private static final float FLOAT_NEG_INF = FloatValues.NEG_INF;
    private static final float FLOAT_PLUS_ZERO = FloatValues.PLUS_ZERO;
    private static final float FLOAT_NEG_ZERO = FloatValues.NEG_ZERO;
    private static final float FLOAT_NAN = FloatValues.NAN;
    private static final float FLOAT_NON_CANONICAL_NAN = FloatValues.NON_CANONICAL_NAN;

    private static final double DOUBLE_PI = DoubleValues.PI;
    private static final double DOUBLE_PLUS_INF = DoubleValues.PLUS_INF;
    private static final double DOUBLE_NEG_INF = DoubleValues.NEG_INF;
    private static final double DOUBLE_PLUS_ZERO = DoubleValues.PLUS_ZERO;
    private static final double DOUBLE_NEG_ZERO = DoubleValues.NEG_ZERO;
    private static final double DOUBLE_NAN = DoubleValues.NAN;
    private static final double DOUBLE_NON_CANONICAL_NAN = DoubleValues.NON_CANONICAL_NAN;

    private static final Integer BOXED_INT = Values.INT;
    private static final Integer BOXED_INT_NULL = Values.NULL_INTEGER;

    private static final List<String> ARRAY_LIST = new ArrayList<>();
    private static final List<String> EMPTY_LIST = List.of();

    private static final IntHolder INT_HOLDER = new IntHolder(Values.INT);
    private static final RefHolder REF_HOLDER = new RefHolder(INT_HOLDER);
    private static final RefHolder REF_HOLDER_WITH_NULL = new RefHolder(null);

    public static final class Values {
        static volatile boolean BOOLEAN;
        static volatile byte BYTE;
        static volatile char CHAR;
        static volatile short SHORT;
        static volatile int INT;
        static volatile long LONG;
        static volatile Integer NULL_INTEGER = null;

        static {
            BOOLEAN = true;
            BYTE = Byte.MAX_VALUE;
            CHAR = Character.MAX_VALUE;
            SHORT = Short.MAX_VALUE;
            INT = Integer.MAX_VALUE;
            LONG = Long.MAX_VALUE;
        }
    }

    private static final class FloatValues {
        static volatile float MAX = Float.MAX_VALUE;
        static volatile float MIN = Float.MIN_VALUE;
        static volatile float PI = (float) Math.PI;
        static volatile float PLUS_INF = Float.POSITIVE_INFINITY;
        static volatile float NEG_INF = Float.NEGATIVE_INFINITY;
        static volatile float PLUS_ZERO = +0.0f;
        static volatile float NEG_ZERO = -0.0f;
        static volatile float NAN = Float.NaN;
        static volatile float NON_CANONICAL_NAN = Float.intBitsToFloat(0xffaf9941);
    }

    private static final class DoubleValues {
        static volatile double MAX = Double.MAX_VALUE;
        static volatile double MIN = Double.MIN_VALUE;
        static volatile double PI = Math.PI;
        static volatile double PLUS_INF = Double.POSITIVE_INFINITY;
        static volatile double NEG_INF = Double.NEGATIVE_INFINITY;
        static volatile double PLUS_ZERO = +0.0d;
        static volatile double NEG_ZERO = -0.0d;
        static volatile double NAN = Double.NaN;
        static volatile double NON_CANONICAL_NAN = Double.longBitsToDouble(0xfffeeeefffffffffL);
    }

    private record IntHolder(int value) {}
    private record RefHolder(IntHolder ref) {}

    public static void main(String[] args) {
        System.loadLibrary(args[0]);

        ensureJitCompiled(Main.class, "$noinline$testBoolean");
        $noinline$testBoolean();

        ensureJitCompiled(Main.class, "$noinline$testByte");
        $noinline$testByte();

        ensureJitCompiled(Main.class, "$noinline$testChar");
        $noinline$testChar();

        ensureJitCompiled(Main.class, "$noinline$testShort");
        $noinline$testShort();

        ensureJitCompiled(Main.class, "$noinline$testInt");
        $noinline$testInt();

        ensureJitCompiled(Main.class, "$noinline$testFloat");
        $noinline$testFloat();

        ensureJitCompiled(Main.class, "$noinline$testLong");
        $noinline$testLong();

        ensureJitCompiled(Main.class, "$noinline$testDouble");
        $noinline$testDouble();

        ensureJitCompiled(Main.class, "$noinline$testInteger");
        $noinline$testInteger();

        ensureJitCompiled(Main.class, "$noinline$testInvocation");
        $noinline$testInvocation();

        ensureJitCompiled(Main.class, "$noinline$testRecords");
        $noinline$testRecords();
    }

    private static void $noinline$testBoolean() {
        boolean actual = BOOLEAN;
        if (actual != Values.BOOLEAN) {
            fail("Expected to be " + Values.BOOLEAN + ", got " + actual);
        }
    }

    private static void $noinline$testByte() {
        byte actual = BYTE;
        if (actual != Values.BYTE) {
            fail("Expected: " + Values.BYTE + ", got: " + actual);
        }
    }

    private static void $noinline$testChar() {
        char actual = CHAR;
        if (actual != Values.CHAR) {
            fail("Expected: " + Values.CHAR + ", got: " + actual);
        }
    }

    private static void $noinline$testShort() {
        short actual = SHORT;
        if (actual != Values.SHORT) {
            fail("Expected: " + Values.SHORT + ", got: " + actual);
        }
    }

    private static void $noinline$testInt() {
        assertEquals(INT, Values.INT);
    }

    private static void $noinline$testInteger() {
        assertEquals(BOXED_INT.intValue(), Values.INT);
        try {
            BOXED_INT_NULL.intValue();
            fail("NPE is expected");
        } catch (NullPointerException expected) {}
    }

    private static void assertEquals(int actual, int expected) {
        if (actual != expected) {
            fail("Expected: " + expected + ", got: " + actual);
        }
    }

    private static void $noinline$testLong() {
        long actual = LONG;
        if (actual != Values.LONG) {
            fail("Expected: " + Values.LONG + ", got: " + actual);
        }
    }

    private static void $noinline$testFloat() {
        assertEquals(FLOAT_PI, FloatValues.PI);
        assertEquals(FLOAT_PLUS_INF, FloatValues.PLUS_INF);
        assertEquals(FLOAT_PLUS_ZERO, FloatValues.PLUS_ZERO);
        assertEquals(FLOAT_NEG_ZERO, FloatValues.NEG_ZERO);
        assertEquals(FLOAT_NAN, FloatValues.NAN);
        assertNaN(FLOAT_NAN);
        assertNaN(FLOAT_NON_CANONICAL_NAN);
    }

    private static void assertEquals(float actual, float expected) {
        int actualBits = Float.floatToRawIntBits(actual);
        int expectedBits = Float.floatToRawIntBits(expected);

        if (actualBits != expectedBits) {
            fail("Expected bits: " + expectedBits + ", got: " + actualBits);
        }
    }

    private static void assertNaN(float subject) {
        if (!Float.isNaN(subject)) {
            String msg = String.format(
                "%f (bits: 0x%x) is not NaN", subject, Float.floatToRawIntBits(subject));
            fail(msg);
        }
    }

    private static void $noinline$testDouble() {
        assertEquals(DOUBLE_PI, DoubleValues.PI);
        assertEquals(DOUBLE_PLUS_INF, DoubleValues.PLUS_INF);
        assertEquals(DOUBLE_PLUS_ZERO, DoubleValues.PLUS_ZERO);
        assertEquals(DOUBLE_NEG_ZERO, DoubleValues.NEG_ZERO);
        assertEquals(DOUBLE_NAN, DoubleValues.NAN);
        assertNaN(DOUBLE_NAN);
        assertNaN(DOUBLE_NON_CANONICAL_NAN);
    }

    private static void assertEquals(double actual, double expected) {
        long actualBits = Double.doubleToRawLongBits(actual);
        long expectedBits = Double.doubleToRawLongBits(expected);

        if (actualBits != expectedBits) {
            fail("Expected bits: " + expectedBits + ", got: " + actualBits);
        }
    }

    private static void assertNaN(double subject) {
        if (!Double.isNaN(subject)) {
            String msg = String.format(
                "%f (bits: 0x%x) is not NaN", subject, Double.doubleToRawLongBits(subject));
            fail(msg);
        }
    }

    private static void $noinline$testInvocation() {
        int total = ARRAY_LIST.size() + EMPTY_LIST.size();

        if (total != 0) {
            fail("Expected 0 elements, got " + total);
        }
    }

    private static void $noinline$testRecords() {
        assertEquals(INT_HOLDER.value(), Values.INT);
        assertEquals(REF_HOLDER.ref().value(), Values.INT);
        try {
            REF_HOLDER_WITH_NULL.ref().value();
            fail("NPE is expected");
        } catch (NullPointerException expected) {}
    }

    private static void fail(String msg) {
        throw new AssertionError(msg);
    }

    private native static void ensureJitCompiled(Class<?> clazz, String method);
}
