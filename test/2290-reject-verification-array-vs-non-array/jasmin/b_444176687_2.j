; Copyright (C) 2025 The Android Open Source Project
;
; Licensed under the Apache License, Version 2.0 (the "License");
; you may not use this file except in compliance with the License.
; You may obtain a copy of the License at
;
;      http://www.apache.org/licenses/LICENSE-2.0
;
; Unless required by applicable law or agreed to in writing, software
; distributed under the License is distributed on an "AS IS" BASIS,
; WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
; See the License for the specific language governing permissions and
; limitations under the License.

.class public B444176687_2
.super java/lang/Object

; Test for "expected type is resolved non-array, src is unresolved array"

; This method takes a non-array type String and returns 1.
.method public static $inline$takesString(Ljava/lang/String;)I
    .limit stack 1
    .limit locals 1

    iconst_1
    ireturn
.end method

.method public static test()I
    .limit stack 2
    .limit locals 1

    iconst_0
    anewarray Fake
    invokestatic B444176687_2/$inline$takesString(Ljava/lang/String;)I
    ireturn
.end method
