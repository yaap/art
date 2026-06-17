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

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;

public class Main {
  public static void assertEquals(int expected, int actual) {
    if (expected != actual) {
      throw new AssertionError("Expected " + expected + " got " + actual);
    }
  }

  public static void main(String[] strArgs) throws Exception {
    Class<?> c = Class.forName("FilledNewArray");

    {
      Method m = c.getMethod("newInt", Integer.TYPE, Integer.TYPE, Integer.TYPE);
      Object[] args = {new Integer(1), new Integer(2), new Integer(3)};
      int[] result = (int[])m.invoke(null, args);
      assertEquals(3, result.length);
      assertEquals(1, result[0]);
      assertEquals(2, result[1]);
      assertEquals(3, result[2]);
    }

    {
      Method m = c.getMethod("newIntRange", Integer.TYPE, Integer.TYPE, Integer.TYPE);
      Object[] args = {new Integer(1), new Integer(2), new Integer(3)};
      int[] result = (int[])m.invoke(null, args);
      assertEquals(3, result.length);
      assertEquals(1, result[0]);
      assertEquals(2, result[1]);
      assertEquals(3, result[2]);
    }
  }
}
