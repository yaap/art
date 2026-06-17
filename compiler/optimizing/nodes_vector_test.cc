/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "base/arena_allocator.h"
#include "base/macros.h"
#include "nodes.h"
#include "optimizing_unit_test.h"
#include "code_generator.h"
#include "driver/compiler_options.h"

namespace art HIDDEN {

/**
 * Fixture class for testing vector nodes.
 */
class NodesVectorTest : public OptimizingUnitTest {
 public:
  NodesVectorTest() {
    CreateGraph();
    graph_->SetNumberOfVRegs(1);
    graph_->SetEntryBlock(AddNewBlock());
    AddExitBlock();
    int8_parameter_ = MakeParam(DataType::Type::kInt8);
    int16_parameter_ = MakeParam(DataType::Type::kInt16);
    int32_parameter_ = MakeParam(DataType::Type::kInt32);
  }

  ~NodesVectorTest() { }

  // General building fields.
  HInstruction* int8_parameter_;
  HInstruction* int16_parameter_;
  HInstruction* int32_parameter_;
};

//
// The actual vector nodes tests.
//

TEST(NodesVector, Alignment) {
  EXPECT_TRUE(Alignment(1, 0).IsAlignedAt(1));
  EXPECT_FALSE(Alignment(1, 0).IsAlignedAt(2));

  EXPECT_TRUE(Alignment(2, 0).IsAlignedAt(1));
  EXPECT_TRUE(Alignment(2, 1).IsAlignedAt(1));
  EXPECT_TRUE(Alignment(2, 0).IsAlignedAt(2));
  EXPECT_FALSE(Alignment(2, 1).IsAlignedAt(2));
  EXPECT_FALSE(Alignment(2, 0).IsAlignedAt(4));
  EXPECT_FALSE(Alignment(2, 1).IsAlignedAt(4));

  EXPECT_TRUE(Alignment(4, 0).IsAlignedAt(1));
  EXPECT_TRUE(Alignment(4, 2).IsAlignedAt(1));
  EXPECT_TRUE(Alignment(4, 0).IsAlignedAt(2));
  EXPECT_TRUE(Alignment(4, 2).IsAlignedAt(2));
  EXPECT_TRUE(Alignment(4, 0).IsAlignedAt(4));
  EXPECT_FALSE(Alignment(4, 2).IsAlignedAt(4));
  EXPECT_FALSE(Alignment(4, 0).IsAlignedAt(8));
  EXPECT_FALSE(Alignment(4, 2).IsAlignedAt(8));

  EXPECT_TRUE(Alignment(16, 0).IsAlignedAt(1));
  EXPECT_TRUE(Alignment(16, 0).IsAlignedAt(2));
  EXPECT_TRUE(Alignment(16, 0).IsAlignedAt(4));
  EXPECT_TRUE(Alignment(16, 8).IsAlignedAt(8));
  EXPECT_TRUE(Alignment(16, 0).IsAlignedAt(16));
  EXPECT_FALSE(Alignment(16, 1).IsAlignedAt(16));
  EXPECT_FALSE(Alignment(16, 7).IsAlignedAt(16));
  EXPECT_FALSE(Alignment(16, 0).IsAlignedAt(32));

  EXPECT_EQ(16u, Alignment(16, 0).Base());
  EXPECT_EQ(0u, Alignment(16, 0).Offset());
  EXPECT_EQ(4u, Alignment(16, 4).Offset());
}

TEST(NodesVector, AlignmentEQ) {
  EXPECT_TRUE(Alignment(2, 0) == Alignment(2, 0));
  EXPECT_TRUE(Alignment(2, 1) == Alignment(2, 1));
  EXPECT_TRUE(Alignment(4, 0) == Alignment(4, 0));
  EXPECT_TRUE(Alignment(4, 2) == Alignment(4, 2));

  EXPECT_FALSE(Alignment(4, 0) == Alignment(2, 0));
  EXPECT_FALSE(Alignment(4, 0) == Alignment(4, 1));
  EXPECT_FALSE(Alignment(4, 0) == Alignment(8, 0));
}

TEST(NodesVector, AlignmentString) {
  EXPECT_STREQ("ALIGN(1,0)", Alignment(1, 0).ToString().c_str());

  EXPECT_STREQ("ALIGN(2,0)", Alignment(2, 0).ToString().c_str());
  EXPECT_STREQ("ALIGN(2,1)", Alignment(2, 1).ToString().c_str());

  EXPECT_STREQ("ALIGN(16,0)", Alignment(16, 0).ToString().c_str());
  EXPECT_STREQ("ALIGN(16,1)", Alignment(16, 1).ToString().c_str());
  EXPECT_STREQ("ALIGN(16,8)", Alignment(16, 8).ToString().c_str());
  EXPECT_STREQ("ALIGN(16,9)", Alignment(16, 9).ToString().c_str());
}

TEST_F(NodesVectorTest, VectorOperationProperties) {
  HVecOperation* v0 = new (GetAllocator())
      HVecReplicateScalar(GetAllocator(), int32_parameter_, DataType::Type::kInt32, 4, kNoDexPc);
  HVecOperation* v1 = new (GetAllocator())
      HVecReplicateScalar(GetAllocator(), int32_parameter_, DataType::Type::kInt32, 4, kNoDexPc);
  HVecOperation* v2 = new (GetAllocator())
      HVecReplicateScalar(GetAllocator(), int32_parameter_, DataType::Type::kInt32, 2, kNoDexPc);
  HVecOperation* v3 = new (GetAllocator())
      HVecReplicateScalar(GetAllocator(), int32_parameter_, DataType::Type::kInt16, 4, kNoDexPc);
  HVecOperation* v4 = new (GetAllocator()) HVecStore(
      GetAllocator(),
      int32_parameter_,
      int32_parameter_,
      v0,
      DataType::Type::kInt32,
      SideEffects::ArrayWriteOfType(DataType::Type::kInt32),
      4,
      kNoDexPc);

  EXPECT_TRUE(v0->Equals(v0));
  EXPECT_TRUE(v1->Equals(v1));
  EXPECT_TRUE(v2->Equals(v2));
  EXPECT_TRUE(v3->Equals(v3));
  EXPECT_TRUE(v4->Equals(v4));

  EXPECT_TRUE(v0->Equals(v1));
  EXPECT_FALSE(v0->Equals(v2));  // different vector lengths
  EXPECT_FALSE(v0->Equals(v3));  // different packed types
  EXPECT_FALSE(v0->Equals(v4));  // different kinds

  EXPECT_TRUE(v1->Equals(v0));  // switch operands
  EXPECT_FALSE(v4->Equals(v0));

  EXPECT_EQ(4u, v0->GetVectorLength());
  EXPECT_EQ(4u, v1->GetVectorLength());
  EXPECT_EQ(2u, v2->GetVectorLength());
  EXPECT_EQ(4u, v3->GetVectorLength());
  EXPECT_EQ(4u, v4->GetVectorLength());

  EXPECT_EQ(DataType::Type::kFloat64, v0->GetType());
  EXPECT_EQ(DataType::Type::kFloat64, v1->GetType());
  EXPECT_EQ(DataType::Type::kFloat64, v2->GetType());
  EXPECT_EQ(DataType::Type::kFloat64, v3->GetType());
  EXPECT_EQ(DataType::Type::kFloat64, v4->GetType());

  EXPECT_EQ(DataType::Type::kInt32, v0->GetPackedType());
  EXPECT_EQ(DataType::Type::kInt32, v1->GetPackedType());
  EXPECT_EQ(DataType::Type::kInt32, v2->GetPackedType());
  EXPECT_EQ(DataType::Type::kInt16, v3->GetPackedType());
  EXPECT_EQ(DataType::Type::kInt32, v4->GetPackedType());

  EXPECT_EQ(16u, v0->GetVectorNumberOfBytes());
  EXPECT_EQ(16u, v1->GetVectorNumberOfBytes());
  EXPECT_EQ(8u, v2->GetVectorNumberOfBytes());
  EXPECT_EQ(8u, v3->GetVectorNumberOfBytes());
  EXPECT_EQ(16u, v4->GetVectorNumberOfBytes());

  EXPECT_FALSE(v0->CanBeMoved());
  EXPECT_FALSE(v1->CanBeMoved());
  EXPECT_FALSE(v2->CanBeMoved());
  EXPECT_FALSE(v3->CanBeMoved());
  EXPECT_FALSE(v4->CanBeMoved());
}

TEST_F(NodesVectorTest, VectorAlignmentAndStringCharAtMatterOnLoad) {
  HVecLoad* v0 = new (GetAllocator()) HVecLoad(GetAllocator(),
                                               int32_parameter_,
                                               int32_parameter_,
                                               DataType::Type::kInt32,
                                               SideEffects::ArrayReadOfType(DataType::Type::kInt32),
                                               4,
                                               /*is_string_char_at*/ false,
                                               kNoDexPc);
  HVecLoad* v1 = new (GetAllocator()) HVecLoad(GetAllocator(),
                                               int32_parameter_,
                                               int32_parameter_,
                                               DataType::Type::kInt32,
                                               SideEffects::ArrayReadOfType(DataType::Type::kInt32),
                                               4,
                                               /*is_string_char_at*/ false,
                                               kNoDexPc);
  HVecLoad* v2 = new (GetAllocator()) HVecLoad(GetAllocator(),
                                               int32_parameter_,
                                               int32_parameter_,
                                               DataType::Type::kInt32,
                                               SideEffects::ArrayReadOfType(DataType::Type::kInt32),
                                                4,
                                               /*is_string_char_at*/ true,
                                               kNoDexPc);

  EXPECT_TRUE(v0->CanBeMoved());
  EXPECT_TRUE(v1->CanBeMoved());
  EXPECT_TRUE(v2->CanBeMoved());

  EXPECT_FALSE(v0->IsStringCharAt());
  EXPECT_FALSE(v1->IsStringCharAt());
  EXPECT_TRUE(v2->IsStringCharAt());

  EXPECT_TRUE(v0->Equals(v0));
  EXPECT_TRUE(v1->Equals(v1));
  EXPECT_TRUE(v2->Equals(v2));

  EXPECT_TRUE(v0->Equals(v1));
  EXPECT_FALSE(v0->Equals(v2));  // different is_string_char_at

  EXPECT_TRUE(v0->GetAlignment() == Alignment(4, 0));
  EXPECT_TRUE(v1->GetAlignment() == Alignment(4, 0));
  EXPECT_TRUE(v2->GetAlignment() == Alignment(4, 0));

  v1->SetAlignment(Alignment(8, 0));

  EXPECT_TRUE(v1->GetAlignment() == Alignment(8, 0));

  EXPECT_FALSE(v0->Equals(v1));  // no longer equal
}

TEST_F(NodesVectorTest, VectorAlignmentMattersOnStore) {
  HVecOperation* p0 = new (GetAllocator())
      HVecReplicateScalar(GetAllocator(), int32_parameter_, DataType::Type::kInt32, 4, kNoDexPc);
  HVecStore* v0 = new (GetAllocator()) HVecStore(
      GetAllocator(),
      int32_parameter_,
      int32_parameter_,
      p0,
      DataType::Type::kInt32,
      SideEffects::ArrayWriteOfType(DataType::Type::kInt32),
      4,
      kNoDexPc);
  HVecStore* v1 = new (GetAllocator()) HVecStore(
      GetAllocator(),
      int32_parameter_,
      int32_parameter_,
      p0,
      DataType::Type::kInt32,
      SideEffects::ArrayWriteOfType(DataType::Type::kInt32),
      4,
      kNoDexPc);

  EXPECT_FALSE(v0->CanBeMoved());
  EXPECT_FALSE(v1->CanBeMoved());

  EXPECT_TRUE(v0->Equals(v1));

  EXPECT_TRUE(v0->GetAlignment() == Alignment(4, 0));
  EXPECT_TRUE(v1->GetAlignment() == Alignment(4, 0));

  v1->SetAlignment(Alignment(8, 0));

  EXPECT_TRUE(v1->GetAlignment() == Alignment(8, 0));

  EXPECT_FALSE(v0->Equals(v1));  // no longer equal
}

TEST_F(NodesVectorTest, VectorAttributesMatterOnHalvingAdd) {
  HVecOperation* u0 = new (GetAllocator())
      HVecReplicateScalar(GetAllocator(), int32_parameter_, DataType::Type::kUint32, 4, kNoDexPc);
  HVecOperation* u1 = new (GetAllocator())
      HVecReplicateScalar(GetAllocator(), int16_parameter_, DataType::Type::kUint16, 8, kNoDexPc);
  HVecOperation* u2 = new (GetAllocator())
      HVecReplicateScalar(GetAllocator(), int8_parameter_, DataType::Type::kUint8, 16, kNoDexPc);

  HVecOperation* p0 = new (GetAllocator())
      HVecReplicateScalar(GetAllocator(), int32_parameter_, DataType::Type::kInt32, 4, kNoDexPc);
  HVecOperation* p1 = new (GetAllocator())
      HVecReplicateScalar(GetAllocator(), int16_parameter_, DataType::Type::kInt16, 8, kNoDexPc);
  HVecOperation* p2 = new (GetAllocator())
      HVecReplicateScalar(GetAllocator(), int8_parameter_, DataType::Type::kInt8, 16, kNoDexPc);

  HVecHalvingAdd* v0 = new (GetAllocator()) HVecHalvingAdd(
      GetAllocator(), u0, u0, DataType::Type::kUint32, 4, /*is_rounded*/ true, kNoDexPc);
  HVecHalvingAdd* v1 = new (GetAllocator()) HVecHalvingAdd(
      GetAllocator(), u0, u0, DataType::Type::kUint32, 4, /*is_rounded*/ false, kNoDexPc);
  HVecHalvingAdd* v2 = new (GetAllocator()) HVecHalvingAdd(
      GetAllocator(), p0, p0, DataType::Type::kInt32, 4, /*is_rounded*/ true, kNoDexPc);
  HVecHalvingAdd* v3 = new (GetAllocator()) HVecHalvingAdd(
      GetAllocator(), p0, p0, DataType::Type::kInt32, 4, /*is_rounded*/ false, kNoDexPc);

  HVecHalvingAdd* v4 = new (GetAllocator()) HVecHalvingAdd(
      GetAllocator(), u1, u1, DataType::Type::kUint16, 8, /*is_rounded*/ true, kNoDexPc);
  HVecHalvingAdd* v5 = new (GetAllocator()) HVecHalvingAdd(
      GetAllocator(), u1, u1, DataType::Type::kUint16, 8, /*is_rounded*/ false, kNoDexPc);
  HVecHalvingAdd* v6 = new (GetAllocator()) HVecHalvingAdd(
      GetAllocator(), p1, p1, DataType::Type::kInt16, 8, /*is_rounded*/ true, kNoDexPc);
  HVecHalvingAdd* v7 = new (GetAllocator()) HVecHalvingAdd(
      GetAllocator(), p1, p1, DataType::Type::kInt16, 8, /*is_rounded*/ false, kNoDexPc);

  HVecHalvingAdd* v8 = new (GetAllocator()) HVecHalvingAdd(
      GetAllocator(), u2, u2, DataType::Type::kUint8, 16, /*is_rounded*/ true, kNoDexPc);
  HVecHalvingAdd* v9 = new (GetAllocator()) HVecHalvingAdd(
      GetAllocator(), u2, u2, DataType::Type::kUint8, 16, /*is_rounded*/ false, kNoDexPc);
  HVecHalvingAdd* v10 = new (GetAllocator()) HVecHalvingAdd(
      GetAllocator(), p2, p2, DataType::Type::kInt8, 16, /*is_rounded*/ true, kNoDexPc);
  HVecHalvingAdd* v11 = new (GetAllocator()) HVecHalvingAdd(
      GetAllocator(), p2, p2, DataType::Type::kInt8, 16, /*is_rounded*/ false, kNoDexPc);

  HVecHalvingAdd* hadd_insns[] = { v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11 };

  EXPECT_FALSE(u0->CanBeMoved());
  EXPECT_FALSE(u1->CanBeMoved());
  EXPECT_FALSE(u2->CanBeMoved());
  EXPECT_FALSE(p0->CanBeMoved());
  EXPECT_FALSE(p1->CanBeMoved());
  EXPECT_FALSE(p2->CanBeMoved());

  for (HVecHalvingAdd* hadd_insn : hadd_insns) {
    EXPECT_TRUE(hadd_insn->CanBeMoved());
  }

  EXPECT_TRUE(v0->IsRounded());
  EXPECT_TRUE(!v1->IsRounded());
  EXPECT_TRUE(v2->IsRounded());
  EXPECT_TRUE(!v3->IsRounded());
  EXPECT_TRUE(v4->IsRounded());
  EXPECT_TRUE(!v5->IsRounded());
  EXPECT_TRUE(v6->IsRounded());
  EXPECT_TRUE(!v7->IsRounded());
  EXPECT_TRUE(v8->IsRounded());
  EXPECT_TRUE(!v9->IsRounded());
  EXPECT_TRUE(v10->IsRounded());
  EXPECT_TRUE(!v11->IsRounded());

  for (HVecHalvingAdd* hadd_insn1 : hadd_insns) {
    for (HVecHalvingAdd* hadd_insn2 : hadd_insns) {
      EXPECT_EQ(hadd_insn1 == hadd_insn2, hadd_insn1->Equals(hadd_insn2));
    }
  }
}

TEST_F(NodesVectorTest, VectorOperationMattersOnMultiplyAccumulate) {
  HVecOperation* v0 = new (GetAllocator())
      HVecReplicateScalar(GetAllocator(), int32_parameter_, DataType::Type::kInt32, 4, kNoDexPc);

  HVecMultiplyAccumulate* v1 = new (GetAllocator()) HVecMultiplyAccumulate(
      GetAllocator(), HInstruction::kAdd, v0, v0, v0, DataType::Type::kInt32, 4, kNoDexPc);
  HVecMultiplyAccumulate* v2 = new (GetAllocator()) HVecMultiplyAccumulate(
      GetAllocator(), HInstruction::kSub, v0, v0, v0, DataType::Type::kInt32, 4, kNoDexPc);
  HVecMultiplyAccumulate* v3 = new (GetAllocator()) HVecMultiplyAccumulate(
      GetAllocator(), HInstruction::kAdd, v0, v0, v0, DataType::Type::kInt32, 2, kNoDexPc);

  EXPECT_FALSE(v0->CanBeMoved());
  EXPECT_TRUE(v1->CanBeMoved());
  EXPECT_TRUE(v2->CanBeMoved());
  EXPECT_TRUE(v3->CanBeMoved());

  EXPECT_EQ(HInstruction::kAdd, v1->GetOpKind());
  EXPECT_EQ(HInstruction::kSub, v2->GetOpKind());
  EXPECT_EQ(HInstruction::kAdd, v3->GetOpKind());

  EXPECT_TRUE(v1->Equals(v1));
  EXPECT_TRUE(v2->Equals(v2));
  EXPECT_TRUE(v3->Equals(v3));

  EXPECT_FALSE(v1->Equals(v2));  // different operators
  EXPECT_FALSE(v1->Equals(v3));  // different vector lengths
}

TEST_F(NodesVectorTest, VectorKindMattersOnReduce) {
  HVecOperation* v0 = new (GetAllocator())
      HVecReplicateScalar(GetAllocator(), int32_parameter_, DataType::Type::kInt32, 4, kNoDexPc);

  HVecReduce* v1 = new (GetAllocator()) HVecReduce(
      GetAllocator(), v0, DataType::Type::kInt32, 4, HVecReduce::kSum, kNoDexPc);
  HVecReduce* v2 = new (GetAllocator()) HVecReduce(
      GetAllocator(), v0, DataType::Type::kInt32, 4, HVecReduce::kMin, kNoDexPc);
  HVecReduce* v3 = new (GetAllocator()) HVecReduce(
      GetAllocator(), v0, DataType::Type::kInt32, 4, HVecReduce::kMax, kNoDexPc);

  EXPECT_FALSE(v0->CanBeMoved());
  EXPECT_TRUE(v1->CanBeMoved());
  EXPECT_TRUE(v2->CanBeMoved());
  EXPECT_TRUE(v3->CanBeMoved());

  EXPECT_EQ(HVecReduce::kSum, v1->GetReductionKind());
  EXPECT_EQ(HVecReduce::kMin, v2->GetReductionKind());
  EXPECT_EQ(HVecReduce::kMax, v3->GetReductionKind());

  EXPECT_TRUE(v1->Equals(v1));
  EXPECT_TRUE(v2->Equals(v2));
  EXPECT_TRUE(v3->Equals(v3));

  EXPECT_FALSE(v1->Equals(v2));  // different kinds
  EXPECT_FALSE(v1->Equals(v3));
}

TEST_F(NodesVectorTest, IgnoreVecLenForLocationEquals) {
  auto loc0 = Location::FpuRegister(0);
  auto loc0_16 = Location::FpuRegister(0, 16);
  auto loc0_32 = Location::FpuRegister(0, 32);
  auto loc1 = Location::FpuRegister(1);
  auto loc1_32 = Location::FpuRegister(1, 32);

  EXPECT_TRUE(loc0.Equals(loc0_16));
  EXPECT_TRUE(loc0.Equals(loc0_32));
  EXPECT_FALSE(loc0.Equals(loc1));
  EXPECT_FALSE(loc0.Equals(loc1_32));
}

TEST_F(NodesVectorTest, OverlappingFPVecRegisterAllocation) {
  std::unique_ptr<CompilerOptions> compiler_options =
      CommonCompilerTest::CreateCompilerOptions(kRuntimeISA, "default");
  CHECK(compiler_options != nullptr);
  std::unique_ptr<CodeGenerator> codegen = CodeGenerator::Create(graph_, *compiler_options);
  CHECK(codegen != nullptr);
  if (!codegen->HasOverlappingFPVecRegisters()) {
    GTEST_SKIP() << "Codegen does not have overlapping FP Vec registers";
  }

  HInstruction* array_ref = MakeParam(DataType::Type::kReference);
  HInstruction* index0 = MakeParam(DataType::Type::kInt32);

  HBasicBlock* main_block = AddNewBlock();
  graph_->GetEntryBlock()->AddSuccessor(main_block);
  main_block->AddSuccessor(graph_->GetExitBlock());

  // 1. Load 256 bits into a vector register (256-bit register)
  HVecLoad* load_256 =
      new (GetAllocator()) HVecLoad(GetAllocator(),
                                    array_ref,
                                    index0,
                                    DataType::Type::kFloat64,
                                    SideEffects::ArrayReadOfType(DataType::Type::kFloat64),
                                    /*vector_length=*/4,
                                    /*is_string_char_at=*/false,
                                    kNoDexPc);
  // 2. Store the lower 128 bits of the 256-bit register.
  // The vector length is explicitly different, to verify that
  // Equals(Location(FpuRegister(R, 32)), Location(FpuRegister(R, 16))) is true
  HVecStore* store_128 =
      new (GetAllocator()) HVecStore(GetAllocator(),
                                     array_ref,
                                     index0,
                                     load_256,
                                     DataType::Type::kFloat64,
                                     SideEffects::ArrayWriteOfType(DataType::Type::kFloat64),
                                     /*vector_length=*/2,
                                     kNoDexPc);
  // 3. 256-bit Add: Perform a 256-bit addition using the result of the original load_256.
  HVecAdd* add_256 = new (GetAllocator()) HVecAdd(GetAllocator(),
                                                  load_256,
                                                  load_256,
                                                  DataType::Type::kFloat64,
                                                  /*vector_length=*/4,
                                                  kNoDexPc);
  // 4. Store the 256-bit result.
  HVecStore* store_256 =
      new (GetAllocator()) HVecStore(GetAllocator(),
                                     array_ref,
                                     index0,
                                     add_256,
                                     DataType::Type::kFloat64,
                                     SideEffects::ArrayWriteOfType(DataType::Type::kFloat64),
                                     /*vector_length=*/4,
                                     kNoDexPc);
  // 5. Load 128 bits into a vector register (128-bit register)
  HVecLoad* load_128 =
      new (GetAllocator()) HVecLoad(GetAllocator(),
                                    array_ref,
                                    index0,
                                    DataType::Type::kFloat64,
                                    SideEffects::ArrayReadOfType(DataType::Type::kFloat64),
                                    /*vector_length=*/2,
                                    /*is_string_char_at=*/false,
                                    kNoDexPc);

  // Add all instructions to main block
  main_block->AddInstruction(load_256);
  main_block->AddInstruction(store_128);
  main_block->AddInstruction(add_256);
  main_block->AddInstruction(store_256);
  main_block->AddInstruction(load_128);

  // Do register allocation
  GraphAnalysisResult result = graph_->BuildDominatorTree();
  ASSERT_EQ(result, kAnalysisSuccess);
  SsaLivenessAnalysis liveness(graph_, codegen.get(), GetScopedAllocator());
  liveness.Analyze();
  std::unique_ptr<RegisterAllocator> register_allocator =
      RegisterAllocator::Create(GetScopedAllocator(), codegen.get(), liveness);
  register_allocator->AllocateRegisters();
  ASSERT_TRUE(register_allocator->Validate(false));

  // Check for the allocation of FpuVec registers with vector length
  Location load_256_loc = load_256->GetLocations()->Out();
  Location store_128_in_loc = store_128->GetLocations()->InAt(2);
  Location add_256_loc = add_256->GetLocations()->Out();
  Location store_256_in_loc = store_256->GetLocations()->InAt(2);
  Location load_128_loc = load_128->GetLocations()->Out();

  EXPECT_TRUE(load_256_loc.IsFpuVecRegister());
  EXPECT_EQ(load_256_loc.GetVecLen(), 32U);
  EXPECT_TRUE(store_128_in_loc.IsFpuVecRegister());
  EXPECT_EQ(store_128_in_loc.GetVecLen(), 32U);
  // Ensure no parallel moves were introduced
  EXPECT_TRUE(load_256_loc.Equals(store_128_in_loc));
  EXPECT_TRUE(add_256_loc.IsFpuVecRegister());
  EXPECT_EQ(add_256_loc.GetVecLen(), 32U);
  EXPECT_TRUE(store_256_in_loc.IsFpuVecRegister());
  EXPECT_EQ(store_256_in_loc.GetVecLen(), 32U);
  EXPECT_TRUE(store_256_in_loc.Equals(add_256_loc));
  // Verify correct vector length
  EXPECT_TRUE(load_128_loc.IsFpuVecRegister());
  EXPECT_EQ(load_128_loc.GetVecLen(), 16U);
}

}  // namespace art
