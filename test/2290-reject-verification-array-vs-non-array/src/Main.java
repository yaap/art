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

public class Main {
    public static void main(String[] args) throws Exception {
        try {
            B444176687.test();
            throw new Error("Expected VerifyError for B444176687");
        } catch (VerifyError expected) {
        }

        try {
            B444176687_2.test();
            throw new Error("Expected VerifyError for B444176687_2");
        } catch (VerifyError expected) {
        }

        // This test should pass verification, as an array can be assigned to an unresolved type.
        // However, it should fail at runtime with NoClassDefFoundError.
        try {
            B444176687_3.test();
            throw new Error("Expected NoClassDefFoundError for B444176687_3");
        } catch (NoClassDefFoundError expected) {
        }
    }
}
