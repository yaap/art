/*
 * Copyright (C) 2022 The Android Open Source Project
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

public class Main {

  static final int ITERATIONS = 16;
  static final int SMALL_ITERATIONS = 8;

  // Test 1: This test checks whether the SuspendCheck is removed from the
  // header.

  // Only one SuspendCheck since the function entry one was removed
  /// CHECK-START: void Main.$noinline$testRemoveSuspendCheck(int[]) disassembly (after)
  /// CHECK:        SuspendCheck
  /// CHECK-NOT:    SuspendCheck

  // The loop suspend check is marked as no op
  /// CHECK-START: void Main.$noinline$testRemoveSuspendCheck(int[]) disassembly (after)
  /// CHECK:        SuspendCheck is_no_op:true loop:<<Loop:B\d+>>
  public static void $noinline$testRemoveSuspendCheck(int[] a) {
    for (int i = 0; i < ITERATIONS; i++) {
      a[i++] = i;
    }
  }

  // Test 2: This test checks that the SuspendCheck is not removed from the
  // header because it contains a call to another function.

  // Note that the function entry suspend check is kept as we have a function call
  /// CHECK-START: void Main.$noinline$testRemoveSuspendCheckWithCall(int[]) disassembly (after)
  /// CHECK:        SuspendCheck
  /// CHECK:        SuspendCheck
  /// CHECK-NOT:    SuspendCheck

  // The loop suspend check is not marked as no op
  /// CHECK-START: void Main.$noinline$testRemoveSuspendCheckWithCall(int[]) disassembly (after)
  /// CHECK:        SuspendCheck is_no_op:false loop:<<Loop:B\d+>>
  public static void $noinline$testRemoveSuspendCheckWithCall(int[] a) {
    for (int i = 0; i < ITERATIONS; i++) {
      a[i++] = i;
      $noinline$testRemoveSuspendCheck(a);
    }
  }

  // Test 3:  This test checks that the SuspendCheck is not removed from the
  // header because INSTR_COUNT * TRIP_COUNT exceeds the defined heuristic.

  // Only one SuspendCheck since the function entry one was removed
  /// CHECK-START: void Main.$noinline$testRemoveSuspendCheckAboveHeuristic(int[]) disassembly (after)
  /// CHECK:        SuspendCheck
  /// CHECK-NOT:    SuspendCheck

  // The loop suspend check is not marked as no op
  /// CHECK-START: void Main.$noinline$testRemoveSuspendCheckAboveHeuristic(int[]) disassembly (after)
  /// CHECK:        SuspendCheck is_no_op:false loop:<<Loop:B\d+>>
  public static void $noinline$testRemoveSuspendCheckAboveHeuristic(int[] a) {
    for (int i = 0; i < ITERATIONS * 6; i++) {
      a[i++] = i;
    }
  }

  // Test 4:  This test checks that the SuspendCheck is not removed from the
  // header because the trip count is not known at compile time.

  // Only one SuspendCheck since the function entry one was removed
  /// CHECK-START: void Main.$noinline$testRemoveSuspendCheckUnknownCount(int[], int) disassembly (after)
  /// CHECK:        SuspendCheck
  /// CHECK-NOT:    SuspendCheck

  // The loop suspend check is not marked as no op
  /// CHECK-START: void Main.$noinline$testRemoveSuspendCheckUnknownCount(int[], int) disassembly (after)
  /// CHECK:        SuspendCheck is_no_op:false loop:<<Loop:B\d+>>
  public static void $noinline$testRemoveSuspendCheckUnknownCount(int[] a, int n) {
    for (int i = 0; i < n; i++) {
      a[i++] = i;
    }
  }

  // Test 5: This test checks that the SuspendCheck is removed from the
  // header because it contains an intrinsic.

  // Only one SuspendCheck since the function entry one was removed
  /// CHECK-START: void Main.$noinline$testRemoveSuspendCheckWithIntrinsic(int[]) disassembly (after)
  /// CHECK:        SuspendCheck
  /// CHECK-NOT:    SuspendCheck

  // The loop suspend check is marked as no op
  /// CHECK-START: void Main.$noinline$testRemoveSuspendCheckWithIntrinsic(int[]) disassembly (after)
  /// CHECK:        SuspendCheck is_no_op:true loop:<<Loop:B\d+>>
  public static void $noinline$testRemoveSuspendCheckWithIntrinsic(int[] a) {
    // For no-image we have an extra LoadClass + ClinitCheck that pushes this loop past the
    // threshold of trip_count * number_of_instructions_per_trip. Use SMALL_ITERATIONS to not go
    // over that limit.
    for (int i = 0; i < SMALL_ITERATIONS; i++) {
      a[i] = Integer.numberOfLeadingZeros(i);
    }
  }

  public static void main(String[] args) {
    int[] a = new int[100];
    $noinline$testRemoveSuspendCheck(a);
    $noinline$testRemoveSuspendCheckWithCall(a);
    $noinline$testRemoveSuspendCheckAboveHeuristic(a);
    $noinline$testRemoveSuspendCheckUnknownCount(a, 4);
    $noinline$testRemoveSuspendCheckWithIntrinsic(a);
  }
}
