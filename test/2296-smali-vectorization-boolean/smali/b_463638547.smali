#
# Copyright (C) 2025 The Android Open Source Project
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
.class public LB463638547;
.super Ljava/lang/Object;

.field static a:[Z

.method static testSubInLoop(Z)V
   .registers 4
   const/4 v0, 0x0
:loop
   const/16 v1, 0x80
   if-gt v0, v1, :break
   sget-object v1, LB463638547;->a:[Z
   aget-boolean v2, v1, v0
   sub-int/2addr v2, p0
   aput-boolean v2, v1, v0
   add-int/lit8 v0, v0, 0x1
 goto :loop
:break
  return-void
.end method

.method static testAddInLoop(Z)V
   .registers 4
   const/4 v0, 0x0
:loop
   const/16 v1, 0x80
   if-gt v0, v1, :break
   sget-object v1, LB463638547;->a:[Z
   aget-boolean v2, v1, v0
   add-int/2addr v2, p0
   aput-boolean v2, v1, v0
   add-int/lit8 v0, v0, 0x1
 goto :loop
:break
  return-void
.end method
