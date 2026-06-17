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

#include "jni.h"
#include "nativehelper/ScopedUtfChars.h"

namespace art {

extern "C" JNIEXPORT void JNICALL Java_Main_setBooleanFieldJni(
    JNIEnv* env, [[maybe_unused]] jclass, jclass clazz, jstring jfield_name, jboolean new_value) {
  ScopedUtfChars field_name(env, jfield_name);
  jfieldID field = env->GetStaticFieldID(clazz, field_name.c_str(), "Z");
  env->SetStaticBooleanField(clazz, field, new_value);
}

extern "C" JNIEXPORT void JNICALL Java_Main_setByteFieldJni(
    JNIEnv* env, [[maybe_unused]] jclass, jclass clazz, jstring jfield_name, jbyte new_value) {
  ScopedUtfChars field_name(env, jfield_name);
  jfieldID field = env->GetStaticFieldID(clazz, field_name.c_str(), "B");
  env->SetStaticByteField(clazz, field, new_value);
}

extern "C" JNIEXPORT void JNICALL Java_Main_setCharFieldJni(
    JNIEnv* env, [[maybe_unused]] jclass, jclass clazz, jstring jfield_name, jchar new_value) {
  ScopedUtfChars field_name(env, jfield_name);
  jfieldID field = env->GetStaticFieldID(clazz, field_name.c_str(), "C");
  env->SetStaticCharField(clazz, field, new_value);
}

extern "C" JNIEXPORT void JNICALL Java_Main_setShortFieldJni(
    JNIEnv* env, [[maybe_unused]] jclass, jclass clazz, jstring jfield_name, jshort new_value) {
  ScopedUtfChars field_name(env, jfield_name);
  jfieldID field = env->GetStaticFieldID(clazz, field_name.c_str(), "S");
  env->SetStaticShortField(clazz, field, new_value);
}

extern "C" JNIEXPORT void JNICALL Java_Main_setIntFieldJni(
    JNIEnv* env, [[maybe_unused]] jclass, jclass clazz, jstring jfield_name, jint new_value) {
  ScopedUtfChars field_name(env, jfield_name);
  jfieldID field = env->GetStaticFieldID(clazz, field_name.c_str(), "I");
  env->SetStaticIntField(clazz, field, new_value);
}

extern "C" JNIEXPORT void JNICALL Java_Main_setFloatFieldJni(
    JNIEnv* env, [[maybe_unused]] jclass, jclass clazz, jstring jfield_name, jfloat new_value) {
  ScopedUtfChars field_name(env, jfield_name);
  jfieldID field = env->GetStaticFieldID(clazz, field_name.c_str(), "F");
  env->SetStaticFloatField(clazz, field, new_value);
}

extern "C" JNIEXPORT void JNICALL Java_Main_setLongFieldJni(
    JNIEnv* env, [[maybe_unused]] jclass, jclass clazz, jstring jfield_name, jlong new_value) {
  ScopedUtfChars field_name(env, jfield_name);
  jfieldID field = env->GetStaticFieldID(clazz, field_name.c_str(), "J");
  env->SetStaticLongField(clazz, field, new_value);
}

extern "C" JNIEXPORT void JNICALL Java_Main_setDoubleFieldJni(
    JNIEnv* env, [[maybe_unused]] jclass, jclass clazz, jstring jfield_name, jdouble new_value) {
  ScopedUtfChars field_name(env, jfield_name);
  jfieldID field = env->GetStaticFieldID(clazz, field_name.c_str(), "D");
  env->SetStaticDoubleField(clazz, field, new_value);
}

extern "C" JNIEXPORT void JNICALL Java_Main_setRefFieldJni(
    JNIEnv* env, [[maybe_unused]] jclass, jclass clazz, jstring jfield_name, jobject new_value) {
  ScopedUtfChars field_name(env, jfield_name);
  jfieldID field = env->GetStaticFieldID(clazz, field_name.c_str(), "Ljava/lang/Object;");
  env->SetStaticObjectField(clazz, field, new_value);
}

}  // namespace art
