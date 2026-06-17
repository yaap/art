/* Copyright (C) 2018 The Android Open Source Project
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

#include "instruction_simplifier_x86_shared.h"

#include "com_android_art_flags.h"
#include "nodes_x86.h"

namespace art HIDDEN {

bool TryCombineAndNot(HAnd* instruction) {
  DataType::Type type = instruction->GetType();
  if (!DataType::IsIntOrLongType(type)) {
    return false;
  }
  // Replace code looking like
  //    Not tmp, y
  //    And dst, x, tmp
  //  with
  //    AndNot dst, x, y
  HInstruction* left = instruction->GetLeft();
  HInstruction* right = instruction->GetRight();
  // Perform simplication only when either left or right
  // is Not. When both are Not, instruction should be simplified with
  // DeMorgan's Laws.
  if (left->IsNot() ^ right->IsNot()) {
    bool left_is_not = left->IsNot();
    HInstruction* other_ins = (left_is_not ? right : left);
    HNot* not_ins = (left_is_not ? left : right)->AsNot();
    // Only do the simplification if instruction has only one use
    // and thus can be safely removed.
    if (not_ins->HasOnlyOneNonEnvironmentUse()) {
      ArenaAllocator* arena = instruction->GetBlock()->GetGraph()->GetAllocator();
      HX86AndNot* and_not = new (arena) HX86AndNot(type,
                                                   not_ins->GetInput(),
                                                   other_ins,
                                                   instruction->GetDexPc());
      instruction->GetBlock()->ReplaceAndRemoveInstructionWith(instruction, and_not);
      DCHECK(!not_ins->HasUses());
      not_ins->GetBlock()->RemoveInstruction(not_ins);
      return true;
    }
  }
  return false;
}

bool TryGenerateResetLeastSetBit(HAnd* instruction) {
  DataType::Type type = instruction->GetType();
  if (!DataType::IsIntOrLongType(type)) {
    return false;
  }
  // Replace code looking like
  //    Add tmp, x, -1 or Sub tmp, x, 1
  //    And dest x, tmp
  //  with
  //    MaskOrResetLeastSetBit dest, x
  HInstruction* candidate = nullptr;
  HInstruction* other = nullptr;
  HInstruction* left = instruction->GetLeft();
  HInstruction* right = instruction->GetRight();
  if (AreLeastSetBitInputs(left, right)) {
    candidate = left;
    other = right;
  } else if (AreLeastSetBitInputs(right, left)) {
    candidate = right;
    other = left;
  }
  if (candidate != nullptr && candidate->HasOnlyOneNonEnvironmentUse()) {
    ArenaAllocator* arena = instruction->GetBlock()->GetGraph()->GetAllocator();
    HX86MaskOrResetLeastSetBit* lsb = new (arena) HX86MaskOrResetLeastSetBit(
        type, HInstruction::kAnd, other, instruction->GetDexPc());
    instruction->GetBlock()->ReplaceAndRemoveInstructionWith(instruction, lsb);
    DCHECK(!candidate->HasUses());
    candidate->GetBlock()->RemoveInstruction(candidate);
    return true;
  }
  return false;
}

bool TryGenerateMaskUptoLeastSetBit(HXor* instruction) {
  DataType::Type type = instruction->GetType();
  if (!DataType::IsIntOrLongType(type)) {
    return false;
  }
  // Replace code looking like
  //    Add tmp, x, -1 or Sub tmp, x, 1
  //    Xor dest x, tmp
  //  with
  //    MaskOrResetLeastSetBit dest, x
  HInstruction* left = instruction->GetLeft();
  HInstruction* right = instruction->GetRight();
  HInstruction* other = nullptr;
  HInstruction* candidate = nullptr;
  if (AreLeastSetBitInputs(left, right)) {
    candidate = left;
    other = right;
  } else if (AreLeastSetBitInputs(right, left)) {
    candidate = right;
    other = left;
  }
  if (candidate != nullptr && candidate->HasOnlyOneNonEnvironmentUse()) {
    ArenaAllocator* arena = instruction->GetBlock()->GetGraph()->GetAllocator();
    HX86MaskOrResetLeastSetBit* lsb = new (arena) HX86MaskOrResetLeastSetBit(
        type, HInstruction::kXor, other, instruction->GetDexPc());
    instruction->GetBlock()->ReplaceAndRemoveInstructionWith(instruction, lsb);
    DCHECK(!candidate->HasUses());
    candidate->GetBlock()->RemoveInstruction(candidate);
    return true;
  }
  return false;
}

bool IsLeaIndexShift(HInstruction* instruction, HInstruction** index, uint32_t* shift) {
  if (!instruction->IsShl() || !instruction->HasOnlyOneNonEnvironmentUse()) {
    return false;
  }
  HShl* shl = instruction->AsShl();
  DCHECK_EQ(shl->GetRight()->IsConstant(), shl->GetRight()->IsIntConstant());
  if (!shl->GetRight()->IsIntConstant()) {
    return false;
  }
  int32_t shift_value = shl->GetRight()->AsIntConstant()->GetValue();
  if (shift_value < 1 || shift_value > 3) {
    return false;
  }
  *index = shl->GetLeft();
  *shift = shift_value;
  return true;
}

bool IsLeaDisplacement(HInstruction* cst, int32_t sign, int32_t* value) {
  DCHECK(cst->IsIntConstant() || cst->IsLongConstant());
  DCHECK(sign == 1 || sign == -1);
  if (cst->IsIntConstant()) {
    *value = sign * cst->AsIntConstant()->GetValue();
    return true;
  } else if (IsInt<32>(sign * cst->AsLongConstant()->GetValue())) {
    *value = dchecked_integral_cast<int32_t>(sign * cst->AsLongConstant()->GetValue());
    return true;
  } else {
    return false;
  }
}

std::tuple<HInstruction*, int32_t, HInstruction*> GetLeaBaseAndDisplacement(
    HInstruction* instruction) {
  HInstruction* base = nullptr;
  int32_t disp = 0;
  HInstruction* dead = nullptr;
  if (instruction->IsConstant() && IsLeaDisplacement(instruction, /*sign=*/ 1, &disp)) {
    // `disp` has been set and `base` shall remain null.
  } else {
    base = instruction;
    // We can embed `HAdd` or `HSub` in the LEA under the right conditions.
    if ((instruction->IsAdd() || instruction->IsSub()) &&
        instruction->HasOnlyOneNonEnvironmentUse()) {
      int32_t sign = instruction->IsAdd() ? 1 : -1;
      HBinaryOperation* binop = instruction->AsBinaryOperation();
      HInstruction* left = binop->GetLeft();
      HInstruction* right = binop->GetRight();
      if (right->IsConstant() && IsLeaDisplacement(right, sign, &disp)) {
        base = left;
        dead = instruction;
      } else if (instruction->IsAdd() &&
                 left->IsConstant() &&
                 IsLeaDisplacement(left, /*sign=*/ 1, &disp)) {
        base = right;
        dead = instruction;
      }
    }
  }
  return {base, disp, dead};
}

bool IsAddForZeroShiftLea(
    HAdd* add, HInstruction* other, HInstruction** index, HInstruction** base, int32_t* disp) {
  if (!add->HasOnlyOneNonEnvironmentUse()) {
    return false;
  }
  HInstruction* inputs[3] = { add->GetLeft(), add->GetRight(), other };
  HInstruction** inputs_end = inputs + std::size(inputs);
  // Check that there is exactly one constant among `inputs`.
  auto is_constant = [](HInstruction* instruction) { return instruction->IsConstant(); };
  auto cst_it = std::find_if(inputs, inputs_end, is_constant);
  if (cst_it == inputs_end ||
      std::find_if(std::next(cst_it), inputs_end, is_constant) != inputs_end) {
    return false;
  }
  HInstruction* cst = *cst_it;
  // Check if the constant can be encoded in LEA.
  if (!IsLeaDisplacement(cst, /*sign=*/ 1, disp)) {
    return false;
  }
  // It does not matter which non-constant instruction is index and which is base. Use the first
  // non-constant `add` input as base, similar to patterns where `other` is `Shl` by 1-3.
  *base = (inputs[0] != cst) ? inputs[0] : inputs[1];
  *index = (inputs[2] != cst) ? inputs[2] : inputs[1];
  return true;
}

bool IsSubForZeroShiftLea(
    HSub* sub, HInstruction* other, HInstruction** index, HInstruction** base, int32_t* disp) {
  if (!sub->HasOnlyOneNonEnvironmentUse()) {
    return false;
  }
  // Check that only the subtracted value is constant.
  if (other->IsConstant() ||
      sub->GetLeft()->IsConstant() ||
      !sub->GetRight()->IsConstant()) {
    return false;
  }
  // Check if the constant can be encoded in LEA.
  if (!IsLeaDisplacement(sub->GetRight(), /*sign=*/ -1, disp)) {
    return false;
  }
  // It does not matter which non-constant instruction is index and which is base.
  // Use the `sub`'s left input as base, similar to patterns where `other` is `Shl` by 1-3.
  *index = other;
  *base = sub->GetLeft();
  return true;
}

bool TryLoadEffectiveAddressSimplification(HBinaryOperation* instruction) {
  DCHECK(instruction->IsAdd() || instruction->IsSub());
  DCHECK(DataType::IsIntOrLongType(instruction->GetType()));
  if (!com::android::art::flags::x86_lea_optimizations()) {
    return false;
  }
  HInstruction* left = instruction->GetLeft();
  HInstruction* right = instruction->GetRight();
  HInstruction* index = nullptr;
  uint32_t shift = 0u;
  HInstruction* base = nullptr;
  int32_t disp = 0;
  HInstruction* dead = nullptr;
  HInstruction* dead2 = nullptr;
  if (instruction->IsAdd()) {
    if (IsLeaIndexShift(left, &index, &shift)) {
      dead = left;
      std::tie(base, disp, dead2) = GetLeaBaseAndDisplacement(right);
    } else if (IsLeaIndexShift(right, &index, &shift)) {
      dead = right;
      std::tie(base, disp, dead2) = GetLeaBaseAndDisplacement(left);
    } else if (left->IsAdd() && IsAddForZeroShiftLea(left->AsAdd(), right, &index, &base, &disp)) {
      dead = left;
      DCHECK_EQ(shift, 0u);
    } else if (right->IsAdd() && IsAddForZeroShiftLea(right->AsAdd(), left, &index, &base, &disp)) {
      dead = right;
      DCHECK_EQ(shift, 0u);
    } else if (left->IsSub() && IsSubForZeroShiftLea(left->AsSub(), right, &index, &base, &disp)) {
      dead = left;
      DCHECK_EQ(shift, 0u);
    } else if (right->IsSub() && IsSubForZeroShiftLea(right->AsSub(), left, &index, &base, &disp)) {
      dead = right;
      DCHECK_EQ(shift, 0u);
    }
  } else if (right->IsConstant()) {  // For `HSub`, we simplify only with a constant `right`.
    DCHECK(instruction->IsSub());
    if (IsLeaIndexShift(left, &index, &shift)) {
      dead = left;
      if (IsLeaDisplacement(right, /*sign=*/ -1, &disp)) {
        // `disp` has been set and `base` shall remain null.
      } else {
        // Use the negated constant as a base. Keep zero `disp`.
        DCHECK(right->IsLongConstant()) << right->DebugName();
        int64_t displacement = -right->AsLongConstant()->GetValue();
        base = instruction->GetBlock()->GetGraph()->GetLongConstant(displacement);
      }
    } else if (left->IsAdd() &&
               left->HasOnlyOneNonEnvironmentUse() &&
               !left->AsAdd()->GetLeft()->IsConstant() &&
               !left->AsAdd()->GetRight()->IsConstant() &&
               IsLeaDisplacement(right, /*sign=*/ -1, &disp)) {
      index = left->AsAdd()->GetLeft();
      base = left->AsAdd()->GetRight();
      dead = left;
    }
  }
  if (index == nullptr) {
    return false;
  }
  ArenaAllocator* arena = instruction->GetBlock()->GetGraph()->GetAllocator();
  HX86LoadEffectiveAddress* lea = new (arena) HX86LoadEffectiveAddress(
      instruction->GetType(), index, base, shift, disp, instruction->GetDexPc());
  instruction->GetBlock()->ReplaceAndRemoveInstructionWith(instruction, lea);
  DCHECK(!dead->HasUses());
  dead->GetBlock()->RemoveInstruction(dead);
  if (dead2 != nullptr) {
    DCHECK(!dead2->HasUses());
    dead2->GetBlock()->RemoveInstruction(dead2);
  }
  return true;
}

bool AreLeastSetBitInputs(HInstruction* to_test, HInstruction* other) {
  if (to_test->IsAdd()) {
    HAdd* add = to_test->AsAdd();
    HConstant* cst = add->GetConstantRight();
    return cst != nullptr && cst->IsMinusOne() && other == add->GetLeastConstantLeft();
  }
  if (to_test->IsSub()) {
    HSub* sub = to_test->AsSub();
    HConstant* cst = sub->GetConstantRight();
    return cst != nullptr && cst->IsOne() && other == sub->GetLeastConstantLeft();
  }
  return false;
}

}  // namespace art
