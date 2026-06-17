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

.class LMain;
.super Ljava/lang/Object;

.method public static main([Ljava/lang/String;)V
.registers 1
    invoke-static {p0}, LMain;->test([Ljava/lang/String;)V
    return-void
.end method

.method public static test([Ljava/lang/String;)V
.registers 2
    const/4 v0, 0
    monitor-enter v1
    :try_start
    monitor-exit v1
    :try_end
    return-void
    .catch Ljava/lang/Exception; {:try_start .. :try_end} :catch_start
    :catch_start
    invoke-super {v1, v0}, Ljava/lang/StringBuilder;->append(I)Ljava/lang/StringBuilder;
.end method
