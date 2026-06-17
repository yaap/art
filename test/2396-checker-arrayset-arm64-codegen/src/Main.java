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

public class Main {
  public static void main(String[] args) {
    // When following are true:
    //
    //   * needs_write_barrier
    //   * can_be_null
    //   * write_barrier_kind != EmitNotBeingReliedOn
    //   * !needs_type_check
    //
    // , the generated assembly for ArraySet should not contain any branching.
    $noinline$ArraySetDoesNotBranch();

    // However, if needs_type_check, it should branch.
    $noinline$ArraySetNeedsTypeCheck();
  }

  /// CHECK-START-ARM64: java.lang.Object[] Main.$noinline$ArraySetDoesNotBranch() disassembly (after)
  /// CHECK: ArraySet value_can_be_null:true needs_type_check:false write_barrier_kind:EmitBeingReliedOn
  // Do not branch to do_store, as it is the next instruction.
  /// CHECK-NOT: cbz
  /// CHECK: ArraySet value_can_be_null:true needs_type_check:false write_barrier_kind:DontEmit
  private static java.lang.Object[] $noinline$ArraySetDoesNotBranch() {
    Object[] arr = new Object[2];
    arr[0] = inner_static;
    arr[1] = inner_static2;
    return arr;
  }

  /// CHECK-START-ARM64: java.lang.Object[] Main.$noinline$ArraySetNeedsTypeCheck() disassembly (after)
  /// CHECK: ArraySet value_can_be_null:true needs_type_check:true write_barrier_kind:EmitBeingReliedOn
  // Generate branch instruction to do_store, as it is not the next basic block.
  /// CHECK-NEXT: cbz
  /// CHECK: ArraySet value_can_be_null:true needs_type_check:false write_barrier_kind:DontEmit
  private static java.lang.Object[] $noinline$ArraySetNeedsTypeCheck() {
    Object[] arr = new Integer[2];
    arr[0] = inner_static;
    arr[1] = inner_static3;
    return arr;
  }

  static Object inner_static;
  static Object inner_static2;
  static Integer inner_static3;
}
