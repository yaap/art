/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include "instruction_simplifier_riscv64.h"

namespace art HIDDEN {

namespace riscv64 {

class InstructionSimplifierRiscv64Visitor final : public HGraphVisitor {
 public:
  InstructionSimplifierRiscv64Visitor(HGraph* graph, OptimizingCompilerStats* stats)
      : HGraphVisitor(graph), stats_(stats) {}

 private:
  void RecordSimplification() {
    MaybeRecordStat(stats_, MethodCompilationStat::kInstructionSimplificationsArch);
  }

  void VisitBasicBlock(HBasicBlock* block) override {
    for (HInstructionIterator it(block->GetInstructions()); !it.Done(); it.Advance()) {
      HInstruction* instruction = it.Current();
      if (instruction->IsInBlock()) {
        instruction->Accept(this);
      }
    }
  }

  bool TryReplaceShiftAddWithOneInstruction(HShl* shl, HAdd* add) {
    // There is no reason to replace Int32 Shl+Add with ShiftAdd because of
    // additional sign-extension required.
    if (shl->GetType() != DataType::Type::kInt64) {
      return false;
    }

    if (!shl->GetRight()->IsIntConstant()) {
      return false;
    }

    const int32_t distance = shl->GetRight()->AsIntConstant()->GetValue();
    if (distance != 1 && distance != 2 && distance != 3) {
      return false;
    }

    if (!shl->HasOnlyOneNonEnvironmentUse()) {
      return false;
    }

    auto* const add_other_input = add->GetLeft() == shl ? add->GetRight() : add->GetLeft();
    auto* const shift_add = new (GetGraph()->GetAllocator())
        HRiscv64ShiftAdd(shl->GetLeft(), add_other_input, distance);

    DCHECK_EQ(add->GetType(), DataType::Type::kInt64)
        << "Riscv64ShiftAdd replacement should have the same 64 bit type";
    add->GetBlock()->ReplaceAndRemoveInstructionWith(add, shift_add);
    shl->GetBlock()->RemoveInstruction(shl);

    return true;
  }

  // Replace code looking like
  //    SHL tmp, a, 1 or 2 or 3
  //    ADD dst, tmp, b
  // with
  //    Riscv64ShiftAdd dst, a, b
  void VisitAdd(HAdd* add) override {
    auto* const left = add->GetLeft();
    auto* const right = add->GetRight();
    if (left->IsShl() && TryReplaceShiftAddWithOneInstruction(left->AsShl(), add)) {
      return;
    } else if (right->IsShl() && TryReplaceShiftAddWithOneInstruction(right->AsShl(), add)) {
      return;
    }
  }

  OptimizingCompilerStats* stats_ = nullptr;
};

bool InstructionSimplifierRiscv64::Run() {
  auto visitor = InstructionSimplifierRiscv64Visitor(graph_, stats_);
  visitor.VisitReversePostOrder();
  return true;
}

}  // namespace riscv64
}  // namespace art
