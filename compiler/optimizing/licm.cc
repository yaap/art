/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "licm.h"

#include "base/bit_vector-inl.h"
#include "base/scoped_arena_containers.h"
#include "common_dominator.h"
#include "loop_information-inl.h"
#include "side_effects_analysis.h"

namespace art HIDDEN {

static bool IsPhiOf(HInstruction* instruction, HBasicBlock* block) {
  return instruction->IsPhi() && instruction->GetBlock() == block;
}

/**
 * Returns whether `instruction` has all its inputs and environment defined
 * before the loop it is in.
 */
static bool InputsAreDefinedBeforeLoop(HInstruction* instruction) {
  DCHECK(instruction->IsInLoop());
  HLoopInformation* info = instruction->GetBlock()->GetLoopInformation();
  for (const HInstruction* input : instruction->GetInputs()) {
    HLoopInformation* input_loop = input->GetBlock()->GetLoopInformation();
    // We only need to check whether the input is defined in the loop. If it is not
    // it is defined before the loop.
    if (input_loop != nullptr && input_loop->IsIn(*info)) {
      return false;
    }
  }

  for (HEnvironment* environment = instruction->GetEnvironment();
       environment != nullptr;
       environment = environment->GetParent()) {
    for (size_t i = 0, e = environment->Size(); i < e; ++i) {
      HInstruction* input = environment->GetInstructionAt(i);
      if (input != nullptr) {
        HLoopInformation* input_loop = input->GetBlock()->GetLoopInformation();
        if (input_loop != nullptr && input_loop->IsIn(*info)) {
          // We can move an instruction that takes a loop header phi in the environment:
          // we will just replace that phi with its first input later in `UpdateLoopPhisIn`.
          bool is_loop_header_phi = IsPhiOf(input, info->GetHeader());
          if (!is_loop_header_phi) {
            return false;
          }
        }
      }
    }
  }
  return true;
}

/**
 * If `environment` has a loop header phi, we replace it with its first input.
 */
static void UpdateLoopPhisIn(ArenaAllocator* allocator,
                             HEnvironment* environment,
                             HLoopInformation* info) {
  for (; environment != nullptr; environment = environment->GetParent()) {
    for (size_t i = 0, e = environment->Size(); i < e; ++i) {
      HInstruction* input = environment->GetInstructionAt(i);
      if (input != nullptr && IsPhiOf(input, info->GetHeader())) {
        environment->RemoveAsUserOfInput(i);
        HInstruction* incoming = input->InputAt(0);
        environment->SetRawEnvAt(i, incoming);
        incoming->AddEnvUseAt(allocator, environment, i);
      }
    }
  }
}

bool LICM::Run() {
  if (!graph_->HasLoops()) {
    // Nothing to do.
    return false;
  }

  bool didLICM = false;
  SideEffectsAnalysis side_effects(graph_);
  side_effects.Run();

  DCHECK(side_effects.HasRun());

  ScopedArenaAllocator allocator(graph_->GetArenaStack());

  // Only used during debug.
  BitVectorView<size_t> visited;
  if (kIsDebugBuild) {
    visited =
        ArenaBitVector::CreateFixedSize(&allocator, graph_->GetBlocks().size(), kArenaAllocLICM);
  }

  // Post order visit to visit inner loops before outer loops.
  for (HBasicBlock* block : graph_->GetPostOrder()) {
    if (!block->IsLoopHeader()) {
      // Only visit the loop when we reach the header.
      continue;
    }

    HLoopInformation* loop_info = block->GetLoopInformation();
    if (loop_info->ContainsIrreducibleLoop()) {
      // We cannot licm in an irreducible loop, or in a natural loop containing an
      // irreducible loop.
      continue;
    }

    SideEffects loop_effects = side_effects.GetLoopEffects(block);
    HBasicBlock* pre_header = loop_info->GetPreHeader();
    DCHECK(loop_info->GetHeader()->GetDominator() == pre_header);

    // Perform LICM only on blocks that are unconditionally executed on each full loop iteration.
    // This is the dominator chain from the common dominator of all back edges to the header.
    // (LICM on conditional blocks increases register pressure and often results in excessive
    // parallel moves without even knowing whether the conditional blocks shall be executed.)
    ArrayRef<HBasicBlock* const> back_edges(loop_info->GetBackEdges());
    HBasicBlock* back_edge_dominator = back_edges[0];
    if (back_edges.size() > 1u) {
      CommonDominator common_dominator(back_edges[0]);
      for (HBasicBlock* back_edge : back_edges.SubArray(/*pos=*/ 1u)) {
        common_dominator.Update(back_edge);
      }
      back_edge_dominator = common_dominator.Get();
    }
    ScopedArenaVector<HBasicBlock*> unconditional_blocks(allocator.Adapter(kArenaAllocLICM));
    for (HBasicBlock* dom = back_edge_dominator; dom != pre_header; dom = dom->GetDominator()) {
      unconditional_blocks.push_back(dom);
    }

    for (HBasicBlock* inner : ReverseRange(unconditional_blocks)) {
      DCHECK(inner->IsInLoop());
      if (inner->GetLoopInformation() != loop_info) {
        // Thanks to post order visit, inner loops were already visited.
        DCHECK(visited.IsBitSet(inner->GetLoopInformation()->GetHeader()->GetBlockId()));
        continue;
      }

      // We can move an instruction that can throw only as long as it is the first visible
      // instruction (throw or write) in the loop. Note that the first potentially visible
      // instruction that is not hoisted stops this optimization. Non-throwing instructions,
      // on the other hand, can still be hoisted.
      bool found_first_non_hoisted_visible_instruction_in_loop = !inner->IsLoopHeader();
      for (HInstructionIteratorPrefetchNext inst_it(inner->GetInstructions());
           !inst_it.Done();
           inst_it.Advance()) {
        HInstruction* instruction = inst_it.Current();
        bool can_move = false;
        if (instruction->CanBeMoved() && InputsAreDefinedBeforeLoop(instruction)) {
          if (instruction->CanThrow()) {
            if (!found_first_non_hoisted_visible_instruction_in_loop) {
              DCHECK(instruction->GetBlock()->IsLoopHeader());
              if (instruction->IsClinitCheck()) {
                // clinit is only done once, and since all visible instructions
                // in the loop header so far have been hoisted out, we can hoist
                // the clinit check out also.
                can_move = true;
              } else if (!instruction->GetSideEffects().MayDependOn(loop_effects)) {
                can_move = true;
              }
            }
          } else if (!instruction->GetSideEffects().MayDependOn(loop_effects)) {
            can_move = true;
          }
        }
        if (can_move) {
          // We need to update the environment if the instruction has a loop header
          // phi in it.
          if (instruction->NeedsEnvironment()) {
            UpdateLoopPhisIn(graph_->GetAllocator(), instruction->GetEnvironment(), loop_info);
          } else {
            DCHECK(!instruction->HasEnvironment());
          }
          instruction->MoveBefore(pre_header->GetLastInstruction());
          MaybeRecordStat(stats_, MethodCompilationStat::kLoopInvariantMoved);
          didLICM = true;
        }

        if (!can_move && (instruction->CanThrow() || instruction->DoesAnyWrite())) {
          // If `instruction` can do something visible (throw or write),
          // we cannot move further instructions that can throw.
          found_first_non_hoisted_visible_instruction_in_loop = true;
        }
      }
    }
    if (kIsDebugBuild) {
      visited.SetBit(block->GetBlockId());
    }
  }
  return didLICM;
}

}  // namespace art
