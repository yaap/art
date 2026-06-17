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

.class public LTest;
.super Ljava/lang/Object;

## CHECK-START: java.lang.String Test.testRelease(java.lang.String) instruction_simplifier (before)
## CHECK:      NewInstance
## CHECK:      InvokeStaticOrDirect
## CHECK:      InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK:      InvokeVirtual intrinsic:StringBuilderToString

## CHECK-START: java.lang.String Test.testRelease(java.lang.String) instruction_simplifier (after)
## CHECK:      StringBuilderAppend
## CHECK-NOT:  NewInstance
## CHECK-NOT:  InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK-NOT:  InvokeVirtual intrinsic:StringBuilderToString

.method public static testRelease(Ljava/lang/String;)Ljava/lang/String;
    .registers 3
    const-string v1, "Hello "
    new-instance v0, Ljava/lang/StringBuilder;
    invoke-direct {v0, v1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V
    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;
    move-result-object v0
    return-object v0
.end method

## CHECK-START: java.lang.String Test.testDebug(java.lang.String) instruction_simplifier (before)
## CHECK:      NewInstance
## CHECK:      InvokeStaticOrDirect
## CHECK:      InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK:      InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK:      InvokeVirtual intrinsic:StringBuilderToString

## CHECK-START: java.lang.String Test.testDebug(java.lang.String) instruction_simplifier (after)
## CHECK:      StringBuilderAppend
## CHECK-NOT:  NewInstance
## CHECK-NOT:  InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK-NOT:  InvokeVirtual intrinsic:StringBuilderToString

.method public static testDebug(Ljava/lang/String;)Ljava/lang/String;
    .registers 3
    const-string v1, "Hello "
    new-instance v0, Ljava/lang/StringBuilder;
    invoke-direct {v0}, Ljava/lang/StringBuilder;-><init>()V
    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;
    move-result-object v0
    return-object v0
.end method

## CHECK-START: java.lang.String Test.testEscape(java.lang.String) instruction_simplifier (before)
## CHECK:      NewInstance
## CHECK:      InvokeStaticOrDirect
## CHECK:      InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK:      InvokeStaticOrDirect method_name:Test.$noinline$consume
## CHECK:      InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK:      InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK:      InvokeVirtual intrinsic:StringBuilderToString

## CHECK-START: java.lang.String Test.testEscape(java.lang.String) instruction_simplifier (after)
## CHECK:      NewInstance
## CHECK:      InvokeStaticOrDirect
## CHECK:      InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK:      InvokeStaticOrDirect method_name:Test.$noinline$consume
## CHECK:      InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK:      InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK:      InvokeVirtual intrinsic:StringBuilderToString

.method public static testEscape(Ljava/lang/String;)Ljava/lang/String;
    .registers 3
    new-instance v0, Ljava/lang/StringBuilder;
    const-string v1, "Start"
    invoke-direct {v0, v1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V
    const-string v1, "Middle"
    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    invoke-static {v0}, LTest;->$noinline$consume(Ljava/lang/StringBuilder;)V
    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    const-string v1, "End"
    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;
    move-result-object v0
    return-object v0
.end method

.method public static $noinline$consume(Ljava/lang/StringBuilder;)V
    .registers 1
    return-void
.end method

## CHECK-START: java.lang.String Test.testStringArgument(java.lang.String, java.lang.String) instruction_simplifier (before)
## CHECK:      NewInstance
## CHECK:      InvokeStaticOrDirect
## CHECK:      InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK:      InvokeVirtual intrinsic:StringBuilderToString

## CHECK-START: java.lang.String Test.testStringArgument(java.lang.String, java.lang.String) instruction_simplifier (after)
## CHECK:      NewInstance
## CHECK:      InvokeStaticOrDirect
## CHECK:      InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK:      InvokeVirtual intrinsic:StringBuilderToString

.method public static testStringArgument(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;
    .registers 3
    new-instance v0, Ljava/lang/StringBuilder;
    invoke-direct {v0, p0}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V
    invoke-virtual {v0, p1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;
    move-result-object v0
    return-object v0
.end method

## CHECK-START: java.lang.String Test.testStringArgumentNullChecked(java.lang.String, java.lang.String) instruction_simplifier (before)
## CHECK:      NewInstance
## CHECK:      InvokeStaticOrDirect
## CHECK:      InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK:      InvokeVirtual intrinsic:StringBuilderToString

## CHECK-START: java.lang.String Test.testStringArgumentNullChecked(java.lang.String, java.lang.String) instruction_simplifier (after)
## CHECK:      StringBuilderAppend
## CHECK-NOT:  NewInstance
## CHECK-NOT:  InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK-NOT:  InvokeVirtual intrinsic:StringBuilderToString

.method public static testStringArgumentNullChecked(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;
    .registers 3
    invoke-virtual {p0}, Ljava/lang/String;->length()I
    new-instance v0, Ljava/lang/StringBuilder;
    invoke-direct {v0, p0}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V
    invoke-virtual {v0, p1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;
    move-result-object v0
    return-object v0
.end method

## CHECK-START: java.lang.String Test.testConstStringAsCharSequence(java.lang.String) instruction_simplifier (before)
## CHECK: NewInstance
## CHECK: InvokeStaticOrDirect
## CHECK: InvokeVirtual intrinsic:StringBuilderAppendCharSequence
## CHECK: InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK: InvokeVirtual intrinsic:StringBuilderToString

## CHECK-START: java.lang.String Test.testConstStringAsCharSequence(java.lang.String) instruction_simplifier (after)
## CHECK: StringBuilderAppend
## CHECK-NOT: NewInstance
## CHECK-NOT: InvokeVirtual intrinsic:StringBuilderAppendCharSequence
## CHECK-NOT: InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK-NOT: InvokeVirtual intrinsic:StringBuilderToString

.method public static testConstStringAsCharSequence(Ljava/lang/String;)Ljava/lang/String;
    .registers 3
    const-string v1, "Hello "
    new-instance v0, Ljava/lang/StringBuilder;
    invoke-direct {v0}, Ljava/lang/StringBuilder;-><init>()V
    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/CharSequence;)Ljava/lang/StringBuilder;
    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;
    move-result-object v0
    return-object v0
.end method

## CHECK-START: java.lang.String Test.testConstStringAsCharSequenceConstructor(java.lang.String) instruction_simplifier (before)
## CHECK: NewInstance
## CHECK: InvokeStaticOrDirect
## CHECK: InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK: InvokeVirtual intrinsic:StringBuilderToString

## CHECK-START: java.lang.String Test.testConstStringAsCharSequenceConstructor(java.lang.String) instruction_simplifier (after)
## CHECK: StringBuilderAppend
## CHECK-NOT: NewInstance
## CHECK-NOT: InvokeStaticOrDirect
## CHECK-NOT: InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK-NOT: InvokeVirtual intrinsic:StringBuilderToString

.method public static testConstStringAsCharSequenceConstructor(Ljava/lang/String;)Ljava/lang/String;
    .registers 3
    const-string v1, "Hello "
    new-instance v0, Ljava/lang/StringBuilder;
    invoke-direct {v0, v1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/CharSequence;)V
    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;
    move-result-object v0
    return-object v0
.end method

## CHECK-START: java.lang.String Test.testCapacity(java.lang.String) instruction_simplifier (before)
## CHECK: NewInstance
## CHECK: InvokeStaticOrDirect
## CHECK: InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK: InvokeVirtual intrinsic:StringBuilderToString

## CHECK-START: java.lang.String Test.testCapacity(java.lang.String) instruction_simplifier (after)
## CHECK: StringBuilderAppend
## CHECK-NOT: NewInstance
## CHECK-NOT: InvokeStaticOrDirect
## CHECK-NOT: InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK-NOT: InvokeVirtual intrinsic:StringBuilderToString

.method public static testCapacity(Ljava/lang/String;)Ljava/lang/String;
    .registers 4
    const/16 v1, 0x2a
    new-instance v0, Ljava/lang/StringBuilder;
    invoke-direct {v0, v1}, Ljava/lang/StringBuilder;-><init>(I)V
    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;
    move-result-object v0
    return-object v0
.end method

.method public static $noinline$getCharSequence()Ljava/lang/CharSequence;
    .registers 1
    const-string v0, "Hello "
    return-object v0
.end method

## CHECK-START: java.lang.String Test.testUnknownCharSequence(java.lang.String) instruction_simplifier (before)
## CHECK: NewInstance
## CHECK: InvokeStaticOrDirect
## CHECK: InvokeVirtual intrinsic:StringBuilderAppendCharSequence
## CHECK: InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK: InvokeVirtual intrinsic:StringBuilderToString

## CHECK-START: java.lang.String Test.testUnknownCharSequence(java.lang.String) instruction_simplifier (after)
## CHECK: NewInstance
## CHECK: InvokeStaticOrDirect
## CHECK: InvokeVirtual intrinsic:StringBuilderAppendCharSequence
## CHECK: InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK: InvokeVirtual intrinsic:StringBuilderToString

.method public static testUnknownCharSequence(Ljava/lang/String;)Ljava/lang/String;
    .registers 3
    invoke-static {}, LTest;->$noinline$getCharSequence()Ljava/lang/CharSequence;
    move-result-object v1
    invoke-interface {v1}, Ljava/lang/CharSequence;->length()I

    new-instance v0, Ljava/lang/StringBuilder;
    invoke-direct {v0}, Ljava/lang/StringBuilder;-><init>()V
    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/CharSequence;)Ljava/lang/StringBuilder;
    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;
    move-result-object v0
    return-object v0
.end method

## CHECK-START: java.lang.String Test.testUnknownCharSequenceConstructor(java.lang.String) instruction_simplifier (before)
## CHECK: NewInstance
## CHECK: InvokeStaticOrDirect
## CHECK: InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK: InvokeVirtual intrinsic:StringBuilderToString

## CHECK-START: java.lang.String Test.testUnknownCharSequenceConstructor(java.lang.String) instruction_simplifier (after)
## CHECK: NewInstance
## CHECK: InvokeStaticOrDirect
## CHECK: InvokeVirtual intrinsic:StringBuilderAppendString
## CHECK: InvokeVirtual intrinsic:StringBuilderToString

.method public static testUnknownCharSequenceConstructor(Ljava/lang/String;)Ljava/lang/String;
    .registers 3
    invoke-static {}, LTest;->$noinline$getCharSequence()Ljava/lang/CharSequence;
    move-result-object v1
    invoke-interface {v1}, Ljava/lang/CharSequence;->length()I

    new-instance v0, Ljava/lang/StringBuilder;
    invoke-direct {v0, v1}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/CharSequence;)V
    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;
    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;
    move-result-object v0
    return-object v0
.end method
