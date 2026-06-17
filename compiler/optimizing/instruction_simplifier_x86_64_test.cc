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

#include "instruction_simplifier_x86_64.h"

#include "base/globals.h"
#include "com_android_art_flags.h"
#include "instruction_simplifier_x86_shared_test.h"

namespace art HIDDEN {
namespace x86_64 {

class InstructionSimplifierX86_64Test : public InstructionSimplifierX86SharedTest {
 public:
  InstructionSet GetInstructionSet() const override { return InstructionSet::kX86_64; }

  void RunSimplifier(CodeGenerator* codegen) override {
    InstructionSimplifierX86_64(graph_, codegen, /*stats=*/ nullptr).Run();
  }
};

TEST_F(InstructionSimplifierX86_64Test, AddToLea32WithDisp) {
  if (!com::android::art::flags::x86_lea_optimizations()) {
    GTEST_SKIP() << "x86 LEA optimizations disabled.";
  }
  for (uint32_t shift : kLea32TestShifts) {
    bool expect_lea = (0u < shift && shift < 4u);
    LeaExpect expect = expect_lea ? LeaExpect::kLeaWithDisp : LeaExpect::kNoLea;
    for (bool lhs_shift : {false, true}) {
      for (int32_t disp : kLea32TestDisplacements) {
        TestAddToLeaSimplification(
            expect, LeaBaseOption::kDisp, DataType::Type::kInt32, shift, lhs_shift, disp);
      }
    }
  }
}

TEST_F(InstructionSimplifierX86_64Test, AddToLea64WithDisp) {
  if (!com::android::art::flags::x86_lea_optimizations()) {
    GTEST_SKIP() << "x86 LEA optimizations disabled.";
  }
  for (uint32_t shift : kLea64TestShifts) {
    bool expect_lea = (0u < shift && shift < 4u);
    for (bool lhs_shift : {false, true}) {
      for (int64_t disp : kLea64TestDisplacements) {
        LeaExpect expect = expect_lea
            // Constants outside the `int32_t` range are added as base, not a displacement.
            ? (IsInt<32>(disp) ? LeaExpect::kLeaWithDisp : LeaExpect::kLeaWithBase)
            : LeaExpect::kNoLea;
        TestAddToLeaSimplification(
            expect, LeaBaseOption::kDisp, DataType::Type::kInt64, shift, lhs_shift, disp);
      }
    }
  }
}

TEST_F(InstructionSimplifierX86_64Test, AddToLea32WithBase) {
  if (!com::android::art::flags::x86_lea_optimizations()) {
    GTEST_SKIP() << "x86 LEA optimizations disabled.";
  }
  for (uint32_t shift : kLea32TestShifts) {
    bool expect_lea = (0u < shift && shift < 4u);
    LeaExpect expect = expect_lea ? LeaExpect::kLeaWithBase : LeaExpect::kNoLea;
    for (bool lhs_shift : {false, true}) {
      TestAddToLeaSimplification(
          expect, LeaBaseOption::kBase, DataType::Type::kInt32, shift, lhs_shift, /*disp=*/ 0);
    }
  }
}

TEST_F(InstructionSimplifierX86_64Test, AddToLea64WithBase) {
  if (!com::android::art::flags::x86_lea_optimizations()) {
    GTEST_SKIP() << "x86 LEA optimizations disabled.";
  }
  for (uint32_t shift : kLea64TestShifts) {
    bool expect_lea = (0u < shift && shift < 4u);
    for (bool lhs_shift : {false, true}) {
      LeaExpect expect = expect_lea ? LeaExpect::kLeaWithBase : LeaExpect::kNoLea;
      TestAddToLeaSimplification(
          expect, LeaBaseOption::kBase, DataType::Type::kInt64, shift, lhs_shift, /*disp=*/ 0);
    }
  }
}

TEST_F(InstructionSimplifierX86_64Test, AddToLea32WithBaseAndDisp) {
  if (!com::android::art::flags::x86_lea_optimizations()) {
    GTEST_SKIP() << "x86 LEA optimizations disabled.";
  }
  for (uint32_t shift : kLea32TestShifts) {
    for (LeaBaseOption base_opt : kLeaBaseOptionsWithBaseAndDisp) {
      // Except for `kDispMinusBase`, all expressions can be simplified with a split base and
      // displacement. For `kDispMinusBase`, the simplification is done only for shifts by 1 to 3.
      bool expect_lea_with_base_and_disp = (base_opt != LeaBaseOption::kDispMinusBase);
      LeaExpect expect = expect_lea_with_base_and_disp
          ? LeaExpect::kLeaWithBaseAndDisp
          : ((0u < shift && shift < 4u) ? LeaExpect::kLeaWithBase : LeaExpect::kNoLea);
      for (bool lhs_shift : {false, true}) {
        for (int32_t disp : kLea32TestDisplacements) {
          TestAddToLeaSimplification(
              expect, base_opt, DataType::Type::kInt32, shift, lhs_shift, disp);
        }
      }
    }
  }
}

TEST_F(InstructionSimplifierX86_64Test, AddToLea64WithBaseAndDisp) {
  if (!com::android::art::flags::x86_lea_optimizations()) {
    GTEST_SKIP() << "x86 LEA optimizations disabled.";
  }
  for (uint32_t shift : kLea64TestShifts) {
    for (LeaBaseOption base_opt : kLeaBaseOptionsWithBaseAndDisp) {
      for (bool lhs_shift : {false, true}) {
        for (int64_t disp : kLea64TestDisplacements) {
          // Except for `kDispMinusBase`, all expressions can be simplified with a split
          // base and displacement as long as the displacement fits in `int32_t`.
          // Otherwise, the simplification is done only for shifts by 1 to 3.
          bool expect_lea_with_base_and_disp =
              (base_opt != LeaBaseOption::kDispMinusBase) &&
              IsInt<32>((base_opt == LeaBaseOption::kBaseMinusDisp ? -1 : 1) * disp);
          LeaExpect expect = expect_lea_with_base_and_disp
              ? LeaExpect::kLeaWithBaseAndDisp
              : ((0u < shift && shift < 4u) ? LeaExpect::kLeaWithBase : LeaExpect::kNoLea);
          TestAddToLeaSimplification(
              expect, base_opt, DataType::Type::kInt64, shift, lhs_shift, disp);
        }
      }
    }
  }
}

TEST_F(InstructionSimplifierX86_64Test, SubToLea32WithDisp) {
  if (!com::android::art::flags::x86_lea_optimizations()) {
    GTEST_SKIP() << "x86 LEA optimizations disabled.";
  }
  for (uint32_t shift : kLea32TestShifts) {
    for (bool lhs_shift : {false, true}) {
      bool expect_lea = lhs_shift && (0u < shift && shift < 4u);
      LeaExpect expect = expect_lea ? LeaExpect::kLeaWithDisp : LeaExpect::kNoLea;
      for (int32_t disp : kLea32TestDisplacements) {
        TestSubToLeaSimplification(
            expect, LeaBaseOption::kDisp, DataType::Type::kInt32, shift, lhs_shift, disp);
      }
    }
  }
}

TEST_F(InstructionSimplifierX86_64Test, SubToLea64WithDisp) {
  if (!com::android::art::flags::x86_lea_optimizations()) {
    GTEST_SKIP() << "x86 LEA optimizations disabled.";
  }
  for (uint32_t shift : kLea64TestShifts) {
    for (bool lhs_shift : {false, true}) {
      bool expect_lea = lhs_shift && (0u < shift && shift < 4u);
      for (int64_t disp : kLea64TestDisplacements) {
        LeaExpect expect = expect_lea
            // Constants outside the `int32_t` range are added as base, not a displacement.
            ? (IsInt<32>(-disp) ? LeaExpect::kLeaWithDisp : LeaExpect::kLeaWithBase)
            : LeaExpect::kNoLea;
        TestSubToLeaSimplification(
            expect, LeaBaseOption::kDisp, DataType::Type::kInt64, shift, lhs_shift, disp);
      }
    }
  }
}

TEST_F(InstructionSimplifierX86_64Test, SubToLea32WithBase) {
  if (!com::android::art::flags::x86_lea_optimizations()) {
    GTEST_SKIP() << "x86 LEA optimizations disabled.";
  }
  for (uint32_t shift : kLea32TestShifts) {
    for (bool lhs_shift : {false, true}) {
      LeaExpect expect = LeaExpect::kNoLea;  // No simplification for `(a << s) - b`.
      TestSubToLeaSimplification(
          expect, LeaBaseOption::kBase, DataType::Type::kInt32, shift, lhs_shift, /*disp=*/ 0);
    }
  }
}

TEST_F(InstructionSimplifierX86_64Test, SubToLea64WithBase) {
  if (!com::android::art::flags::x86_lea_optimizations()) {
    GTEST_SKIP() << "x86 LEA optimizations disabled.";
  }
  for (uint32_t shift : kLea64TestShifts) {
    for (bool lhs_shift : {false, true}) {
      LeaExpect expect = LeaExpect::kNoLea;  // No simplification for `(a << s) - b`.
      TestSubToLeaSimplification(
          expect, LeaBaseOption::kBase, DataType::Type::kInt64, shift, lhs_shift, /*disp=*/ 0);
    }
  }
}

TEST_F(InstructionSimplifierX86_64Test, SubToLea32WithBaseAndDisp) {
  if (!com::android::art::flags::x86_lea_optimizations()) {
    GTEST_SKIP() << "x86 LEA optimizations disabled.";
  }
  for (uint32_t shift : kLea32TestShifts) {
    for (LeaBaseOption base_opt : kLeaBaseOptionsWithBaseAndDisp) {
      for (bool lhs_shift : {false, true}) {
        for (int32_t disp : kLea32TestDisplacements) {
          TestSubToLeaSimplification(
              LeaExpect::kNoLea, base_opt, DataType::Type::kInt32, shift, lhs_shift, disp);
        }
      }
    }
  }
}

TEST_F(InstructionSimplifierX86_64Test, SubToLea64WithBaseAndDisp) {
  if (!com::android::art::flags::x86_lea_optimizations()) {
    GTEST_SKIP() << "x86 LEA optimizations disabled.";
  }
  for (uint32_t shift : kLea64TestShifts) {
    for (LeaBaseOption base_opt : kLeaBaseOptionsWithBaseAndDisp) {
      for (bool lhs_shift : {false, true}) {
        for (int64_t disp : kLea64TestDisplacements) {
          TestSubToLeaSimplification(
              LeaExpect::kNoLea, base_opt, DataType::Type::kInt64, shift, lhs_shift, disp);
        }
      }
    }
  }
}

TEST_F(InstructionSimplifierX86_64Test, AddAdd_AddSub_SubAdd_ToLea32) {
  if (!com::android::art::flags::x86_lea_optimizations()) {
    GTEST_SKIP() << "x86 LEA optimizations disabled.";
  }
  for (int32_t disp : kLea32TestDisplacements) {
    for (LeaSubPosition sub_pos : kLeaSubPositions) {
      for (bool inbinop_is_left : {false, true}) {
        for (LeaDispPosition disp_pos : kLeaDispPositions) {
          // From `Sub` cases, only those with constant on the right in the `HSub` are simplified.
          bool expression_ok = HasNoSubOrSubWithConstRhs(sub_pos, inbinop_is_left, disp_pos);
          LeaExpect expect = expression_ok ? LeaExpect::kLeaWithBaseAndDisp : LeaExpect::kNoLea;
          Test_AddAdd_AddSub_SubAdd_ToLeaSimplification(
              expect, DataType::Type::kInt32, sub_pos, inbinop_is_left, disp_pos, disp);
        }
      }
    }
  }
}

TEST_F(InstructionSimplifierX86_64Test, AddAdd_AddSub_SubAdd_ToLea64) {
  if (!com::android::art::flags::x86_lea_optimizations()) {
    GTEST_SKIP() << "x86 LEA optimizations disabled.";
  }
  for (int64_t disp : kLea64TestDisplacements) {
    for (LeaSubPosition sub_pos : kLeaSubPositions) {
      bool disp_ok = IsInt<32>(sub_pos == LeaSubPosition::kNone ? disp : -disp);
      for (bool inbinop_is_left : {false, true}) {
        for (LeaDispPosition disp_pos : kLeaDispPositions) {
          // From `Sub` cases, only those with constant on the right in the `HSub` are simplified.
          bool expression_ok = HasNoSubOrSubWithConstRhs(sub_pos, inbinop_is_left, disp_pos);
          LeaExpect expect =
              (disp_ok && expression_ok) ? LeaExpect::kLeaWithBaseAndDisp : LeaExpect::kNoLea;
          Test_AddAdd_AddSub_SubAdd_ToLeaSimplification(
              expect, DataType::Type::kInt64, sub_pos, inbinop_is_left, disp_pos, disp);
        }
      }
    }
  }
}

}  // namespace x86_64
}  // namespace art
