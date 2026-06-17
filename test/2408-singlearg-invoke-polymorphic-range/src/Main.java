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
import java.lang.reflect.Method;

public final class Main {

    static double static_v = Math.PI;
    static VarHandle vhStaticField;

    static {
        try {
            vhStaticField = MethodHandles.lookup()
                .findStaticVarHandle(Main.class, "static_v", double.class);
        } catch (Exception e) {
            throw new InternalError(e);
        }
    }

    public static void main(String[] args) throws Exception {
        Class testClass = Class.forName("Test");
        {
            Method testRange = testClass.getMethod("testRange", VarHandle.class);
            Double result = (Double) testRange.invoke(null, vhStaticField);
            assertEquals(result.doubleValue(), Math.PI, "testRange");
        }

        {
            Method testNonRange = testClass.getMethod("testNonRange", VarHandle.class);
            Double result = (Double) testNonRange.invoke(null, vhStaticField);
            assertEquals(result.doubleValue(), Math.PI, "testNonRange");
        }
    }

    private static void assertEquals(double actual, double expected, String msg) {
        if (actual != expected) {
            throw new AssertionError(msg);
        }
    }
}
