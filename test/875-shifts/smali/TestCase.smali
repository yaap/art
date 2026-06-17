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

.class public LTestCase;

.super Ljava/lang/Object;

.method public static shiftLeft(JI)V
   .registers 15
   const/4 v11, 0
   goto :back_edge2
   :loop_entry
   shl-long v10, p0, p2
   if-eqz p2, :loop_entry
   return-void
   :back_edge2
   goto :loop_entry
.end method

.method public static shiftRight(JI)V
   .registers 15
   const/4 v11, 0
   goto :back_edge2
   :loop_entry
   shr-long v10, p0, p2
   if-eqz p2, :loop_entry
   return-void
   :back_edge2
   goto :loop_entry
.end method

.method public static unsignedShiftRight(JI)V
   .registers 15
   const/4 v11, 0
   goto :back_edge2
   :loop_entry
   ushr-long v10, p0, p2
   if-eqz p2, :loop_entry
   return-void
   :back_edge2
   goto :loop_entry
.end method
