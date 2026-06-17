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

#ifndef ART_COMPILER_OPTIMIZING_INSTRUCTION_SIMPLIFIER_X86_SHARED_TEST_H_
#define ART_COMPILER_OPTIMIZING_INSTRUCTION_SIMPLIFIER_X86_SHARED_TEST_H_

#include <gtest/gtest.h>

#include "base/macros.h"
#include "code_generator.h"
#include "driver/compiler_options.h"
#include "optimizing_unit_test.h"

namespace art HIDDEN {

class InstructionSimplifierX86SharedTest : public OptimizingUnitTest {
 protected:
  enum class LeaExpect {
    kNoLea,
    kLeaWithBase,
    kLeaWithDisp,
    kLeaWithBaseAndDisp
  };

  enum class LeaBaseOption {
    kDisp,
    kBase,
    kBasePlusDisp,
    kDispPlusBase,
    kBaseMinusDisp,
    kDispMinusBase,  // Should not be split; expect `kLeaWithBase` if simplified.
  };

  enum class LeaSubPosition {
    kNone,        // Add(Add(., .), .) or Add(., Add(., .)).
    kInputBinop,  // Add(Sub(., .), .) or Add(., Sub(., .)).
    kMainBinop,   // Sub(Add(., .), .) or Sub(., Add(., .)).
  };

  enum class LeaDispPosition {
    kOther,
    kInputBinopLeft,
    kInputBinopRight,
  };

  static constexpr LeaBaseOption kLeaBaseOptionsWithBaseAndDisp[] = {
      LeaBaseOption::kBasePlusDisp,
      LeaBaseOption::kDispPlusBase,
      LeaBaseOption::kBaseMinusDisp,
      LeaBaseOption::kDispMinusBase,
  };

  static constexpr LeaSubPosition kLeaSubPositions[] = {
      LeaSubPosition::kNone,
      LeaSubPosition::kInputBinop,
      LeaSubPosition::kMainBinop,
  };

  static constexpr LeaDispPosition kLeaDispPositions[] = {
      LeaDispPosition::kOther,
      LeaDispPosition::kInputBinopLeft,
      LeaDispPosition::kInputBinopRight,
  };

  static constexpr uint32_t kLea32TestShifts[] = {0u, 1u, 2u, 3u, 4u, 7u, 31u};
  static constexpr uint32_t kLea64TestShifts[] = {0u, 1u, 2u, 3u, 4u, 7u, 31u, 32u, 63u};

  static constexpr int64_t kLea32TestDisplacements[] = {
      0,
      1,
      -1,
      std::numeric_limits<int32_t>::min(),
      std::numeric_limits<int32_t>::max(),
  };

  static constexpr int64_t kLea64TestDisplacements[] = {
      0,
      1,
      -1,
      std::numeric_limits<int32_t>::min() + INT64_C(1),
      std::numeric_limits<int32_t>::max() - INT64_C(1),
      std::numeric_limits<int32_t>::min(),
      std::numeric_limits<int32_t>::max(),
      std::numeric_limits<int32_t>::min() - INT64_C(1),
      std::numeric_limits<int32_t>::max() + INT64_C(1),
      std::numeric_limits<int64_t>::min(),
      std::numeric_limits<int64_t>::max(),
  };

  virtual InstructionSet GetInstructionSet() const = 0;
  virtual void RunSimplifier(CodeGenerator* codegen) = 0;

  HInstruction* PrepareLeaOperand(LeaBaseOption base_opt,
                                  HBasicBlock* block,
                                  DataType::Type type,
                                  HInstruction* base,
                                  HInstruction* disp) {
    CHECK_EQ(base_opt == LeaBaseOption::kDisp, base == nullptr);
    CHECK_EQ(base_opt == LeaBaseOption::kBase, disp == nullptr);
    switch (base_opt) {
      case LeaBaseOption::kDisp:
        return disp;
      case LeaBaseOption::kBase:
        return base;
      case LeaBaseOption::kBasePlusDisp:
        return MakeBinOp<HAdd>(block, type, base, disp);
      case LeaBaseOption::kDispPlusBase:
        return MakeBinOp<HAdd>(block, type, disp, base);
      case LeaBaseOption::kBaseMinusDisp:
        return MakeBinOp<HSub>(block, type, base, disp);
      case LeaBaseOption::kDispMinusBase:
        return MakeBinOp<HSub>(block, type, disp, base);
    }
  }

  template <typename BinOpType>
  HBinaryOperation* PrepareLeaExpression(LeaBaseOption base_opt,
                                         HBasicBlock* block,
                                         DataType::Type type,
                                         HInstruction* index,
                                         HInstruction* base,
                                         uint32_t shift,
                                         bool lhs_shift,
                                         int64_t disp) {
    HInstruction* disp_cst = nullptr;
    if (base_opt != LeaBaseOption::kBase) {
      disp_cst = (type == DataType::Type::kInt32)
          ? graph_->GetIntConstant(dchecked_integral_cast<int32_t>(disp))
          : static_cast<HInstruction*>(graph_->GetLongConstant(disp));
    }
    HInstruction* shifted =
        (shift != 0u) ? MakeBinOp<HShl>(block, type, index, graph_->GetIntConstant(shift)) : index;
    HInstruction* other = PrepareLeaOperand(base_opt, block, type, base, disp_cst);
    return MakeBinOp<BinOpType>(
        block, type, lhs_shift ? shifted : other, lhs_shift ? other : shifted);
  }

  void VerifyLeaSimplification(HInstruction* instruction,
                               HBinaryOperation* binop,
                               HInstruction* index,
                               HInstruction* base,
                               LeaExpect expect,
                               LeaBaseOption base_opt,
                               uint32_t shift,
                               bool lhs_shift,
                               int64_t disp) {
    ASSERT_EQ(expect == LeaExpect::kNoLea, instruction == binop)
        << "base_opt: " << enum_cast<int>(base_opt) << ", shift: " << shift
        << ", lhs_shift: " << std::boolalpha << lhs_shift << ", disp: " << disp;
    if (expect == LeaExpect::kNoLea) {
      EXPECT_INS_RETAINED(binop);
      return;
    }
    ASSERT_TRUE(instruction->IsX86LoadEffectiveAddress());
    HX86LoadEffectiveAddress* lea = instruction->AsX86LoadEffectiveAddress();
    if (shift < 4u) {
      ASSERT_INS_EQ(index, lea->GetIndex());
      ASSERT_EQ(lea->GetShift(), shift);
    } else {
      ASSERT_TRUE(lea->GetIndex()->IsShl());
      ASSERT_TRUE(lea->GetIndex()->AsShl()->GetRight()->IsIntConstant());
      ASSERT_EQ(dchecked_integral_cast<int32_t>(shift),
                lea->GetIndex()->AsShl()->GetRight()->AsIntConstant()->GetValue());
      ASSERT_INS_EQ(index, lea->GetIndex()->AsShl()->GetLeft());
    }
    if (expect == LeaExpect::kLeaWithDisp) {
      CHECK(base_opt == LeaBaseOption::kDisp);
      ASSERT_FALSE(lea->HasBase());
      ASSERT_EQ(lea->GetDisplacement(), disp);
    } else if (expect == LeaExpect::kLeaWithBase) {
      ASSERT_TRUE(lea->HasBase());
      ASSERT_EQ(0, lea->GetDisplacement());
      if (binop->IsAdd()) {
        // The other instruction was not split into base and displacement.
        HInstruction* other = lhs_shift ? binop->GetRight() : binop->GetLeft();
        ASSERT_INS_EQ(other, lea->GetBase());
      } else {
        // Constant that outside the `int32_t` range.
        ASSERT_TRUE(base->IsLongConstant());
        ASSERT_FALSE(IsInt<32>(base->AsLongConstant()->GetValue()));
        ASSERT_INS_EQ(base, lea->GetBase());
      }
    } else {
      CHECK(expect == LeaExpect::kLeaWithBaseAndDisp);
      CHECK(base_opt != LeaBaseOption::kDisp);
      ASSERT_TRUE(lea->HasBase());
      ASSERT_INS_EQ(base, lea->GetBase());
      int32_t expected_disp =
          static_cast<int32_t>((base_opt == LeaBaseOption::kBaseMinusDisp) ? -disp : disp);
      ASSERT_EQ(expected_disp, lea->GetDisplacement());
    }
  }

  void CreateCodegenAndRunSimplifier() {
    std::unique_ptr<CompilerOptions> compiler_options =
        CommonCompilerTest::CreateCompilerOptions(GetInstructionSet(), "default");
    std::unique_ptr<CodeGenerator> codegen = CodeGenerator::Create(graph_, *compiler_options);
    ASSERT_TRUE(codegen != nullptr);
    graph_->BuildDominatorTree();
    RunSimplifier(codegen.get());
  }

  void TestAddToLeaSimplification(LeaExpect expect,
                                  LeaBaseOption base_opt,
                                  DataType::Type type,
                                  uint32_t shift,
                                  bool lhs_shift,
                                  int64_t disp) {
    CHECK(type == DataType::Type::kInt32 || type == DataType::Type::kInt64);
    HBasicBlock* block = InitEntryMainExitGraph();
    HInstruction* index = MakeParam(type);
    HInstruction* base = (base_opt != LeaBaseOption::kDisp) ? MakeParam(type) : nullptr;
    HBinaryOperation* binop = PrepareLeaExpression<HAdd>(
        base_opt, block, type, index, base, shift, lhs_shift, disp);
    HReturn* ret = MakeReturn(block, binop);

    CreateCodegenAndRunSimplifier();

    VerifyLeaSimplification(
        ret->InputAt(0), binop, index, base, expect, base_opt, shift, lhs_shift, disp);
  }

  void TestSubToLeaSimplification(LeaExpect expect,
                                  LeaBaseOption base_opt,
                                  DataType::Type type,
                                  uint32_t shift,
                                  bool lhs_shift,
                                  int64_t disp) {
    CHECK(type == DataType::Type::kInt32 || type == DataType::Type::kInt64);
    HBasicBlock* block = InitEntryMainExitGraph();
    HInstruction* index = MakeParam(type);
    HInstruction* base = (base_opt != LeaBaseOption::kDisp) ? MakeParam(type) : nullptr;
    HBinaryOperation* binop = PrepareLeaExpression<HSub>(
        base_opt, block, type, index, base, shift, lhs_shift, disp);
    HReturn* ret = MakeReturn(block, binop);

    CreateCodegenAndRunSimplifier();

    HInstruction* verify_base = nullptr;
    int64_t verify_disp = -disp;
    if (type == DataType::Type::kInt32) {
      verify_disp = static_cast<int32_t>(verify_disp);
    } else if (!IsInt<32>(verify_disp)) {
      verify_base = graph_->GetLongConstant(verify_disp);
      verify_disp = 0;
    }
    HInstruction* ret_input = ret->InputAt(0);
    VerifyLeaSimplification(
        ret_input, binop, index, verify_base, expect, base_opt, shift, lhs_shift, verify_disp);
  }

  static bool HasNoSubOrSubWithConstRhs(LeaSubPosition sub_pos,
                                        bool inbinop_is_left,
                                        LeaDispPosition disp_pos) {
    switch (sub_pos) {
      case LeaSubPosition::kNone:
        return true;
      case LeaSubPosition::kMainBinop:
        return inbinop_is_left && (disp_pos == LeaDispPosition::kOther);
      case LeaSubPosition::kInputBinop:
        return (disp_pos == LeaDispPosition::kInputBinopRight);
    }
  }

  void Test_AddAdd_AddSub_SubAdd_ToLeaSimplification(LeaExpect expect,
                                                     DataType::Type type,
                                                     LeaSubPosition sub_pos,
                                                     bool inbinop_is_left,
                                                     LeaDispPosition disp_pos,
                                                     int64_t disp) {
    CHECK(type == DataType::Type::kInt32 || type == DataType::Type::kInt64);
    HBasicBlock* block = InitEntryMainExitGraph();
    HInstruction* param = MakeParam(type);
    HInstruction* param2 = MakeParam(type);
    HInstruction* disp_cst = (type == DataType::Type::kInt32)
        ? graph_->GetIntConstant(dchecked_integral_cast<int32_t>(disp))
        : static_cast<HInstruction*>(graph_->GetLongConstant(disp));
    HInstruction* other = (disp_pos != LeaDispPosition::kOther) ? param : disp_cst;
    HInstruction* inbinop_left = (disp_pos != LeaDispPosition::kOther)
        ? ((disp_pos == LeaDispPosition::kInputBinopLeft) ? disp_cst : param2)
        : param;
    HInstruction* inbinop_right =
        (disp_pos != LeaDispPosition::kInputBinopRight) ? param2 : disp_cst;
    HBinaryOperation* inbinop = (sub_pos == LeaSubPosition::kInputBinop)
        ? static_cast<HBinaryOperation*>(MakeBinOp<HSub>(block, type, inbinop_left, inbinop_right))
        : static_cast<HBinaryOperation*>(MakeBinOp<HAdd>(block, type, inbinop_left, inbinop_right));
    HInstruction* binop_left = inbinop_is_left ? inbinop : other;
    HInstruction* binop_right = inbinop_is_left ? other : inbinop;
    HBinaryOperation* binop = (sub_pos == LeaSubPosition::kMainBinop)
        ? static_cast<HBinaryOperation*>(MakeBinOp<HSub>(block, type, binop_left, binop_right))
        : static_cast<HBinaryOperation*>(MakeBinOp<HAdd>(block, type, binop_left, binop_right));
    HReturn* ret = MakeReturn(block, binop);

    CreateCodegenAndRunSimplifier();

    HInstruction* ret_input = ret->InputAt(0);
    ASSERT_EQ(expect == LeaExpect::kNoLea, ret_input == binop)
        << "type: " << type << ", sub_pos: " << static_cast<int>(sub_pos) << ", disp: " << disp
        << ", inbinop_is_left: " << inbinop_is_left << ", disp_pos: " << static_cast<int>(disp_pos);
    if (expect == LeaExpect::kNoLea) {
      EXPECT_INS_RETAINED(binop);
      return;
    }
    EXPECT_INS_REMOVED(binop);
    CHECK(expect == LeaExpect::kLeaWithBaseAndDisp);
    HX86LoadEffectiveAddress* lea = ret_input->AsX86LoadEffectiveAddress();
    int64_t expected_disp = (sub_pos != LeaSubPosition::kNone)
        ? (type == DataType::Type::kInt64 ? -disp : static_cast<int32_t>(-disp)) :
        disp;
    EXPECT_EQ(expected_disp, lea->GetDisplacement());
    ASSERT_TRUE(lea->HasBase());
    if (disp_pos != LeaDispPosition::kOther || sub_pos != LeaSubPosition::kNone) {
      EXPECT_INS_EQ(param, lea->GetIndex());
      EXPECT_INS_EQ(param2, lea->GetBase());
    } else {
      EXPECT_INS_EQ(param2, lea->GetIndex());
      EXPECT_INS_EQ(param, lea->GetBase());
    }
  }
};

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_INSTRUCTION_SIMPLIFIER_X86_SHARED_TEST_H_
