/*
 * Copyright (C) 2015 The Android Open Source Project
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

import java.lang.String;
import java.lang.reflect.Constructor;
import java.lang.reflect.Executable;
import java.lang.reflect.Method;
import java.lang.reflect.Parameter;

public class Main {
    public static void main(String[] args) throws Exception {
        Constructor<?> cons = String.class.getDeclaredConstructor((Class<?>[]) null);
        // Use reflection to call serializationCopy. serializationCopy is not a part of standard
        // java.
        Method serialize_method =
                Constructor.class.getDeclaredMethod("serializationCopy", Class.class, Class.class);
        Constructor<?> serialized_cons =
                (Constructor<?>) serialize_method.invoke(cons, String.class, String.class);

        Method get_art_method = Executable.class.getDeclaredMethod("getArtMethod");
        long art_method_cons = (long) get_art_method.invoke(cons);
        if (art_method_cons == 0) {
            throw new Exception("Unexpected nullptr for ArtMethod");
        }
        long art_method_serialize_cons = (long) get_art_method.invoke(serialized_cons);
        if (art_method_serialize_cons != 0) {
            throw new Exception("Unexpected ArtMethod for serialized constructor");
        }

        // newInstance should pass for both cons and serialized_cons. The implementation chooses the
        // correct way of instantiating it based on whether the constructor is serialized or not.
        String s = (String) cons.newInstance();
        if (s == null) {
            throw new Exception("newInstance failed");
        }
        String s1 = (String) serialized_cons.newInstance();
        if (s1 == null) {
            throw new Exception("newInstance failed");
        }
        // This shouldn't throw. It should just return null.
        Parameter[] params = serialized_cons.getParameters();
    }
}
