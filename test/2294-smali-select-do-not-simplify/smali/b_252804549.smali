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
.class public LB454322200;

.super Ljava/lang/Object;

.method public static foo(I)Ljava/lang/String;
   .registers 3
   const-string v1, "str"
   const/4 v0, 0x1
   if-eq p0, v0, :extra_set
   const/16 v0, 0x2
   if-ne p0, v0, :exit
   :extra_set
   # Note it is the same `str`. We didn't use to crash with a different `str`
   const-string v1, "str"
   :exit
   return-object v1
.end method
