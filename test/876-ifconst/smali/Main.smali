# Copyright 2025 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

.class public LMain;
.super Ljava/lang/Object;

.method public static main([Ljava/lang/String;)V
.registers 1
    const/4 v0, 0x0
    invoke-static {v0}, LMain;->testCase(I)I
    return-void
.end method


.method public static testCase(I)I
.registers 3
    const v1, 2
    const v0, 1
    if-eqz v0, :unreachable
    const-wide/16 v0, 0
    return p0
    :unreachable
    add-int v1, v1, v1
    return v1
.end method
