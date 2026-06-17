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

import dalvik.annotation.optimization.DeadReferenceSafe;

@DeadReferenceSafe
class DeadReferenceSafeClass {
  // Check that we remove reference inputs from NullCheck's environment in case of
  // method is dead reference safe.

  /// CHECK-START: void DeadReferenceSafeClass.$noinline$testEnvCleanup(byte[], int, int) environment_input_elimination (before)
  /// CHECK:     <<Param0:l\d+>>      ParameterValue
  /// CHECK:     <<Param1:i\d+>>      ParameterValue
  /// CHECK:     <<Param2:i\d+>>      ParameterValue
  /// CHECK:     <<Conv:b\d+>>        TypeConversion [<<Param2>>]
  /// CHECK:     <<NullCheck:l\d+>>   NullCheck [<<Param0>>] env:[[<<Conv>>,<<Param0>>,<<Param1>>,<<Param2>>]]

  /// CHECK-START: void DeadReferenceSafeClass.$noinline$testEnvCleanup(byte[], int, int) environment_input_elimination (after)
  /// CHECK:     <<Param0:l\d+>>      ParameterValue
  /// CHECK:     <<Param1:i\d+>>      ParameterValue
  /// CHECK:     <<Param2:i\d+>>      ParameterValue
  /// CHECK:     <<Conv:b\d+>>        TypeConversion [<<Param2>>]
  /// CHECK:     <<NullCheck:l\d+>>   NullCheck [<<Param0>>] env:[[_,_,_,_]]
  public static void $noinline$testEnvCleanup(byte[] buf, int bp, int code) {
    buf[bp] = (byte) code;
  }
}

public class Main {
  private static void assertByteEquals(byte expected, byte actual) {
    if (expected != actual) {
      throw new AssertionError("Wrong result: " + expected + " != " + actual);
    }
  }

  // Check that we have precise environment before passes that require it:
  // * Inliner
  // * Bounds check elimination
  // * Loop optimization
  // * CHA guard elimination

  /// CHECK-START: void Main.$noinline$testEnvCleanup(byte[], int, int) inliner (before)
  /// CHECK:     <<Param0:l\d+>>      ParameterValue
  /// CHECK:     <<Param1:i\d+>>      ParameterValue
  /// CHECK:     <<Param2:i\d+>>      ParameterValue
  /// CHECK:     <<Conv:b\d+>>        TypeConversion [<<Param2>>]
  /// CHECK:     <<NullCheck:l\d+>>   NullCheck [<<Param0>>] env:[[<<Conv>>,<<Param0>>,<<Param1>>,<<Param2>>]]

  /// CHECK-START: void Main.$noinline$testEnvCleanup(byte[], int, int) BCE (before)
  /// CHECK:     <<Param0:l\d+>>      ParameterValue
  /// CHECK:     <<Param1:i\d+>>      ParameterValue
  /// CHECK:     <<Param2:i\d+>>      ParameterValue
  /// CHECK:     <<Conv:b\d+>>        TypeConversion [<<Param2>>]
  /// CHECK:     <<NullCheck:l\d+>>   NullCheck [<<Param0>>] env:[[<<Conv>>,<<Param0>>,<<Param1>>,<<Param2>>]]

  /// CHECK-START: void Main.$noinline$testEnvCleanup(byte[], int, int) loop_optimization (before)
  /// CHECK:     <<Param0:l\d+>>      ParameterValue
  /// CHECK:     <<Param1:i\d+>>      ParameterValue
  /// CHECK:     <<Param2:i\d+>>      ParameterValue
  /// CHECK:     <<Conv:b\d+>>        TypeConversion [<<Param2>>]
  /// CHECK:     <<NullCheck:l\d+>>   NullCheck [<<Param0>>] env:[[<<Conv>>,<<Param0>>,<<Param1>>,<<Param2>>]]

  /// CHECK-START: void Main.$noinline$testEnvCleanup(byte[], int, int) cha_guard_optimization (before)
  /// CHECK:     <<Param0:l\d+>>      ParameterValue
  /// CHECK:     <<Param1:i\d+>>      ParameterValue
  /// CHECK:     <<Param2:i\d+>>      ParameterValue
  /// CHECK:     <<Conv:b\d+>>        TypeConversion [<<Param2>>]
  /// CHECK:     <<NullCheck:l\d+>>   NullCheck [<<Param0>>] env:[[<<Conv>>,<<Param0>>,<<Param1>>,<<Param2>>]]

  /// CHECK-START: void Main.$noinline$testEnvCleanup(byte[], int, int) environment_input_elimination (before)
  /// CHECK:     <<Param0:l\d+>>      ParameterValue
  /// CHECK:     <<Param1:i\d+>>      ParameterValue
  /// CHECK:     <<Param2:i\d+>>      ParameterValue
  /// CHECK:     <<Conv:b\d+>>        TypeConversion [<<Param2>>]
  /// CHECK:     <<NullCheck:l\d+>>   NullCheck [<<Param0>>] env:[[<<Conv>>,<<Param0>>,<<Param1>>,<<Param2>>]]

  // Check that we successfully clean up NullCheck's environment and could remove
  // redundant TypeConversion after it.

  /// CHECK-START: void Main.$noinline$testEnvCleanup(byte[], int, int) environment_input_elimination (after)
  /// CHECK:     <<Param0:l\d+>>      ParameterValue
  /// CHECK:     <<Param1:i\d+>>      ParameterValue
  /// CHECK:     <<Param2:i\d+>>      ParameterValue
  /// CHECK:     <<Conv:b\d+>>        TypeConversion [<<Param2>>]
  /// CHECK:     <<NullCheck:l\d+>>   NullCheck [<<Param0>>] env:[[_,<<Param0>>,_,_]]

  /// CHECK-START: void Main.$noinline$testEnvCleanup(byte[], int, int) instruction_simplifier$before_codegen (after)
  /// CHECK-NOT:                      TypeConversion

  // Check that we don't clean up environment in the case of debuggable graph.

  /// CHECK-START-DEBUGGABLE: void Main.$noinline$testEnvCleanup(byte[], int, int) environment_input_elimination (after)
  /// CHECK:     <<Param0:l\d+>>      ParameterValue
  /// CHECK:     <<Param1:i\d+>>      ParameterValue
  /// CHECK:     <<Param2:i\d+>>      ParameterValue
  /// CHECK:     <<Conv:b\d+>>        TypeConversion [<<Param2>>]
  /// CHECK:     <<NullCheck:l\d+>>   NullCheck [<<Param0>>] env:[[<<Conv>>,<<Param0>>,<<Param1>>,<<Param2>>]]
  private static void $noinline$testEnvCleanup(byte[] buf, int bp, int code) {
    buf[bp] = (byte) code;
  }

  // Check that we clean up parent environment in the case of inlining.

  private static void $inline$testEnvCleanupInlining(byte[] buf, int bp, int code) {
    buf[bp] = (byte) code;
  }

  /// CHECK-START: void Main.$noinline$testEnvCleanupInlining(byte[], int, int) environment_input_elimination (after)
  /// CHECK:     <<Param0:l\d+>>      ParameterValue
  /// CHECK:     <<Param1:i\d+>>      ParameterValue
  /// CHECK:     <<Param2:i\d+>>      ParameterValue
  /// CHECK:     <<Conv:b\d+>>        TypeConversion [<<Param2>>]
  /// CHECK:     <<NullCheck:l\d+>>   NullCheck [<<Param0>>] env:[[_,<<Param0>>,_,_],[<<Param0>>,_,_]]

  /// CHECK-START: void Main.$noinline$testEnvCleanupInlining(byte[], int, int) instruction_simplifier$before_codegen (after)
  /// CHECK-NOT:                      TypeConversion
  private static void $noinline$testEnvCleanupInlining(byte[] buf, int bp, int code) {
    $inline$testEnvCleanupInlining(buf, bp, code);
  }

  // Check that we don't remove instructions with reference type from the environment
  // in the case of presence of monitor operations.

  /// CHECK-START: void Main.$noinline$testEnvCleanupUnrelatedSyncBlock(byte[], int, int) environment_input_elimination (after)
  /// CHECK:     <<Param0:l\d+>>      ParameterValue
  /// CHECK:     <<Param1:i\d+>>      ParameterValue
  /// CHECK:     <<Param2:i\d+>>      ParameterValue
  /// CHECK:                          MonitorOperation
  /// CHECK:     <<Conv:b\d+>>        TypeConversion [<<Param2>>]
  /// CHECK:     <<NullCheck:l\d+>>   NullCheck [<<Param0>>] env:[[_,<<Param0>>,_,_]]
  private static void $noinline$testEnvCleanupUnrelatedSyncBlock(byte[] buf, int bp, int code) {
    synchronized (buf) {}
    buf[bp] = (byte) code;
  }

  // Check that we don't clean up instruction's environment if it is in try block.

  /// CHECK-START: void Main.$noinline$testEnvCleanupInTryBlock(byte[], int, int) environment_input_elimination (before)
  /// CHECK:     <<Param0:l\d+>>      ParameterValue
  /// CHECK:     <<Param1:i\d+>>      ParameterValue
  /// CHECK:     <<Param2:i\d+>>      ParameterValue
  /// CHECK:     <<Conv:b\d+>>        TypeConversion [<<Param2>>]
  /// CHECK:     <<NullCheck:l\d+>>   NullCheck [<<Param0>>] env:[[<<Conv>>,<<Param0>>,<<Param1>>,<<Param2>>]]
  /// CHECK:                          TryBoundary

  /// CHECK-START: void Main.$noinline$testEnvCleanupInTryBlock(byte[], int, int) environment_input_elimination (after)
  /// CHECK:     <<Param0:l\d+>>      ParameterValue
  /// CHECK:     <<Param1:i\d+>>      ParameterValue
  /// CHECK:     <<Param2:i\d+>>      ParameterValue
  /// CHECK:     <<Conv:b\d+>>        TypeConversion [<<Param2>>]
  /// CHECK:     <<NullCheck:l\d+>>   NullCheck [<<Param0>>] env:[[<<Conv>>,<<Param0>>,<<Param1>>,<<Param2>>]]
  /// CHECK:                          TryBoundary
  private static void $noinline$testEnvCleanupInTryBlock(byte[] buf, int bp, int code) {
    try {
      buf[bp] = (byte) code;
    } catch (Exception e) {}
  }

  private static void $noinline$throw() {
    throw new RuntimeException();
  }

  // Check that we don't clean up environment of the first instruction of a catch block, other
  // instructions should be treated as usual.

  /// CHECK-START: void Main.$noinline$testEnvCleanupInCatchBlock(byte[], int, int) environment_input_elimination (before)
  /// CHECK:     <<Param0:l\d+>>      ParameterValue
  /// CHECK:     <<Param1:i\d+>>      ParameterValue
  /// CHECK:     <<Param2:i\d+>>      ParameterValue
  /// CHECK:     <<Nop:v\d+>>         Nop env:[[_,_,<<Param0>>,<<Param1>>,<<Param2>>]]
  /// CHECK:     <<Exception:l\d+>>   LoadException
  /// CHECK:     <<Conv:b\d+>>        TypeConversion [<<Param2>>]
  /// CHECK:     <<NullCheck:l\d+>>   NullCheck [<<Param0>>] env:[[<<Exception>>,<<Conv>>,<<Param0>>,<<Param1>>,<<Param2>>]]

  /// CHECK-START: void Main.$noinline$testEnvCleanupInCatchBlock(byte[], int, int) environment_input_elimination (after)
  /// CHECK:     <<Param0:l\d+>>      ParameterValue
  /// CHECK:     <<Param1:i\d+>>      ParameterValue
  /// CHECK:     <<Param2:i\d+>>      ParameterValue
  /// CHECK:     <<Nop:v\d+>>         Nop env:[[_,_,<<Param0>>,<<Param1>>,<<Param2>>]]
  /// CHECK:     <<Exception:l\d+>>   LoadException
  /// CHECK:     <<Conv:b\d+>>        TypeConversion [<<Param2>>]
  /// CHECK:     <<NullCheck:l\d+>>   NullCheck [<<Param0>>] env:[[<<Exception>>,_,<<Param0>>,_,_]]
  private static void $noinline$testEnvCleanupInCatchBlock(byte[] buf, int bp, int code) {
    try {
      $noinline$throw();
    } catch (Exception e) {
      buf[bp] = (byte) code;
    }
  }

  // Check that we don't clean up environment of Deoptimize instruction.

  /// CHECK-START: void Main.$noinline$testDeoptEnvCleanup(byte[], int, int) environment_input_elimination (before)
  /// CHECK:     <<Param0:l\d+>>      ParameterValue
  /// CHECK:     <<Param1:i\d+>>      ParameterValue
  /// CHECK:     <<Param2:i\d+>>      ParameterValue
  /// CHECK:     <<Conv:b\d+>>        TypeConversion [<<Param2>>]
  /// CHECK:     <<Deoptimize:v\d+>>  Deoptimize env:[[<<Conv>>,_,<<Param0>>,<<Param1>>,<<Param2>>]]

  /// CHECK-START: void Main.$noinline$testDeoptEnvCleanup(byte[], int, int) environment_input_elimination (after)
  /// CHECK:     <<Param0:l\d+>>      ParameterValue
  /// CHECK:     <<Param1:i\d+>>      ParameterValue
  /// CHECK:     <<Param2:i\d+>>      ParameterValue
  /// CHECK:     <<Conv:b\d+>>        TypeConversion [<<Param2>>]
  /// CHECK:     <<Deoptimize:v\d+>>  Deoptimize env:[[<<Conv>>,_,<<Param0>>,<<Param1>>,<<Param2>>]]
  private static void $noinline$testDeoptEnvCleanup(byte[] buf, int bp, int code) {
    buf[bp] = (byte) code;
    buf[bp + 1] = (byte) (code + 1);
    buf[bp + 2] = (byte) (code + 2);
  }

  public static void main(String[] args) {
    byte[] buf = new byte[3];

    $noinline$testEnvCleanup(buf, 0, 0xaabb);
    assertByteEquals((byte) 0xbb, buf[0]);

    $noinline$testEnvCleanupUnrelatedSyncBlock(buf, 0, 0xaabb);
    assertByteEquals((byte) 0xbb, buf[0]);

    $noinline$testEnvCleanupInTryBlock(buf, 0, 0xaabb);
    assertByteEquals((byte) 0xbb, buf[0]);

    $noinline$testEnvCleanupInCatchBlock(buf, 0, 0xaabb);
    assertByteEquals((byte) 0xbb, buf[0]);

    $noinline$testDeoptEnvCleanup(buf, 0, 0xaab0);
    assertByteEquals((byte) 0xb0, buf[0]);
    assertByteEquals((byte) 0xb1, buf[1]);
    assertByteEquals((byte) 0xb2, buf[2]);

    DeadReferenceSafeClass.$noinline$testEnvCleanup(buf, 0, 0xaabb);
    assertByteEquals((byte) 0xbb, buf[0]);
  }
}
