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

.class public B444176687
.super java/lang/Object

; Test for "expected type is array, but the actual type is a non-array"

; This method takes an array of type [LFake; and returns its length.
.method public static $inline$queryLength([LFake;)I
    .limit stack 1
    .limit locals 1

    aload_0
    arraylength
    ireturn
.end method

.method public static test()I
    .limit stack 1
    .limit locals 1

    ldc "MyString"
    invokestatic B444176687/$inline$queryLength([LFake;)I
    ireturn
.end method
