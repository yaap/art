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

.class public abstract LB477529788;
.super Ljava/lang/Object;

# Method marked as abstract so that it is not pattern matched and we try to inline it.
.method public abstract foo()V
.end method

.method public final main()V
    .registers 1
    const v0, 0x0
    :loop
    # This would throw NPE
    invoke-virtual {v0}, LB477529788;->foo()V
    check-cast v0, LB477529788;
    if-eq v0, v0, :loop
    return-void
.end method
