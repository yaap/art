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

import java.io.File;
import java.io.FileDescriptor;
import java.io.FileOutputStream;
import java.io.IOException;
import java.lang.reflect.Method;

public class Main {
    private static final int WALL_CLOCK_FLAG = 0x010;
    private static File file;

    public static void main(String[] args) throws Exception {
        System.loadLibrary(args[0]);
        String name = System.getProperty("java.vm.name");
        if (!"Dalvik".equals(name)) {
            System.out.println("This test is not supported on " + name);
            return;
        }

        ensureJitCompiled(Main.class, "$noinline$doSomeWorkJIT");

        System.out.println("***** streaming test - wall clock *******");
        StreamTraceParser stream_parser = new StreamTraceParser();
        testTracing(
                /* streaming=*/true, /* flags= */ WALL_CLOCK_FLAG, stream_parser,
                BaseTraceParser.STREAMING_WALL_CLOCK_VERSION);
    }

    public static void testTracing(boolean streaming, int flags, BaseTraceParser parser,
                                   int expected_version) throws Exception {
        Main m = new Main();
        Method jitMethod = Main.class.getDeclaredMethod("$noinline$doSomeWorkJIT",
                                                        FileDescriptor.class, Integer.TYPE);
        Thread t = new Thread(() -> {
            try {
                file = VMDebug.createTempFile();
                FileOutputStream out_file = new FileOutputStream(file);
                Main m1 = new Main();
                try {
                    jitMethod.invoke(m, out_file.getFD(), flags);
                } catch (Exception e) {
                    System.out.println("Exception " + e);
                }
                VMDebug.$noinline$stopMethodTracing();
                out_file.close();
                // Only check methods from this class. There are too many different outputs possible
                // if we allow all methods for configurations like debuggable / interpreter and a
                // combination of them.
                parser.CheckTraceFileFormat(file, expected_version, "TestThread2246", "Main.java");
                file.delete();
            } catch (Exception e) {
                System.out.println("Exception in thread " + e);
                e.printStackTrace();
            } finally {
                file.delete();
            }
        }, "TestThread2246");
        try {
            if (VMDebug.getMethodTracingMode() != 0) {
                VMDebug.$noinline$stopMethodTracing();
            }

            t.start();
            t.join();
        } finally {
            file.delete();
        }
    }

    public static void $noinline$doSomeWorkJIT(FileDescriptor fd, int flags) throws Exception {
        VMDebug.startMethodTracing(file.getPath(), fd, 0, flags, false, 0, true);
        throw new Exception("test");
    }

    private static native void ensureJitCompiled(Class<?> cls, String methodName);
}
