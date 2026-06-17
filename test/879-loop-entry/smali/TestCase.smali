# Copyright (C) 2026 The Android Open Source Project
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
#

.class public final LTestCase;
.super Ljava/lang/Object;

.method public constructor <init>()V
  .registers 1
  invoke-direct {p0}, Ljava/lang/Object;-><init>()V
  return-void
.end method

.method public static test(F)F
  .registers 5
:entry
  add-float/2addr p0, p0
  float-to-double v0, p0
  const-wide/high16 v2, 4621819117588971520L
  cmpl-double v0, v0, v2
  if-lez v0, :entry
  return p0
.end method
