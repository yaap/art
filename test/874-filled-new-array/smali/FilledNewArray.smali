# /*
#  * Copyright 2025 The Android Open Source Project
#  *
#  * Licensed under the Apache License, Version 2.0 (the "License");
#  * you may not use this file except in compliance with the License.
#  * You may obtain a copy of the License at
#  *
#  *      http://www.apache.org/licenses/LICENSE-2.0
#  *
#  * Unless required by applicable law or agreed to in writing, software
#  * distributed under the License is distributed on an "AS IS" BASIS,
#  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  * See the License for the specific language governing permissions and
#  * limitations under the License.
#  */

.class public LFilledNewArray;

.super Ljava/lang/Object;

.method public static newInt(III)[I
   .registers 10
   const/4 v3, 0
   :loop_entry
   const/4 v3, 2
   filled-new-array {p0, p1, p2}, [I
   move-result-object v0
   if-eqz v0, :loop_entry
   return-object v0
.end method

.method public static newIntRange(III)[I
   .registers 10
   const/4 v3, 0
   :loop_entry
   const/4 v3, 2
   filled-new-array/range {p0 .. p2}, [I
   move-result-object v0
   if-eqz v0, :loop_entry
   return-object v0
.end method
