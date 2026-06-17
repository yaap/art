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

import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;

// Test `getAndSet` operation with zero argument.
public class VarHandleFpGetAndSetZeroTests {
    public static class GetAndSetZeroFloatTest extends VarHandleUnitTest {
        private static final VarHandle vh;
        private static final float VALUE = 1.1111f;
        private static float field = VALUE;

        static {
            try {
                vh = MethodHandles.lookup().findStaticVarHandle(
                        GetAndSetZeroFloatTest.class, "field", float.class);
            } catch (Exception e) {
                throw new RuntimeException("Unexpected initialization exception", e);
            }
        }

        @Override
        protected void doTest() {
            assertEquals((float) vh.getAndSet(0.0f), VALUE);
            assertEquals(field, 0.0f);
        }

        public static void main(String[] args) {
            new GetAndSetZeroFloatTest().run();
        }
    }

    public static class GetAndSetZeroDoubleTest extends VarHandleUnitTest {
        private static final VarHandle vh;
        private static final double VALUE = 1.1111;
        private static double field = VALUE;

        static {
            try {
                vh = MethodHandles.lookup().findStaticVarHandle(
                        GetAndSetZeroDoubleTest.class, "field", double.class);
            } catch (Exception e) {
                throw new RuntimeException("Unexpected initialization exception", e);
            }
        }

        @Override
        protected void doTest() {
            assertEquals((double) vh.getAndSet(0.0), VALUE);
            assertEquals(field, 0.0);
        }

        public static void main(String[] args) {
            new GetAndSetZeroDoubleTest().run();
        }
    }

    public static void main(String[] args) {
        GetAndSetZeroFloatTest.main(args);
        GetAndSetZeroDoubleTest.main(args);
    }
}
