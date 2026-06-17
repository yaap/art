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

public final class Main {
    private static final boolean BOOLEAN_FIELD;
    private static final byte BYTE_FIELD;
    private static final char CHAR_FIELD;
    private static final short SHORT_FIELD;
    private static final int INT_FIELD;
    private static final float FLOAT_FIELD;
    private static final long LONG_FIELD;
    private static final double DOUBLE_FIELD;
    private static final Object REF_FIELD;

    // Primitive fields can't be initialized at definition site because javac is allowed to
    // treat them as compile-time constants.
    static {
        BOOLEAN_FIELD = false;
        BYTE_FIELD = (byte) 0;
        CHAR_FIELD = 0;
        SHORT_FIELD = 0;
        INT_FIELD = 0;
        FLOAT_FIELD = 0f;
        LONG_FIELD = 0;
        DOUBLE_FIELD = 0d;
        REF_FIELD = null;
    }

    public static void main(String[] args) {
        System.loadLibrary(args[0]);

        setBooleanFieldJni(Main.class, "BOOLEAN_FIELD", true);
        if (!BOOLEAN_FIELD) {
            throw new AssertionError("Expected: true, got: false");
        }

        setByteFieldJni(Main.class, "BYTE_FIELD", (byte) 101);
        if (BYTE_FIELD != 101) {
            throw new AssertionError("Expected: 101, got: " + BYTE_FIELD);
        }

        setCharFieldJni(Main.class, "CHAR_FIELD", 'a');
        if (CHAR_FIELD != 'a') {
            throw new AssertionError("Expected: a, got: " + CHAR_FIELD);
        }

        setShortFieldJni(Main.class, "SHORT_FIELD", (short) 102);
        if (SHORT_FIELD != 102) {
            throw new AssertionError("Expected: 102, got: " + SHORT_FIELD);
        }

        setIntFieldJni(Main.class, "INT_FIELD", 42);
        if (INT_FIELD != 42) {
            throw new AssertionError("Expected: 42, got: " + INT_FIELD);
        }

        setFloatFieldJni(Main.class, "FLOAT_FIELD", (float) Math.PI);
        if (FLOAT_FIELD != (float) Math.PI) {
            throw new AssertionError("Expected: " + (float) Math.PI + ", got: " + FLOAT_FIELD);
        }

        setLongFieldJni(Main.class, "LONG_FIELD", 43L);
        if (LONG_FIELD != 43L) {
            throw new AssertionError("Expected: 43, got: " + LONG_FIELD);
        }

        setDoubleFieldJni(Main.class, "DOUBLE_FIELD", Math.E);
        if (DOUBLE_FIELD != Math.E) {
            throw new AssertionError("Expected: " + Math.E + ", got: " + DOUBLE_FIELD);
        }

        Object newValue = new Object();
        setRefFieldJni(Main.class, "REF_FIELD", newValue);
        if (REF_FIELD != newValue) {
            throw new AssertionError("Expected: " + newValue + ", got: " + REF_FIELD);
        }
    }

    private native static void setBooleanFieldJni(Class<?> clazz, String name, boolean value);
    private native static void setByteFieldJni(Class<?> clazz, String name, byte value);
    private native static void setCharFieldJni(Class<?> clazz, String name, char value);
    private native static void setShortFieldJni(Class<?> clazz, String name, short value);
    private native static void setIntFieldJni(Class<?> clazz, String name, int value);
    private native static void setFloatFieldJni(Class<?> clazz, String name, float value);
    private native static void setLongFieldJni(Class<?> clazz, String name, long value);
    private native static void setDoubleFieldJni(Class<?> clazz, String name, double value);
    private native static void setRefFieldJni(Class<?> clazz, String name, Object value);
}
