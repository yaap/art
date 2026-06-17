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
    invoke-static {v0}, LMain;->packedSwitch(I)V
    return-void
.end method


.method public static packedSwitch(I)V
.registers 90
    const-wide/high16 v85, 0x4000000000000000L
    packed-switch p0, :switch_data
    goto :return

    :switch_data
    .packed-switch 0x0
        :case
    .end packed-switch

    :return
    return-void

    :case
    goto :return

.end method
