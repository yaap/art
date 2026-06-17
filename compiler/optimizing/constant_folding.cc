/*
 * Copyright (C) 2014 The Android Open Source Project
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

#include "constant_folding.h"

#include <algorithm>

#include "art_field-inl.h"
#include "base/bit_utils.h"
#include "base/casts.h"
#include "base/locks.h"
#include "base/logging.h"
#include "base/sdk_version.h"
#include "class_root-inl.h"
#include "dex/dex_file-inl.h"
#include "driver/compiler_options.h"
#include "intrinsics_enum.h"
#include "mirror/method_type.h"
#include "mirror/object.h"
#include "obj_ptr.h"
#include "optimizing/data_type.h"
#include "handle_cache-inl.h"
#include "optimizing/nodes.h"
#include "runtime.h"

namespace art HIDDEN {

// This visitor tries to simplify instructions that can be evaluated
// as constants.
class HConstantFoldingVisitor final : public CRTPGraphVisitor<HConstantFoldingVisitor> {
 public:
  explicit HConstantFoldingVisitor(HGraph* graph,
                                   const CompilerOptions& compiler_options,
                                   OptimizingCompilerStats* stats)
      : CRTPGraphVisitor(graph),
        compiler_options_(compiler_options),
        stats_(stats),
        optimizations_occurred_(false) {}

  bool OptimizationsOccurred() const {
    return optimizations_occurred_;
  }

 private:
  // Forward visit functions using the base class forwarding except for those we forward below.
  template <typename U, typename I>
  static constexpr auto ForwardVisit(void (U::*visit)(I*)) {
    return CRTPGraphVisitor::ForwardVisit(visit);
  }

  template <typename I,
            typename = std::enable_if_t<std::is_base_of_v<HBinaryOperation, I>>>
  static constexpr auto ForwardVisit([[maybe_unused]] void (CRTPGraphVisitor::*visit)(I*)) {
    return &HConstantFoldingVisitor::HandleBinaryOperation<I>;
  }

  void VisitUnaryOperation(HUnaryOperation* inst);

  template <typename InstructionType>
  void HandleBinaryOperation(InstructionType* inst);

  // Tries to replace constants in binary operations like:
  // * BinaryOp(Select(false_constant, true_constant, condition), other_constant), or
  // * BinaryOp(other_constant, Select(false_constant, true_constant, condition))
  // with consolidated constants. For example, Add(Select(10, 20, condition), 5) can be replaced
  // with Select(15, 25, condition).
  bool TryRemoveBinaryOperationViaSelect(HBinaryOperation* inst);

  void VisitArrayLength(HArrayLength* inst);
  void VisitDivZeroCheck(HDivZeroCheck* inst);
  void VisitIf(HIf* inst);
  void VisitInvoke(HInvoke* inst);
  void VisitTypeConversion(HTypeConversion* inst);
  void VisitStaticFieldGet(HStaticFieldGet* inst);
  void VisitInstanceFieldGet(HInstanceFieldGet* inst);
  void VisitLoadConstantTableEntry(HLoadConstantTableEntry* inst);

  void FoldFieldValue(HFieldAccess* inst, ObjPtr<mirror::Object> receiver)
      REQUIRES_SHARED(Locks::mutator_lock_);

  void PropagateValue(HBasicBlock* starting_block,
                      HInstruction* variable,
                      std::variant<HConstant*, bool> constant);

  // Intrinsics foldings
  void FoldReverseIntrinsic(HInvoke* invoke);
  void FoldReverseBytesIntrinsic(HInvoke* invoke);
  void FoldBitCountIntrinsic(HInvoke* invoke);
  void FoldDivideUnsignedIntrinsic(HInvoke* invoke);
  void FoldHighestOneBitIntrinsic(HInvoke* invoke);
  void FoldLowestOneBitIntrinsic(HInvoke* invoke);
  void FoldNumberOfLeadingZerosIntrinsic(HInvoke* invoke);
  void FoldNumberOfTrailingZerosIntrinsic(HInvoke* invoke);

  const CompilerOptions& compiler_options_;

  OptimizingCompilerStats* stats_;

  bool optimizations_occurred_;

  template <typename T> friend class CRTPGraphVisitor;

  DISALLOW_COPY_AND_ASSIGN(HConstantFoldingVisitor);
};

// This visitor tries to simplify operations with an absorbing input,
// yielding a constant. For example `input * 0` is replaced by a
// null constant.
class InstructionWithAbsorbingInputSimplifier final
    : public CRTPGraphVisitor<InstructionWithAbsorbingInputSimplifier> {
 public:
  explicit InstructionWithAbsorbingInputSimplifier(HGraph* graph)
      : CRTPGraphVisitor(graph), should_replace_(false), replacement_(nullptr) {}

  bool ShouldReplace() const {
    DCHECK_EQ(should_replace_, replacement_ != nullptr);
    return should_replace_;
  }

  HInstruction* GetReplacement() const {
    return replacement_;
  }

 private:
  // Keep `ForwardVisit()` functions from base class visible except for those we replace below.
  using CRTPGraphVisitor::ForwardVisit;

  // Forward shifts to `HandleShift()`.
  static constexpr auto ForwardVisit(void (CRTPGraphVisitor::*visit)(HShl*)) {
    DCHECK(visit == &CRTPGraphVisitor::VisitShl);
    return &InstructionWithAbsorbingInputSimplifier::HandleShift;
  }
  static constexpr auto ForwardVisit(void (CRTPGraphVisitor::*visit)(HShr*)) {
    DCHECK(visit == &CRTPGraphVisitor::VisitShr);
    return &InstructionWithAbsorbingInputSimplifier::HandleShift;
  }
  static constexpr auto ForwardVisit(void (CRTPGraphVisitor::*visit)(HUShr*)) {
    DCHECK(visit == &CRTPGraphVisitor::VisitUShr);
    return &InstructionWithAbsorbingInputSimplifier::HandleShift;
  }

  // Forward `Equal` and `NotEqual` to `HandleEquality()`.
  static constexpr auto ForwardVisit(void (CRTPGraphVisitor::*visit)(HEqual*)) {
    DCHECK(visit == &CRTPGraphVisitor::VisitEqual);
    return &InstructionWithAbsorbingInputSimplifier::HandleEquality;
  }
  static constexpr auto ForwardVisit(void (CRTPGraphVisitor::*visit)(HNotEqual*)) {
    DCHECK(visit == &CRTPGraphVisitor::VisitNotEqual);
    return &InstructionWithAbsorbingInputSimplifier::HandleEquality;
  }

  // Forward `{Above,Below}{,OrEqual}` to `HandleUnsignedInequality()`.
  static constexpr auto ForwardVisit(void (CRTPGraphVisitor::*visit)(HAbove*)) {
    DCHECK(visit == &CRTPGraphVisitor::VisitAbove);
    return &InstructionWithAbsorbingInputSimplifier::HandleUnsignedInequality;
  }
  static constexpr auto ForwardVisit(void (CRTPGraphVisitor::*visit)(HAboveOrEqual*)) {
    DCHECK(visit == &CRTPGraphVisitor::VisitAboveOrEqual);
    return &InstructionWithAbsorbingInputSimplifier::HandleUnsignedInequality;
  }
  static constexpr auto ForwardVisit(void (CRTPGraphVisitor::*visit)(HBelow*)) {
    DCHECK(visit == &CRTPGraphVisitor::VisitBelow);
    return &InstructionWithAbsorbingInputSimplifier::HandleUnsignedInequality;
  }
  static constexpr auto ForwardVisit(void (CRTPGraphVisitor::*visit)(HBelowOrEqual*)) {
    DCHECK(visit == &CRTPGraphVisitor::VisitBelowOrEqual);
    return &InstructionWithAbsorbingInputSimplifier::HandleUnsignedInequality;
  }

  // Forward `{GreaterThan,LessThan}{,OrEqual}` to `HandleInequality()`.
  static constexpr auto ForwardVisit(void (CRTPGraphVisitor::*visit)(HGreaterThan*)) {
    DCHECK(visit == &CRTPGraphVisitor::VisitGreaterThan);
    return &InstructionWithAbsorbingInputSimplifier::HandleInequality;
  }
  static constexpr auto ForwardVisit(void (CRTPGraphVisitor::*visit)(HGreaterThanOrEqual*)) {
    DCHECK(visit == &CRTPGraphVisitor::VisitGreaterThanOrEqual);
    return &InstructionWithAbsorbingInputSimplifier::HandleInequality;
  }
  static constexpr auto ForwardVisit(void (CRTPGraphVisitor::*visit)(HLessThan*)) {
    DCHECK(visit == &CRTPGraphVisitor::VisitLessThan);
    return &InstructionWithAbsorbingInputSimplifier::HandleInequality;
  }
  static constexpr auto ForwardVisit(void (CRTPGraphVisitor::*visit)(HLessThanOrEqual*)) {
    DCHECK(visit == &CRTPGraphVisitor::VisitLessThanOrEqual);
    return &InstructionWithAbsorbingInputSimplifier::HandleInequality;
  }

  void SetReplacement(HInstruction* replacement) {
    DCHECK(!should_replace_);
    DCHECK(replacement_ == nullptr);
    DCHECK(replacement != nullptr);
    should_replace_ = true;
    replacement_ = replacement;
  }

  void HandleShift(HBinaryOperation* shift);
  void HandleEquality(HBinaryOperation* instruction);
  void HandleUnsignedInequality(HCondition* instruction);
  void HandleInequality(HCondition* instruction);

  void VisitAnd(HAnd* instruction);
  void VisitCompare(HCompare* instruction);
  void VisitMul(HMul* instruction);
  void VisitOr(HOr* instruction);
  void VisitRem(HRem* instruction);
  void VisitSub(HSub* instruction);
  void VisitXor(HXor* instruction);

  bool should_replace_;
  HInstruction* replacement_;

  template <typename T> friend class CRTPGraphVisitor;
};

bool HConstantFolding::Run() {
  HConstantFoldingVisitor visitor(graph_, compiler_options_, stats_);
  // Process basic blocks in reverse post-order in the dominator tree,
  // so that an instruction turned into a constant, used as input of
  // another instruction, may possibly be used to turn that second
  // instruction into a constant as well.
  visitor.VisitReversePostOrder();
  return visitor.OptimizationsOccurred();
}

void HConstantFoldingVisitor::VisitUnaryOperation(HUnaryOperation* inst) {
  // Constant folding: replace `op(a)' with a constant at compile
  // time if `a' is a constant.
  HConstant* constant = inst->TryStaticEvaluation();
  if (constant != nullptr) {
    inst->ReplaceWith(constant);
    inst->GetBlock()->RemoveInstruction(inst);
    optimizations_occurred_ = true;
  } else if (inst->InputAt(0)->IsSelect() && inst->InputAt(0)->HasOnlyOneNonEnvironmentUse()) {
    // Try to replace the select's inputs in Select+UnaryOperation cases. We can do this if both
    // inputs to the select are constants, and this is the only use of the select.
    HSelect* select = inst->InputAt(0)->AsSelect();
    HConstant* false_constant = inst->TryStaticEvaluation(select->GetFalseValue());
    if (false_constant == nullptr) {
      return;
    }
    HConstant* true_constant = inst->TryStaticEvaluation(select->GetTrueValue());
    if (true_constant == nullptr) {
      return;
    }
    DCHECK_EQ(select->InputAt(0), select->GetFalseValue());
    DCHECK_EQ(select->InputAt(1), select->GetTrueValue());
    select->ReplaceInput(false_constant, 0);
    select->ReplaceInput(true_constant, 1);
    select->UpdateType();
    inst->ReplaceWith(select);
    inst->GetBlock()->RemoveInstruction(inst);
    optimizations_occurred_ = true;
  }
}

namespace {

// Similar to HasOnlyOneNonEnvironmentUse but allows several uses iff they are equal. Since this
// will be used only for binops, it will be relatively fast. Relevant for
// TryRemoveBinaryOperationViaSelect.
bool OnlyOneNonEnvironmentUseAllowingDuplicates(HSelect* select) {
  if (select->HasEnvironmentUses()) {
    return false;
  }
  const HUseList<HInstruction*>& use_list = select->GetUses();
  DCHECK(!use_list.empty());
  HInstruction* user = use_list.front().GetUser();
  for (auto& use : use_list) {
    if (use.GetUser() != user) {
      return false;
    }
  }
  return true;
}

}  // anonymous namespace

bool HConstantFoldingVisitor::TryRemoveBinaryOperationViaSelect(HBinaryOperation* inst) {
  HInstruction* left = inst->GetLeft();
  HInstruction* right = inst->GetRight();
  const bool left_is_select = left->IsSelect();
  const bool right_is_select = right->IsSelect();

  if (!left_is_select && !right_is_select) {
    // If both of them are constants, HandleBinaryOperation already tried the static evaluation and
    // failed.
    return false;
  }

  if (left_is_select &&
      right_is_select &&
      left->AsSelect()->GetCondition() != right->AsSelect()->GetCondition()) {
    // If both of them are selects, this optimization can only be done when both conditions are the
    // same.
    return false;
  }

  HSelect* select_to_update = nullptr;
  if (left_is_select && OnlyOneNonEnvironmentUseAllowingDuplicates(left->AsSelect())) {
    select_to_update = left->AsSelect();
  } else if (right_is_select && OnlyOneNonEnvironmentUseAllowingDuplicates(right->AsSelect())) {
    select_to_update = right->AsSelect();
  }

  if (select_to_update == nullptr) {
    // Can't perform the optimization if there's no select with only one non environment use as the
    // select's result will be used by other instructions.
    return false;
  }

  // Try to replace the select's inputs in Select+BinaryOperation. We can do this if both
  // inputs to the select are constants i.e. if TryStaticEvaluation returns an HConstant.
  HInstruction* left_false_value = left_is_select ? left->AsSelect()->GetFalseValue() : left;
  HInstruction* right_false_value = right_is_select ? right->AsSelect()->GetFalseValue() : right;
  HConstant* false_constant = inst->TryStaticEvaluation(left_false_value, right_false_value);
  if (false_constant == nullptr) {
    return false;
  }

  HInstruction* left_true_value = left_is_select ? left->AsSelect()->GetTrueValue() : left;
  HInstruction* right_true_value = right_is_select ? right->AsSelect()->GetTrueValue() : right;
  HConstant* true_constant = inst->TryStaticEvaluation(left_true_value, right_true_value);
  if (true_constant == nullptr) {
    return false;
  }

  DCHECK_EQ(select_to_update->InputAt(0), select_to_update->GetFalseValue());
  DCHECK_EQ(select_to_update->InputAt(1), select_to_update->GetTrueValue());
  select_to_update->ReplaceInput(false_constant, 0);
  select_to_update->ReplaceInput(true_constant, 1);
  select_to_update->UpdateType();
  inst->ReplaceWith(select_to_update);
  inst->GetBlock()->RemoveInstruction(inst);
  return true;
}

template <typename InstructionType>
ALWAYS_INLINE inline void HConstantFoldingVisitor::HandleBinaryOperation(InstructionType* inst) {
  static_assert(std::is_base_of_v<HBinaryOperation, InstructionType>);
  // Try the `InstructionWithAbsorbingInputSimplifier` first. We reach this from `Dispatch()` in
  // the caller, so this `Dispatch()` is redirected with jump-threading to the visit function.
  bool replace = false;
  HInstruction* replacement = nullptr;
  {
    InstructionWithAbsorbingInputSimplifier simplifier(GetGraph());
    simplifier.Dispatch(inst);
    replace = simplifier.ShouldReplace();
    replacement = simplifier.GetReplacement();
  }
  // This `replace` check is optimized away with jump-threading if the visit above is inlined.
  if (!replace) {
    // Constant folding: replace `op(a, b)' with a constant at
    // compile time if `a' and `b' are both constants.
    replacement = inst->TryStaticEvaluation();
    replace = (replacement != nullptr);
  }
  if (replace) {
    inst->ReplaceWith(replacement);
    inst->GetBlock()->RemoveInstruction(inst);
    optimizations_occurred_ = true;
  } else if (TryRemoveBinaryOperationViaSelect(inst)) {
    // Already replaced inside TryRemoveBinaryOperationViaSelect.
    optimizations_occurred_ = true;
  }
}

void HConstantFoldingVisitor::VisitDivZeroCheck(HDivZeroCheck* inst) {
  // We can safely remove the check if the input is a non-null constant.
  HInstruction* check_input = inst->InputAt(0);
  if (check_input->IsConstant() && !check_input->AsConstant()->IsArithmeticZero()) {
    inst->ReplaceWith(check_input);
    inst->GetBlock()->RemoveInstruction(inst);
    optimizations_occurred_ = true;
  }
}

void HConstantFoldingVisitor::PropagateValue(HBasicBlock* starting_block,
                                             HInstruction* variable,
                                             std::variant<HConstant*, bool> constant) {
  const bool recording_stats = stats_ != nullptr;
  size_t uses_before = 0;
  size_t uses_after = 0;
  if (recording_stats) {
    uses_before = variable->GetUses().SizeSlow();
  }

  HConstant* c = std::holds_alternative<HConstant*>(constant)
                     ? std::get<HConstant*>(constant)
                     : GetGraph()->GetIntConstant(std::get<bool>(constant) ? 1 : 0);
  variable->ReplaceUsesDominatedBy(starting_block->GetFirstInstruction(),
                                   c,
                                   /* strictly_dominated= */ false);

  if (recording_stats) {
    uses_after = variable->GetUses().SizeSlow();
    DCHECK_GE(uses_after, 1u) << "we must at least have the use in the if clause.";
    DCHECK_GE(uses_before, uses_after);
    MaybeRecordStat(stats_, MethodCompilationStat::kPropagatedIfValue, uses_before - uses_after);
    if (uses_before != uses_after) {
      optimizations_occurred_ = true;
    }
  } else {
    // Optimization might not actually happen, but calculating it is slow. Pessimistically assume
    // that optimization happened.
    optimizations_occurred_ = true;
  }
}

void HConstantFoldingVisitor::VisitIf(HIf* inst) {
  // Consistency check: the true and false successors do not dominate each other.
  DCHECK(!inst->IfTrueSuccessor()->Dominates(inst->IfFalseSuccessor()) &&
         !inst->IfFalseSuccessor()->Dominates(inst->IfTrueSuccessor()));

  HInstruction* if_input = inst->InputAt(0);

  // Already a constant.
  if (if_input->IsConstant()) {
    return;
  }

  // if (variable) {
  //   SSA `variable` guaranteed to be true
  // } else {
  //   and here false
  // }
  if (!if_input->GetUses().HasExactlyOneElement()) {
    PropagateValue(inst->IfTrueSuccessor(), if_input, true);
    PropagateValue(inst->IfFalseSuccessor(), if_input, false);
  }

  // If the input is a condition, we can propagate the information of the condition itself.
  // We want either `==` or `!=`, since we cannot make assumptions for other conditions e.g. `>`.
  if (!if_input->IsEqual() && !if_input->IsNotEqual()) {
    return;
  }
  HCondition* condition = if_input->AsCondition();

  HInstruction* left = condition->GetLeft();
  HInstruction* right = condition->GetRight();

  // We want one of them to be a constant and not the other.
  if (left->IsConstant() == right->IsConstant()) {
    return;
  }

  // At this point we have something like:
  // if (variable == constant) {
  //   SSA `variable` guaranteed to be equal to constant here
  // } else {
  //   No guarantees can be made here (usually, see boolean case below).
  // }
  // Similarly with variable != constant, except that we can make guarantees in the else case.

  HConstant* constant = left->IsConstant() ? left->AsConstant() : right->AsConstant();
  HInstruction* variable = left->IsConstant() ? right : left;

  // Don't deal with floats/doubles since they bring a lot of edge cases e.g.
  // if (f == 0.0f) {
  //   // f is not really guaranteed to be 0.0f. It could be -0.0f, for example
  // }
  if (DataType::IsFloatingPointType(variable->GetType())) {
    return;
  }
  DCHECK(!DataType::IsFloatingPointType(constant->GetType()));

  // Sometimes we have an HCompare flowing into an Equals/NonEquals, which can act as a proxy. For
  // example: `Equals(Compare(var, constant), 0)`. This is common for long, float, and double.
  if (variable->IsCompare()) {
    // We only care about equality comparisons so we skip if it is a less or greater comparison.
    if (!constant->IsArithmeticZero()) {
      return;
    }

    // Update left and right to be the ones from the HCompare.
    left = variable->AsCompare()->GetLeft();
    right = variable->AsCompare()->GetRight();

    // Re-check that one of them to be a constant and not the other.
    if (left->IsConstant() == right->IsConstant()) {
      return;
    }

    constant = left->IsConstant() ? left->AsConstant() : right->AsConstant();
    variable = left->IsConstant() ? right : left;

    // Re-check floating point values.
    if (DataType::IsFloatingPointType(variable->GetType())) {
      return;
    }
    DCHECK(!DataType::IsFloatingPointType(constant->GetType()));
  }

  if (!variable->GetUses().HasExactlyOneElement()) {
    // From this block forward we want to replace the SSA value. We use `starting_block`
    // and not the `if` block as we want to update one of the branches but not the other.
    HBasicBlock* starting_block =
        condition->IsEqual() ? inst->IfTrueSuccessor() : inst->IfFalseSuccessor();

    PropagateValue(starting_block, variable, constant);

    // Special case for booleans since they have only two values so we know what to propagate
    // in the other branch. However, sometimes our boolean values are not compared to 0 or 1.
    // In those cases we cannot make an assumption for the `else` branch.
    if (variable->GetType() == DataType::Type::kBool &&
        constant->IsIntConstant() &&
        (constant->AsIntConstant()->IsTrue() || constant->AsIntConstant()->IsFalse())) {
      HBasicBlock* other_starting_block =
          condition->IsEqual() ? inst->IfFalseSuccessor() : inst->IfTrueSuccessor();
      DCHECK_NE(other_starting_block, starting_block);
      PropagateValue(other_starting_block, variable, !constant->AsIntConstant()->IsTrue());
    }
  }
}

void HConstantFoldingVisitor::VisitInvoke(HInvoke* inst) {
  switch (inst->GetIntrinsic()) {
    case Intrinsics::kIntegerReverse:
    case Intrinsics::kLongReverse:
      FoldReverseIntrinsic(inst);
      break;
    case Intrinsics::kIntegerReverseBytes:
    case Intrinsics::kLongReverseBytes:
    case Intrinsics::kShortReverseBytes:
      FoldReverseBytesIntrinsic(inst);
      break;
    case Intrinsics::kIntegerBitCount:
    case Intrinsics::kLongBitCount:
      FoldBitCountIntrinsic(inst);
      break;
    case Intrinsics::kIntegerDivideUnsigned:
    case Intrinsics::kLongDivideUnsigned:
      FoldDivideUnsignedIntrinsic(inst);
      break;
    case Intrinsics::kIntegerHighestOneBit:
    case Intrinsics::kLongHighestOneBit:
      FoldHighestOneBitIntrinsic(inst);
      break;
    case Intrinsics::kIntegerLowestOneBit:
    case Intrinsics::kLongLowestOneBit:
      FoldLowestOneBitIntrinsic(inst);
      break;
    case Intrinsics::kIntegerNumberOfLeadingZeros:
    case Intrinsics::kLongNumberOfLeadingZeros:
      FoldNumberOfLeadingZerosIntrinsic(inst);
      break;
    case Intrinsics::kIntegerNumberOfTrailingZeros:
    case Intrinsics::kLongNumberOfTrailingZeros:
      FoldNumberOfTrailingZerosIntrinsic(inst);
      break;
    default:
      break;
  }
}

void HConstantFoldingVisitor::FoldReverseIntrinsic(HInvoke* inst) {
  DCHECK(inst->GetIntrinsic() == Intrinsics::kIntegerReverse ||
         inst->GetIntrinsic() == Intrinsics::kLongReverse);

  HInstruction* input = inst->InputAt(0);
  if (!input->IsConstant()) {
    return;
  }

  // Integer and Long intrinsics have different return types.
  if (inst->GetIntrinsic() == Intrinsics::kIntegerReverse) {
    DCHECK(input->IsIntConstant());
    inst->ReplaceWith(
        GetGraph()->GetIntConstant(ReverseBits32(input->AsIntConstant()->GetValue())));
  } else {
    DCHECK(input->IsLongConstant());
    inst->ReplaceWith(
        GetGraph()->GetLongConstant(ReverseBits64(input->AsLongConstant()->GetValue())));
  }
  inst->GetBlock()->RemoveInstruction(inst);
  optimizations_occurred_ = true;
}

void HConstantFoldingVisitor::FoldReverseBytesIntrinsic(HInvoke* inst) {
  DCHECK(inst->GetIntrinsic() == Intrinsics::kIntegerReverseBytes ||
         inst->GetIntrinsic() == Intrinsics::kLongReverseBytes ||
         inst->GetIntrinsic() == Intrinsics::kShortReverseBytes);

  HInstruction* input = inst->InputAt(0);
  if (!input->IsConstant()) {
    return;
  }

  // Integer, Long, and Short intrinsics have different return types.
  if (inst->GetIntrinsic() == Intrinsics::kIntegerReverseBytes) {
    DCHECK(input->IsIntConstant());
    inst->ReplaceWith(GetGraph()->GetIntConstant(BSWAP(input->AsIntConstant()->GetValue())));
  } else if (inst->GetIntrinsic() == Intrinsics::kLongReverseBytes) {
    DCHECK(input->IsLongConstant());
    inst->ReplaceWith(GetGraph()->GetLongConstant(BSWAP(input->AsLongConstant()->GetValue())));
  } else {
    DCHECK(input->IsIntConstant());
    inst->ReplaceWith(
        GetGraph()->GetIntConstant(BSWAP<int16_t>(input->AsIntConstant()->GetValue())));
  }
  inst->GetBlock()->RemoveInstruction(inst);
  optimizations_occurred_ = true;
}

void HConstantFoldingVisitor::FoldBitCountIntrinsic(HInvoke* inst) {
  DCHECK(inst->GetIntrinsic() == Intrinsics::kIntegerBitCount ||
         inst->GetIntrinsic() == Intrinsics::kLongBitCount);

  HInstruction* input = inst->InputAt(0);
  if (!input->IsConstant()) {
    return;
  }

  DCHECK_IMPLIES(inst->GetIntrinsic() == Intrinsics::kIntegerBitCount, input->IsIntConstant());
  DCHECK_IMPLIES(inst->GetIntrinsic() == Intrinsics::kLongBitCount, input->IsLongConstant());

  // Note that both the Integer and Long intrinsics return an int as a result.
  int result = inst->GetIntrinsic() == Intrinsics::kIntegerBitCount ?
                   POPCOUNT(input->AsIntConstant()->GetValue()) :
                   POPCOUNT(input->AsLongConstant()->GetValue());
  inst->ReplaceWith(GetGraph()->GetIntConstant(result));
  inst->GetBlock()->RemoveInstruction(inst);
  optimizations_occurred_ = true;
}

void HConstantFoldingVisitor::FoldDivideUnsignedIntrinsic(HInvoke* inst) {
  DCHECK(inst->GetIntrinsic() == Intrinsics::kIntegerDivideUnsigned ||
         inst->GetIntrinsic() == Intrinsics::kLongDivideUnsigned);

  HInstruction* divisor = inst->InputAt(1);
  if (!divisor->IsConstant()) {
    return;
  }
  DCHECK_IMPLIES(inst->GetIntrinsic() == Intrinsics::kIntegerDivideUnsigned,
                 divisor->IsIntConstant());
  DCHECK_IMPLIES(inst->GetIntrinsic() == Intrinsics::kLongDivideUnsigned,
                 divisor->IsLongConstant());
  const bool is_int_intrinsic = inst->GetIntrinsic() == Intrinsics::kIntegerDivideUnsigned;
  if ((is_int_intrinsic && divisor->AsIntConstant()->IsArithmeticZero()) ||
      (!is_int_intrinsic && divisor->AsLongConstant()->IsArithmeticZero())) {
    // We will be throwing, don't constant fold.
    inst->SetAlwaysThrows(true);
    GetGraph()->SetHasAlwaysThrowingInvokes(true);
    return;
  }

  HInstruction* dividend = inst->InputAt(0);
  if (!dividend->IsConstant()) {
    return;
  }
  DCHECK_IMPLIES(inst->GetIntrinsic() == Intrinsics::kIntegerDivideUnsigned,
                 dividend->IsIntConstant());
  DCHECK_IMPLIES(inst->GetIntrinsic() == Intrinsics::kLongDivideUnsigned,
                 dividend->IsLongConstant());

  if (is_int_intrinsic) {
    uint32_t dividend_val =
        dchecked_integral_cast<uint32_t>(dividend->AsIntConstant()->GetValueAsUint64());
    uint32_t divisor_val =
        dchecked_integral_cast<uint32_t>(divisor->AsIntConstant()->GetValueAsUint64());
    inst->ReplaceWith(GetGraph()->GetIntConstant(static_cast<int32_t>(dividend_val / divisor_val)));
  } else {
    uint64_t dividend_val = dividend->AsLongConstant()->GetValueAsUint64();
    uint64_t divisor_val = divisor->AsLongConstant()->GetValueAsUint64();
    inst->ReplaceWith(
        GetGraph()->GetLongConstant(static_cast<int64_t>(dividend_val / divisor_val)));
  }

  inst->GetBlock()->RemoveInstruction(inst);
  optimizations_occurred_ = true;
}

void HConstantFoldingVisitor::FoldHighestOneBitIntrinsic(HInvoke* inst) {
  DCHECK(inst->GetIntrinsic() == Intrinsics::kIntegerHighestOneBit ||
         inst->GetIntrinsic() == Intrinsics::kLongHighestOneBit);

  HInstruction* input = inst->InputAt(0);
  if (!input->IsConstant()) {
    return;
  }

  // Integer and Long intrinsics have different return types.
  if (inst->GetIntrinsic() == Intrinsics::kIntegerHighestOneBit) {
    DCHECK(input->IsIntConstant());
    inst->ReplaceWith(
        GetGraph()->GetIntConstant(HighestOneBitValue(input->AsIntConstant()->GetValue())));
  } else {
    DCHECK(input->IsLongConstant());
    inst->ReplaceWith(
        GetGraph()->GetLongConstant(HighestOneBitValue(input->AsLongConstant()->GetValue())));
  }
  inst->GetBlock()->RemoveInstruction(inst);
  optimizations_occurred_ = true;
}

void HConstantFoldingVisitor::FoldLowestOneBitIntrinsic(HInvoke* inst) {
  DCHECK(inst->GetIntrinsic() == Intrinsics::kIntegerLowestOneBit ||
         inst->GetIntrinsic() == Intrinsics::kLongLowestOneBit);

  HInstruction* input = inst->InputAt(0);
  if (!input->IsConstant()) {
    return;
  }

  // Integer and Long intrinsics have different return types.
  if (inst->GetIntrinsic() == Intrinsics::kIntegerLowestOneBit) {
    DCHECK(input->IsIntConstant());
    inst->ReplaceWith(
        GetGraph()->GetIntConstant(LowestOneBitValue(input->AsIntConstant()->GetValue())));
  } else {
    DCHECK(input->IsLongConstant());
    inst->ReplaceWith(
        GetGraph()->GetLongConstant(LowestOneBitValue(input->AsLongConstant()->GetValue())));
  }
  inst->GetBlock()->RemoveInstruction(inst);
  optimizations_occurred_ = true;
}

void HConstantFoldingVisitor::FoldNumberOfLeadingZerosIntrinsic(HInvoke* inst) {
  DCHECK(inst->GetIntrinsic() == Intrinsics::kIntegerNumberOfLeadingZeros ||
         inst->GetIntrinsic() == Intrinsics::kLongNumberOfLeadingZeros);

  HInstruction* input = inst->InputAt(0);
  if (!input->IsConstant()) {
    return;
  }

  DCHECK_IMPLIES(inst->GetIntrinsic() == Intrinsics::kIntegerNumberOfLeadingZeros,
                 input->IsIntConstant());
  DCHECK_IMPLIES(inst->GetIntrinsic() == Intrinsics::kLongNumberOfLeadingZeros,
                 input->IsLongConstant());

  // Note that both the Integer and Long intrinsics return an int as a result.
  int result = input->IsIntConstant() ? JAVASTYLE_CLZ(input->AsIntConstant()->GetValue()) :
                                        JAVASTYLE_CLZ(input->AsLongConstant()->GetValue());
  inst->ReplaceWith(GetGraph()->GetIntConstant(result));
  inst->GetBlock()->RemoveInstruction(inst);
  optimizations_occurred_ = true;
}

void HConstantFoldingVisitor::FoldNumberOfTrailingZerosIntrinsic(HInvoke* inst) {
  DCHECK(inst->GetIntrinsic() == Intrinsics::kIntegerNumberOfTrailingZeros ||
         inst->GetIntrinsic() == Intrinsics::kLongNumberOfTrailingZeros);

  HInstruction* input = inst->InputAt(0);
  if (!input->IsConstant()) {
    return;
  }

  DCHECK_IMPLIES(inst->GetIntrinsic() == Intrinsics::kIntegerNumberOfTrailingZeros,
                 input->IsIntConstant());
  DCHECK_IMPLIES(inst->GetIntrinsic() == Intrinsics::kLongNumberOfTrailingZeros,
                 input->IsLongConstant());

  // Note that both the Integer and Long intrinsics return an int as a result.
  int result = input->IsIntConstant() ? JAVASTYLE_CTZ(input->AsIntConstant()->GetValue()) :
                                        JAVASTYLE_CTZ(input->AsLongConstant()->GetValue());
  inst->ReplaceWith(GetGraph()->GetIntConstant(result));
  inst->GetBlock()->RemoveInstruction(inst);
  optimizations_occurred_ = true;
}

void HConstantFoldingVisitor::VisitArrayLength(HArrayLength* inst) {
  HInstruction* input = inst->InputAt(0);
  if (input->IsLoadString()) {
    DCHECK(inst->IsStringLength());
    HLoadString* load_string = input->AsLoadString();
    const DexFile& dex_file = load_string->GetDexFile();
    const dex::StringId& string_id = dex_file.GetStringId(load_string->GetStringIndex());
    inst->ReplaceWith(GetGraph()->GetIntConstant(
        dchecked_integral_cast<int32_t>(dex_file.GetStringUtf16Length(string_id))));
    optimizations_occurred_ = true;
  }
}

void HConstantFoldingVisitor::VisitTypeConversion(HTypeConversion* inst) {
  // Constant folding: replace `TypeConversion(a)' with a constant at
  // compile time if `a' is a constant.
  HConstant* constant = inst->TryStaticEvaluation();
  if (constant != nullptr) {
    inst->ReplaceWith(constant);
    inst->GetBlock()->RemoveInstruction(inst);
    optimizations_occurred_ = true;
  } else if (inst->InputAt(0)->IsSelect() && inst->InputAt(0)->HasOnlyOneNonEnvironmentUse()) {
    // Try to replace the select's inputs in Select+TypeConversion. We can do this if both
    // inputs to the select are constants, and this is the only use of the select.
    HSelect* select = inst->InputAt(0)->AsSelect();
    HConstant* false_constant = inst->TryStaticEvaluation(select->GetFalseValue());
    if (false_constant == nullptr) {
      return;
    }
    HConstant* true_constant = inst->TryStaticEvaluation(select->GetTrueValue());
    if (true_constant == nullptr) {
      return;
    }
    DCHECK_EQ(select->InputAt(0), select->GetFalseValue());
    DCHECK_EQ(select->InputAt(1), select->GetTrueValue());
    select->ReplaceInput(false_constant, 0);
    select->ReplaceInput(true_constant, 1);
    select->UpdateType();
    inst->ReplaceWith(select);
    inst->GetBlock()->RemoveInstruction(inst);
    optimizations_occurred_ = true;
  }
}

static bool IsUnmodifiableAndInitialized(ArtField* field, const CompilerOptions& compiler_options)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  DCHECK(field->IsStatic()) << field->PrettyField();
  DCHECK(field->IsFinal()) << field->PrettyField();

  if (field->IsWriteProtected()) {
    return false;
  }

  if (!field->GetDeclaringClass()->IsVisiblyInitialized()) {
    return false;
  }

  if (field->IsMonotonic()) {
    return true;
  }

  uint32_t target_sdk_version = Runtime::Current()->GetTargetSdkVersion();
  // If target_sdk_verson is not yet set then no assumptions about static final fields can be made.
  if (IsSdkVersionUnset(target_sdk_version) ||
      IsSdkVersionSetAndAtMost(target_sdk_version, SdkVersion::kB)) {
    return false;
  }

  ObjPtr<mirror::Class> field_type = field->LookupResolvedType();
  if (field_type == nullptr) {
    return false;
  }

  if (field_type->IsBootStrapClassLoaded()) {
    // These classes are abstract and exact implementations are exposed neither to apps
    // nor in the platform, hence plain comparison instead of subtype checks.
    if (field_type == GetClassRoot(ClassRoot::kJavaLangInvokeMethodHandle) ||
        field_type == GetClassRoot(ClassRoot::kJavaLangInvokeVarHandle) ||
        field_type == WellKnownClasses::ToClass(
            WellKnownClasses::java_util_concurrent_atomic_AIFU) ||
        field_type == WellKnownClasses::ToClass(
            WellKnownClasses::java_util_concurrent_atomic_ALFU) ||
        field_type == WellKnownClasses::ToClass(
            WellKnownClasses::java_util_concurrent_atomic_ARFU) ||
        // Unsafe classes are final and there is only one instance of them.
        field_type == WellKnownClasses::ToClass(
            WellKnownClasses::jdk_internal_misc_Unsafe) ||
        field_type == WellKnownClasses::ToClass(
            WellKnownClasses::sun_misc_Unsafe)) {
      return true;
    }
  }

  // Can't use Runtime::GetSdkVersion in the compiler. See Runtime.sdk_version_ comment.
  uint32_t assumed_sdk_int = compiler_options.GetAssumeValueOptions().SdkInt();
  if (IsSdkVersionUnset(assumed_sdk_int) ||
      IsSdkVersionSetAndAtMost(assumed_sdk_int, SdkVersion::kB)) {
    return false;
  }

  return true;
}

void HConstantFoldingVisitor::VisitStaticFieldGet(HStaticFieldGet* instruction) {
  ArtField* field = instruction->GetFieldInfo().GetField();
  if (!field->IsFinal()) {
    return;
  }

  // Handle kInt32 separately first, as it has a special case for assumed values.
  if (instruction->GetFieldType() == DataType::Type::kInt32) {
    int32_t assumed_value;
    if (compiler_options_.GetAssumeValueOptions().MaybeGetAssumedValue(field, &assumed_value)) {
      instruction->ReplaceWith(GetGraph()->GetIntConstant(assumed_value));
      instruction->GetBlock()->RemoveInstruction(instruction);
      optimizations_occurred_ = true;
      return;
    }
  }

  // JNI can modify static final fields in debuggable runtime.
  if (GetGraph()->IsDebuggable()) {
    return;
  }

  ScopedObjectAccess soa(Thread::Current());

  if (!IsUnmodifiableAndInitialized(field, compiler_options_)) {
    return;
  }

  FoldFieldValue(instruction, field->GetDeclaringClass());
}

void HConstantFoldingVisitor::VisitInstanceFieldGet(HInstanceFieldGet* inst) {
  // IsDebuggable() check is not needed here as monotonic fields can not be modified.
  ArtField* field = inst->GetFieldInfo().GetField();
  if (!field->IsMonotonic()) {
    return;
  }
  DCHECK(field->IsFinal());
  HInstruction* input = inst->InputAt(0u);
  if (input->IsFieldAccess() &&
      input->AsFieldAccess()->HasConstantValue()) {
    Handle<mirror::Object> receiver = input->AsFieldAccess()->GetConstantValue();
    ScopedObjectAccess soa(Thread::Current());
    FoldFieldValue(inst, receiver.Get());
  }
}

void HConstantFoldingVisitor::VisitLoadConstantTableEntry(HLoadConstantTableEntry* inst) {
  if (inst->GetIndex()->IsIntConstant()) {
    size_t index = dchecked_integral_cast<size_t>(inst->GetIndex()->AsIntConstant()->GetValue());
    DCHECK_LT(index, inst->GetNumEntries());
    int64_t value = inst->GetEntry(index);
    HInstruction* constant = nullptr;
    if (inst->GetType() == DataType::Type::kInt64) {
      constant = GetGraph()->GetLongConstant(value);
    } else if (inst->GetType() == DataType::Type::kFloat64) {
      constant = GetGraph()->GetDoubleConstant(bit_cast<double, int64_t>(value));
    } else if (inst->GetType() == DataType::Type::kFloat32) {
      constant = GetGraph()->GetFloatConstant(
          bit_cast<float, uint32_t>(dchecked_integral_cast<uint32_t>(value)));
    } else {
      constant = GetGraph()->GetIntConstant(dchecked_integral_cast<int32_t>(value));
    }
    inst->ReplaceWith(constant);
    inst->GetBlock()->RemoveInstruction(inst);
    optimizations_occurred_ = true;
  }
}

void HConstantFoldingVisitor::FoldFieldValue(HFieldAccess* instruction,
                                             ObjPtr<mirror::Object> receiver) {
  ArtField* field = instruction->GetFieldInfo().GetField();
  DCHECK_IMPLIES(field->IsStatic(), IsUnmodifiableAndInitialized(field, compiler_options_))
      << field->PrettyField();
  DCHECK_IMPLIES(!field->IsStatic(), field->IsMonotonic()) << field->PrettyField();
  DCHECK(!receiver.IsNull());
  HConstant* constant = nullptr;
  switch (instruction->GetFieldType()) {
    case DataType::Type::kBool: {
      uint8_t value = field->GetBoolean(receiver);
      constant = GetGraph()->GetConstant(DataType::Type::kBool, value);
      break;
    }
    case DataType::Type::kInt8: {
      int8_t value = field->GetByte(receiver);
      constant = GetGraph()->GetConstant(DataType::Type::kInt8, value);
      break;
    }
    case DataType::Type::kUint16: {
      uint16_t value = field->GetChar(receiver);
      constant = GetGraph()->GetConstant(DataType::Type::kUint16, value);
      break;
    }
    case DataType::Type::kInt16: {
      int16_t value = field->GetShort(receiver);
      constant = GetGraph()->GetConstant(DataType::Type::kInt16, value);
      break;
    }
    case DataType::Type::kInt32: {
      uint32_t value = field->Get32(receiver);
      constant = GetGraph()->GetIntConstant(value);
      break;
    }
    case DataType::Type::kFloat32: {
      // On x86-32 ArtField::GetFloat might canonicalize NaNs when floats are put and read
      // back from FP register stack.
      uint32_t raw_bits = field->Get32(receiver);
      float value = bit_cast<float, uint32_t>(raw_bits);
      constant = GetGraph()->GetFloatConstant(value);
      break;
    }
    case DataType::Type::kInt64: {
      uint64_t value = field->Get64(receiver);
      constant = GetGraph()->GetLongConstant(static_cast<int64_t>(value));
      break;
    }
    case DataType::Type::kFloat64: {
      // On x86-32 ArtField::GetDouble might canonicalize NaNs when doubles are put and read
      // back from FP register stack.
      uint64_t raw_bits = field->Get64(receiver);
      double value = bit_cast<double, uint64_t>(raw_bits);
      constant = GetGraph()->GetDoubleConstant(value);
      break;
    }
    case DataType::Type::kReference: {
      if (instruction->HasConstantValue()) {
        // Constant folding is run multiple times. Setting value only once, but making sure that
        // its value is still the same.
        DCHECK_EQ(field->GetObject(receiver),
                  instruction->GetConstantValue().Get());
      } else {
        ObjPtr<mirror::Object> obj = field->GetObject(receiver);
        if (obj.IsNull()) {
          constant = GetGraph()->GetNullConstant();
        } else {
          instruction->SetConstantValue(
              GetGraph()->GetHandleCache()->GetHandles()->NewHandle(obj));
          optimizations_occurred_ = true;
        }
      }
      break;
    }
    default:
      break;
  }

  if (constant != nullptr) {
    instruction->ReplaceWith(constant);
    instruction->GetBlock()->RemoveInstruction(instruction);
    optimizations_occurred_ = true;
  }
}

void InstructionWithAbsorbingInputSimplifier::HandleShift(HBinaryOperation* instruction) {
  DCHECK(instruction->IsShl() || instruction->IsShr() || instruction->IsUShr());
  HInstruction* left = instruction->GetLeft();
  if (left->IsConstant() && left->AsConstant()->IsArithmeticZero()) {
    // Replace code looking like
    //    SHL dst, 0, shift_amount
    // with
    //    CONSTANT 0
    SetReplacement(left);
  }
}

static ObjPtr<mirror::Object> ExtractConstant(HInstruction* inst)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  if (inst->IsLoadClass()) {
    Handle<mirror::Class> cls = inst->AsLoadClass()->GetClass();
    if (cls.GetReference() != nullptr) {
      return cls.Get();
    }
  }

  if (inst->IsLoadString()) {
    Handle<mirror::String> string = inst->AsLoadString()->GetString();
    if (string.GetReference() != nullptr) {
      return string.Get();
    }
  }

  if (inst->IsLoadMethodType()) {
    Handle<mirror::MethodType> method_type = inst->AsLoadMethodType()->GetMethodType();
    if (method_type.GetReference() != nullptr) {
      return method_type.Get();
    }
  }

  if (inst->IsFieldAccess()) {
    HFieldAccess* field_access = inst->AsFieldAccess();
    if (field_access->HasConstantValue()) {
      return field_access->GetConstantValue().Get();
    }
  }

  return nullptr;
}

void InstructionWithAbsorbingInputSimplifier::HandleEquality(HBinaryOperation* instruction) {
  DCHECK(instruction->IsEqual() || instruction->IsNotEqual());
  auto reverse_for_not_equal = [instruction](bool result) {
    return result != instruction->IsNotEqual();
  };
  HInstruction* left = instruction->GetLeft();
  HInstruction* right = instruction->GetRight();
  if (left == right && !DataType::IsFloatingPointType(left->GetType())) {
    // Replace code looking like
    //    EQUAL/NOT_EQUAL lhs, lhs
    //    CONSTANT true/false
    // We don't perform this optimizations for FP types since Double.NaN != Double.NaN, which is the
    // opposite value.
    bool result = reverse_for_not_equal(true);
    SetReplacement(GetGraph()->GetConstant(DataType::Type::kBool, result ? 1 : 0));
  } else if ((left->IsNullConstant() && !right->CanBeNull()) ||
             (right->IsNullConstant() && !left->CanBeNull())) {
    // Replace code looking like
    //    EQUAL/NOT_EQUAL lhs, null
    // where lhs cannot be null with
    //    CONSTANT false/true
    bool result = reverse_for_not_equal(false);
    SetReplacement(GetGraph()->GetConstant(DataType::Type::kBool, result ? 1 : 0));
  } else {
    ScopedObjectAccess soa(Thread::Current());
    ObjPtr<mirror::Object> maybe_left_constant = ExtractConstant(left);
    if (maybe_left_constant != nullptr) {
      ObjPtr<mirror::Object> maybe_right_constant = ExtractConstant(right);
      if (maybe_right_constant != nullptr) {
        bool result = reverse_for_not_equal(maybe_left_constant == maybe_right_constant);
        SetReplacement(GetGraph()->GetConstant(DataType::Type::kBool, result ? 1 : 0));
      }
    }
  }
}

void InstructionWithAbsorbingInputSimplifier::HandleUnsignedInequality(HCondition* instruction) {
  DCHECK(instruction->IsAbove() ||
         instruction->IsAboveOrEqual() ||
         instruction->IsBelow() ||
         instruction->IsBelowOrEqual());
  HInstruction* left = instruction->GetLeft();
  HInstruction* right = instruction->GetRight();
  if (left == right) {
    // Replace code looking like
    //    ABOVE/ABOVE_OR_EQUAL/BELOW/BELOW_OR_EQUAL lhs, lhs
    // with
    //    CONSTANT false/true/false/true
    bool result = instruction->IsAboveOrEqual() || instruction->IsBelowOrEqual();
    SetReplacement(GetGraph()->GetConstant(DataType::Type::kBool, result ? 1 : 0));
    return;
  }
  HInstruction* check_for_zero =
      (instruction->IsAbove() || instruction->IsBelowOrEqual()) ? left : right;
  if (check_for_zero->IsConstant() && check_for_zero->AsConstant()->IsArithmeticZero()) {
    // Replace code looking like
    //    ABOVE/BELOW_OR_EQUAL 0, rhs
    // with
    //    CONSTANT false/true
    // and
    //    ABOVE_OR_EQUAL/BELOW lhs, 0
    // with
    //    CONSTANT true/false
    bool result = instruction->IsAboveOrEqual() || instruction->IsBelowOrEqual();
    SetReplacement(GetGraph()->GetConstant(DataType::Type::kBool, result ? 1 : 0));
  }
}

void InstructionWithAbsorbingInputSimplifier::HandleInequality(HCondition* instruction) {
  DCHECK(instruction->IsGreaterThan() ||
         instruction->IsGreaterThanOrEqual() ||
         instruction->IsLessThan() ||
         instruction->IsLessThanOrEqual());
  HInstruction* left = instruction->GetLeft();
  HInstruction* right = instruction->GetRight();
  if (left == right) {
    // Replace code looking like
    //    GREATER_THAN/GREATER_THAN_OR_EQUAL/LESS_THAN/LESS_THAN_OR_EQUAL lhs, lhs
    // with
    //    CONSTANT false/true/false/true
    // For FP types, check if NaN comparison yields the same result.
    auto same_result_for_nan = [instruction]() {
      return (instruction->IsGreaterThan() || instruction->IsLessThanOrEqual())
          ? instruction->IsLtBias()
          : instruction->IsGtBias();
    };
    if ((!DataType::IsFloatingPointType(left->GetType()) || same_result_for_nan())) {
      bool result = instruction->IsGreaterThanOrEqual() || instruction->IsLessThanOrEqual();
      SetReplacement(GetGraph()->GetConstant(DataType::Type::kBool, result ? 1 : 0));
    }
  }
}

void InstructionWithAbsorbingInputSimplifier::VisitAnd(HAnd* instruction) {
  DataType::Type type = instruction->GetType();
  HConstant* input_cst = instruction->GetConstantRight();
  if ((input_cst != nullptr) && input_cst->IsZeroBitPattern()) {
    // Replace code looking like
    //    AND dst, src, 0
    // with
    //    CONSTANT 0
    SetReplacement(input_cst);
  }

  HInstruction* left = instruction->GetLeft();
  HInstruction* right = instruction->GetRight();

  if (left->IsNot() ^ right->IsNot()) {
    // Replace code looking like
    //    NOT notsrc, src
    //    AND dst, notsrc, src
    // with
    //    CONSTANT 0
    HInstruction* hnot = (left->IsNot() ? left : right);
    HInstruction* hother = (left->IsNot() ? right : left);
    HInstruction* src = hnot->AsNot()->GetInput();

    if (src == hother) {
      SetReplacement(GetGraph()->GetConstant(type, 0));
    }
  }
}

void InstructionWithAbsorbingInputSimplifier::VisitCompare(HCompare* instruction) {
  HInstruction* left = instruction->GetLeft();
  HInstruction* right = instruction->GetRight();
  DataType::Type type = left->GetType();

  if (DataType::IsFloatingPointType(type)) {
    // FP comparisons
    HConstant* input_cst = instruction->GetConstantRight();
    if (input_cst != nullptr) {
      if ((input_cst->IsFloatConstant() && input_cst->AsFloatConstant()->IsNaN()) ||
          (input_cst->IsDoubleConstant() && input_cst->AsDoubleConstant()->IsNaN())) {
        // Replace code looking like
        //    CMP{G,L}-{FLOAT,DOUBLE} dst, src, NaN
        // with
        //    CONSTANT +1 (gt bias)
        // or
        //    CONSTANT -1 (lt bias)
        SetReplacement(GetGraph()->GetIntConstant(instruction->IsGtBias() ? 1 : -1));
        return;
      }
    }
    // For left == right on FP, we cannot simplify to 0 because NaN != NaN.
    // The result of cmpg(NaN, NaN) is 1, and cmpl(NaN, NaN) is -1.
  } else {
    // Integral comparisons
    if (left == right) {
      // Replace code looking like
      //    COMPARE lhs, lhs
      // with
      //    CONSTANT 0
      SetReplacement(GetGraph()->GetIntConstant(0));
      return;
    }
  }
}

void InstructionWithAbsorbingInputSimplifier::VisitMul(HMul* instruction) {
  HConstant* input_cst = instruction->GetConstantRight();
  DataType::Type type = instruction->GetType();
  if (DataType::IsIntOrLongType(type) &&
      (input_cst != nullptr) && input_cst->IsArithmeticZero()) {
    // Replace code looking like
    //    MUL dst, src, 0
    // with
    //    CONSTANT 0
    // Integral multiplication by zero always yields zero, but floating-point
    // multiplication by zero does not always do. For example `Infinity * 0.0`
    // should yield a NaN.
    SetReplacement(input_cst);
  }
}

void InstructionWithAbsorbingInputSimplifier::VisitOr(HOr* instruction) {
  HConstant* input_cst = instruction->GetConstantRight();
  if (input_cst != nullptr && Int64FromConstant(input_cst) == -1) {
    // Replace code looking like
    //    OR dst, src, 0xFFF...FF
    // with
    //    CONSTANT 0xFFF...FF
    SetReplacement(input_cst);
    return;
  }

  HInstruction* left = instruction->GetLeft();
  HInstruction* right = instruction->GetRight();
  if (left->IsNot() ^ right->IsNot()) {
    // Replace code looking like
    //    NOT notsrc, src
    //    OR  dst, notsrc, src
    // with
    //    CONSTANT 0xFFF...FF
    HInstruction* hnot = (left->IsNot() ? left : right);
    HInstruction* hother = (left->IsNot() ? right : left);
    HInstruction* src = hnot->AsNot()->GetInput();

    if (src == hother) {
      DCHECK(instruction->GetType() == DataType::Type::kInt32 ||
             instruction->GetType() == DataType::Type::kInt64);
      SetReplacement(GetGraph()->GetConstant(instruction->GetType(), -1));
      return;
    }
  }
}

void InstructionWithAbsorbingInputSimplifier::VisitRem(HRem* instruction) {
  DataType::Type type = instruction->GetType();

  if (!DataType::IsIntegralType(type)) {
    return;
  }

  HInstruction* left = instruction->GetLeft();
  if (left->IsConstant() && left->AsConstant()->IsArithmeticZero()) {
    // Replace code looking like
    //    REM dst, 0, src
    // with
    //    CONSTANT 0
    SetReplacement(left);
    return;
  }

  HInstruction* right = instruction->GetRight();
  if ((right->IsConstant() &&
       (right->AsConstant()->IsOne() || right->AsConstant()->IsMinusOne())) ||
      (left == right)) {
    // Replace code looking like
    //    REM dst, src, 1
    // or
    //    REM dst, src, -1
    // or
    //    REM dst, src, src
    // with
    //    CONSTANT 0
    SetReplacement(GetGraph()->GetConstant(type, 0));
  }
}

void InstructionWithAbsorbingInputSimplifier::VisitSub(HSub* instruction) {
  DataType::Type type = instruction->GetType();

  if (!DataType::IsIntegralType(type)) {
    return;
  }

  // We assume that GVN has run before, so we only perform a pointer
  // comparison.  If for some reason the values are equal but the pointers are
  // different, we are still correct and only miss an optimization
  // opportunity.
  if (instruction->GetLeft() == instruction->GetRight()) {
    // Replace code looking like
    //    SUB dst, src, src
    // with
    //    CONSTANT 0
    // Note that we cannot optimize `x - x` to `0` for floating-point. It does
    // not work when `x` is an infinity.
    SetReplacement(GetGraph()->GetConstant(type, 0));
  }
}

void InstructionWithAbsorbingInputSimplifier::VisitXor(HXor* instruction) {
  HInstruction* left = instruction->GetLeft();
  HInstruction* right = instruction->GetRight();
  if (left == right) {
    // Replace code looking like
    //    XOR dst, src, src
    // with
    //    CONSTANT 0
    DataType::Type type = instruction->GetType();
    SetReplacement(GetGraph()->GetConstant(type, 0));
    return;
  }

  if (left->IsNot() ^ right->IsNot()) {
    // Replace code looking like
    //    NOT notsrc, src
    //    XOR dst, notsrc, src
    // with
    //    CONSTANT 0xFFF...FF
    HInstruction* hnot = (left->IsNot() ? left : right);
    HInstruction* hother = (left->IsNot() ? right : left);
    HInstruction* src = hnot->AsNot()->GetInput();

    if (src == hother) {
      DCHECK(instruction->GetType() == DataType::Type::kInt32 ||
             instruction->GetType() == DataType::Type::kInt64);
      SetReplacement(GetGraph()->GetConstant(instruction->GetType(), -1));
      return;
    }
  }
}

}  // namespace art
