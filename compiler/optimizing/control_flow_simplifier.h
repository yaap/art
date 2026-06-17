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

/*
 * This optimization recognizes the common diamond selection pattern and
 * replaces it with an instance of the HSelect instruction.
 *
 * Recognized patterns:
 *
 *          If [ Condition ]
 *            /          \
 *      false branch  true branch
 *            \          /
 *     Phi [FalseValue, TrueValue]
 *
 * and
 *
 *             If [ Condition ]
 *               /          \
 *     false branch        true branch
 *     return FalseValue   return TrueValue
 *
 * The pattern will be simplified if `true_branch` and `false_branch` each
 * contain at most one instruction without any side effects.
 *
 * Blocks are merged into one and Select replaces the If and the Phi.
 *
 * For the first pattern it simplifies to:
 *
 *              true branch
 *              false branch
 *              Select [FalseValue, TrueValue, Condition]
 *
 * For the second pattern it simplifies to:
 *
 *              true branch
 *              false branch
 *              return Select [FalseValue, TrueValue, Condition]
 *
 * Note: In order to recognize no side-effect blocks, this optimization must be
 * run after the instruction simplifier has removed redundant suspend checks.
 */

#ifndef ART_COMPILER_OPTIMIZING_CONTROL_FLOW_SIMPLIFIER_H_
#define ART_COMPILER_OPTIMIZING_CONTROL_FLOW_SIMPLIFIER_H_

#include "base/bit_vector.h"
#include "base/macros.h"
#include "base/scoped_arena_containers.h"
#include "optimization.h"

namespace art HIDDEN {

class HBasicBlock;
class HInstruction;
class HSelect;

class HControlFlowSimplifier : public HOptimization {
 public:
  HControlFlowSimplifier(HGraph* graph,
                         OptimizingCompilerStats* stats,
                         const char* name = kControlFlowSimplifierPassName);

  bool Run() override;

  static constexpr const char* kControlFlowSimplifierPassName = "control_flow_simplifier";

 private:
  // Coalesces the Return/ReturnVoid instructions into one, if we have two or more.
  // We do this to avoid generating the exit frame code several times.
  // This also makes some other optimizations applicable in more cases without
  // implementing explicit handling of additional `HReturn`/`HReturnVoid` patterns.
  bool ReturnSinking();

  // Try simplifying `HPackedSwitch` using a load from constant table.
  // Certain cases can be simplified even further.
  bool TrySimplifyPackedSwitch(HBasicBlock* block, ScopedArenaAllocator* allocator);

  bool TryGenerateSelectSimpleDiamondPattern(HBasicBlock* block,
                                             ScopedArenaSafeMap<HInstruction*, HSelect*>* cache);

  // When generating code for nested ternary operators (e.g. `return (x > 100) ? 100 : ((x < -100) ?
  // -100 : x);`), a dexer can generate a double diamond pattern but it is not a clear cut one due
  // to the merging of the blocks. `TryFixupDoubleDiamondPattern` recognizes that pattern and fixes
  // up the graph to have a clean double diamond that `TryGenerateSelectSimpleDiamondPattern` can
  // use to generate selects.
  //
  // In ASCII, it turns:
  //
  //      1 (outer if)
  //     / \
  //    2   3 (inner if)
  //    |  / \
  //    | 4  5
  //     \/  |
  //      6  |
  //       \ |
  //         7
  //         |
  //         8
  // into:
  //      1 (outer if)
  //     / \
  //    2   3 (inner if)
  //    |  / \
  //    | 4  5
  //     \/ /
  //      6
  //      |
  //      8
  //
  // In short, block 7 disappears and we merge 6 and 7. Now we have a diamond with {3,4,5,6}, and
  // when that gets resolved we get another one with the outer if.
  HBasicBlock* TryFixupDoubleDiamondPattern(HBasicBlock* block);

  // Try merging a block that contains only a `HGoto` and `Phi`s with the successor. We do this
  // if both the `block` and its single successor have two or more predecessors each to flatten
  // the merge blocks for multi-if/switch control flow. This can reduce the number of parrallel
  // moves created by the register allocator and/or expose new optimization opportunities.
  //
  // For example, nested ternary operators such as `(x > 100) ? 100 : ((x < -100) ? -100 : x)`
  // can be represented in bytecode in a double diamond pattern that is not easily recognized
  // for `HSelect` simplification and `TryFlattenMerge()` can help. It turns
  //
  //      1 (outer if)
  //     / \
  //    2   3 (inner if)
  //    |  / \
  //    | 4  5
  //     \/  |
  //      6  |
  //       \ |
  //         7
  //         |
  //         8
  //
  // with block 6 containing only a `HGoto` and `HPhi`s into
  //
  //      1 (outer if)
  //     / \
  //    2   3 (inner if)
  //    |  / \
  //    | 4  5
  //     \/ /
  //      6
  //      |
  //      8
  //
  // In short, block 7 disappears as we merge blocks 6 and 7. Now we have a diamond with {3,4,5,6},
  // and if that gets converted to `HSelect`, we get another diamond with the outer if and a chance
  // to convert that to a `HSelect`.
  bool TryFlattenMerge(HBasicBlock* block,
                       size_t reverse_post_order_index,
                       BitVectorView<size_t> visited_blocks);

  DISALLOW_COPY_AND_ASSIGN(HControlFlowSimplifier);
};

}  // namespace art

#endif  // ART_COMPILER_OPTIMIZING_CONTROL_FLOW_SIMPLIFIER_H_
