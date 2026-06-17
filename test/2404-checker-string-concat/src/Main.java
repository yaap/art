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

import java.lang.reflect.Method;

public class Main {
    public static void main(String[] args) throws Exception {
        Class<?> c = Class.forName("Test");

        // Case 1: Release Pattern (StringBuilder(String))
        // This is the classic string concatenation pattern that should be optimized.
        // A new StringBuilder is created with a string literal and immediately appended to.
        Method mRelease = c.getMethod("testRelease", String.class);
        String resultRelease = (String) mRelease.invoke(null, "World");
        if (!"Hello World".equals(resultRelease)) {
            throw new Error("testRelease failed: " + resultRelease);
        }

        // Case 2: Debug Pattern (StringBuilder() -> append(String))
        // Similar to the `d8 --release` pattern, but starting with an empty StringBuilder.
        // This should also be optimized.
        Method mDebug = c.getMethod("testDebug", String.class);
        String resultDebug = (String) mDebug.invoke(null, "World");
        if (!"Hello World".equals(resultDebug)) {
            throw new Error("testDebug failed: " + resultDebug);
        }

        // Case 3: Escape/Safety Check
        // Ensures that if the StringBuilder is passed to an unknown method (`$noinline$consume`)
        // before the final `toString()` call, the optimization is aborted. This is because
        // the compiler cannot prove that the `consume` method doesn't modify the StringBuilder
        // in an unexpected way.
        Method mEscape = c.getMethod("testEscape", String.class);
        String resultEscape = (String) mEscape.invoke(null, "World");
        if (!"StartMiddleWorldEnd".equals(resultEscape)) {
            throw new Error("testEscape failed: " + resultEscape);
        }

        // Case 4: String argument
        // Tests the case where the constructor is called with a String argument that is not
        // a `const-string` (HLoadString). The optimization should not apply because the
        // argument could be null, and the `StringBuilder` constructor would throw an NPE.
        Method mStringArg = c.getMethod("testStringArgument", String.class, String.class);
        String resultStringArg = (String) mStringArg.invoke(null, "Hello ", "World");
        if (!"Hello World".equals(resultStringArg)) {
            throw new Error("testStringArgument failed: " + resultStringArg);
        }
        try {
            mStringArg.invoke(null, null, "World");
            throw new Error("testStringArgument should have thrown NPE");
        } catch (java.lang.reflect.InvocationTargetException e) {
            if (!(e.getCause() instanceof NullPointerException)) {
                throw new Error("testStringArgument threw wrong exception: " + e.getCause());
            }
        }

        // Case 5: Null-checked String argument
        // Similar to Case 4, but the String argument is null-checked by calling `.length()`
        // before the StringBuilder is constructed. This allows the compiler to prove that
        // the argument is not null, and the optimization should be applied.
        Method mStringArgNC =
                c.getMethod("testStringArgumentNullChecked", String.class, String.class);
        String resultStringArgNC = (String) mStringArgNC.invoke(null, "Hello ", "World");
        if (!"Hello World".equals(resultStringArgNC)) {
            throw new Error("testStringArgumentNullChecked failed: " + resultStringArgNC);
        }
        try {
            mStringArgNC.invoke(null, null, "World");
            throw new Error("testStringArgumentNullChecked should have thrown NPE");
        } catch (java.lang.reflect.InvocationTargetException e) {
            if (!(e.getCause() instanceof NullPointerException)) {
                throw new Error(
                        "testStringArgumentNullChecked threw wrong exception: " + e.getCause());
            }
        }

        // Case 6: const-string as CharSequence
        // Tests that the optimization works when a `const-string` is passed to an `append`
        // method that takes a `CharSequence`. The compiler should be able to identify that
        // the CharSequence is a String and apply the optimization.
        Method mConstStringAsCharSequence =
            c.getMethod("testConstStringAsCharSequence", String.class);
        String resultConstStringAsCharSequence =
                (String) mConstStringAsCharSequence.invoke(null, "World");
        if (!"Hello World".equals(resultConstStringAsCharSequence)) {
            throw new Error(
                    "testConstStringAsCharSequence failed: " + resultConstStringAsCharSequence);
        }

        // Case 7: CharSequence that is not known to be a String
        // Tests that the optimization is NOT applied when `append(CharSequence)` is called
        // with a CharSequence that is not known to be a String at compile time. In this
        // case, `getCharSequence()` is `$noinline$` and returns a string literal but it's
        // seen as an opaque `CharSequence` that the compiler does not see as an actual `String`.
        // The call to `getCharSequence` is placed before the StringBuilder constructor
        // so that it's not in the `ChaseSequence`'s environment.
        Method mUnknownCharSequence = c.getMethod("testUnknownCharSequence", String.class);
        String resultUnknownCharSequence = (String) mUnknownCharSequence.invoke(null, "World");
        if (!"Hello World".equals(resultUnknownCharSequence)) {
            throw new Error("testUnknownCharSequence failed: " + resultUnknownCharSequence);
        }

        // Case 8: CharSequence that is not known to be a String passed to constructor
        Method mUnknownCharSequenceConstructor =
            c.getMethod("testUnknownCharSequenceConstructor", String.class);
        String resultUnknownCharSequenceConstructor =
            (String) mUnknownCharSequenceConstructor.invoke(null, "World");
        if (!"Hello World".equals(resultUnknownCharSequenceConstructor)) {
            throw new Error("testUnknownCharSequenceConstructor failed: " +
                            resultUnknownCharSequenceConstructor);
        }

        // Case 9: StringBuilder(CharSequence) constructor
        Method mConstStringAsCharSequenceConstructor =
                c.getMethod("testConstStringAsCharSequenceConstructor", String.class);
        String resultConstStringAsCharSequenceConstructor =
                (String) mConstStringAsCharSequenceConstructor.invoke(null, "World");
        if (!"Hello World".equals(resultConstStringAsCharSequenceConstructor)) {
            throw new Error(
                    "testConstStringAsCharSequenceConstructor failed: " +
                    resultConstStringAsCharSequenceConstructor);
        }

        // Case 10: StringBuilder(int) constructor
        // Tests that the optimization works when the StringBuilder is created with a capacity
        // specified as an int literal.
        Method mCapacity = c.getMethod("testCapacity", String.class);
        String resultCapacity = (String) mCapacity.invoke(null, "World");
        if (!"World".equals(resultCapacity)) {
            throw new Error("testCapacity failed: " + resultCapacity);
        }

        System.out.println("passed");
    }
}
