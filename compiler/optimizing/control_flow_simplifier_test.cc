/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "base/arena_allocator.h"
#include "base/macros.h"
#include "builder.h"
#include "com_android_art_rw_flags.h"
#include "nodes.h"
#include "optimizing_unit_test.h"
#include "side_effects_analysis.h"

namespace art HIDDEN {

class ControlFlowSimplifierTest : public OptimizingUnitTest {
 protected:
  HPhi* ConstructBasicGraphForSelect(HBasicBlock* return_block, HInstruction* instr) {
    HParameterValue* bool_param = MakeParam(DataType::Type::kBool);
    HIntConstant* const1 =  graph_->GetIntConstant(1);

    auto [if_block, then_block, else_block] = CreateDiamondPattern(return_block, bool_param);

    AddOrInsertInstruction(then_block, instr);
    HPhi* phi = MakePhi(return_block, {instr, const1});
    return phi;
  }

  bool CheckGraphAndTryControlFlowSimplifier() {
    graph_->BuildDominatorTree();
    EXPECT_TRUE(CheckGraph());
    return HControlFlowSimplifier(graph_, /*handles*/ nullptr, /*stats*/ nullptr).Run();
  }

  template <typename T>
  bool EntriesMatch(HLoadConstantTableEntry* lcte, ArrayRef<const T> entries) {
    if (entries.size() != lcte->GetNumEntries()) {
      return false;
    }
    for (size_t i : Range(entries.size())) {
      int64_t expected;
      if constexpr (std::is_same_v<T, float>) {
        expected = bit_cast<uint32_t, float>(entries[i]);
      } else if constexpr (std::is_same_v<T, double>) {
        expected = bit_cast<uint64_t, double>(entries[i]);
      } else {
        expected = entries[i];
      }
      if (lcte->GetEntry(i) != expected) {
        return false;
      }
    }
    return true;
  }

  void UpdateSwitchWithReturns(HBasicBlock* switch_block,
                               HBasicBlock* return_block,
                               ArrayRef<const size_t> return_block_indexes);

  template <typename T>
  void TestSwitchToTable(DataType::Type type,
                         ArrayRef<const T> entries,
                         int32_t start_value,
                         ArrayRef<const size_t> return_block_indexes = ArrayRef<const size_t>());

  template <typename T, size_t num_entries>
  void TestSwitchToTable(DataType::Type type,
                         const T (&entries)[num_entries],
                         int32_t start_value) {
    TestSwitchToTable(type, ArrayRef<const T>(entries, num_entries), start_value);
  }

  template <typename T, size_t num_entries>
  void TestSwitchToTable(DataType::Type type,
                         const T (&entries)[num_entries],
                         int32_t start_value,
                         const size_t (&return_block_indexes)[num_entries]) {
    TestSwitchToTable(type,
                      ArrayRef<const T>(entries, num_entries),
                      start_value,
                      ArrayRef<const size_t>(return_block_indexes, num_entries));
  }

  template <typename T>
  void TestSwitchToTableWithDefault(
      DataType::Type type,
      ArrayRef<const T> entries,
      int32_t start_value,
      T dflt,
      ArrayRef<const size_t> return_block_indexes = ArrayRef<const size_t>());

  template <typename T, size_t num_entries>
  void TestSwitchToTableWithDefault(DataType::Type type,
                                    const T (&entries)[num_entries],
                                    int32_t start_value,
                                    T dflt) {
    TestSwitchToTableWithDefault(type, ArrayRef<const T>(entries, num_entries), start_value, dflt);
  }

  template <typename T, size_t num_entries>
  void TestSwitchToTableWithDefault(DataType::Type type,
                                    const T (&entries)[num_entries],
                                    int32_t start_value,
                                    T dflt,
                                    const size_t (&return_block_indexes)[num_entries]) {
    TestSwitchToTableWithDefault(type,
                                 ArrayRef<const T>(entries, num_entries),
                                 start_value,
                                 dflt,
                                 ArrayRef<const size_t>(return_block_indexes, num_entries));
  }

  void TestReturnMerging(ArrayRef<const size_t> return_block_indexes);
};

// HDivZeroCheck might throw and should not be hoisted from the conditional to an unconditional.
TEST_F(ControlFlowSimplifierTest, testZeroCheckPreventsSelect) {
  HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
  HParameterValue* param = MakeParam(DataType::Type::kInt32);
  HDivZeroCheck* instr = new (GetAllocator()) HDivZeroCheck(param, 0);
  HPhi* phi = ConstructBasicGraphForSelect(return_block, instr);

  ManuallyBuildEnvFor(instr, {param, graph_->GetIntConstant(1)});

  EXPECT_FALSE(CheckGraphAndTryControlFlowSimplifier());
  EXPECT_INS_RETAINED(phi);
}

// Test that ControlFlowSimplifier succeeds with HAdd.
TEST_F(ControlFlowSimplifierTest, testSelectWithAdd) {
  HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
  HParameterValue* param = MakeParam(DataType::Type::kInt32);
  HAdd* instr = new (GetAllocator()) HAdd(DataType::Type::kInt32, param, param, /*dex_pc=*/ 0);
  HPhi* phi = ConstructBasicGraphForSelect(return_block, instr);
  EXPECT_TRUE(CheckGraphAndTryControlFlowSimplifier());
  EXPECT_INS_REMOVED(phi);
}

// Test that ControlFlowSimplifier succeeds if there is an additional `HPhi` with identical inputs.
TEST_F(ControlFlowSimplifierTest, testSelectWithAddAndExtraPhi) {
  // Create a graph with three blocks merging to the `return_block`.
  HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
  HParameterValue* bool_param1 = MakeParam(DataType::Type::kBool);
  HParameterValue* bool_param2 = MakeParam(DataType::Type::kBool);
  HParameterValue* param = MakeParam(DataType::Type::kInt32);
  HInstruction* const0 = graph_->GetIntConstant(0);
  auto [if_block1, left, mid] = CreateDiamondPattern(return_block, bool_param1);
  HBasicBlock* if_block2 = AddNewBlock();
  if_block1->ReplaceSuccessor(mid, if_block2);
  HBasicBlock* right = AddNewBlock();
  if_block2->AddSuccessor(mid);
  if_block2->AddSuccessor(right);
  HIf* if2 = MakeIf(if_block2, bool_param2);
  right->AddSuccessor(return_block);
  MakeGoto(right);
  ASSERT_TRUE(PredecessorsEqual(return_block, {left, mid, right}));
  HAdd* add = MakeBinOp<HAdd>(right, DataType::Type::kInt32, param, param);
  HPhi* phi1 = MakePhi(return_block, {param, param, add});
  HPhi* phi2 = MakePhi(return_block, {param, const0, const0});

  // Prevent second `HSelect` match. Do not rely on the "instructions per branch" limit.
  MakeInvokeStatic(left, DataType::Type::kVoid, {}, {});

  EXPECT_TRUE(CheckGraphAndTryControlFlowSimplifier());

  ASSERT_BLOCK_RETAINED(left);
  ASSERT_BLOCK_REMOVED(mid);
  ASSERT_BLOCK_REMOVED(right);
  HInstruction* select = if2->GetPrevious();  // `HSelect` is inserted before `HIf`.
  ASSERT_TRUE(select->IsSelect());
  ASSERT_INS_RETAINED(phi1);
  ASSERT_TRUE(InputsEqual(phi1, {param, select}));
  ASSERT_INS_RETAINED(phi2);
  ASSERT_TRUE(InputsEqual(phi2, {param, const0}));
}

// Test `HSelect` optimization in an irreducible loop.
TEST_F(ControlFlowSimplifierTest, testSelectInIrreducibleLoop) {
  HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
  auto [split, left_header, right_header, body] = CreateIrreducibleLoop(return_block);

  HParameterValue* split_param = MakeParam(DataType::Type::kBool);
  HParameterValue* bool_param = MakeParam(DataType::Type::kBool);
  HParameterValue* n_param = MakeParam(DataType::Type::kInt32);

  MakeIf(split, split_param);

  HInstruction* const0 = graph_->GetIntConstant(0);
  HInstruction* const1 = graph_->GetIntConstant(1);
  auto [left_phi, right_phi, add] =
      MakeLinearIrreducibleLoopVar(left_header, right_header, body, const1, const0, const1);
  HCondition* condition = MakeCondition(left_header, kCondGE, left_phi, n_param);
  MakeIf(left_header, condition);

  auto [if_block, then_block, else_block] = CreateDiamondPattern(body, bool_param);
  HPhi* phi = MakePhi(body, {const1, const0});

  EXPECT_TRUE(CheckGraphAndTryControlFlowSimplifier());
  HLoopInformation* loop_info = left_header->GetLoopInformation();
  ASSERT_TRUE(loop_info != nullptr);
  ASSERT_TRUE(loop_info->IsIrreducible());

  EXPECT_INS_REMOVED(phi);
  ASSERT_TRUE(if_block->GetFirstInstruction()->IsSelect());

  ASSERT_EQ(if_block, add->GetBlock());  // Moved when merging blocks.

  for (HBasicBlock* removed_block : {then_block, else_block, body}) {
    ASSERT_BLOCK_REMOVED(removed_block);
    uint32_t removed_block_id = removed_block->GetBlockId();
    ASSERT_FALSE(loop_info->GetBlockMask().IsBitSet(removed_block_id)) << removed_block_id;
  }
}

// Update switch case blocks for return pattern based on `return_block_indexes` for switch cases.
//
// Index 0 specifies the `return_block` and higher indexes specify new blocks that shall be added.
// If an index is specified only once, it shall get a pure return block, otherwise it shall get
// a single-goto block that shall merge into a return block, except for index 0 (`return_block`),
// supporting tests with and without merges with the default case.
void ControlFlowSimplifierTest::UpdateSwitchWithReturns(
    HBasicBlock* switch_block,
    HBasicBlock* return_block,
    ArrayRef<const size_t> return_block_indexes) {
  CHECK(switch_block->GetLastInstruction()->IsPackedSwitch());
  HPackedSwitch* packed_switch = switch_block->GetLastInstruction()->AsPackedSwitch();
  CHECK_EQ(packed_switch->GetNumEntries(), return_block_indexes.size());
  CHECK(return_block->HasSinglePhi());
  HPhi* phi = return_block->GetFirstPhi()->AsPhi();
  std::vector<HBasicBlock*> ret_blocks;
  ret_blocks.push_back(return_block);
  size_t redirected = 0;
  for (size_t i : Range(return_block_indexes.size())) {
    if (return_block_indexes[i] != 0) {
      HInstruction* ret_input = phi->InputAt(i - redirected);
      phi->RemoveInputAt(i - redirected);
      ++redirected;
      HBasicBlock* successor = switch_block->GetSuccessors()[i];
      if (return_block_indexes[i] == ret_blocks.size()) {
        if (ContainsElement(return_block_indexes.SubArray(i + 1u), return_block_indexes[i])) {
          // Create a new block for merging two or more switch cases.
          HBasicBlock* new_block = AddNewBlock();
          successor->ReplaceSuccessor(return_block, new_block);
          new_block->AddSuccessor(exit_block_);
          MakeReturn(new_block, ret_input);
          ret_blocks.push_back(new_block);
        } else {
          // Update the successor to a return block.
          HReturn* ret = new (GetAllocator()) HReturn(ret_input);
          successor->ReplaceAndRemoveInstructionWith(successor->GetLastInstruction(), ret);
          successor->ReplaceSuccessor(return_block, exit_block_);
          ret_blocks.push_back(successor);
        }
      } else {
        CHECK_LT(return_block_indexes[i], return_block_indexes.size());
        HBasicBlock* ret_block = ret_blocks[return_block_indexes[i]];
        successor->ReplaceSuccessor(return_block, ret_block);
        if (ret_block->GetPhis().IsEmpty()) {
          HReturn* ret = ret_block->GetLastInstruction()->AsReturn();
          if (ret->InputAt(0) != ret_input) {
            size_t num_predecessors = ret_block->GetPredecessors().size();
            CHECK_GE(num_predecessors, 2u);
            std::vector<HInstruction*> phi_inputs(num_predecessors - 1u, ret->InputAt(0));
            phi_inputs.push_back(ret_input);
            HPhi* new_phi = MakePhi(ret_block, phi_inputs);
            ret->ReplaceInput(new_phi, 0);
          }
        } else {
          CHECK(ret_block->HasSinglePhi());
          ret_block->GetFirstPhi()->AsPhi()->AddInput(ret_input);
        }
      }
    }
  }
  if (phi->InputCount() == 1u) {
    phi->ReplaceWith(phi->InputAt(0));
    return_block->RemovePhi(phi);
    CHECK_EQ(return_block->GetPredecessors().size(), 1u);
    HBasicBlock* predecessor = return_block->GetSinglePredecessor();
    CHECK(predecessor->IsSingleGoto());
    switch_block->ReplaceSuccessor(predecessor, return_block);
    // Remove the `predecessor` manually. Helper functions would try to update
    // the reverse post order but we have not built it yet.
    predecessor->RemoveInstruction(predecessor->GetLastInstruction());
    predecessor->DisconnectFromSuccessors();
    const_cast<HBasicBlock*&>(graph_->GetBlocks()[predecessor->GetBlockId()]) = nullptr;
    predecessor->SetGraph(nullptr);
  }
}

template <typename T>
void ControlFlowSimplifierTest::TestSwitchToTable(DataType::Type type,
                                                  ArrayRef<const T> entries,
                                                  int32_t start_value,
                                                  ArrayRef<const size_t> return_block_indexes) {
  HBasicBlock* return_block = InitEntryMainExitGraph();
  HInstruction* switch_input = MakeParam(DataType::Type::kInt32);
  HInstruction* default_value = MakeParam(type);
  HBasicBlock* switch_block =
      CreateSwitchPattern(return_block, entries.size(), switch_input, start_value);
  HPackedSwitch* packed_switch = switch_block->GetLastInstruction()->AsPackedSwitch();
  std::vector<HInstruction*> phi_inputs;
  for (T entry : entries) {
    phi_inputs.push_back(GetConstant(type, entry));
  }
  phi_inputs.push_back(default_value);
  HPhi* phi = MakePhi(return_block, phi_inputs);
  if (return_block_indexes.empty()) {
    MakeInvokeStatic(return_block, DataType::Type::kVoid, {}, {});  // Avoid the return pattern.
  }
  HReturn* ret = MakeReturn(return_block, phi);
  if (!return_block_indexes.empty()) {
    CHECK_EQ(return_block_indexes.size(), entries.size());
    UpdateSwitchWithReturns(switch_block, return_block, return_block_indexes);
  }

  bool success = CheckGraphAndTryControlFlowSimplifier();
  ASSERT_TRUE(success);

  // Check `HPackedSwitch` replacement.
  ASSERT_INS_REMOVED(packed_switch);
  ASSERT_TRUE(switch_block->GetLastInstruction()->IsIf());
  HInstruction* cond = switch_block->GetLastInstruction()->AsIf()->InputAt(0);
  ASSERT_TRUE(cond->IsBelow());
  HInstruction* lhs = cond->AsBelow()->GetLeft();
  HInstruction* rhs = cond->AsBelow()->GetRight();
  if (start_value != 0) {
    ASSERT_TRUE(lhs->IsAdd());
    ASSERT_INS_EQ(switch_input, lhs->AsAdd()->GetLeft());
    ASSERT_TRUE(lhs->AsAdd()->GetRight()->IsIntConstant());
    ASSERT_EQ(-start_value, lhs->AsAdd()->GetRight()->AsIntConstant()->GetValue());
  } else {
    ASSERT_INS_EQ(switch_input, lhs);
  }
  ASSERT_TRUE(rhs->IsIntConstant());
  EXPECT_EQ(dchecked_integral_cast<int32_t>(entries.size()), rhs->AsIntConstant()->GetValue());
  // Check `HLoadConstantTableEntry`.
  HBasicBlock* then_block = switch_block->GetSuccessors()[0];
  ASSERT_TRUE(then_block == switch_block->GetLastInstruction()->AsIf()->IfTrueSuccessor());
  ASSERT_TRUE(then_block->GetFirstInstruction()->IsLoadConstantTableEntry());
  HLoadConstantTableEntry* lcte = then_block->GetFirstInstruction()->AsLoadConstantTableEntry();
  EXPECT_EQ(type, lcte->GetType());
  ASSERT_TRUE(EntriesMatch(lcte, entries));
  if (!return_block_indexes.empty()) {
    // Due to the splitting and merging of blocks, the return block may or may not be the
    // original `return_block`. Similar for the phi and return instructions.
    return_block = then_block->GetSingleSuccessor();
    ASSERT_TRUE(return_block->HasSinglePhi());
    phi = return_block->GetFirstPhi()->AsPhi();
    ASSERT_TRUE(return_block->GetLastInstruction()->IsReturn());
    ret = return_block->GetLastInstruction()->AsReturn();
  }
  // Check the phi.
  ASSERT_TRUE(then_block->GetLastInstruction()->IsGoto());
  ASSERT_INS_EQ(phi, ret->InputAt(0));
  ASSERT_EQ(2u, phi->InputCount());
  ASSERT_INS_EQ(lcte, phi->InputAt(0));
  ASSERT_INS_EQ(default_value, phi->InputAt(1));
}

TEST_F(ControlFlowSimplifierTest, SwitchToTableBoolean) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kEntries[] = {1, 0, 1, 1, 0, 0, 0, 1, 0, 1};
  TestSwitchToTable(DataType::Type::kBool, kEntries, /*start_value=*/ 0);
  TestSwitchToTable(DataType::Type::kBool, kEntries, /*start_value=*/ 1);
}

TEST_F(ControlFlowSimplifierTest, SwitchToTableByte) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kEntries[] = {42, 88, -7, 11, 123};
  TestSwitchToTable(DataType::Type::kInt8, kEntries, /*start_value=*/ 0);
  TestSwitchToTable(DataType::Type::kInt8, kEntries, /*start_value=*/ 1);
}

TEST_F(ControlFlowSimplifierTest, SwitchToTableUint8) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kEntries[] = {42, 88, 7, 11, 255};
  TestSwitchToTable(DataType::Type::kUint8, kEntries, /*start_value=*/ 0);
  TestSwitchToTable(DataType::Type::kUint8, kEntries, /*start_value=*/ 1);
}

TEST_F(ControlFlowSimplifierTest, SwitchToTableShort) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kEntries[] = {42, 88, -7, 11, 12345};
  TestSwitchToTable(DataType::Type::kInt16, kEntries, /*start_value=*/ 0);
  TestSwitchToTable(DataType::Type::kInt16, kEntries, /*start_value=*/ 1);
}

TEST_F(ControlFlowSimplifierTest, SwitchToTableChar) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kEntries[] = {42, 88, 7, 11, 54321};
  TestSwitchToTable(DataType::Type::kUint16, kEntries, /*start_value=*/ 0);
  TestSwitchToTable(DataType::Type::kUint16, kEntries, /*start_value=*/ 1);
}

TEST_F(ControlFlowSimplifierTest, SwitchToTableInt) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kEntries[] = {42, 88, -7, 11, 123456789};
  TestSwitchToTable(DataType::Type::kInt32, kEntries, /*start_value=*/ 0);
  TestSwitchToTable(DataType::Type::kInt32, kEntries, /*start_value=*/ 1);
}

TEST_F(ControlFlowSimplifierTest, SwitchToTableLong) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int64_t kEntries[] = {42, 88, -7, 11, INT64_C(123456789987654321)};
  TestSwitchToTable(DataType::Type::kInt64, kEntries, /*start_value=*/ 0);
  TestSwitchToTable(DataType::Type::kInt64, kEntries, /*start_value=*/ 1);
}

TEST_F(ControlFlowSimplifierTest, SwitchToTableFloat) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static constexpr float nan = std::numeric_limits<float>::quiet_NaN();
  static const float kEntries[] = {42.0f, 88.0f, -7.0f, 11.0f, 123456789.0f, nan};
  TestSwitchToTable(DataType::Type::kFloat32, kEntries, /*start_value=*/ 0);
  TestSwitchToTable(DataType::Type::kFloat32, kEntries, /*start_value=*/ 1);
}

TEST_F(ControlFlowSimplifierTest, SwitchToTableDouble) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static constexpr double nan = std::numeric_limits<double>::quiet_NaN();
  static const double kEntries[] = {42.0, 88.0, -7.0, 11.0, 123456789.0, nan};
  TestSwitchToTable(DataType::Type::kFloat64, kEntries, /*start_value=*/ 0);
  TestSwitchToTable(DataType::Type::kFloat64, kEntries, /*start_value=*/ 1);
}

TEST_F(ControlFlowSimplifierTest, SwitchReturnToTableBoolean) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kEntries[] = {1, 0, 1, 1, 0, 0, 0, 1, 0, 1};
  static const size_t kReturnBlockIndexes[] = {0, 0, 1, 1, 0, 2, 2, 2, 3, 2};
  TestSwitchToTable(DataType::Type::kBool, kEntries, /*start_value=*/ 0, kReturnBlockIndexes);
  TestSwitchToTable(DataType::Type::kBool, kEntries, /*start_value=*/ 1, kReturnBlockIndexes);
}

TEST_F(ControlFlowSimplifierTest, SwitchReturnToTableByte) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kEntries[] = {42, 88, -7, 11, 123};
  static const size_t kReturnBlockIndexes[] = {0, 0, 0, 0, 0};
  TestSwitchToTable(DataType::Type::kInt8, kEntries, /*start_value=*/ 0, kReturnBlockIndexes);
  TestSwitchToTable(DataType::Type::kInt8, kEntries, /*start_value=*/ 1, kReturnBlockIndexes);
}

TEST_F(ControlFlowSimplifierTest, SwitchReturnToTableUint8) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kEntries[] = {42, 88, 7, 11, 255};
  static const size_t kReturnBlockIndexes[] = {0, 1, 2, 3, 4};
  TestSwitchToTable(DataType::Type::kUint8, kEntries, /*start_value=*/ 0, kReturnBlockIndexes);
  TestSwitchToTable(DataType::Type::kUint8, kEntries, /*start_value=*/ 1, kReturnBlockIndexes);
}

TEST_F(ControlFlowSimplifierTest, SwitchReturnToTableShort) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kEntries[] = {42, 88, -7, 11, 12345};
  static const size_t kReturnBlockIndexes[] = {1, 2, 3, 4, 5};
  TestSwitchToTable(DataType::Type::kInt16, kEntries, /*start_value=*/ 0, kReturnBlockIndexes);
  TestSwitchToTable(DataType::Type::kInt16, kEntries, /*start_value=*/ 1, kReturnBlockIndexes);
}

TEST_F(ControlFlowSimplifierTest, SwitchReturnToTableChar) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kEntries[] = {42, 88, 7, 11, 54321};
  static const size_t kReturnBlockIndexes[] = {1, 2, 3, 3, 3};
  TestSwitchToTable(DataType::Type::kUint16, kEntries, /*start_value=*/ 0, kReturnBlockIndexes);
  TestSwitchToTable(DataType::Type::kUint16, kEntries, /*start_value=*/ 1, kReturnBlockIndexes);
}

TEST_F(ControlFlowSimplifierTest, SwitchReturnToTableInt) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kEntries[] = {42, 88, -7, 11, 123456789};
  static const size_t kReturnBlockIndexes[] = {0, 0, 1, 0, 0};
  TestSwitchToTable(DataType::Type::kInt32, kEntries, /*start_value=*/ 0, kReturnBlockIndexes);
  TestSwitchToTable(DataType::Type::kInt32, kEntries, /*start_value=*/ 1, kReturnBlockIndexes);
}

TEST_F(ControlFlowSimplifierTest, SwitchReturnToTableLong) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int64_t kEntries[] = {42, 88, -7, 11, INT64_C(123456789987654321)};
  static const size_t kReturnBlockIndexes[] = {1, 0, 1, 0, 1};
  TestSwitchToTable(DataType::Type::kInt64, kEntries, /*start_value=*/ 0, kReturnBlockIndexes);
  TestSwitchToTable(DataType::Type::kInt64, kEntries, /*start_value=*/ 1, kReturnBlockIndexes);
}

TEST_F(ControlFlowSimplifierTest, SwitchReturnToTableFloat) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static constexpr float nan = std::numeric_limits<float>::quiet_NaN();
  static const float kEntries[] = {42.0f, 88.0f, -7.0f, 11.0f, 123456789.0f, nan};
  static const size_t kReturnBlockIndexes[] = {1, 0, 0, 0, 0, 0};
  TestSwitchToTable(DataType::Type::kFloat32, kEntries, /*start_value=*/ 0, kReturnBlockIndexes);
  TestSwitchToTable(DataType::Type::kFloat32, kEntries, /*start_value=*/ 1, kReturnBlockIndexes);
}

TEST_F(ControlFlowSimplifierTest, SwitchReturnToTableDouble) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static constexpr double nan = std::numeric_limits<double>::quiet_NaN();
  static const double kEntries[] = {42.0, 88.0, -7.0, 11.0, 123456789.0, nan};
  static const size_t kReturnBlockIndexes[] = {1, 1, 1, 1, 1, 1};
  TestSwitchToTable(DataType::Type::kFloat64, kEntries, /*start_value=*/ 0, kReturnBlockIndexes);
  TestSwitchToTable(DataType::Type::kFloat64, kEntries, /*start_value=*/ 1, kReturnBlockIndexes);
}

template <typename T>
void ControlFlowSimplifierTest::TestSwitchToTableWithDefault(
    DataType::Type type,
    ArrayRef<const T> entries,
    int32_t start_value,
    T dflt,
    ArrayRef<const size_t> return_block_indexes) {
  HBasicBlock* return_block = InitEntryMainExitGraph();
  HInstruction* switch_input = MakeParam(DataType::Type::kInt32);
  HInstruction* default_value = GetConstant(type, dflt);
  HBasicBlock* switch_block =
      CreateSwitchPattern(return_block, entries.size(), switch_input, start_value);
  HPackedSwitch* packed_switch = switch_block->GetLastInstruction()->AsPackedSwitch();
  std::vector<HInstruction*> phi_inputs;
  for (T entry : entries) {
    phi_inputs.push_back(GetConstant(type, entry));
  }
  phi_inputs.push_back(default_value);
  HPhi* phi = MakePhi(return_block, phi_inputs);
  if (return_block_indexes.empty()) {
    MakeInvokeStatic(return_block, DataType::Type::kVoid, {}, {});  // Avoid the return pattern.
  }
  HReturn* ret = MakeReturn(return_block, phi);
  if (!return_block_indexes.empty()) {
    CHECK_EQ(return_block_indexes.size(), entries.size());
    UpdateSwitchWithReturns(switch_block, return_block, return_block_indexes);
  }

  bool success = CheckGraphAndTryControlFlowSimplifier();
  ASSERT_TRUE(success);

  // The packed switch and phi have been replaced by the load from constant table.
  ASSERT_INS_REMOVED(packed_switch);
  ASSERT_INS_REMOVED(phi);
  if (return_block_indexes.empty()) {
    ASSERT_INS_RETAINED(ret);
  } else {
    // Due to the splitting and merging of blocks, the return block may or may not be the
    // original `return_block`. Similar for the return instruction.
    ASSERT_TRUE(switch_block->GetLastInstruction()->IsReturn());
    ret = switch_block->GetLastInstruction()->AsReturn();
  }
  ASSERT_TRUE(ret->InputAt(0)->IsLoadConstantTableEntry());
  HLoadConstantTableEntry* lcte = ret->InputAt(0)->AsLoadConstantTableEntry();
  EXPECT_EQ(type, lcte->GetType());
  std::vector<T> expected_entries(entries.begin(), entries.end());
  expected_entries.insert(
      start_value == 1 ? expected_entries.begin() : expected_entries.end(), dflt);
  ASSERT_TRUE(EntriesMatch(lcte, ArrayRef<const T>(expected_entries)));
  // The `lcte->GetIndex()` must be a `HSelect` between a raw index and default index.
  ASSERT_TRUE(lcte->GetIndex()->IsSelect());
  HSelect* select = lcte->GetIndex()->AsSelect();
  HInstruction* raw_index = select->GetTrueValue();
  HInstruction* default_index = select->GetFalseValue();
  int32_t expected_default_index = start_value == 1 ? 0 : static_cast<int32_t>(entries.size());
  ASSERT_TRUE(default_index->IsIntConstant());
  ASSERT_EQ(expected_default_index, default_index->AsIntConstant()->GetValue());
  // The select condition must be `Below (raw_index, entries.size() + <0 or 1>)`.
  ASSERT_TRUE(select->GetCondition()->IsBelow());
  HBelow* below = select->GetCondition()->AsBelow();
  ASSERT_TRUE(below->GetRight()->IsIntConstant());
  ASSERT_EQ(static_cast<int32_t>(entries.size()) + (start_value == 1 ? 1 : 0),
            below->GetRight()->AsIntConstant()->GetValue());
  ASSERT_INS_EQ(raw_index, below->GetLeft());
  // Check raw index.
  if (start_value != 0 && start_value != 1) {
    ASSERT_TRUE(raw_index->IsAdd());
    ASSERT_INS_EQ(switch_input, raw_index->AsAdd()->GetLeft());
    ASSERT_TRUE(raw_index->AsAdd()->GetRight()->IsIntConstant());
    ASSERT_EQ(-start_value, raw_index->AsAdd()->GetRight()->AsIntConstant()->GetValue());
  } else {
    ASSERT_INS_EQ(switch_input, raw_index);
  }
  // The `return_block` has been merged into `switch_block`.
  ASSERT_BLOCK_REMOVED(return_block);
  ASSERT_TRUE(switch_block->EndsWithReturn());
}

TEST_F(ControlFlowSimplifierTest, SwitchToTableBooleanWithDefault) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kDefault = 0;
  static const int32_t kEntries[] = {1, 0, 1, 1, 0, 0, 0, 1, 0, 1};
  TestSwitchToTableWithDefault(DataType::Type::kBool, kEntries, /*start_value=*/ 0, kDefault);
  TestSwitchToTableWithDefault(DataType::Type::kBool, kEntries, /*start_value=*/ 1, kDefault);
  TestSwitchToTableWithDefault(DataType::Type::kBool, kEntries, /*start_value=*/ 3, kDefault);
}

TEST_F(ControlFlowSimplifierTest, SwitchToTableByteWithDefault) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kDefault = -42;
  static const int32_t kEntries[] = {42, 88, -7, 11, 123};
  TestSwitchToTableWithDefault(DataType::Type::kInt8, kEntries, /*start_value=*/ 0, kDefault);
  TestSwitchToTableWithDefault(DataType::Type::kInt8, kEntries, /*start_value=*/ 1, kDefault);
  TestSwitchToTableWithDefault(DataType::Type::kInt8, kEntries, /*start_value=*/ 3, kDefault);
}

TEST_F(ControlFlowSimplifierTest, SwitchToTableUint8WithDefault) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kDefault = 43;
  static const int32_t kEntries[] = {42, 88, 7, 11, 255};
  TestSwitchToTableWithDefault(DataType::Type::kUint8, kEntries, /*start_value=*/ 0, kDefault);
  TestSwitchToTableWithDefault(DataType::Type::kUint8, kEntries, /*start_value=*/ 1, kDefault);
  TestSwitchToTableWithDefault(DataType::Type::kUint8, kEntries, /*start_value=*/ 3, kDefault);
}

TEST_F(ControlFlowSimplifierTest, SwitchToTableShortWithDefault) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kDefault = -42;
  static const int32_t kEntries[] = {42, 88, -7, 11, 12345};
  TestSwitchToTableWithDefault(DataType::Type::kInt16, kEntries, /*start_value=*/ 0, kDefault);
  TestSwitchToTableWithDefault(DataType::Type::kInt16, kEntries, /*start_value=*/ 1, kDefault);
  TestSwitchToTableWithDefault(DataType::Type::kInt16, kEntries, /*start_value=*/ 3, kDefault);
}

TEST_F(ControlFlowSimplifierTest, SwitchToTableCharWithDefault) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kDefault = 43;
  static const int32_t kEntries[] = {42, 88, 7, 11, 54321};
  TestSwitchToTableWithDefault(DataType::Type::kUint16, kEntries, /*start_value=*/ 0, kDefault);
  TestSwitchToTableWithDefault(DataType::Type::kUint16, kEntries, /*start_value=*/ 1, kDefault);
  TestSwitchToTableWithDefault(DataType::Type::kUint16, kEntries, /*start_value=*/ 3, kDefault);
}

TEST_F(ControlFlowSimplifierTest, SwitchToTableIntWithDefault) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kDefault = -42;
  static const int32_t kEntries[] = {42, 88, -7, 11, 123456789};
  TestSwitchToTableWithDefault(DataType::Type::kInt32, kEntries, /*start_value=*/ 0, kDefault);
  TestSwitchToTableWithDefault(DataType::Type::kInt32, kEntries, /*start_value=*/ 1, kDefault);
  TestSwitchToTableWithDefault(DataType::Type::kInt32, kEntries, /*start_value=*/ 3, kDefault);
}

TEST_F(ControlFlowSimplifierTest, SwitchToTableLongWithDefault) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int64_t kDefault = -42;
  static const int64_t kEntries[] = {42, 88, -7, 11, INT64_C(123456789987654321)};
  TestSwitchToTableWithDefault(DataType::Type::kInt64, kEntries, /*start_value=*/ 0, kDefault);
  TestSwitchToTableWithDefault(DataType::Type::kInt64, kEntries, /*start_value=*/ 1, kDefault);
  TestSwitchToTableWithDefault(DataType::Type::kInt64, kEntries, /*start_value=*/ 2, kDefault);
}

TEST_F(ControlFlowSimplifierTest, SwitchToTableFloatWithDefault) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const float kDefault = -42.0f;
  static constexpr float nan = std::numeric_limits<float>::quiet_NaN();
  static const float kEntries[] = {42.0f, 88.0f, -7.0f, 11.0f, 123456789.0f, nan};
  TestSwitchToTableWithDefault(DataType::Type::kFloat32, kEntries, /*start_value=*/ 0, kDefault);
  TestSwitchToTableWithDefault(DataType::Type::kFloat32, kEntries, /*start_value=*/ 1, kDefault);
  TestSwitchToTableWithDefault(DataType::Type::kFloat32, kEntries, /*start_value=*/ 2, kDefault);
}

TEST_F(ControlFlowSimplifierTest, SwitchToTableDoubleWithDefault) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const double kDefault = -42.0;
  static constexpr double nan = std::numeric_limits<double>::quiet_NaN();
  static const double kEntries[] = {42.0, 88.0, -7.0, 11.0, 123456789.0, nan};
  TestSwitchToTableWithDefault(DataType::Type::kFloat64, kEntries, /*start_value=*/ 0, kDefault);
  TestSwitchToTableWithDefault(DataType::Type::kFloat64, kEntries, /*start_value=*/ 1, kDefault);
  TestSwitchToTableWithDefault(DataType::Type::kFloat64, kEntries, /*start_value=*/ 2, kDefault);
}

TEST_F(ControlFlowSimplifierTest, SwitchReturnToTableBooleanWithDefault) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kDefault = 0;
  static const int32_t kEntries[] = {1, 0, 1, 1, 0, 0, 0, 1, 0, 1};
  static const size_t kReturnBlockIndexes[] = {0, 0, 1, 1, 0, 2, 2, 2, 3, 2};
  TestSwitchToTableWithDefault(
      DataType::Type::kBool, kEntries, /*start_value=*/ 0, kDefault, kReturnBlockIndexes);
  TestSwitchToTableWithDefault(
      DataType::Type::kBool, kEntries, /*start_value=*/ 1, kDefault, kReturnBlockIndexes);
  TestSwitchToTableWithDefault(
      DataType::Type::kBool, kEntries, /*start_value=*/ 3, kDefault, kReturnBlockIndexes);
}

TEST_F(ControlFlowSimplifierTest, SwitchReturnToTableByteWithDefault) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kDefault = -42;
  static const int32_t kEntries[] = {42, 88, -7, 11, 123};
  static const size_t kReturnBlockIndexes[] = {0, 0, 0, 0, 0};
  TestSwitchToTableWithDefault(
      DataType::Type::kInt8, kEntries, /*start_value=*/ 0, kDefault, kReturnBlockIndexes);
  TestSwitchToTableWithDefault(
      DataType::Type::kInt8, kEntries, /*start_value=*/ 1, kDefault, kReturnBlockIndexes);
  TestSwitchToTableWithDefault(
      DataType::Type::kInt8, kEntries, /*start_value=*/ 3, kDefault, kReturnBlockIndexes);
}

TEST_F(ControlFlowSimplifierTest, SwitchReturnToTableUint8WithDefault) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kDefault = 43;
  static const int32_t kEntries[] = {42, 88, 7, 11, 255};
  static const size_t kReturnBlockIndexes[] = {0, 1, 2, 3, 4};
  TestSwitchToTableWithDefault(
      DataType::Type::kUint8, kEntries, /*start_value=*/ 0, kDefault, kReturnBlockIndexes);
  TestSwitchToTableWithDefault(
      DataType::Type::kUint8, kEntries, /*start_value=*/ 1, kDefault, kReturnBlockIndexes);
  TestSwitchToTableWithDefault(
      DataType::Type::kUint8, kEntries, /*start_value=*/ 3, kDefault, kReturnBlockIndexes);
}

TEST_F(ControlFlowSimplifierTest, SwitchReturnToTableShortWithDefault) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kDefault = -42;
  static const int32_t kEntries[] = {42, 88, -7, 11, 12345};
  static const size_t kReturnBlockIndexes[] = {1, 2, 3, 4, 5};
  TestSwitchToTableWithDefault(
      DataType::Type::kInt16, kEntries, /*start_value=*/ 0, kDefault, kReturnBlockIndexes);
  TestSwitchToTableWithDefault(
      DataType::Type::kInt16, kEntries, /*start_value=*/ 1, kDefault, kReturnBlockIndexes);
  TestSwitchToTableWithDefault(
      DataType::Type::kInt16, kEntries, /*start_value=*/ 3, kDefault, kReturnBlockIndexes);
}

TEST_F(ControlFlowSimplifierTest, SwitchReturnToTableCharWithDefault) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kDefault = 43;
  static const int32_t kEntries[] = {42, 88, 7, 11, 54321};
  static const size_t kReturnBlockIndexes[] = {1, 2, 3, 3, 3};
  TestSwitchToTableWithDefault(
      DataType::Type::kUint16, kEntries, /*start_value=*/ 0, kDefault, kReturnBlockIndexes);
  TestSwitchToTableWithDefault(
      DataType::Type::kUint16, kEntries, /*start_value=*/ 1, kDefault, kReturnBlockIndexes);
  TestSwitchToTableWithDefault(
      DataType::Type::kUint16, kEntries, /*start_value=*/ 3, kDefault, kReturnBlockIndexes);
}

TEST_F(ControlFlowSimplifierTest, SwitchReturnToTableIntWithDefault) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int32_t kDefault = -42;
  static const int32_t kEntries[] = {42, 88, -7, 11, 123456789};
  static const size_t kReturnBlockIndexes[] = {0, 0, 1, 0, 0};
  TestSwitchToTableWithDefault(
      DataType::Type::kInt32, kEntries, /*start_value=*/ 0, kDefault, kReturnBlockIndexes);
  TestSwitchToTableWithDefault
      (DataType::Type::kInt32, kEntries, /*start_value=*/ 1, kDefault, kReturnBlockIndexes);
  TestSwitchToTableWithDefault(
      DataType::Type::kInt32, kEntries, /*start_value=*/ 3, kDefault, kReturnBlockIndexes);
}

TEST_F(ControlFlowSimplifierTest, SwitchReturnToTableLongWithDefault) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const int64_t kDefault = -42;
  static const int64_t kEntries[] = {42, 88, -7, 11, INT64_C(123456789987654321)};
  static const size_t kReturnBlockIndexes[] = {1, 0, 1, 0, 1};
  TestSwitchToTableWithDefault(
      DataType::Type::kInt64, kEntries, /*start_value=*/ 0, kDefault, kReturnBlockIndexes);
  TestSwitchToTableWithDefault(
      DataType::Type::kInt64, kEntries, /*start_value=*/ 1, kDefault, kReturnBlockIndexes);
  TestSwitchToTableWithDefault(
      DataType::Type::kInt64, kEntries, /*start_value=*/ 2, kDefault, kReturnBlockIndexes);
}

TEST_F(ControlFlowSimplifierTest, SwitchReturnToTableFloatWithDefault) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const float kDefault = -42.0f;
  static constexpr float nan = std::numeric_limits<float>::quiet_NaN();
  static const float kEntries[] = {42.0f, 88.0f, -7.0f, 11.0f, 123456789.0f, nan};
  static const size_t kReturnBlockIndexes[] = {1, 0, 0, 0, 0, 0};
  TestSwitchToTableWithDefault(
      DataType::Type::kFloat32, kEntries, /*start_value=*/ 0, kDefault, kReturnBlockIndexes);
  TestSwitchToTableWithDefault(
      DataType::Type::kFloat32, kEntries, /*start_value=*/ 1, kDefault, kReturnBlockIndexes);
  TestSwitchToTableWithDefault(
      DataType::Type::kFloat32, kEntries, /*start_value=*/ 2, kDefault, kReturnBlockIndexes);
}

TEST_F(ControlFlowSimplifierTest, SwitchReturnToTableDoubleWithDefault) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const double kDefault = -42.0;
  static constexpr double nan = std::numeric_limits<double>::quiet_NaN();
  static const double kEntries[] = {42.0, 88.0, -7.0, 11.0, 123456789.0, nan};
  static const size_t kReturnBlockIndexes[] = {1, 1, 1, 1, 1, 1};
  TestSwitchToTableWithDefault(
      DataType::Type::kFloat64, kEntries, /*start_value=*/ 0, kDefault, kReturnBlockIndexes);
  TestSwitchToTableWithDefault(
      DataType::Type::kFloat64, kEntries, /*start_value=*/ 1, kDefault, kReturnBlockIndexes);
  TestSwitchToTableWithDefault(
      DataType::Type::kFloat64, kEntries, /*start_value=*/ 2, kDefault, kReturnBlockIndexes);
}

TEST_F(ControlFlowSimplifierTest, SwitchReplacedByIf) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static constexpr size_t kNumEntries = 5;
  static constexpr int32_t kStartValue = 1;
  HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
  HInstruction* switch_input = MakeParam(DataType::Type::kInt32);
  HBasicBlock* switch_block =
      CreateSwitchPattern(return_block, kNumEntries, switch_input, kStartValue);
  HPackedSwitch* packed_switch = switch_block->GetLastInstruction()->AsPackedSwitch();
  MakeInvokeStatic(packed_switch->GetDefaultBlock(), DataType::Type::kVoid, {}, {});

  bool success = CheckGraphAndTryControlFlowSimplifier();
  ASSERT_TRUE(success);

  ASSERT_INS_REMOVED(packed_switch);
  ASSERT_TRUE(switch_block->GetLastInstruction()->IsIf());
  ASSERT_TRUE(switch_block->GetLastInstruction()->AsIf()->InputAt(0)->IsBelow());
  HBelow* below = switch_block->GetLastInstruction()->AsIf()->InputAt(0)->AsBelow();
  ASSERT_TRUE(below->GetRight()->IsIntConstant());
  ASSERT_EQ(static_cast<int32_t>(kNumEntries), below->GetRight()->AsIntConstant()->GetValue());
  ASSERT_TRUE(below->GetLeft()->IsAdd());
  HAdd* add = below->GetLeft()->AsAdd();
  ASSERT_TRUE(add->GetRight()->IsIntConstant());
  ASSERT_EQ(-kStartValue, add->GetRight()->AsIntConstant()->GetValue());
  ASSERT_INS_EQ(switch_input, add->GetLeft());
  ASSERT_BLOCK_RETAINED(return_block);
}

TEST_F(ControlFlowSimplifierTest, SwitchEliminatedWithDefault) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static constexpr size_t kNumEntries = 5;
  static constexpr int32_t kStartValue = 1;
  HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
  HInstruction* switch_input = MakeParam(DataType::Type::kInt32);
  HBasicBlock* switch_block =
      CreateSwitchPattern(return_block, kNumEntries, switch_input, kStartValue);
  HPackedSwitch* packed_switch = switch_block->GetLastInstruction()->AsPackedSwitch();

  bool success = CheckGraphAndTryControlFlowSimplifier();
  ASSERT_TRUE(success);

  ASSERT_INS_REMOVED(packed_switch);
  ASSERT_TRUE(switch_block->GetLastInstruction()->IsReturnVoid());
  ASSERT_BLOCK_REMOVED(return_block);
}

void ControlFlowSimplifierTest::TestReturnMerging(ArrayRef<const size_t> return_block_indexes) {
  HBasicBlock* return_block = InitEntryMainExitGraph();
  HInstruction* switch_input = MakeParam(DataType::Type::kInt32);
  static constexpr int32_t kStartValue = 0;
  std::vector<HInstruction*> phi_inputs;
  for ([[maybe_unused]] size_t i : Range(return_block_indexes.size() + 1u)) {
    phi_inputs.push_back(MakeParam(DataType::Type::kInt32));
  }
  HBasicBlock* switch_block =
      CreateSwitchPattern(return_block, return_block_indexes.size(), switch_input, kStartValue);
  HPhi* phi = MakePhi(return_block, phi_inputs);
  HReturn* ret = MakeReturn(return_block, phi);

  UpdateSwitchWithReturns(switch_block, return_block, return_block_indexes);
  CHECK_NE(phi->InputCount(), phi_inputs.size());

  // Prevent flattening by inserting invokes.
  for (HBasicBlock* block : switch_block->GetSuccessors()) {
    if (block->IsSingleGoto()) {
      HBasicBlock* merge = block->GetSingleSuccessor();
      CHECK(merge->GetLastInstruction()->IsReturn());
      if (merge->GetFirstInstruction() == merge->GetLastInstruction()) {
        MakeInvokeStatic(merge, DataType::Type::kVoid, {}, {});
      } else {
        CHECK(merge->GetFirstInstruction()->IsInvokeStaticOrDirect());
      }
    } else {
      CHECK(block->GetLastInstruction()->IsReturn());
      CHECK(block->GetFirstInstruction() == block->GetLastInstruction());
      MakeInvokeStatic(block, DataType::Type::kVoid, {}, {});
    }
  }

  bool success = CheckGraphAndTryControlFlowSimplifier();
  ASSERT_TRUE(success);

  ASSERT_EQ(exit_block_->GetPredecessors().size(), 1u);
  HBasicBlock* new_return_block = exit_block_->GetPredecessors()[0];
  ASSERT_TRUE(return_block != new_return_block);
  ASSERT_EQ(return_block->GetSuccessors().size(), 1u);
  ASSERT_TRUE(return_block->GetSingleSuccessor() == new_return_block);
  ASSERT_TRUE(new_return_block->GetLastInstruction()->IsReturn());
  ASSERT_TRUE(new_return_block->GetLastInstruction()->AsReturn()->InputAt(0)->IsPhi());
  HPhi* new_phi = new_return_block->GetLastInstruction()->AsReturn()->InputAt(0)->AsPhi();
  size_t max_return_block_index =
      *std::max_element(return_block_indexes.begin(), return_block_indexes.end());
  ASSERT_EQ(new_phi->InputCount(), max_return_block_index + 1u);
}

TEST_F(ControlFlowSimplifierTest, ReturnMerging) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  static const size_t kReturnBlockIndexes1[] = {0, 0, 1, 1, 0, 2, 2, 2, 3, 2};
  TestReturnMerging(ArrayRef<const size_t>(kReturnBlockIndexes1));
  static const size_t kReturnBlockIndexes2[] = {0, 1, 2, 3, 4};
  TestReturnMerging(ArrayRef<const size_t>(kReturnBlockIndexes2));
  static const size_t kReturnBlockIndexes3[] = {1, 2, 3, 4, 5};
  TestReturnMerging(ArrayRef<const size_t>(kReturnBlockIndexes3));
  static const size_t kReturnBlockIndexes4[] = {1, 2, 3, 3, 3};
  TestReturnMerging(ArrayRef<const size_t>(kReturnBlockIndexes4));
  static const size_t kReturnBlockIndexes5[] = {0, 0, 1, 0, 0};
  TestReturnMerging(ArrayRef<const size_t>(kReturnBlockIndexes5));
  static const size_t kReturnBlockIndexes6[] = {1, 0, 1, 0, 1};
  TestReturnMerging(ArrayRef<const size_t>(kReturnBlockIndexes6));
  static const size_t kReturnBlockIndexes7[] = {1, 0, 0, 0, 0, 0};
  TestReturnMerging(ArrayRef<const size_t>(kReturnBlockIndexes7));
  static const size_t kReturnBlockIndexes8[] = {1, 1, 1, 1, 1, 1};
  TestReturnMerging(ArrayRef<const size_t>(kReturnBlockIndexes8));
}

TEST_F(ControlFlowSimplifierTest, FlattenGotoRight) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
  HParameterValue* bool_param1 = MakeParam(DataType::Type::kBool);
  HParameterValue* bool_param2 = MakeParam(DataType::Type::kBool);
  HInstruction* const0 = graph_->GetIntConstant(0);
  HInstruction* const1 = graph_->GetIntConstant(1);
  HInstruction* const2 = graph_->GetIntConstant(2);
  auto [start, left, right_end] = CreateDiamondPattern(return_block, bool_param1);
  auto [right_start, right_left, right_right] = CreateDiamondPattern(right_end, bool_param2);
  HPhi* phi1 = MakePhi(right_end, {const1, const2});
  HPhi* phi2 = MakePhi(return_block, {const0, phi1});  // `phi1` shall be replaced by its inputs.
  HPhi* phi3 = MakePhi(return_block, {const0, const1});  // `const1` shall be duplicated.

  MakeInvokeStatic(right_right, DataType::Type::kVoid, {}, {});  // Prevent `HSelect` generation.

  bool success = CheckGraphAndTryControlFlowSimplifier();
  ASSERT_TRUE(success);

  ASSERT_BLOCK_REMOVED(return_block);
  ASSERT_BLOCK_RETAINED(right_end);
  ASSERT_TRUE(PredecessorsEqual(right_end, {left, right_left, right_right}));
  ASSERT_INS_REMOVED(phi1);
  ASSERT_INS_RETAINED(phi2);
  ASSERT_TRUE(InputsEqual(phi2, {const0, const1, const2}));
  ASSERT_INS_RETAINED(phi3);
  ASSERT_TRUE(InputsEqual(phi3, {const0, const1, const1}));
}

TEST_F(ControlFlowSimplifierTest, FlattenGotoLeft) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
  HParameterValue* bool_param1 = MakeParam(DataType::Type::kBool);
  HParameterValue* bool_param2 = MakeParam(DataType::Type::kBool);
  HInstruction* const0 = graph_->GetIntConstant(0);
  HInstruction* const1 = graph_->GetIntConstant(1);
  HInstruction* const2 = graph_->GetIntConstant(2);
  auto [start, left_end, right] = CreateDiamondPattern(return_block, bool_param1);
  auto [left_start, left_left, left_right] = CreateDiamondPattern(left_end, bool_param2);
  HPhi* phi1 = MakePhi(left_end, {const0, const1});
  HPhi* phi2 = MakePhi(return_block, {phi1, const2});  // `phi1` shall be replaced by its inputs.
  HPhi* phi3 = MakePhi(return_block, {const0, const2});  // `const0` shall be duplicated.

  MakeInvokeStatic(left_right, DataType::Type::kVoid, {}, {});  // Prevent `HSelect` generation.

  bool success = CheckGraphAndTryControlFlowSimplifier();
  ASSERT_TRUE(success);

  ASSERT_BLOCK_REMOVED(return_block);
  ASSERT_BLOCK_RETAINED(left_end);
  ASSERT_TRUE(PredecessorsEqual(left_end, {left_left, left_right, right}));
  ASSERT_INS_REMOVED(phi1);
  ASSERT_INS_RETAINED(phi2);
  ASSERT_TRUE(InputsEqual(phi2, {const0, const1, const2}));
  ASSERT_INS_RETAINED(phi3);
  ASSERT_TRUE(InputsEqual(phi3, {const0, const0, const2}));

  // The initial reverse post order has `right` after `left_end` and these
  // need to be reordered when we flatten the block merging.
  auto&& rpo = graph_->GetReversePostOrder();
  ASSERT_LT(IndexOfElement(rpo, right), IndexOfElement(rpo, left_end));
}

TEST_F(ControlFlowSimplifierTest, IrreducibleLoopFlattenGotoRight) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
  HParameterValue* bool_param1 = MakeParam(DataType::Type::kBool);
  HParameterValue* bool_param2 = MakeParam(DataType::Type::kBool);
  HParameterValue* n_param = MakeParam(DataType::Type::kInt32);
  HInstruction* const0 = graph_->GetIntConstant(0);
  HInstruction* const1 = graph_->GetIntConstant(1);
  HInstruction* const2 = graph_->GetIntConstant(2);
  auto [split, left_header, right_header, body] = CreateIrreducibleLoop(return_block);
  auto [start, left, right_end] = CreateDiamondPattern(body, bool_param1);
  auto [right_start, right_left, right_right] = CreateDiamondPattern(right_end, bool_param2);
  HPhi* phi1 = MakePhi(right_end, {const1, const2});
  HPhi* phi2 = MakePhi(body, {const0, phi1});  // `phi1` shall be replaced by its inputs.
  HPhi* phi3 = MakePhi(body, {const0, const1});  // `const1` shall be duplicated.

  MakeInvokeStatic(right_right, DataType::Type::kVoid, {}, {});  // Prevent `HSelect` generation.

  MakeIf(split, bool_param1);
  auto [left_phi, right_phi, add] =
      MakeLinearIrreducibleLoopVar(left_header, right_header, body, const0, const1, const1);
  HCondition* condition = MakeCondition(left_header, kCondGE, left_phi, n_param);
  MakeIf(left_header, condition);

  bool success = CheckGraphAndTryControlFlowSimplifier();
  ASSERT_TRUE(success);

  ASSERT_BLOCK_REMOVED(body);
  ASSERT_BLOCK_RETAINED(right_end);
  ASSERT_TRUE(PredecessorsEqual(right_end, {left, right_left, right_right}));
  ASSERT_INS_REMOVED(phi1);
  ASSERT_INS_RETAINED(phi2);
  ASSERT_TRUE(InputsEqual(phi2, {const0, const1, const2}));
  ASSERT_INS_RETAINED(phi3);
  ASSERT_TRUE(InputsEqual(phi3, {const0, const1, const1}));
}

TEST_F(ControlFlowSimplifierTest, IrreducibleLoopFlattenGotoNotApplicable) {
  if (!com::android::art::rw::flags::packed_switch_simplification()) {
    GTEST_SKIP() << "packed switch simplification disabled.";
  }
  HBasicBlock* return_block = InitEntryMainExitGraphWithReturnVoid();
  auto [split, left_header, right_header, body] = CreateIrreducibleLoop(return_block);
  HBasicBlock* left_preheader = split->GetSuccessors()[0];
  ASSERT_EQ(left_header, left_preheader->GetSingleSuccessor());
  HBasicBlock* right_preheader = split->GetSuccessors()[1];
  ASSERT_EQ(right_header, right_preheader->GetSingleSuccessor());
  HParameterValue* bool_param1 = MakeParam(DataType::Type::kBool);
  HParameterValue* bool_param2 = MakeParam(DataType::Type::kBool);
  HParameterValue* n_param = MakeParam(DataType::Type::kInt32);
  MakeIf(split, bool_param1);

  // Change each preheader and the body to have two predecessors to test code paths
  // that prevent header merging even if all other conditions are satisfied.
  auto [left_split, left_left, left_right] = CreateDiamondPattern(left_preheader, bool_param2);
  auto [right_split, right_left, right_right] = CreateDiamondPattern(right_preheader, bool_param2);
  auto [body_split, body_left, body_right] = CreateDiamondPattern(body, bool_param2);
  MakeInvokeStatic(left_right, DataType::Type::kVoid, {}, {});  // Prevent `HSelect` generation.
  MakeInvokeStatic(right_right, DataType::Type::kVoid, {}, {});  // Prevent `HSelect` generation.
  MakeInvokeStatic(body_right, DataType::Type::kVoid, {}, {});  // Prevent `HSelect` generation.

  HInstruction* const0 = graph_->GetIntConstant(0);
  HInstruction* const1 = graph_->GetIntConstant(1);
  auto [left_phi, right_phi, add] =
      MakeLinearIrreducibleLoopVar(left_header, right_header, body, const0, const1, const1);
  HCondition* condition = MakeCondition(left_header, kCondGE, left_phi, n_param);
  MakeIf(left_header, condition);

  bool success = CheckGraphAndTryControlFlowSimplifier();
  ASSERT_FALSE(success);

  ASSERT_BLOCK_RETAINED(left_preheader);
  ASSERT_BLOCK_RETAINED(right_preheader);
  ASSERT_BLOCK_RETAINED(left_header);
  ASSERT_BLOCK_RETAINED(right_header);
  ASSERT_BLOCK_RETAINED(body);
}

}  // namespace art
