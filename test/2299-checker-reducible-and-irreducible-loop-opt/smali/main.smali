#
# Copyright (C) 2026 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

.class public LMain;
.super Ljava/lang/Object;

# Test case to verify loop optimization proceeds on regular loops
# even if the graph contains an irreducible loop.

## CHECK-START: int Main.testRegularAndIrredubleLoop(boolean, boolean) loop_optimization (before)
## CHECK-DAG:     SuspendCheck loop:<<Loop1:B\d+>>
## CHECK-DAG:     SuspendCheck loop:<<Loop2:B\d+>>

## CHECK-START: int Main.testRegularAndIrredubleLoop(boolean, boolean) loop_optimization (after)
## CHECK:         SuspendCheck loop:{{B\d+}}
## CHECK-NOT:     SuspendCheck loop:{{B\d+}}

.method public static testRegularAndIrredubleLoop(ZZ)I
   .registers 4
   # p0: condition for irreducible entry
   # p1: condition inside irreducible loop

   # Irreducible loop
   if-eqz p0, :header

   goto :body

   :header
   if-eqz p1, :body2

   :body
   goto :body_merge

   :body2
   goto :body_merge

   :body_merge
   # Exit condition for irreducible loop
   if-eqz p0, :regular_loop

   :back_edge
   goto :header

   # Regular loop
   # Simple constant trip count loop (0 to 4)
   # Should be fully unrolled/removed.
   :regular_loop
   const/4 v0, 0x0
   const/4 v1, 0x4

   :loop_start
   if-ge v0, v1, :exit

   add-int/lit8 v0, v0, 0x1
   goto :loop_start

   :exit
   return v0
.end method

.method public static main([Ljava/lang/String;)V
   .registers 1
   return-void
.end method
