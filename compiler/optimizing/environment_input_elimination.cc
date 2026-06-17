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

#include "environment_input_elimination.h"
#include "optimizing/nodes.h"

namespace art HIDDEN {

static bool TryOptimizeEnvironment(HEnvironment* environment, HGraph* graph) {
  bool optimization_occurred = false;
  while (environment != nullptr) {
    size_t environment_size = environment->Size();
    for (size_t i = 0; i < environment_size; i++) {
      HInstruction* instruction = environment->GetInstructionAt(i);
      if (instruction == nullptr) {
        continue;
      }

      // We can remove instructions with reference types from the environment only if the
      // graph is dead reference safe and does not contain monitor operations.
      //
      // We cannot remove such instructions if the graph is not dead reference safe, because
      // doing so may affect their reachability and, consequently, user code behavior that
      // relies on finalization. See the comments in the following file for details:
      // libcore/dalvik/src/main/java/dalvik/annotation/optimization/DeadReferenceSafe.java
      //
      // If the graph contains monitor operations, we can remove instructions with reference
      // types that corresponds to objects that do not hold a lock. However, we currently
      // conservatively keep all instructions with reference types, as this has little impact
      // on the optimization effectiveness while simplifying the implementation.
      if (instruction->GetType() == DataType::Type::kReference &&
          (!graph->IsDeadReferenceSafe() || graph->HasMonitorOperations())) {
        continue;
      }

      environment->RemoveAsUserOfInput(i);
      environment->SetRawEnvAt(i, /*instruction=*/ nullptr);
      optimization_occurred = true;
    }
    environment = environment->GetParent();
  }
  return optimization_occurred;
}

bool HEnvironmentInputElimination::Run() {
  if (graph_->IsDebuggable() || graph_->IsCompilingOsr()) {
    return false;
  }

  bool optimization_occurred = false;

  for (HBasicBlock* block : graph_->GetReversePostOrder()) {
    for (HInstructionIterator it(block->GetInstructions()); !it.Done(); it.Advance()) {
      HInstruction* instruction = it.Current();
      if (!instruction->HasEnvironment() ||
          InstructionNeedsPreciseEnvironment(instruction, /*osr=*/ false)) {
        continue;
      }

      if (TryOptimizeEnvironment(instruction->GetEnvironment(), graph_)) {
        MaybeRecordStat(stats_, MethodCompilationStat::kEnvironmentRedundantInputsRemoved);
        optimization_occurred = true;
      }
    }
  }

  return optimization_occurred;
}

}  // namespace art
