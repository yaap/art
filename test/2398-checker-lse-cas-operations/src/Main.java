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

public class Main {
  public static void main(String[] args) {
    Main m = new Main();
    m.compareAndSet(0, 1);
    m.getAndAdd(1);
    m.getAndSet(1);
  }

  private volatile int a;
  private static final VarHandle VH_A;
  static {
    try {
      VH_A = MethodHandles.lookup().findVarHandle(Main.class, "a", int.class);
    } catch (ReflectiveOperationException e) {
      throw new Error(e);
    }
  }

  // CHECK-START-ARM64: int Main.compareAndSet(int, int) disassembly (after)
  // CHECK-IF:     hasIsaFeature("lse")
  // CHECK:            cas
  // CHECK-ELSE:
  // CHECK:            ldxr
  // CHECK:            stxr
  // CHECK-FI:
  public int compareAndSet(int expected, int newValue) {
    VH_A.compareAndSet(this, expected, newValue);
    return 0;
  }

  // CHECK-START-ARM64: int Main.getAndAdd(int) disassembly (after)
  // CHECK-IF:     hasIsaFeature("lse")
  // CHECK:            ldadd
  // CHECK-ELSE:
  // CHECK:            ldxr
  // CHECK:            stxr
  // CHECK-FI:
  public int getAndAdd(int value) {
    return (int) VH_A.getAndAdd(this, value);
  }

  // CHECK-START-ARM64: int Main.getAndSet(int) disassembly (after)
  // CHECK-IF:     hasIsaFeature("lse")
  // CHECK:            swp
  // CHECK-ELSE:
  // CHECK:            ldxr
  // CHECK:            stxr
  // CHECK-FI:
  public int getAndSet(int newValue) {
    return (int) VH_A.getAndSet(this, newValue);
  }
}
