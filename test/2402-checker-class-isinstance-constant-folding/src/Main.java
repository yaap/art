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

import java.util.ArrayList;
import java.util.List;

public class Main {

    private static List<String> A_LIST = new ArrayList<>();
    private static ArrayList<String> ARRAY_LIST = new ArrayList<>();
    private static Class<?> STRING_CLASS = String.class;

    private final Class<?> aClass = List.class;

    public static void main(String[] args) {
        $noinline$subclasses();
        $noinline$argumentOfUnknownType("args");
        $noinline$arrays();
        $noinline$absurdArray(new Object());
        $noinline$isAssignableFrom(new Main());
    }

    /// CHECK-START-ARM64: void Main.$noinline$subclasses() inliner (before)
    /// CHECK:             InvokeVirtual method_name:java.lang.Class.isInstance
    /// CHECK:             InvokeVirtual method_name:java.lang.Class.isInstance

    /// CHECK-START-ARM64: void Main.$noinline$subclasses() instruction_simplifier$after_inlining (after)
    /// CHECK:             NotEqual
    /// CHECK:             InstanceOf
    private static void $noinline$subclasses() {
        // Field's type is `ArrayList` - it is enough to check that it is not null.
        assertTrue(List.class.isInstance(ARRAY_LIST));
        // `A_LIST` can hold any List - InstanceOf check can't be eliminated.
        assertTrue(ArrayList.class.isInstance(A_LIST));
    }

    /// CHECK-START-ARM64: void Main.$noinline$argumentOfUnknownType(java.lang.Object) inliner (before)
    /// CHECK:             InvokeVirtual method_name:java.lang.Class.isInstance

    /// CHECK-START-ARM64: void Main.$noinline$argumentOfUnknownType(java.lang.Object) instruction_simplifier$after_inlining (after)
    /// CHECK:             InstanceOf
    private static void $noinline$argumentOfUnknownType(Object obj) {
        assertTrue(String.class.isInstance(obj));
    }

    /// CHECK-START-ARM64: void Main.$noinline$arrays() inliner (before)
    /// CHECK:             InvokeVirtual method_name:java.lang.Class.isInstance
    /// CHECK:             InvokeVirtual method_name:java.lang.Class.isInstance

    /// CHECK-START-ARM64: void Main.$noinline$arrays() instruction_simplifier$after_inlining (after)
    /// CHECK:             InstanceOf
    /// CHECK-NOT:         InvokeVirtual method_name:java.lang.Class.isInstance
    private static void $noinline$arrays() {
        assertFalse(byte[].class.isInstance(ARRAY_LIST));
        // This is not yet eliminated.
        assertFalse(Main[].class.isInstance(A_LIST));
    }

    /// CHECK-START-ARM64: void Main.$noinline$absurdArray(java.lang.Object) inliner (before)
    /// CHECK:             InvokeVirtual method_name:java.lang.Class.isInstance

    /// CHECK-START-ARM64: void Main.$noinline$absurdArray(java.lang.Object) instruction_simplifier$after_inlining (after)
    /// CHECK:             InstanceOf
    private static void $noinline$absurdArray(Object obj) {
        assertFalse(Throwable[][][][][].class.isInstance(obj));
    }

    /// CHECK-START: void Main.$noinline$isAssignableFrom(Main) inliner (before)
    /// CHECK:       InvokeVirtual method_name:java.lang.Class.isAssignableFrom
    /// CHECK:       InvokeVirtual method_name:java.lang.Class.isAssignableFrom
    /// CHECK:       InvokeVirtual method_name:java.lang.Class.isAssignableFrom

    /// CHECK-START: void Main.$noinline$isAssignableFrom(Main) instruction_simplifier$after_inlining (after)
    /// CHECK:       InstanceOf
    /// CHECK:       InvokeStaticOrDirect method_name:java.lang.Class.isAssignableFrom
    /// CHECK:       InvokeStaticOrDirect method_name:java.lang.Class.isAssignableFrom
    private static void $noinline$isAssignableFrom(Main m) {
        assertFalse(List.class.isAssignableFrom(m.getClass()));
        assertFalse(Main.class.isAssignableFrom(STRING_CLASS));
        assertFalse(String.class.isAssignableFrom(m.aClass));
    }

    private static void assertTrue(boolean result) {
        if (!result) {
            throw new AssertionError();
        }
    }

    private static void assertFalse(boolean result) {
        if (result) {
            throw new AssertionError();
        }
    }
}
