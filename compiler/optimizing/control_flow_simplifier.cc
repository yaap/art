/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "control_flow_simplifier.h"

#include "com_android_art_rw_flags.h"
#include "common_dominator.h"
#include "optimizing/nodes.h"
#include "reference_type_propagation.h"

namespace art HIDDEN {

static constexpr size_t kMaxInstructionsInBranch = 1u;

HControlFlowSimplifier::HControlFlowSimplifier(HGraph* graph,
                                               OptimizingCompilerStats* stats,
                                               const char* name)
    : HOptimization(graph, name, stats) {
}

bool HControlFlowSimplifier::ReturnSinking() {
  HBasicBlock* exit = graph_->GetExitBlock();
  if (exit == nullptr) {
    return false;  // No exit block, only infinite loop(s).
  }

  size_t number_of_returns = 0u;
  bool saw_return = false;
  for (HBasicBlock* pred : exit->GetPredecessors()) {
    // TODO(solanes): We might have Return/ReturnVoid->TryBoundary->Exit. We can theoretically
    // handle them and move them out of the TryBoundary. However, it is a border case and it adds
    // codebase complexity.
    if (pred->GetLastInstruction()->IsReturn() || pred->GetLastInstruction()->IsReturnVoid()) {
      DCHECK(!pred->IsInLoop());  // Return can be in a loop only if it's in a try-block.
      saw_return |= pred->GetLastInstruction()->IsReturn();
      ++number_of_returns;
    }
  }

  if (number_of_returns < 2) {
    // Nothing to do.
    return false;
  }

  // `new_block` will coalesce the Return instructions into Phi+Return,
  // or the ReturnVoid instructions into a ReturnVoid.
  HBasicBlock* new_block = nullptr;
  HInstruction::InstructionKind return_kind =
      saw_return ? HInstruction::kReturn : HInstruction::kReturnVoid;
  HReturn* first_return = nullptr;  // The first `HReturn` shall be reused by the new block.
  HPhi* new_phi = nullptr;
  size_t new_phi_input_index = 0u;
  ArrayRef<HBasicBlock* const> rpo(graph_->GetReversePostOrder());
  size_t rpo_insert_index = 0u;
  for (size_t rpo_index : Range(rpo.size())) {
    HBasicBlock* pred = rpo[rpo_index];
    HInstruction* last_inst = pred->GetLastInstruction();
    DCHECK_IMPLIES(last_inst == nullptr, pred == exit);
    if (last_inst == nullptr ||                 // Exit block?
        last_inst->GetKind() != return_kind ||  // Not `HReturn`/`HReturnVoid`?
        pred->GetSingleSuccessor() != exit) {   // Leading to try boundary instead of `exit`?
      continue;
    }
    if (new_block == nullptr) {
      new_block = pred->SplitBefore(last_inst);
      if (saw_return) {
        first_return = last_inst->AsReturn();
        // Create the `new_phi`. We do it here since we need to know the type to assign to it.
        new_phi = new (graph_->GetAllocator()) HPhi(
            graph_->GetAllocator(),
            kNoRegNumber,
            /*number_of_inputs=*/ number_of_returns,
            DataType::Kind(first_return->InputAt(0)->GetType()));
        new_phi->SetRawInputAt(new_phi_input_index, first_return->InputAt(0));
        ++new_phi_input_index;
      }
    } else {
      pred->ReplaceSuccessor(exit, new_block);
      if (saw_return) {
        DCHECK(new_phi != nullptr);
        new_phi->SetRawInputAt(new_phi_input_index, last_inst->AsReturn()->InputAt(0));
        ++new_phi_input_index;
      }
      pred->ReplaceAndRemoveInstructionWith(
          last_inst, new (graph_->GetAllocator()) HGoto(last_inst->GetDexPc()));
    }
    rpo_insert_index = rpo_index + 1u;
  }
  if (saw_return) {
    DCHECK_EQ(number_of_returns, new_phi_input_index);
    DCHECK(new_phi != nullptr);
    new_block->AddPhi(new_phi);
    DCHECK(first_return != nullptr);
    first_return->ReplaceInput(new_phi, 0);
  }

  graph_->reverse_post_order_.insert(graph_->reverse_post_order_.begin() + rpo_insert_index,
                                     new_block);
  if (exit->GetPredecessors().size() == 1u) {
    HBasicBlock* dominator = exit->GetDominator();
    dominator->RemoveDominatedBlock(exit);
    new_block->SetDominator(dominator);
    dominator->AddDominatedBlock(new_block);
    exit->SetDominator(new_block);
    new_block->AddDominatedBlock(exit);
  } else {
    ArrayRef<HBasicBlock* const> predecessors(new_block->GetPredecessors());
    CommonDominator dominator(predecessors[0]);
    for (HBasicBlock* pred : predecessors.SubArray(/*pos=*/ 1u)) {
      dominator.Update(pred);
    }
    new_block->SetDominator(dominator.Get());
    dominator.Get()->AddDominatedBlock(new_block);
  }
  return true;
}

bool HControlFlowSimplifier::TrySimplifyPackedSwitch(HBasicBlock* block,
                                                     ScopedArenaAllocator* allocator) {
  DCHECK(!block->GetInstructions().IsEmpty());
  DCHECK(block->GetLastInstruction()->IsPackedSwitch());
  HPackedSwitch* packed_switch = block->GetLastInstruction()->AsPackedSwitch();

  // Defensively check if we have at least 4 entries. This should be true as long as we build
  // a decision tree for smaller switches, see `DexSwitchTable::ShouldBuildDecisionTree()`.
  static constexpr size_t kMinEntries = 4u;
  size_t num_entries = packed_switch->GetNumEntries();
  if (num_entries < kMinEntries) {
    return false;
  }

  // Check if all non-default successors are single-goto blocks merging at the same block.
  ArrayRef<HBasicBlock* const> successors(block->GetSuccessors());
  DCHECK_EQ(num_entries + 1u, successors.size());
  if (!successors[0]->IsSingleGoto()) {
    return false;
  }
  HBasicBlock* merge = successors[0]->GetSingleSuccessor();
  for (size_t i : Range(1, num_entries)) {
    if (!successors[i]->IsSingleGoto() || successors[i]->GetSingleSuccessor() != merge) {
      return false;
    }
  }

  // Check if the `merge` has at most one Phi.
  HPhi* phi = nullptr;
  if (merge->GetPhis().IsEmpty()) {
    // We won't even need a constant table load to simplify this.
  } else if (merge->HasSinglePhi()) {
    phi = merge->GetFirstPhi()->AsPhi();
    DCHECK(phi != nullptr);
  } else {
    return false;  // Do not simplify when there are two or more phis.
  }

  auto get_value = [](HInstruction* input, int64_t* value) {
    if (input->IsIntConstant()) {
      *value = input->AsIntConstant()->GetValue();
    } else if (input->IsLongConstant()) {
      *value = input->AsLongConstant()->GetValue();
    } else if (input->IsFloatConstant()) {
      *value = input->AsFloatConstant()->GetValueAsUint64();
    } else if (input->IsDoubleConstant()) {
      *value = input->AsDoubleConstant()->GetValueAsUint64();
    } else {
      return false;
    }
    return true;
  };

  // Check if the default case can be included in the simplified pattern.
  HBasicBlock* default_block = successors[num_entries];
  int64_t default_value = 0;
  bool with_default =
      default_block->IsSingleGoto() &&
      default_block->GetSingleSuccessor() == merge &&
      (phi == nullptr || get_value(phi->InputAt(merge->GetPredecessorIndexOf(default_block)),
                                   &default_value));
  bool with_default_index_0 = with_default && packed_switch->GetStartValue() == 1;

  ArrayRef<int64_t> entries;
  if (phi != nullptr) {
    // Check if all the phi's non-default inputs are constants and collect the values.
    size_t size = num_entries + (with_default ? 1u : 0u);
    entries = ArrayRef<int64_t>(
        allocator->AllocArray<int64_t>(size, kArenaAllocControlFlowSimplifier), size);
    ArrayRef<int64_t> non_default_entries =
        entries.SubArray(with_default_index_0 ? 1u : 0u, num_entries);
    for (size_t i : Range(num_entries)) {
      size_t predecessor_index = merge->GetPredecessorIndexOf(successors[i]);
      if (!get_value(phi->InputAt(predecessor_index), &non_default_entries[i])) {
        return false;
      }
    }
    if (with_default) {
      entries[with_default_index_0 ? 0u : num_entries] = default_value;
    }
  }

  // Determine table type.
  DataType::Type table_type = (phi != nullptr) ? phi->GetType() : DataType::Type::kVoid;
  if (DataType::Kind(table_type) == DataType::Type::kInt32) {
    // Try to narrow down the type to save space.
    auto [min_it, max_it] = std::minmax_element(entries.begin(), entries.end());
    DCHECK_GE(*min_it, std::numeric_limits<int32_t>::min());
    DCHECK_LE(*max_it, std::numeric_limits<int32_t>::max());
    bool has_negative = (*min_it < 0);
    int max_value_to_encode = has_negative
        ? dchecked_integral_cast<int32_t>(std::max(*max_it, -1 - *min_it))
        : dchecked_integral_cast<int32_t>(*max_it);
    if (!has_negative && max_value_to_encode <= 1) {
      table_type = DataType::Type::kBool;
    } else if (max_value_to_encode <= std::numeric_limits<int8_t>::max()) {
      table_type = DataType::Type::kInt8;
    } else if (!has_negative && max_value_to_encode <= std::numeric_limits<uint8_t>::max()) {
      table_type = DataType::Type::kUint8;
    } else if (max_value_to_encode <= std::numeric_limits<int16_t>::max()) {
      table_type = DataType::Type::kInt16;
    } else if (!has_negative && max_value_to_encode <= std::numeric_limits<uint16_t>::max()) {
      table_type = DataType::Type::kUint16;
    } else {
      DCHECK_EQ(table_type, DataType::Type::kInt32);  // Keep the `kInt32`.
    }
  }

  // Prepare the index calculation.
  uint32_t dex_pc = packed_switch->GetDexPc();
  HInstruction* index = packed_switch->InputAt(0);
  if (packed_switch->GetStartValue() != 0 && !with_default_index_0) {
    HInstruction* addend = graph_->GetIntConstant(-packed_switch->GetStartValue());
    index = new (graph_->GetAllocator()) HAdd(DataType::Type::kInt32, index, addend, dex_pc);
    block->InsertInstructionBefore(index, packed_switch);
  }
  // Prepare the comparison with the upper bound.
  HInstruction* bound = graph_->GetIntConstant(num_entries + (with_default_index_0 ? 1 : 0));
  HBelow* below = new (graph_->GetAllocator()) HBelow(index, bound, dex_pc);
  block->InsertInstructionBefore(below, packed_switch);
  if (with_default) {
    // Insert `HSelect` to clamp the index.
    HInstruction* false_value = graph_->GetIntConstant(with_default_index_0 ? 0 : num_entries);
    index = new (graph_->GetAllocator()) HSelect(below, index, false_value, dex_pc);
    block->InsertInstructionBefore(index, packed_switch);
  }
  if (phi != nullptr) {
    // Insert constant table load and update the phi input we intend to keep.
    HLoadConstantTableEntry* load = new (graph_->GetAllocator()) HLoadConstantTableEntry(
        table_type, index, ArrayRef<const int64_t>(entries), graph_->GetAllocator(), dex_pc);
    if (with_default) {
      block->InsertInstructionBefore(load, packed_switch);
    } else {
      successors[0]->InsertInstructionBefore(load, successors[0]->GetFirstInstruction());
    }
    phi->ReplaceInput(load, merge->GetPredecessorIndexOf(successors[0]));
  }
  // Remove the default successor if `with_default` and all non-default successors,
  // except the first. This also removes `phi` inputs and replaces the Phi with the
  // `load` if it's the only remaining input.
  // Note: Stop using `successors` as the underlying vector is being modified.
  for (size_t i : Range(with_default ? 0u : 1u, num_entries)) {
    block->GetSuccessors()[num_entries - i]->DisconnectAndDelete();
  }
  // If the merge block has only one predecessor now, update domination info and merge.
  if (merge->GetPredecessors().size() == 1u) {
    HBasicBlock* remaining_block = block->GetSuccessors()[0];
    DCHECK(remaining_block == merge->GetSinglePredecessor());
    DCHECK(merge->GetDominator() == block);
    block->RemoveDominatedBlock(merge);
    merge->SetDominator(remaining_block);
    remaining_block->AddDominatedBlock(merge);
    remaining_block->MergeWith(merge);
  }
  if (with_default) {
    // Merge `block` with its remaining successor.
    DCHECK(block->GetLastInstruction()->IsGoto());  // Updated by last `DisconnectAndDelete()`.
    block->MergeWith(block->GetSingleSuccessor());
  } else {
    // Replace the switch with `HBelow` and `HIf`.
    DCHECK_EQ(2u, block->GetSuccessors().size());
    HIf* if_ = new (graph_->GetAllocator()) HIf(below, dex_pc);
    block->ReplaceAndRemoveInstructionWith(packed_switch, if_);
  }

  MaybeRecordStat(stats_, MethodCompilationStat::kControlFlowSwitchSimplified);
  return true;
}

// Returns true if `block` has only one predecessor, ends with a Goto
// or a Return and contains at most `kMaxInstructionsInBranch` other
// movable instruction with no side-effects.
static bool IsSimpleBlock(HBasicBlock* block) {
  if (block->GetPredecessors().size() != 1u) {
    return false;
  }
  DCHECK(block->GetPhis().IsEmpty());

  size_t num_instructions = 0u;
  for (HInstructionIteratorPrefetchNext it(block->GetInstructions()); !it.Done(); it.Advance()) {
    HInstruction* instruction = it.Current();
    if (instruction->IsControlFlow()) {
      return instruction->IsGoto() || instruction->IsReturn();
    } else if (instruction->CanBeMoved() &&
               !instruction->HasSideEffects() &&
               !instruction->CanThrow()) {
      if (instruction->IsSelect() && instruction->AsSelect()->GetCondition()->GetBlock() == block) {
        // Count one HCondition and HSelect in the same block as a single instruction.
        // This enables finding nested selects.
        continue;
      } else if (++num_instructions > kMaxInstructionsInBranch) {
        return false;  // bail as soon as we exceed number of allowed instructions
      }
    } else {
      return false;
    }
  }

  LOG(FATAL) << "Unreachable";
  UNREACHABLE();
}

// Returns true if 'block1' and 'block2' are empty and merge into the
// same single successor.
static bool BlocksMergeTogether(HBasicBlock* block1, HBasicBlock* block2) {
  return block1->GetSingleSuccessor() == block2->GetSingleSuccessor();
}

// Search `block` for phis that have different inputs at `index1` and `index2`.
// If none is found, returns `{true, nullptr}`.
// If exactly one such `phi` is found, returns `{true, phi}`.
// Otherwise (if more than one such phi is found), returns `{false, nullptr}`.
static std::pair<bool, HPhi*> HasAtMostOnePhiWithDifferentInputs(HBasicBlock* block,
                                                                 size_t index1,
                                                                 size_t index2) {
  DCHECK_NE(index1, index2);

  HPhi* select_phi = nullptr;
  for (HInstructionIteratorPrefetchNext it(block->GetPhis()); !it.Done(); it.Advance()) {
    HPhi* phi = it.Current()->AsPhi();
    auto&& inputs = phi->GetInputs();
    if (inputs[index1] == inputs[index2]) {
      continue;
    }
    if (select_phi == nullptr) {
      // First phi found.
      select_phi = phi;
    } else {
      // More than one phi found, return null.
      return {false, nullptr};
    }
  }
  return {true, select_phi};
}

bool HControlFlowSimplifier::TryGenerateSelectSimpleDiamondPattern(
    HBasicBlock* block, ScopedArenaSafeMap<HInstruction*, HSelect*>* cache) {
  DCHECK(block->GetLastInstruction()->IsIf());
  HIf* if_instruction = block->GetLastInstruction()->AsIf();
  HBasicBlock* true_block = if_instruction->IfTrueSuccessor();
  HBasicBlock* false_block = if_instruction->IfFalseSuccessor();
  DCHECK_NE(true_block, false_block);

  if (!IsSimpleBlock(true_block) ||
      !IsSimpleBlock(false_block) ||
      !BlocksMergeTogether(true_block, false_block)) {
    return false;
  }
  HBasicBlock* merge_block = true_block->GetSingleSuccessor();

  // If the branches are not empty, move instructions in front of the If.
  // TODO(dbrazdil): This puts an instruction between If and its condition.
  //                 Implement moving of conditions to first users if possible.
  while (!true_block->IsSingleGoto() && !true_block->IsSingleReturn()) {
    HInstruction* instr = true_block->GetFirstInstruction();
    DCHECK(!instr->CanThrow());
    instr->MoveBefore(if_instruction);
  }
  while (!false_block->IsSingleGoto() && !false_block->IsSingleReturn()) {
    HInstruction* instr = false_block->GetFirstInstruction();
    DCHECK(!instr->CanThrow());
    instr->MoveBefore(if_instruction);
  }
  DCHECK(true_block->IsSingleGoto() || true_block->IsSingleReturn());
  DCHECK(false_block->IsSingleGoto() || false_block->IsSingleReturn());

  // Find the resulting true/false values.
  size_t predecessor_index_true = merge_block->GetPredecessorIndexOf(true_block);
  size_t predecessor_index_false = merge_block->GetPredecessorIndexOf(false_block);
  DCHECK_NE(predecessor_index_true, predecessor_index_false);

  bool both_successors_return = true_block->IsSingleReturn() && false_block->IsSingleReturn();
  // TODO(solanes): Extend to support multiple phis? e.g.
  //   int a, b;
  //   if (bool) {
  //     a = 0; b = 1;
  //   } else {
  //     a = 1; b = 2;
  //   }
  //   // use a and b
  bool at_most_one_phi_with_different_inputs = false;
  HPhi* phi = nullptr;
  HInstruction* true_value = nullptr;
  HInstruction* false_value = nullptr;
  if (both_successors_return) {
    // Note: This can create a select with the same then-value and else-value.
    true_value = true_block->GetFirstInstruction()->InputAt(0);
    false_value = false_block->GetFirstInstruction()->InputAt(0);
  } else {
    std::tie(at_most_one_phi_with_different_inputs, phi) = HasAtMostOnePhiWithDifferentInputs(
        merge_block, predecessor_index_true, predecessor_index_false);
    if (!at_most_one_phi_with_different_inputs) {
      return false;
    }
    if (phi != nullptr) {
      true_value = phi->InputAt(predecessor_index_true);
      false_value = phi->InputAt(predecessor_index_false);
    }  // else we don't need to create a `HSelect` at all.
  }
  DCHECK(both_successors_return || at_most_one_phi_with_different_inputs);

  // Create the Select instruction and insert it in front of the If.
  HInstruction* condition = if_instruction->InputAt(0);
  HSelect* select = nullptr;
  if (both_successors_return || phi != nullptr) {
    select = new (graph_->GetAllocator()) HSelect(condition,
                                                  true_value,
                                                  false_value,
                                                  if_instruction->GetDexPc());
    block->InsertInstructionBefore(select, if_instruction);
    if (both_successors_return) {
      if (true_value->GetType() == DataType::Type::kReference) {
        DCHECK(false_value->GetType() == DataType::Type::kReference);
        ReferenceTypePropagation::FixUpSelectType(select, graph_->GetHandleCache());
      }
      false_block->GetFirstInstruction()->ReplaceInput(select, 0);
    } else {
      if (phi->GetType() == DataType::Type::kReference) {
        select->SetReferenceTypeInfoIfValid(phi->GetReferenceTypeInfo());
      }
      phi->ReplaceInput(select, predecessor_index_false);  // We'll remove the true branch below.
    }
  }

  // Remove the true branch which removes the corresponding Phi input if needed.
  // If left only with the false branch, the Phi is automatically removed.
  true_block->DisconnectAndDelete();

  // Merge remaining blocks which are now connected with Goto.
  DCHECK_EQ(block->GetSingleSuccessor(), false_block);
  block->MergeWith(false_block);
  if (!both_successors_return && merge_block->GetPredecessors().size() == 1u) {
    DCHECK_IMPLIES(phi != nullptr, phi->GetBlock() == nullptr);
    DCHECK(merge_block->GetPhis().IsEmpty());
    DCHECK_EQ(block->GetSingleSuccessor(), merge_block);
    block->MergeWith(merge_block);
  }

  MaybeRecordStat(stats_, select != nullptr ? MethodCompilationStat::kControlFlowSelectGenerated
                                            : MethodCompilationStat::kControlFlowDiamondRemoved);

  // Very simple way of finding common subexpressions in the generated HSelect statements
  // (since this runs after GVN). Lookup by condition, and reuse latest one if possible
  // (due to post order, latest select is most likely replacement). If needed, we could
  // improve this by e.g. using the operands in the map as well.
  if (select != nullptr) {
    auto it = cache->find(condition);
    if (it == cache->end()) {
      cache->Put(condition, select);
    } else {
      // Found cached value. See if latest can replace cached in the HIR.
      HSelect* cached_select = it->second;
      DCHECK_EQ(cached_select->GetCondition(), select->GetCondition());
      if (cached_select->GetTrueValue() == select->GetTrueValue() &&
          cached_select->GetFalseValue() == select->GetFalseValue() &&
          select->StrictlyDominates(cached_select)) {
        cached_select->ReplaceWith(select);
        cached_select->GetBlock()->RemoveInstruction(cached_select);
      }
      it->second = select;  // always cache latest
    }
  }

  // No need to update dominance information, as we are simplifying
  // a simple diamond shape, where the join block is merged with the
  // entry block. Any following blocks would have had the join block
  // as a dominator, and `MergeWith` handles changing that to the
  // entry block
  return true;
}

HBasicBlock* HControlFlowSimplifier::TryFixupDoubleDiamondPattern(HBasicBlock* block) {
  DCHECK(block->GetLastInstruction()->IsIf());
  HIf* if_instruction = block->GetLastInstruction()->AsIf();
  HBasicBlock* true_block = if_instruction->IfTrueSuccessor();
  HBasicBlock* false_block = if_instruction->IfFalseSuccessor();
  DCHECK_NE(true_block, false_block);

  // One branch must be a single goto, and the other one the inner if.
  if (true_block->IsSingleGoto() == false_block->IsSingleGoto()) {
    return nullptr;
  }

  HBasicBlock* single_goto = true_block->IsSingleGoto() ? true_block : false_block;
  HBasicBlock* inner_if_block = true_block->IsSingleGoto() ? false_block : true_block;

  // The innner if branch has to be a block with just a comparison and an if.
  if (!inner_if_block->EndsWithIf() ||
      inner_if_block->GetLastInstruction()->AsIf()->InputAt(0) !=
          inner_if_block->GetFirstInstruction() ||
      inner_if_block->GetLastInstruction()->GetPrevious() !=
          inner_if_block->GetFirstInstruction() ||
      !inner_if_block->GetFirstInstruction()->IsCondition()) {
    return nullptr;
  }

  HIf* inner_if_instruction = inner_if_block->GetLastInstruction()->AsIf();
  HBasicBlock* inner_if_true_block = inner_if_instruction->IfTrueSuccessor();
  HBasicBlock* inner_if_false_block = inner_if_instruction->IfFalseSuccessor();
  if (!inner_if_true_block->IsSingleGoto() || !inner_if_false_block->IsSingleGoto()) {
    return nullptr;
  }

  // One must merge into the outer condition and the other must not.
  if (BlocksMergeTogether(single_goto, inner_if_true_block) ==
      BlocksMergeTogether(single_goto, inner_if_false_block)) {
    return nullptr;
  }

  // First merge merges the outer if with one of the inner if branches. The block must be a Phi and
  // a Goto.
  HBasicBlock* first_merge = single_goto->GetSingleSuccessor();
  if (first_merge->GetNumberOfPredecessors() != 2 ||
      first_merge->GetPhis().CountSize() != 1 ||
      !first_merge->GetLastInstruction()->IsGoto() ||
      first_merge->GetFirstInstruction() != first_merge->GetLastInstruction()) {
    return nullptr;
  }

  HPhi* first_phi = first_merge->GetFirstPhi()->AsPhi();

  // Second merge is first_merge and the remainder branch merging. It must be phi + goto, or phi +
  // return. Depending on the first merge, we define the second merge.
  HBasicBlock* merges_into_second_merge =
    BlocksMergeTogether(single_goto, inner_if_true_block)
      ? inner_if_false_block
      : inner_if_true_block;
  if (!BlocksMergeTogether(first_merge, merges_into_second_merge)) {
    return nullptr;
  }

  HBasicBlock* second_merge = merges_into_second_merge->GetSingleSuccessor();
  if (second_merge->GetNumberOfPredecessors() != 2 ||
      second_merge->GetPhis().CountSize() != 1 ||
      !(second_merge->GetLastInstruction()->IsGoto() ||
        second_merge->GetLastInstruction()->IsReturn()) ||
      second_merge->GetFirstInstruction() != second_merge->GetLastInstruction()) {
    return nullptr;
  }

  HPhi* second_phi = second_merge->GetFirstPhi()->AsPhi();
  size_t first_merge_index = second_merge->GetPredecessorIndexOf(first_merge);
  if (second_phi->InputAt(first_merge_index) != first_phi) {
    // The first phi does not merge into the second one. This is an odd case where `first_phi` is
    // dead but hasn't been eliminated from the graph yet.
    // TODO(solanes): We can refactor this method to do:
    //                1) Keep the `second_merge` alive instead of `first_merge`
    //                2) For all `first_phi` instances, fetch the appropriate `first_phi` input
    //                3) For the rest of the inputs, duplicate the value that was already present in
    //                `second_phi`.
    //                When we do this, we can eliminate this `if` case, but it would complicate the
    //                logic of this method.
    return nullptr;
  }

  size_t merges_into_second_merge_index =
      second_merge->GetPredecessorIndexOf(merges_into_second_merge);

  // Merge the phis.
  first_phi->AddInput(second_phi->InputAt(merges_into_second_merge_index));
  merges_into_second_merge->ReplaceSuccessor(second_merge, first_merge);
  second_phi->ReplaceWith(first_phi);
  second_merge->RemovePhi(second_phi);

  // Sort out the new domination before merging the blocks
  DCHECK_EQ(second_merge->GetSinglePredecessor(), first_merge);
  second_merge->GetDominator()->RemoveDominatedBlock(second_merge);
  second_merge->SetDominator(first_merge);
  first_merge->AddDominatedBlock(second_merge);
  first_merge->MergeWith(second_merge);

  // No need to update dominance information. There's a chance that `merges_into_second_merge`
  // doesn't come before `first_merge` but we don't need to fix it since `merges_into_second_merge`
  // will disappear from the graph altogether when doing the follow-up
  // TryGenerateSelectSimpleDiamondPattern.

  return inner_if_block;
}

bool HControlFlowSimplifier::TryFlattenMerge(HBasicBlock* block,
                                             size_t reverse_post_order_index,
                                             BitVectorView<size_t> visited_blocks) {
  DCHECK(block->GetFirstInstruction()->IsGoto());
  DCHECK_EQ(block->GetFirstInstruction(), block->GetLastInstruction());
  HBasicBlock* successor = block->GetSingleSuccessor();
  DCHECK(!graph_->IsExitBlock(successor));  // `HGoto` does not flow to exit block.
  if (block->GetPredecessors().size() < 2u || successor->GetPredecessors().size() < 2u) {
    return false;
  }
  if (block->IsCatchBlock() || successor->IsCatchBlock()) {
    // Phi inputs do not correspond to catch block predecessors. Do not flatten.
    return false;
  }
  if (block->GetLoopInformation() != successor->GetLoopInformation()) {
    // The `block` is a pre-header, including the case when `successor` is an irreducible
    // loop entry that's not actually marked as loop header in the `HLoopInformation`.
    return false;
  }
  if (block->IsInLoop()) {
    // Do not merge if the `block` or the `successor` is a loop header, including irreducible
    // loop entries that are not actually marked as loop header in the `HLoopInformation`.
    // Even for irreducible loops, check for the recorded loop header first.
    HLoopInformation* loop_info = block->GetLoopInformation();
    if (block == loop_info->GetHeader() || successor == loop_info->GetHeader()) {
      return false;
    }
    if (UNLIKELY(loop_info->IsIrreducible())) {
      auto is_loop_header = [loop_info](HBasicBlock* b) {
        DCHECK_EQ(loop_info, b->GetLoopInformation());
        auto&& predecessors = b->GetPredecessors();
        return std::any_of(
            predecessors.begin(),
            predecessors.end(),
            [loop_info](HBasicBlock* p) { return p->GetLoopInformation() != loop_info; });
      };
      if (is_loop_header(block) || is_loop_header(successor)) {
        return false;
      }
    }
  }

  block->TakeGotoBlockSuccessorsOtherPredecessorsAndMergePhis();

  // Fix up domination information for unmerged blocks before calling `MergeWith()`.
  HBasicBlock* dominator = successor->GetDominator();
  if (block->GetDominator() == dominator) {
    dominator->RemoveDominatedBlock(successor);
  } else {
    block->GetDominator()->RemoveDominatedBlock(block);
    block->SetDominator(dominator);
    dominator->ReplaceDominatedBlock(successor, block);
  }
  successor->SetDominator(block);
  block->AddDominatedBlock(successor);

  // Move predecessors before `block` in reverse post order if needed.
  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  BitVectorView<size_t> predecessors_to_move = ArenaBitVector::CreateFixedSize(
      &allocator, graph_->GetBlocks().size(), kArenaAllocControlFlowSimplifier);
  ScopedArenaVector<HBasicBlock*> work_queue(allocator.Adapter(kArenaAllocControlFlowSimplifier));
  auto mark_predecessors = [&](HBasicBlock* current) {
    for (HBasicBlock* predecessor : current->GetPredecessors()) {
      if (visited_blocks.IsBitSet(predecessor->GetBlockId()) &&
          !predecessors_to_move.IsBitSet(predecessor->GetBlockId())) {
        predecessors_to_move.SetBit(predecessor->GetBlockId());
        work_queue.push_back(predecessor);
      }
    }
  };
  mark_predecessors(block);
  if (!work_queue.empty()) {
    do {
      HBasicBlock* current = work_queue.back();
      work_queue.pop_back();
      mark_predecessors(current);
    } while (!work_queue.empty());
    // Move blocks marked in `predecessors_to_move` to the correct position in the reverse
    // post order while extracting `block` and other unmarked blocks to a temporary vector.
    ScopedArenaVector<HBasicBlock*> extracted(allocator.Adapter(kArenaAllocControlFlowSimplifier));
    auto moved_end = graph_->reverse_post_order_.begin() + reverse_post_order_index;
    DCHECK_EQ(block, *moved_end);
    DCHECK(!predecessors_to_move.IsBitSet(block->GetBlockId()));
    extracted.push_back(block);
    auto move_it = std::next(moved_end);
    DCHECK(move_it != graph_->reverse_post_order_.end());
    while (*move_it != successor) {
      if (predecessors_to_move.IsBitSet((*move_it)->GetBlockId())) {
        *moved_end = *move_it;
        ++moved_end;
      } else {
        extracted.push_back(*move_it);
      }
      ++move_it;
      DCHECK(move_it != graph_->reverse_post_order_.end());
    }
    // Place extracted blocks in the freed range in reverse post order.
    DCHECK_EQ(static_cast<size_t>(std::distance(moved_end, move_it)), extracted.size());
    std::copy(extracted.begin(), extracted.end(), moved_end);
  }

  // Finish the merge using `MergeWith()`.
  block->MergeWith(successor);

  MaybeRecordStat(stats_, MethodCompilationStat::kControlFlowFlattenedMerge);
  return true;
}

bool HControlFlowSimplifier::Run() {
  bool simplified = com::android::art::rw::flags::packed_switch_simplification() && ReturnSinking();

  ScopedArenaAllocator allocator(graph_->GetArenaStack());
  // Select cache with local allocator for `TryGenerateSelectSimpleDiamondPattern()`.
  ScopedArenaSafeMap<HInstruction*, HSelect*> select_cache(
      std::less<HInstruction*>(), allocator.Adapter(kArenaAllocControlFlowSimplifier));
  // Mark visited blocks by block id for reverse post order fixup in `TryFlattenMerge()`.
  BitVectorView<size_t> visited_blocks = ArenaBitVector::CreateFixedSize(
      &allocator, graph_->GetBlocks().size(), kArenaAllocControlFlowSimplifier);

  // Iterate in post order in the case that simplifying a block exposes simplification
  // opportunities for earier blocks. Do not process the entry block.
  // We may remove blocks from the reverse post order array, so make the iteration very explicit.
  HBasicBlock* const * reverse_post_order_data = graph_->GetReversePostOrder().data();
  size_t reverse_post_order_index = graph_->GetReversePostOrder().size();
  while (reverse_post_order_index != /* Do not process entry block with index 0. */ 1u) {
    --reverse_post_order_index;
    HBasicBlock* block = reverse_post_order_data[reverse_post_order_index];
    DCHECK(block != nullptr);
    DCHECK(!block->GetInstructions().IsEmpty());
    HInstruction* last_inst = block->GetLastInstruction();
    bool block_may_have_moved_in_rpo = false;
    if (com::android::art::rw::flags::packed_switch_simplification() &&
        last_inst->IsPackedSwitch() &&
        TrySimplifyPackedSwitch(block, &allocator)) {
      simplified = true;
    } else if (com::android::art::rw::flags::packed_switch_simplification() &&
               last_inst->IsGoto() &&
               last_inst == block->GetFirstInstruction() &&
               TryFlattenMerge(block, reverse_post_order_index, visited_blocks)) {
      simplified = true;
      block_may_have_moved_in_rpo = true;
    } else if (last_inst->IsIf()) {
      if (TryGenerateSelectSimpleDiamondPattern(block, &select_cache)) {
        simplified = true;
      } else if (!com::android::art::rw::flags::packed_switch_simplification()) {
        // Note: The `TryFlattenMerge()` simplification above replaces the
        // `TryFixupDoubleDiamondPattern()` here if the flag is enabled.
        // Try to fix up the odd version of the double diamond pattern. If we could do it, it means
        // that we can generate two selects.
        HBasicBlock* inner_if_block = TryFixupDoubleDiamondPattern(block);
        if (inner_if_block != nullptr) {
          // Generate the selects now since `inner_if_block` should be after `block` in PostOrder.
          bool result = TryGenerateSelectSimpleDiamondPattern(inner_if_block, &select_cache);
          DCHECK(result);
          result = TryGenerateSelectSimpleDiamondPattern(block, &select_cache);
          DCHECK(result);
          simplified = true;
        }
      }
    }
    DCHECK_LT(block->GetBlockId(), graph_->GetBlocks().size());
    visited_blocks.SetBit(block->GetBlockId());
    // Blocks with higher indexes may have been removed and the `block` may have been moved
    // further back. Removing from a `std::vector<>` does not change the data pointer.
    DCHECK_EQ(reverse_post_order_data, graph_->GetReversePostOrder().data());
    DCHECK_LT(reverse_post_order_index, graph_->GetReversePostOrder().size());
    if (block_may_have_moved_in_rpo) {
      DCHECK_GE(IndexOfElement(graph_->GetReversePostOrder(), block), reverse_post_order_index);
    } else {
      DCHECK_EQ(block, reverse_post_order_data[reverse_post_order_index]);
    }
  }
  DCHECK_EQ(reverse_post_order_index, 1u);
  DCHECK(graph_->IsEntryBlock(reverse_post_order_data[0u]));

  return simplified;
}

}  // namespace art
