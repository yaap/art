/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <bit>
#include <iostream>
#include <limits>

#include "art_method-inl.h"
#include "jni.h"

namespace {

extern "C" JNIEXPORT void JNICALL Java_Main_jniSimpleMethod(JNIEnv*) {
  std::cout << "Java_Main_simpleJniMethod REACHED!" << std::endl;
}

static uint32_t bitsFP(float val) {
  return art::bit_cast<uint32_t>(val);
}

static uint64_t bitsFP(double val) {
  return art::bit_cast<uint64_t>(val);
}

extern "C" jbyte JNICALL Java_Main_jniMethodWithManyParameters(JNIEnv*,
                                                               jclass,
                                                               jbyte b1,
                                                               jchar c2,
                                                               jshort s3,
                                                               jint i4,
                                                               jlong l5,
                                                               jobject o6,
                                                               jbyte b7,
                                                               jchar c8,
                                                               jshort s9,
                                                               jint i10,
                                                               jlong l11,
                                                               jobject o12,
                                                               jbyte b13,
                                                               jchar c14,
                                                               jshort s15,
                                                               jint i16,
                                                               jlong l17,
                                                               jobject o18,
                                                               jfloat f19,
                                                               jdouble d20,
                                                               jfloat f21,
                                                               jdouble d22,
                                                               jfloat f23,
                                                               jdouble d24,
                                                               jfloat f25,
                                                               jdouble d26,
                                                               jfloat f27,
                                                               jdouble d28,
                                                               jfloat f29,
                                                               jdouble d30) {
  CHECK_EQ(b1, 123);
  CHECK_EQ(c2, 123);
  CHECK_EQ(s3, 123);
  CHECK_EQ(i4, 123);
  CHECK_EQ(l5, 123);
  CHECK(o6 == NULL);
  CHECK_EQ(b7, std::numeric_limits<int8_t>::max() - 1);
  CHECK_EQ(c8, std::numeric_limits<uint16_t>::max() - 1);
  CHECK_EQ(s9, std::numeric_limits<int16_t>::max() - 1);
  CHECK_EQ(i10, std::numeric_limits<int32_t>::max() - 1);
  CHECK_EQ(l11, std::numeric_limits<int64_t>::max() - 1);
  CHECK(o12 != NULL);
  CHECK_EQ(b13, std::numeric_limits<int8_t>::min() + 1);
  CHECK_EQ(c14, std::numeric_limits<uint16_t>::min() + 1);
  CHECK_EQ(s15, std::numeric_limits<int16_t>::min() + 1);
  CHECK_EQ(i16, std::numeric_limits<int32_t>::min() + 1);
  CHECK_EQ(l17, std::numeric_limits<int64_t>::min() + 1);
  CHECK(o18 != NULL);
  CHECK_EQ(bitsFP(f19), bitsFP(0.0f));
  CHECK_EQ(bitsFP(d20), bitsFP(0.0));
  CHECK_EQ(bitsFP(f21), bitsFP(-0.0f));
  CHECK_EQ(bitsFP(d22), bitsFP(-0.0));
  CHECK_EQ(bitsFP(f23), bitsFP(123.456f));
  CHECK_EQ(bitsFP(d24), bitsFP(123.456));
  CHECK(isnan(f25));
  CHECK(isnan(d26));
  CHECK_EQ(bitsFP(f27), bitsFP(std::numeric_limits<float>::max()));
  CHECK_EQ(bitsFP(d28), bitsFP(std::numeric_limits<double>::max()));
  CHECK_EQ(bitsFP(f29), bitsFP(-5.5f));
  CHECK_EQ(bitsFP(d30), bitsFP(-5.5));

  std::cout << "Java_Main_jniMethodWithManyParameters REACHED!" << std::endl;

  return 0;
}

extern "C" jdouble JNICALL Java_Main_jniNonStaticReturnsDouble(JNIEnv*,
                                                               jobject thisObj,
                                                               jdouble val) {
  CHECK(thisObj != NULL);

  std::cout << "Java_Main_jniNonStaticReturnsDouble REACHED!" << std::endl;
  return val + 1.0;
}

extern "C" void JNICALL Java_Main_jniNewException(JNIEnv* jenv, jclass) {
  std::cout << "Java_Main_jniNewException REACHED!" << std::endl;

  jclass cls = jenv->FindClass("java/lang/RuntimeException");
  jenv->ThrowNew(cls, "Thrown from JNI thrown");
}

}  // namespace
