/*
 * Copyright (C) 2021 The Android Open Source Project
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

import java.lang.reflect.Field;
import java.util.concurrent.ThreadLocalRandom;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import jdk.internal.misc.Unsafe;

public class Main {

  private static final long ADDRESS;

  static {
    try {
      // Needed for absolute address methods. They operate on primitives, hence 8 bytes.
      ADDRESS = getUnsafe().allocateMemory(8);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private static void check(boolean actual, boolean expected, String msg) {
    if (actual != expected) {
      System.out.println(msg + " : " + actual + " != " + expected);
      System.exit(1);
    }
  }

  private static void check(byte actual, byte expected, String msg) {
    if (actual != expected) {
      System.out.println(msg + " : " + actual + " != " + expected);
      System.exit(1);
    }
  }

  private static void check(char actual, char expected, String msg) {
    if (actual != expected) {
      System.out.println(msg + " : " + actual + " != " + expected);
      System.exit(1);
    }
  }

  private static void check(short actual, short expected, String msg) {
    if (actual != expected) {
      System.out.println(msg + " : " + actual + " != " + expected);
      System.exit(1);
    }
  }

  private static void check(int actual, int expected, String msg) {
    if (actual != expected) {
      System.out.println(msg + " : " + actual + " != " + expected);
      System.exit(1);
    }
  }

  private static void check(long actual, long expected, String msg) {
    if (actual != expected) {
      System.out.println(msg + " : " + actual + " != " + expected);
      System.exit(1);
    }
  }

  private static void check(float actual, float expected, String msg) {
    if (actual != expected) {
      System.out.println(msg + " : " + actual + " != " + expected);
      System.exit(1);
    }
  }

  private static void check(double actual, double expected, String msg) {
    if (actual != expected) {
      System.out.println(msg + " : " + actual + " != " + expected);
      System.exit(1);
    }
  }

  private static void check(Object actual, Object expected, String msg) {
    if (actual != expected) {
      System.out.println(msg + " : " + actual + " != " + expected);
      System.exit(1);
    }
  }

  private static void expectThrow(Class<?> exceptionClass, String msg) {
    System.out.println(msg + " : expected " + exceptionClass.getName());
    System.exit(1);
  }

  private static Unsafe getUnsafe() throws NoSuchFieldException, IllegalAccessException {
    Class<?> unsafeClass = Unsafe.class;
    Field f = unsafeClass.getDeclaredField("theUnsafe");
    f.setAccessible(true);
    return (Unsafe) f.get(null);
  }

  public static void main(String[] args) throws NoSuchFieldException, IllegalAccessException {
    System.loadLibrary(args[0]);
    Unsafe unsafe = getUnsafe();

    testArrayBaseOffset(unsafe);
    testArrayIndexScale(unsafe);
    testGetAndPut(unsafe);
    testCompareAndSet(unsafe);
    testGetAndPutVolatile(unsafe);
    testGetAcquireAndPutRelease(unsafe);
    testCopyMemory(unsafe);
    testNullBasedAccessors(unsafe);
    testConstants(unsafe);

    testGetAndAdd(unsafe);
    testGetBoolean(unsafe);
    testPutBoolean(unsafe);
    testGetByte(unsafe);
    testPutByte(unsafe);
    testGetChar(unsafe);
    testPutChar(unsafe);
    testGetShort(unsafe);
    testPutShort(unsafe);
    testGetInt(unsafe);
    testPutInt(unsafe);
    testGetFloat(unsafe);
    testPutFloat(unsafe);
    testGetLong(unsafe);
    testPutLong(unsafe);
    testGetDouble(unsafe);
    testPutDouble(unsafe);
    testGetReference(unsafe);
    testPutReference(unsafe);
    testAbsoluteAddress(unsafe);
    testCasIntConstantOffset(unsafe);
    testCasLongConstantOffset(unsafe);
    testCasReferenceConstantOffset(unsafe);

    testFinalFields();
  }

  private static void testArrayBaseOffset(Unsafe unsafe) {
    check(unsafe.arrayBaseOffset(boolean[].class), vmJdkArrayBaseOffset(boolean[].class),
        "Unsafe.arrayBaseOffset(boolean[])");
    check(unsafe.arrayBaseOffset(byte[].class), vmJdkArrayBaseOffset(byte[].class),
        "Unsafe.arrayBaseOffset(byte[])");
    check(unsafe.arrayBaseOffset(char[].class), vmJdkArrayBaseOffset(char[].class),
        "Unsafe.arrayBaseOffset(char[])");
    check(unsafe.arrayBaseOffset(double[].class), vmJdkArrayBaseOffset(double[].class),
        "Unsafe.arrayBaseOffset(double[])");
    check(unsafe.arrayBaseOffset(float[].class), vmJdkArrayBaseOffset(float[].class),
        "Unsafe.arrayBaseOffset(float[])");
    check(unsafe.arrayBaseOffset(int[].class), vmJdkArrayBaseOffset(int[].class),
        "Unsafe.arrayBaseOffset(int[])");
    check(unsafe.arrayBaseOffset(long[].class), vmJdkArrayBaseOffset(long[].class),
        "Unsafe.arrayBaseOffset(long[])");
    check(unsafe.arrayBaseOffset(Object[].class), vmJdkArrayBaseOffset(Object[].class),
        "Unsafe.arrayBaseOffset(Object[])");
  }

  private static void testArrayIndexScale(Unsafe unsafe) {
    check(unsafe.arrayIndexScale(boolean[].class), vmJdkArrayIndexScale(boolean[].class),
        "Unsafe.arrayIndexScale(boolean[])");
    check(unsafe.arrayIndexScale(byte[].class), vmJdkArrayIndexScale(byte[].class),
        "Unsafe.arrayIndexScale(byte[])");
    check(unsafe.arrayIndexScale(char[].class), vmJdkArrayIndexScale(char[].class),
        "Unsafe.arrayIndexScale(char[])");
    check(unsafe.arrayIndexScale(double[].class), vmJdkArrayIndexScale(double[].class),
        "Unsafe.arrayIndexScale(double[])");
    check(unsafe.arrayIndexScale(float[].class), vmJdkArrayIndexScale(float[].class),
        "Unsafe.arrayIndexScale(float[])");
    check(unsafe.arrayIndexScale(int[].class), vmJdkArrayIndexScale(int[].class),
        "Unsafe.arrayIndexScale(int[])");
    check(unsafe.arrayIndexScale(long[].class), vmJdkArrayIndexScale(long[].class),
        "Unsafe.arrayIndexScale(long[])");
    check(unsafe.arrayIndexScale(Object[].class), vmJdkArrayIndexScale(Object[].class),
        "Unsafe.arrayIndexScale(Object[])");
  }

  private static void testGetAndPut(Unsafe unsafe) throws NoSuchFieldException {
    TestClass t = new TestClass();

    int intValue = 12345678;
    Field intField = TestClass.class.getDeclaredField("intVar");
    long intOffset = unsafe.objectFieldOffset(intField);
    check(unsafe.getInt(t, intOffset), 0, "Unsafe.getInt(Object, long) - initial");
    unsafe.putInt(t, intOffset, intValue);
    check(t.intVar, intValue, "Unsafe.putInt(Object, long, int)");
    check(unsafe.getInt(t, intOffset), intValue, "Unsafe.getInt(Object, long)");

    long longValue = 1234567887654321L;
    Field longField = TestClass.class.getDeclaredField("longVar");
    long longOffset = unsafe.objectFieldOffset(longField);
    check(unsafe.getLong(t, longOffset), 0, "Unsafe.getLong(Object, long) - initial");
    unsafe.putLong(t, longOffset, longValue);
    check(t.longVar, longValue, "Unsafe.putLong(Object, long, long)");
    check(unsafe.getLong(t, longOffset), longValue, "Unsafe.getLong(Object, long)");

    Object objectValue = new Object();
    Field objectField = TestClass.class.getDeclaredField("objectVar");
    long objectOffset = unsafe.objectFieldOffset(objectField);
    check(unsafe.getObject(t, objectOffset), null, "Unsafe.getObject(Object, long) - initial");
    unsafe.putObject(t, objectOffset, objectValue);
    check(t.objectVar, objectValue, "Unsafe.putObject(Object, long, Object)");
    check(unsafe.getObject(t, objectOffset), objectValue, "Unsafe.getObject(Object, long)");
  }

  private static void testCompareAndSet(Unsafe unsafe) throws NoSuchFieldException {
    TestClass t = new TestClass();

    int intValue = 12345678;
    Field intField = TestClass.class.getDeclaredField("intVar");
    long intOffset = unsafe.objectFieldOffset(intField);
    unsafe.putInt(t, intOffset, intValue);

    long longValue = 1234567887654321L;
    Field longField = TestClass.class.getDeclaredField("longVar");
    long longOffset = unsafe.objectFieldOffset(longField);
    unsafe.putLong(t, longOffset, longValue);

    Object objectValue = new Object();
    Field objectField = TestClass.class.getDeclaredField("objectVar");
    long objectOffset = unsafe.objectFieldOffset(objectField);
    unsafe.putObject(t, objectOffset, objectValue);

    if (unsafe.compareAndSetInt(t, intOffset, 0, 1)) {
      System.out.println("Unexpectedly succeeding compareAndSetInt(t, intOffset, 0, 1)");
    }
    check(t.intVar, intValue, "Unsafe.compareAndSetInt(Object, long, int, int) - not set");
    if (!unsafe.compareAndSetInt(t, intOffset, intValue, 0)) {
      System.out.println(
          "Unexpectedly not succeeding compareAndSetInt(t, intOffset, intValue, 0)");
    }
    check(t.intVar, 0, "Unsafe.compareAndSetInt(Object, long, int, int) - gets set");
    if (!unsafe.compareAndSetInt(t, intOffset, 0, 1)) {
      System.out.println("Unexpectedly not succeeding compareAndSetInt(t, intOffset, 0, 1)");
    }
    check(t.intVar, 1, "Unsafe.compareAndSetInt(Object, long, int, int) - gets re-set");
    // Exercise jdk.internal.misc.Unsafe.compareAndSetInt using the same
    // integer (1) for the `expectedValue` and `newValue` arguments.
    if (!unsafe.compareAndSetInt(t, intOffset, 1, 1)) {
      System.out.println("Unexpectedly not succeeding compareAndSetInt(t, intOffset, 1, 1)");
    }
    check(t.intVar, 1, "Unsafe.compareAndSetInt(Object, long, int, int) - gets set to same");

    if (unsafe.compareAndSetLong(t, longOffset, 0, 1)) {
      System.out.println("Unexpectedly succeeding compareAndSetLong(t, longOffset, 0, 1)");
    }
    check(t.longVar, longValue, "Unsafe.compareAndSetLong(Object, long, long, long) - not set");
    if (!unsafe.compareAndSetLong(t, longOffset, longValue, 0)) {
      System.out.println(
          "Unexpectedly not succeeding compareAndSetLong(t, longOffset, longValue, 0)");
    }
    check(t.longVar, 0, "Unsafe.compareAndSetLong(Object, long, long, long) - gets set");
    if (!unsafe.compareAndSetLong(t, longOffset, 0, 1)) {
      System.out.println("Unexpectedly not succeeding compareAndSetLong(t, longOffset, 0, 1)");
    }
    check(t.longVar, 1, "Unsafe.compareAndSetLong(Object, long, long, long) - gets re-set");
    // Exercise jdk.internal.misc.Unsafe.compareAndSetLong using the same
    // integer (1) for the `expectedValue` and `newValue` arguments.
    if (!unsafe.compareAndSetLong(t, longOffset, 1, 1)) {
      System.out.println("Unexpectedly not succeeding compareAndSetLong(t, longOffset, 1, 1)");
    }
    check(t.longVar, 1, "Unsafe.compareAndSetLong(Object, long, long, long) - gets set to same");

    // We do not use `null` as argument to jdk.internal.misc.Unsafe.compareAndSetObject
    // in those tests, as this value is not affected by heap poisoning
    // (which uses address negation to poison and unpoison heap object
    // references).  This way, when heap poisoning is enabled, we can
    // better exercise its implementation within that method.
    if (unsafe.compareAndSetObject(t, objectOffset, new Object(), new Object())) {
      System.out.println("Unexpectedly succeeding compareAndSetObject(t, objectOffset, 0, 1)");
    }
    check(t.objectVar, objectValue, "Unsafe.compareAndSetObject(Object, long, Object, Object) - not set");
    Object objectValue2 = new Object();
    if (!unsafe.compareAndSetObject(t, objectOffset, objectValue, objectValue2)) {
      System.out.println(
          "Unexpectedly not succeeding compareAndSetObject(t, objectOffset, objectValue, 0)");
    }
    check(t.objectVar, objectValue2, "Unsafe.compareAndSetObject(Object, long, Object, Object) - gets set");
    Object objectValue3 = new Object();
    if (!unsafe.compareAndSetObject(t, objectOffset, objectValue2, objectValue3)) {
      System.out.println("Unexpectedly not succeeding compareAndSetObject(t, objectOffset, 0, 1)");
    }
    check(t.objectVar, objectValue3, "Unsafe.compareAndSetObject(Object, long, Object, Object) - gets re-set");
    // Exercise jdk.internal.misc.Unsafe.compareAndSetObject using the same
    // object for the `expectedValue` and `newValue` arguments.
    if (!unsafe.compareAndSetObject(t, objectOffset, objectValue3, objectValue3)) {
      System.out.println("Unexpectedly not succeeding compareAndSetObject(t, objectOffset, 1, 1)");
    }
    check(t.objectVar, objectValue3, "Unsafe.compareAndSetObject(Object, long, Object, Object) - gets set to same");

    // Reset and now try with `compareAndSetReference` which replaced `compareAndSetObject`.
    unsafe.putObject(t, objectOffset, objectValue);

    if (unsafe.compareAndSetReference(t, objectOffset, new Object(), new Object())) {
      System.out.println("Unexpectedly succeeding compareAndSetReference(t, objectOffset, 0, 1)");
    }
    check(t.objectVar, objectValue, "Unsafe.compareAndSetReference(Object, long, Object, Object) - not set");
    objectValue2 = new Object();
    if (!unsafe.compareAndSetReference(t, objectOffset, objectValue, objectValue2)) {
      System.out.println(
          "Unexpectedly not succeeding compareAndSetReference(t, objectOffset, objectValue, 0)");
    }
    check(t.objectVar, objectValue2, "Unsafe.compareAndSetReference(Object, long, Object, Object) - gets set");
    objectValue3 = new Object();
    if (!unsafe.compareAndSetReference(t, objectOffset, objectValue2, objectValue3)) {
      System.out.println("Unexpectedly not succeeding compareAndSetReference(t, objectOffset, 0, 1)");
    }
    check(t.objectVar, objectValue3, "Unsafe.compareAndSetReference(Object, long, Object, Object) - gets re-set");
    // Exercise jdk.internal.misc.Unsafe.compareAndSetReference using the same
    // object for the `expectedValue` and `newValue` arguments.
    if (!unsafe.compareAndSetReference(t, objectOffset, objectValue3, objectValue3)) {
      System.out.println("Unexpectedly not succeeding compareAndSetReference(t, objectOffset, 1, 1)");
    }
    check(t.objectVar, objectValue3, "Unsafe.compareAndSetReference(Object, long, Object, Object) - gets set to same");
 }

  private static void testGetAndPutVolatile(Unsafe unsafe) throws NoSuchFieldException {
    TestVolatileClass tv = new TestVolatileClass();

    int intValue = 12345678;
    Field volatileIntField = TestVolatileClass.class.getDeclaredField("volatileIntVar");
    long volatileIntOffset = unsafe.objectFieldOffset(volatileIntField);
    check(unsafe.getIntVolatile(tv, volatileIntOffset),
          0,
          "Unsafe.getIntVolatile(Object, long) - initial");
    unsafe.putIntVolatile(tv, volatileIntOffset, intValue);
    check(tv.volatileIntVar, intValue, "Unsafe.putIntVolatile(Object, long, int)");
    check(unsafe.getIntVolatile(tv, volatileIntOffset),
          intValue,
          "Unsafe.getIntVolatile(Object, long)");

    boolean booleanValue = true;
    Field volatileBooleanField = TestVolatileClass.class.getDeclaredField("volatileBooleanVar");
    long volatileBooleanOffset = unsafe.objectFieldOffset(volatileBooleanField);
    check(unsafe.getBooleanVolatile(tv, volatileBooleanOffset),
          false,
          "Unsafe.getBooleanVolatile(Object, long) - initial");
    unsafe.putBooleanVolatile(tv, volatileBooleanOffset, booleanValue);
    check(tv.volatileBooleanVar, booleanValue, "Unsafe.putBooleanVolatile(Object, long, boolean)");
    check(unsafe.getBooleanVolatile(tv, volatileBooleanOffset),
          booleanValue,
          "Unsafe.getBooleanVolatile(Object, long)");

    byte byteValue = 125;
    Field volatileByteField = TestVolatileClass.class.getDeclaredField("volatileByteVar");
    long volatileByteOffset = unsafe.objectFieldOffset(volatileByteField);
    check(unsafe.getByteVolatile(tv, volatileByteOffset),
          0,
          "Unsafe.getByteVolatile(Object, long) - initial");
    unsafe.putByteVolatile(tv, volatileByteOffset, byteValue);
    check(tv.volatileByteVar, byteValue, "Unsafe.putByteVolatile(Object, long, byte)");
    check(unsafe.getByteVolatile(tv, volatileByteOffset),
          byteValue,
          "Unsafe.getByteVolatile(Object, long)");

    char charValue = 'X';
    Field volatileCharField = TestVolatileClass.class.getDeclaredField("volatileCharVar");
    long volatileCharOffset = unsafe.objectFieldOffset(volatileCharField);
    check(unsafe.getCharVolatile(tv, volatileCharOffset),
          '\0',
          "Unsafe.getCharVolatile(Object, long) - initial");
    unsafe.putCharVolatile(tv, volatileCharOffset, charValue);
    check(tv.volatileCharVar, charValue, "Unsafe.putCharVolatile(Object, long, char)");
    check(unsafe.getCharVolatile(tv, volatileCharOffset),
          charValue,
          "Unsafe.getCharVolatile(Object, long)");

    short shortValue = 32523;
    Field volatileShortField = TestVolatileClass.class.getDeclaredField("volatileShortVar");
    long volatileShortOffset = unsafe.objectFieldOffset(volatileShortField);
    check(unsafe.getShortVolatile(tv, volatileShortOffset),
          0,
          "Unsafe.getShortVolatile(Object, long) - initial");
    unsafe.putShortVolatile(tv, volatileShortOffset, shortValue);
    check(tv.volatileShortVar, shortValue, "Unsafe.putShortVolatile(Object, long, short)");
    check(unsafe.getShortVolatile(tv, volatileShortOffset),
          shortValue,
          "Unsafe.getShortVolatile(Object, long)");

    long longValue = 1234567887654321L;
    Field volatileLongField = TestVolatileClass.class.getDeclaredField("volatileLongVar");
    long volatileLongOffset = unsafe.objectFieldOffset(volatileLongField);
    check(unsafe.getLongVolatile(tv, volatileLongOffset),
          0,
          "Unsafe.getLongVolatile(Object, long) - initial");
    unsafe.putLongVolatile(tv, volatileLongOffset, longValue);
    check(tv.volatileLongVar, longValue, "Unsafe.putLongVolatile(Object, long, long)");
    check(unsafe.getLongVolatile(tv, volatileLongOffset),
          longValue,
          "Unsafe.getLongVolatile(Object, long)");

    float floatValue = 123456.7890123f;
    Field volatileFloatField = TestVolatileClass.class.getDeclaredField("volatileFloatVar");
    long volatileFloatOffset = unsafe.objectFieldOffset(volatileFloatField);
    check(unsafe.getFloatVolatile(tv, volatileFloatOffset),
          0.0f,
          "Unsafe.getFloatVolatile(Object, long) - initial");
    unsafe.putFloatVolatile(tv, volatileFloatOffset, floatValue);
    check(tv.volatileFloatVar, floatValue, "Unsafe.putFloatVolatile(Object, long, float)");
    check(unsafe.getFloatVolatile(tv, volatileFloatOffset),
          floatValue,
          "Unsafe.getFloatVolatile(Object, long)");

    double doubleValue = 654321.7890123d;
    Field volatileDoubleField = TestVolatileClass.class.getDeclaredField("volatileDoubleVar");
    long volatileDoubleOffset = unsafe.objectFieldOffset(volatileDoubleField);
    check(unsafe.getDoubleVolatile(tv, volatileDoubleOffset),
          0.0d,
          "Unsafe.getDoubleVolatile(Object, double) - initial");
    unsafe.putDoubleVolatile(tv, volatileDoubleOffset, doubleValue);
    check(tv.volatileDoubleVar, doubleValue, "Unsafe.putDoubleVolatile(Object, long, double)");
    check(unsafe.getDoubleVolatile(tv, volatileDoubleOffset),
          doubleValue,
          "Unsafe.getDoubleVolatile(Object, long)");

    Object objectValue = new Object();
    Field volatileObjectField = TestVolatileClass.class.getDeclaredField("volatileObjectVar");
    long volatileObjectOffset = unsafe.objectFieldOffset(volatileObjectField);
    check(unsafe.getReferenceVolatile(tv, volatileObjectOffset),
          null,
          "Unsafe.getReferenceVolatile(Object, long) - initial");
    unsafe.putReferenceVolatile(tv, volatileObjectOffset, objectValue);
    check(tv.volatileObjectVar, objectValue, "Unsafe.putReferenceVolatile(Object, long, Object)");
    check(unsafe.getReferenceVolatile(tv, volatileObjectOffset),
          objectValue,
          "Unsafe.getReferenceVolatile(Object, long)");
  }

  private static void testGetAcquireAndPutRelease(Unsafe unsafe) throws NoSuchFieldException {
    TestVolatileClass tv = new TestVolatileClass();

    int intValue = 12345678;
    Field volatileIntField = TestVolatileClass.class.getDeclaredField("volatileIntVar");
    long volatileIntOffset = unsafe.objectFieldOffset(volatileIntField);
    check(unsafe.getIntAcquire(tv, volatileIntOffset),
          0,
          "Unsafe.getIntAcquire(Object, long) - initial");
    unsafe.putIntRelease(tv, volatileIntOffset, intValue);
    check(tv.volatileIntVar, intValue, "Unsafe.putIntRelease(Object, long, int)");
    check(unsafe.getIntAcquire(tv, volatileIntOffset),
          intValue,
          "Unsafe.getIntAcquire(Object, long)");

    long longValue = 1234567887654321L;
    Field volatileLongField = TestVolatileClass.class.getDeclaredField("volatileLongVar");
    long volatileLongOffset = unsafe.objectFieldOffset(volatileLongField);
    check(unsafe.getLongAcquire(tv, volatileLongOffset),
          0,
          "Unsafe.getLongAcquire(Object, long) - initial");
    unsafe.putLongRelease(tv, volatileLongOffset, longValue);
    check(tv.volatileLongVar, longValue, "Unsafe.putLongRelease(Object, long, long)");
    check(unsafe.getLongAcquire(tv, volatileLongOffset),
          longValue,
          "Unsafe.getLongAcquire(Object, long)");

    Object objectValue = new Object();
    Field volatileObjectField = TestVolatileClass.class.getDeclaredField("volatileObjectVar");
    long volatileObjectOffset = unsafe.objectFieldOffset(volatileObjectField);
    check(unsafe.getObjectAcquire(tv, volatileObjectOffset),
          null,
          "Unsafe.getObjectAcquire(Object, long) - initial");
    unsafe.putObjectRelease(tv, volatileObjectOffset, objectValue);
    check(tv.volatileObjectVar, objectValue, "Unsafe.putObjectRelease(Object, long, Object)");
    check(unsafe.getObjectAcquire(tv, volatileObjectOffset),
          objectValue,
          "Unsafe.getObjectAcquire(Object, long)");
  }

  private static void testCopyMemory(Unsafe unsafe) {
    final int size = 4 * 1024;

    final int intSize = 4;
    int[] inputInts = new int[size / intSize];
    for (int i = 0; i != inputInts.length; ++i) {
      inputInts[i] = ((int)i) + 1;
    }
    int[] outputInts = new int[size / intSize];
    unsafe.copyMemory(inputInts, Unsafe.ARRAY_INT_BASE_OFFSET,
                      outputInts, Unsafe.ARRAY_INT_BASE_OFFSET,
                      size);
    for (int i = 0; i != inputInts.length; ++i) {
      check(inputInts[i], outputInts[i], "unsafe.copyMemory/int");
    }

    final int longSize = 8;
    long[] inputLongs = new long[size / longSize];
    for (int i = 0; i != inputLongs.length; ++i) {
      inputLongs[i] = ((long)i) + 1L;
    }
    long[] outputLongs = new long[size / longSize];
    unsafe.copyMemory(inputLongs, Unsafe.ARRAY_LONG_BASE_OFFSET,
                      outputLongs, Unsafe.ARRAY_LONG_BASE_OFFSET,
                      size);
    for (int i = 0; i != inputLongs.length; ++i) {
      check(inputLongs[i], outputLongs[i], "unsafe.copyMemory/long");
    }

    final int floatSize = 4;
    float[] inputFloats = new float[size / floatSize];
    for (int i = 0; i != inputFloats.length; ++i) {
      inputFloats[i] = ((float)i) + 0.5f;
    }
    float[] outputFloats = new float[size / floatSize];
    unsafe.copyMemory(inputFloats, Unsafe.ARRAY_FLOAT_BASE_OFFSET,
                      outputFloats, Unsafe.ARRAY_FLOAT_BASE_OFFSET,
                      size);
    for (int i = 0; i != inputFloats.length; ++i) {
      check(inputFloats[i], outputFloats[i], "unsafe.copyMemory/float");
    }

    final int doubleSize = 8;
    double[] inputDoubles = new double[size / doubleSize];
    for (int i = 0; i != inputDoubles.length; ++i) {
      inputDoubles[i] = ((double)i) + 0.5;
    }
    double[] outputDoubles = new double[size / doubleSize];
    unsafe.copyMemory(inputDoubles, Unsafe.ARRAY_DOUBLE_BASE_OFFSET,
                      outputDoubles, Unsafe.ARRAY_DOUBLE_BASE_OFFSET,
                      size);
    for (int i = 0; i != inputDoubles.length; ++i) {
      check(inputDoubles[i], outputDoubles[i], "unsafe.copyMemory/double");
    }

    // check the version that works with memory pointers
    try (TestMemoryPtr srcPtr = new TestMemoryPtr(size);
         TestMemoryPtr dstPtr = new TestMemoryPtr(size)) {
        // use the integer array to fill the source
        unsafe.copyMemory(inputInts, Unsafe.ARRAY_INT_BASE_OFFSET,
                          null, srcPtr.get(),
                          size);

        unsafe.copyMemory(srcPtr.get(), dstPtr.get(), size);
        for (int i = 0; i != size; ++i) {
          check(unsafe.getByte(srcPtr.get() + i),
                unsafe.getByte(dstPtr.get() + i),
                "unsafe.copyMemory/memoryAddress");
        }
    }

    try {
        TestClass srcObj = new TestClass();
        srcObj.intVar = 12345678;
        int[] dstArray = new int[1];
        unsafe.copyMemory(srcObj, unsafe.objectFieldOffset(TestClass.class, "intVar"),
                          dstArray, Unsafe.ARRAY_INT_BASE_OFFSET,
                          4);
        expectThrow(RuntimeException.class, "unsafe.copyMemory/non-array-src");
    } catch (RuntimeException expected) {
    }

    try {
        int[] srcArray = { 12345678 };
        TestClass dstObj = new TestClass();
        unsafe.copyMemory(srcArray, Unsafe.ARRAY_INT_BASE_OFFSET,
                          dstObj, unsafe.objectFieldOffset(TestClass.class, "intVar"),
                          4);
        expectThrow(RuntimeException.class, "unsafe.copyMemory/non-array-dst");
    } catch (RuntimeException expected) {
    }
  }

  private static void testNullBasedAccessors(Unsafe unsafe) {
    long ptr = unsafe.allocateMemory(16);
    int intV = ThreadLocalRandom.current().nextInt();

    unsafe.putInt(/*o=*/ null, ptr, intV);
    check(unsafe.getInt(/*o=*/ null, ptr), intV, "putInt/getInt");

    unsafe.putIntVolatile(/*o=*/ null, ptr, intV + 1);
    check(unsafe.getIntVolatile(/*o=*/ null, ptr), intV + 1, "getInt/putInt volatile");

    check(unsafe.compareAndSetInt(/*o=*/ null, ptr, intV + 1, intV), true, "CAS int");
    check(unsafe.compareAndSetInt(/*o=*/ null, ptr, intV + 1, intV), false, "CAS int repeat");

    long longV = ThreadLocalRandom.current().nextLong();

    unsafe.putLong(/*o=*/ null, ptr, longV);
    check(unsafe.getLong(/*o=*/ null, ptr), longV, "getLong/putLong");

    unsafe.putLongVolatile(/*o=*/ null, ptr, longV + 1);
    check(unsafe.getLongVolatile(/*o=*/ null, ptr), longV + 1, "getLong/putLong volatile");
    check(unsafe.compareAndSetLong(/*o=*/ null, ptr, longV + 1, longV), true, "CAS long");
    check(unsafe.compareAndSetLong(/*o=*/ null, ptr, longV + 1, longV), false, "CAS long repeat");

    check(unsafe.compareAndExchangeLong(/*o=*/ null, ptr, longV, longV + 1), longV, "CAE long");
    check(
        unsafe.compareAndExchangeLong(/*o=*/ null, ptr, longV, longV + 1),
        longV + 1,
        "CAE long repeat");

    boolean booleanV = ThreadLocalRandom.current().nextBoolean();

    unsafe.putBoolean(/*o=*/ null, ptr, booleanV);
    check(unsafe.getBoolean(/*o=*/ null, ptr), booleanV, "putBoolean/getBoolean");
    unsafe.putBooleanVolatile(/*o=*/ null, ptr, !booleanV);
    check(unsafe.getBooleanVolatile(/*o=*/ null, ptr), !booleanV, "putBoolean/getBoolean volatile");

    byte byteV = (byte) ThreadLocalRandom.current().nextInt();

    unsafe.putByte(/*o=*/ null, ptr, byteV);
    check(unsafe.getByte(/*o=*/ null, ptr), byteV, "putByte/getByte");

    byteV = (byte) ThreadLocalRandom.current().nextInt();
    unsafe.putByteVolatile(/*o=*/ null, ptr, byteV);
    check(unsafe.getByteVolatile(/*o=*/ null, ptr), byteV, "putByte/getByte volatile");

    char charV = (char) ThreadLocalRandom.current().nextInt();

    unsafe.putChar(/*o=*/ null, ptr, charV);
    check(unsafe.getChar(/*o=*/ null, ptr), charV, "putChar/getChar");

    charV = (char) ThreadLocalRandom.current().nextInt();
    unsafe.putCharVolatile(/*o=*/ null, ptr, charV);
    check(unsafe.getCharVolatile(/*o=*/ null, ptr), charV, "putChar/getChar volatile");

    short shortV = (short) ThreadLocalRandom.current().nextInt();

    unsafe.putShort(/*o=*/ null, ptr, shortV);
    check(unsafe.getShort(/*o=*/ null, ptr), shortV, "putShort/getShort");

    shortV = (short) ThreadLocalRandom.current().nextInt();
    unsafe.putShortVolatile(/*o=*/ null, ptr, shortV);
    check(unsafe.getShortVolatile(/*o=*/ null, ptr), shortV, "putShort/getShort volatile");

    float floatV = ThreadLocalRandom.current().nextFloat();

    unsafe.putFloat(/*o=*/ null, ptr, floatV);
    check(
        Float.floatToRawIntBits(unsafe.getFloat(/*o=*/ null, ptr)),
        Float.floatToRawIntBits(floatV),
        "putFloat/getFloat");

    floatV = ThreadLocalRandom.current().nextFloat();

    unsafe.putFloatVolatile(/*o=*/ null, ptr, floatV);
    check(
        Float.floatToRawIntBits(unsafe.getFloatVolatile(/*o=*/ null, ptr)),
        Float.floatToRawIntBits(floatV),
        "putFloat/getFloat volatile");

    double doubleV = ThreadLocalRandom.current().nextDouble();

    unsafe.putDouble(/*o=*/ null, ptr, doubleV);
    check(
        Double.doubleToRawLongBits(unsafe.getDouble(/*o=*/ null, ptr)),
        Double.doubleToRawLongBits(doubleV),
        "putDouble/getDouble");

    doubleV = ThreadLocalRandom.current().nextDouble();

    unsafe.putDoubleVolatile(/*o=*/ null, ptr, doubleV);
    check(
        Double.doubleToRawLongBits(unsafe.getDoubleVolatile(/*o=*/ null, ptr)),
        Double.doubleToRawLongBits(doubleV),
        "putDouble/getDouble volatile");
  }

  private static void testConstants(Unsafe unsafe) {
    check(Unsafe.ADDRESS_SIZE, unsafe.addressSize(), "ADDRESS_SIZE vs addressSize()");
    check(
        Unsafe.ARRAY_BOOLEAN_BASE_OFFSET,
        unsafe.arrayBaseOffset(boolean[].class),
        "boolean array offset");
    check(
        Unsafe.ARRAY_BYTE_BASE_OFFSET,
        unsafe.arrayBaseOffset(byte[].class),
        "byte array offset");
    check(
        Unsafe.ARRAY_SHORT_BASE_OFFSET,
        unsafe.arrayBaseOffset(short[].class),
        "short array offset");
    check(
        Unsafe.ARRAY_CHAR_BASE_OFFSET,
        unsafe.arrayBaseOffset(char[].class),
        "char array offset");
    check(
        Unsafe.ARRAY_INT_BASE_OFFSET,
        unsafe.arrayBaseOffset(int[].class),
        "int array offset");
    check(
        Unsafe.ARRAY_LONG_BASE_OFFSET,
        unsafe.arrayBaseOffset(long[].class),
        "long array offset");
    check(
        Unsafe.ARRAY_FLOAT_BASE_OFFSET,
        unsafe.arrayBaseOffset(float[].class),
        "float array offset");
    check(
        Unsafe.ARRAY_DOUBLE_BASE_OFFSET,
        unsafe.arrayBaseOffset(double[].class),
        "double array offset");
    check(
        Unsafe.ARRAY_OBJECT_BASE_OFFSET,
        unsafe.arrayBaseOffset(Object[].class),
        "Object array offset");
  }


  private static class Holder {
    Object obj;           // 8
    int intField;         // 12
    long longField;       // 16
    double doubleField;   // 24
    float floatField;     // 32
    char charField;       // 36
    short shortField;     // 38
    boolean booleanField; // 40
    byte byteField;       // 41
  }

  private static void testGetAndAdd(Unsafe unsafe) {
    long fieldOffset = 16L;
    check(
        unsafe.objectFieldOffset(Holder.class, "longField"),
        fieldOffset,
        "unsafe.objectFieldOffset");

    Holder obj = new Holder();
    long val = 1L << 40 - 1;
    // Assuming that offset of `field` is 8 bytes and passing it as a constant value explicitly.
    check(unsafe.getAndAddLong(obj, fieldOffset, val), 0, "unsafe.getAndAddLong");
    check(obj.longField, val, "obj.field");
    check(unsafe.getAndAddLong(obj, fieldOffset, val), val, "unsafe.getAndAddLong");
    check(obj.longField, 2 * val, "obj.field");
  }

  private static void testGetBoolean(Unsafe unsafe) {
    long fieldOffset = 40L;
    check(
        unsafe.objectFieldOffset(Holder.class, "booleanField"),
        fieldOffset,
        "unsafe.objectFieldOffset");

    Holder holder = new Holder();
    int length = 100_000;
    boolean[] arr = new boolean[length];
    long lastElementOffset = 12 + 1 * (length - 1);

    boolean val = true;
    holder.booleanField = val;
    arr[length - 1] = val;

    check(unsafe.getBoolean(holder, fieldOffset), val, "unsafe.getBoolean");
    check(unsafe.getBoolean(arr, lastElementOffset), val, "unsafe.getBoolean");

    check(unsafe.getBooleanVolatile(holder, fieldOffset), val, "unsafe.getBooleanVolatile");
    check(unsafe.getBooleanVolatile(arr, lastElementOffset), val, "unsafe.getBooleanVolatile");
  }

  private static void testPutBoolean(Unsafe unsafe) {
    long fieldOffset = 40L;
    check(
        unsafe.objectFieldOffset(Holder.class, "booleanField"),
        fieldOffset,
        "unsafe.objectFieldOffset");

    Holder holder = new Holder();
    int length = 100_000;
    boolean[] arr = new boolean[length];
    long lastElementOffset = 12 + 1 * (length - 1);

    {
      boolean val = true;
      unsafe.putBoolean(holder, fieldOffset, val);
      unsafe.putBoolean(arr, lastElementOffset, val);

      check(holder.booleanField, val, "unsafe.putBoolean");
      check(arr[length - 1], val, "unsafe.putBoolean");
    }
    {
      boolean val = false;
      unsafe.putBooleanVolatile(holder, fieldOffset, val);
      unsafe.putBooleanVolatile(arr, lastElementOffset, val);

      check(holder.booleanField, val, "unsafe.putBooleanVolatile");
      check(arr[length - 1], val, "unsafe.putBooleanVolatile");
    }
  }

  private static void testGetByte(Unsafe unsafe) {
    long fieldOffset = 41L;
    check(
        unsafe.objectFieldOffset(Holder.class, "byteField"),
        fieldOffset,
        "unsafe.objectFieldOffset");

    Holder holder = new Holder();
    int length = 100_000;
    byte[] arr = new byte[length];
    long lastElementOffset = 12 + 1 * (length - 1);

    byte val = (byte) ThreadLocalRandom.current().nextLong();
    holder.byteField = val;
    arr[length - 1] = val;

    check(unsafe.getByte(holder, fieldOffset), val, "unsafe.getByte");
    check(unsafe.getByte(arr, lastElementOffset), val, "unsage.getByte");

    check(unsafe.getByteVolatile(holder, fieldOffset), val, "unsafe.getByteVolatile");
    check(unsafe.getByteVolatile(arr, lastElementOffset), val, "unsage.getByteVolatile");
  }

  private static void testPutByte(Unsafe unsafe) {
    long fieldOffset = 41L;
    check(
        unsafe.objectFieldOffset(Holder.class, "byteField"),
        fieldOffset,
        "unsafe.objectFieldOffset");

    Holder holder = new Holder();
    int length = 100_000;
    byte[] arr = new byte[length];
    long lastElementOffset = 12 + 1 * (length - 1);

    {
      byte val = (byte) ThreadLocalRandom.current().nextLong();

      unsafe.putByte(holder, fieldOffset, val);
      unsafe.putByte(arr, lastElementOffset, val);
      check(holder.byteField, val, "unsafe.putByte");
      check(arr[length - 1], val, "unsafe.putByte");
    }

    {
      byte val = (byte) ThreadLocalRandom.current().nextLong();

      unsafe.putByteVolatile(holder, fieldOffset, val);
      unsafe.putByteVolatile(arr, lastElementOffset, val);
      check(holder.byteField, val, "unsafe.putByteVolatile");
      check(arr[length - 1], val, "unsafe.putByteVolatile");
    }
  }

  private static void testGetChar(Unsafe unsafe) {
    long fieldOffset = 36L;
    check(
        unsafe.objectFieldOffset(Holder.class, "charField"),
        fieldOffset,
        "unsafe.objectFieldOffset");

    Holder holder = new Holder();
    int length = 100_000;
    char[] arr = new char[length];
    long lastElementOffset = 12 + 2 * (length - 1);

    char val = (char) ThreadLocalRandom.current().nextLong();
    holder.charField = val;
    arr[length - 1] = val;

    check(unsafe.getChar(holder, fieldOffset), val, "unsafe.getChar");
    check(unsafe.getChar(arr, lastElementOffset), val, "unsage.getChar");

    check(unsafe.getCharVolatile(holder, fieldOffset), val, "unsafe.getCharVolatile");
    check(unsafe.getCharVolatile(arr, lastElementOffset), val, "unsage.getCharVolatile");
  }

  private static void testPutChar(Unsafe unsafe) {
    long fieldOffset = 36L;
    check(
        unsafe.objectFieldOffset(Holder.class, "charField"),
        fieldOffset,
        "unsafe.objectFieldOffset");

    Holder holder = new Holder();
    int length = 100_000;
    char[] arr = new char[length];
    long lastElementOffset = 12 + 2 * (length - 1);

    {
      char val = (char) ThreadLocalRandom.current().nextLong();

      unsafe.putChar(holder, fieldOffset, val);
      unsafe.putChar(arr, lastElementOffset, val);
      check(holder.charField, val, "unsafe.putChar");
      check(arr[length - 1], val, "unsafe.putChar");
    }

    {
      char val = (char) ThreadLocalRandom.current().nextLong();

      unsafe.putCharVolatile(holder, fieldOffset, val);
      unsafe.putCharVolatile(arr, lastElementOffset, val);
      check(holder.charField, val, "unsafe.putCharVolatile");
      check(arr[length - 1], val, "unsafe.putCharVolatile");
    }
  }

  private static void testGetShort(Unsafe unsafe) {
    long fieldOffset = 38L;
    check(
        unsafe.objectFieldOffset(Holder.class, "shortField"),
        fieldOffset,
        "unsafe.objectFieldOffset");

    Holder holder = new Holder();
    int length = 100_000;
    short[] arr = new short[length];
    long lastElementOffset = 12 + 2 * (length - 1);

    short val = (short) ThreadLocalRandom.current().nextLong();
    holder.shortField = val;
    arr[length - 1] = val;

    check(unsafe.getShort(holder, fieldOffset), val, "unsafe.getShort");
    check(unsafe.getShort(arr, lastElementOffset), val, "unsage.getShort");

    check(unsafe.getShortVolatile(holder, fieldOffset), val, "unsafe.getShortVolatile");
    check(unsafe.getShortVolatile(arr, lastElementOffset), val, "unsage.getShortVolatile");
  }

  private static void testPutShort(Unsafe unsafe) {
    long fieldOffset = 38L;
    check(
        unsafe.objectFieldOffset(Holder.class, "shortField"),
        fieldOffset,
        "unsafe.objectFieldOffset");

    Holder holder = new Holder();
    int length = 100_000;
    short[] arr = new short[length];
    long lastElementOffset = 12 + 2 * (length - 1);

    {
      short val = (short) ThreadLocalRandom.current().nextLong();

      unsafe.putShort(holder, fieldOffset, val);
      unsafe.putShort(arr, lastElementOffset, val);
      check(holder.shortField, val, "unsafe.putShort");
      check(arr[length - 1], val, "unsafe.putShort");
    }

    {
      short val = (short) ThreadLocalRandom.current().nextLong();

      unsafe.putShortVolatile(holder, fieldOffset, val);
      unsafe.putShortVolatile(arr, lastElementOffset, val);
      check(holder.shortField, val, "unsafe.putShortVolatile");
      check(arr[length - 1], val, "unsafe.putShortVolatile");
    }
  }

  private static void testGetInt(Unsafe unsafe) {
    long fieldOffset = 12L;
    check(
        unsafe.objectFieldOffset(Holder.class, "intField"),
        fieldOffset,
        "unsafe.objectFieldOffset");

    Holder holder = new Holder();
    int length = 100_000;
    int[] arr = new int[length];
    long lastElementOffset = 12 + 4 * (length - 1);

    int val = ThreadLocalRandom.current().nextInt();
    holder.intField = val;
    arr[length - 1] = val;

    check(unsafe.getInt(holder, fieldOffset), val, "unsafe.getInt");
    check(unsafe.getInt(arr, lastElementOffset), val, "unsage.getInt");

    check(unsafe.getIntAcquire(holder, fieldOffset), val, "unsafe.getIntAcquire");
    check(unsafe.getIntAcquire(arr, lastElementOffset), val, "unsage.getIntAcquire");

    check(unsafe.getIntOpaque(holder, fieldOffset), val, "unsafe.getIntOpaque");
    check(unsafe.getIntOpaque(arr, lastElementOffset), val, "unsage.getIntOpaque");

    check(unsafe.getIntVolatile(holder, fieldOffset), val, "unsafe.getIntVolatile");
    check(unsafe.getIntVolatile(arr, lastElementOffset), val, "unsage.getIntVolatile");
  }

  private static void testPutInt(Unsafe unsafe) {
    long fieldOffset = 12L;
    check(
        unsafe.objectFieldOffset(Holder.class, "intField"),
        fieldOffset,
        "unsafe.objectFieldOffset");

    Holder holder = new Holder();
    int length = 100_000;
    int[] arr = new int[length];
    long lastElementOffset = 12 + 4 * (length - 1);

    {
      int val = ThreadLocalRandom.current().nextInt();

      unsafe.putInt(holder, fieldOffset, val);
      unsafe.putInt(arr, lastElementOffset, val);
      check(holder.intField, val, "unsafe.putInt");
      check(arr[length - 1], val, "unsafe.putInt");
    }

    {
      int val = ThreadLocalRandom.current().nextInt();

      unsafe.putIntRelease(holder, fieldOffset, val);
      unsafe.putIntRelease(arr, lastElementOffset, val);
      check(holder.intField, val, "unsafe.putIntRelease");
      check(arr[length - 1], val, "unsafe.putIntRelease");
    }

    {
      int val = ThreadLocalRandom.current().nextInt();

      unsafe.putIntOpaque(holder, fieldOffset, val);
      unsafe.putIntOpaque(arr, lastElementOffset, val);
      check(holder.intField, val, "unsafe.putIntOpaque");
      check(arr[length - 1], val, "unsafe.putIntOpaque");
    }

    {
      int val = ThreadLocalRandom.current().nextInt();

      unsafe.putIntVolatile(holder, fieldOffset, val);
      unsafe.putIntVolatile(arr, lastElementOffset, val);
      check(holder.intField, val, "unsafe.putIntVolatile");
      check(arr[length - 1], val, "unsafe.putIntVolatile");
    }
  }

  private static void testGetFloat(Unsafe unsafe) {
    long fieldOffset = 32L;
    check(
        unsafe.objectFieldOffset(Holder.class, "floatField"),
        fieldOffset,
        "unsafe.objectFieldOffset");

    Holder holder = new Holder();
    int length = 100_000;
    float[] arr = new float[length];
    long lastElementOffset = 12 + 4 * (length - 1);

    float val = ThreadLocalRandom.current().nextFloat();
    holder.floatField = val;
    arr[length - 1] = val;

    check(unsafe.getFloat(holder, fieldOffset), val, "unsafe.getFloat");
    check(unsafe.getFloat(arr, lastElementOffset), val, "unsage.getFloat");

    check(unsafe.getFloatVolatile(holder, fieldOffset), val, "unsafe.getFloatVolatile");
    check(unsafe.getFloatVolatile(arr, lastElementOffset), val, "unsage.getFloatVolatile");
  }

  private static void testPutFloat(Unsafe unsafe) {
    long fieldOffset = 32L;
    check(
        unsafe.objectFieldOffset(Holder.class, "floatField"),
        fieldOffset,
        "unsafe.objectFieldOffset");

    Holder holder = new Holder();
    int length = 100_000;
    float[] arr = new float[length];
    long lastElementOffset = 12 + 4 * (length - 1);

    {
      float val = ThreadLocalRandom.current().nextFloat();

      unsafe.putFloat(holder, fieldOffset, val);
      unsafe.putFloat(arr, lastElementOffset, val);
      check(holder.floatField, val, "unsafe.putFloat");
      check(arr[length - 1], val, "unsafe.putFloat");
    }

    {
      float val = ThreadLocalRandom.current().nextFloat();

      unsafe.putFloatVolatile(holder, fieldOffset, val);
      unsafe.putFloatVolatile(arr, lastElementOffset, val);
      check(holder.floatField, val, "unsafe.putFloatVolatile");
      check(arr[length - 1], val, "unsafe.putFloatVolatile");
    }
  }

  private static void testGetLong(Unsafe unsafe) {
    long fieldOffset = 16L;
    check(
        unsafe.objectFieldOffset(Holder.class, "longField"),
        fieldOffset,
        "unsafe.objectFieldOffset");

    Holder holder = new Holder();
    int length = 100_000;
    long[] arr = new long[length];
    long lastElementOffset = 12 + 4 + 8 * (length - 1);

    long val = ThreadLocalRandom.current().nextLong();
    holder.longField = val;
    arr[length - 1] = val;

    check(unsafe.getLong(holder, fieldOffset), val, "unsafe.getLong");
    check(unsafe.getLong(arr, lastElementOffset), val, "unsage.getLong");

    check(unsafe.getLongAcquire(holder, fieldOffset), val, "unsafe.getLongAcquire");
    check(unsafe.getLongAcquire(arr, lastElementOffset), val, "unsage.getLongAcquire");

    check(unsafe.getLongOpaque(holder, fieldOffset), val, "unsafe.getLongOpaque");
    check(unsafe.getLongOpaque(arr, lastElementOffset), val, "unsage.getLongOpaque");

    check(unsafe.getLongVolatile(holder, fieldOffset), val, "unsafe.getLongVolatile");
    check(unsafe.getLongVolatile(arr, lastElementOffset), val, "unsage.getLongVolatile");
  }

  private static void testPutLong(Unsafe unsafe) {
    long fieldOffset = 16L;
    check(
        unsafe.objectFieldOffset(Holder.class, "longField"),
        fieldOffset,
        "unsafe.objectFieldOffset");

    Holder holder = new Holder();
    int length = 100_000;
    long[] arr = new long[length];
    long lastElementOffset = 12 + 4 + 8 * (length - 1);

    {
      long val = ThreadLocalRandom.current().nextLong();

      unsafe.putLong(holder, fieldOffset, val);
      unsafe.putLong(arr, lastElementOffset, val);
      check(holder.longField, val, "unsafe.putLong");
      check(arr[length - 1], val, "unsafe.putLong");
    }

    {
      long val = ThreadLocalRandom.current().nextLong();

      unsafe.putLongRelease(holder, fieldOffset, val);
      unsafe.putLongRelease(arr, lastElementOffset, val);
      check(holder.longField, val, "unsafe.putLongRelease");
      check(arr[length - 1], val, "unsafe.putLongRelease");
    }

    {
      long val = ThreadLocalRandom.current().nextLong();

      unsafe.putLongOpaque(holder, fieldOffset, val);
      unsafe.putLongOpaque(arr, lastElementOffset, val);
      check(holder.longField, val, "unsafe.putLongOpaque");
      check(arr[length - 1], val, "unsafe.putLongOpaque");
    }

    {
      long val = ThreadLocalRandom.current().nextLong();

      unsafe.putLongVolatile(holder, fieldOffset, val);
      unsafe.putLongVolatile(arr, lastElementOffset, val);
      check(holder.longField, val, "unsafe.putLongVolatile");
      check(arr[length - 1], val, "unsafe.putLongVolatile");
    }
  }

  private static void testGetDouble(Unsafe unsafe) {
    long fieldOffset = 24L;
    check(
        unsafe.objectFieldOffset(Holder.class, "doubleField"),
        fieldOffset,
        "unsafe.objectFieldOffset");

    Holder holder = new Holder();
    int length = 100_000;
    double[] arr = new double[length];
    long lastElementOffset = 12 + 4 + 8 * (length - 1);

    double val = (double) ThreadLocalRandom.current().nextLong();
    holder.doubleField = val;
    arr[length - 1] = val;

    check(unsafe.getDouble(holder, fieldOffset), val, "unsafe.getDouble");
    check(unsafe.getDouble(arr, lastElementOffset), val, "unsage.getDouble");

    check(unsafe.getDoubleVolatile(holder, fieldOffset), val, "unsafe.getDoubleVolatile");
    check(unsafe.getDoubleVolatile(arr, lastElementOffset), val, "unsage.getDoubleVolatile");
  }

  private static void testPutDouble(Unsafe unsafe) {
    long fieldOffset = 24L;
    check(
        unsafe.objectFieldOffset(Holder.class, "doubleField"),
        fieldOffset,
        "unsafe.objectFieldOffset");

    Holder holder = new Holder();
    int length = 100_000;
    double[] arr = new double[length];
    long lastElementOffset = 12 + 4 + 8 * (length - 1);

    {
      double val = (double) ThreadLocalRandom.current().nextLong();

      unsafe.putDouble(holder, fieldOffset, val);
      unsafe.putDouble(arr, lastElementOffset, val);
      check(holder.doubleField, val, "unsafe.putDouble");
      check(arr[length - 1], val, "unsafe.putDouble");
    }

    {
      double val = (double) ThreadLocalRandom.current().nextLong();

      unsafe.putDoubleVolatile(holder, fieldOffset, val);
      unsafe.putDoubleVolatile(arr, lastElementOffset, val);
      check(holder.doubleField, val, "unsafe.putDoubleVolatile");
      check(arr[length - 1], val, "unsafe.putDoubleVolatile");
    }
  }

  private static void testAbsoluteAddress(Unsafe unsafe) {
    {
      byte val = (byte) ThreadLocalRandom.current().nextInt();
      unsafe.putByte(ADDRESS, val);
      check(unsafe.getByte(ADDRESS), val, "unsafe.getByte/unsafe.putByte");
    }
    {
      char val = (char) ThreadLocalRandom.current().nextInt();
      unsafe.putChar(ADDRESS, val);
      check(unsafe.getChar(ADDRESS), val, "unsafe.getChar/unsafe.putChar");
    }
    {
      short val = (short) ThreadLocalRandom.current().nextInt();
      unsafe.putShort(ADDRESS, val);
      check(unsafe.getShort(ADDRESS), val, "unsafe.getShort/unsafe.putShort");
    }
    {
      int val = ThreadLocalRandom.current().nextInt();
      unsafe.putInt(ADDRESS, val);
      check(unsafe.getInt(ADDRESS), val, "unsafe.getInt/unsafe.putInt");
    }
    {
      float val = ThreadLocalRandom.current().nextFloat();
      unsafe.putFloat(ADDRESS, val);
      check(unsafe.getFloat(ADDRESS), val, "unsafe.getFloat/unsafe.putFloat");
    }
    {
      long val = ThreadLocalRandom.current().nextLong();
      unsafe.putLong(ADDRESS, val);
      check(unsafe.getLong(ADDRESS), val, "unsafe.getLong/unsafe.putLong");
    }
    {
      double val = ThreadLocalRandom.current().nextDouble();
      unsafe.putDouble(ADDRESS, val);
      check(unsafe.getDouble(ADDRESS), val, "unsafe.getDouble/unsafe.putDouble");
    }
  }

  private static void testCasIntConstantOffset(Unsafe unsafe) {
    long fieldOffset = 12L;
    check(
        unsafe.objectFieldOffset(Holder.class, "intField"),
        fieldOffset,
        "unsafe.objectFieldOffset");

    Holder holder = new Holder();
    int length = 100_000;
    int[] arr = new int[length];
    long lastElementOffset = 12 + 4 * (length - 1);

    int val = ThreadLocalRandom.current().nextInt();
    check(unsafe.compareAndSetInt(holder, fieldOffset, 0, val), true, "unsafe.casint");
    check(holder.intField, val, "unsafe.casint");
    check(unsafe.compareAndSetInt(holder, fieldOffset, val, 0), true, "unsafe.castint");

    check(unsafe.compareAndSetInt(holder, fieldOffset, 0, val), true, "unsafe.casint");
    check(holder.intField, val, "unsafe.casint");
    check(unsafe.compareAndSetInt(holder, fieldOffset, val, 0), true, "unsafe.castint");

    check(unsafe.compareAndSetInt(arr, lastElementOffset, 0, val), true, "unsafe.casint");
    check(arr[length - 1], val, "unsafe.casint");
    check(unsafe.compareAndSetInt(arr, lastElementOffset, val, 0), true, "unsafe.casint");

    check(unsafe.compareAndSetInt(arr, lastElementOffset, 0, val), true, "unsafe.casint");
    check(arr[length - 1], val, "unsafe.casint");
    check(unsafe.compareAndSetInt(arr, lastElementOffset, val, 0), true, "unsafe.casint");
  }

  private static void testCasLongConstantOffset(Unsafe unsafe) {
    long fieldOffset = 16L;
    check(
        unsafe.objectFieldOffset(Holder.class, "longField"),
        fieldOffset,
        "unsafe.objectFieldOffset");

    Holder holder = new Holder();
    int length = 100_000;
    long[] arr = new long[length];
    long lastElementOffset = 12 + 4 + 8 * (length - 1);

    long val = ThreadLocalRandom.current().nextLong();
    check(unsafe.compareAndSetLong(holder, fieldOffset, 0, val), true, "unsafe.caslong");
    check(holder.longField, val, "unsafe.caslong");
    check(unsafe.compareAndSetLong(holder, fieldOffset, val, 0), true, "unsafe.caslong");

    check(unsafe.compareAndSetLong(holder, fieldOffset, 0, val), true, "unsafe.caslong");
    check(holder.longField, val, "unsafe.caslong");
    check(unsafe.compareAndSetLong(holder, fieldOffset, val, 0), true, "unsafe.caslong");

    check(unsafe.compareAndSetLong(arr, lastElementOffset, 0, val), true, "unsafe.caslong");
    check(arr[length - 1], val, "unsafe.caslong");
    check(unsafe.compareAndSetLong(arr, lastElementOffset, val, 0), true, "unsafe.caslong");

    check(unsafe.compareAndSetLong(arr, lastElementOffset, 0, val), true, "unsafe.caslong");
    check(arr[length - 1], val, "unsafe.caslong");
    check(unsafe.compareAndSetLong(arr, lastElementOffset, val, 0), true, "unsafe.caslong");
  }

  private static void testCasReferenceConstantOffset(Unsafe unsafe) {
    long fieldOffset = 8L;
    check(unsafe.objectFieldOffset(Holder.class, "obj"), fieldOffset, "unsafe.objectFieldOffset");

    Holder holder = new Holder();
    int length = 100_000;
    Object[] arr = new Object[length];
    long lastElementOffset = 12 + 4 * (length - 1);

    Object obj = new Object();

    check(unsafe.compareAndSetReference(holder, fieldOffset, null, obj), true, "unsafe.casref");
    check(holder.obj, obj, "unsafe.casreference");
    check(unsafe.compareAndSetReference(holder, fieldOffset, obj, null), true, "unsafe.casref");

    check(unsafe.compareAndSetReference(holder, fieldOffset, null, obj), true, "unsafe.casref");
    check(holder.obj, obj, "unsafe.casreference");
    check(unsafe.compareAndSetReference(holder, fieldOffset, obj, null), true, "unsafe.casref");

    check(unsafe.compareAndSetReference(arr, lastElementOffset, null, obj), true, "unsafe.casref");
    check(arr[length - 1], obj, "unsafe.casref");
    check(unsafe.compareAndSetReference(arr, lastElementOffset, obj, null), true, "unsafe.casref");

    check(unsafe.compareAndSetReference(arr, lastElementOffset, null, obj), true, "unsafe.casref");
    check(arr[length - 1], obj, "unsafe.casref");
    check(unsafe.compareAndSetReference(arr, lastElementOffset, obj, null), true, "unsafe.casref");
  }

  private static void testGetReference(Unsafe unsafe) {
    check(unsafe.objectFieldOffset(Holder.class, "obj"), 8L, "unsafe.objectFieldOffset");

    Holder holder = new Holder();
    int length = 100_000;
    Object[] arr = new Object[length];
    long lastElementOffset = 12 + 4 * (length - 1);

    Object obj = new Object();
    holder.obj = obj;
    arr[length - 1] = obj;

    check(unsafe.getReference(holder, 8), obj, "unsafe.getReference");
    check(unsafe.getReference(arr, lastElementOffset), obj, "unsafe.getReference");

    check(unsafe.getReferenceAcquire(holder, 8), obj, "unsafe.getReferenceAcquire");
    check(unsafe.getReferenceAcquire(arr, lastElementOffset), obj, "unsafe.getReferenceAcquire");

    check(unsafe.getReferenceOpaque(holder, 8), obj, "unsafe.getReferenceOpaque");
    check(unsafe.getReferenceOpaque(arr, lastElementOffset), obj, "unsafe.getReferenceOpaque");

    check(unsafe.getReferenceVolatile(holder, 8), obj, "unsafe.getReferenceVolatile");
    check(unsafe.getReferenceVolatile(arr, lastElementOffset), obj, "unsafe.getReferenceVolatile");
  }

  private static void testPutReference(Unsafe unsafe) {
    check(unsafe.objectFieldOffset(Holder.class, "obj"), 8L, "unsafe.objectFieldOffset");

    Holder holder = new Holder();
    int length = 100_000;
    Object[] arr = new Object[length];
    long lastElementOffset = 12 + 4 * (length - 1);

    {
      Object obj = new Object();

      unsafe.putReference(holder, 8, obj);
      unsafe.putReference(arr, lastElementOffset, obj);
      check(holder.obj, obj, "unsafe.putReference");
      check(arr[length - 1], obj, "unsafe.putReference");
    }

    {
      Object obj = new Object();

      unsafe.putReferenceRelease(holder, 8, obj);
      unsafe.putReferenceRelease(arr, lastElementOffset, obj);
      check(holder.obj, obj, "unsafe.putReferenceRelease");
      check(arr[length - 1], obj, "unsafe.putReferenceRelease");
    }

    {
      Object obj = new Object();

      unsafe.putReferenceOpaque(holder, 8, obj);
      unsafe.putReferenceOpaque(arr, lastElementOffset, obj);
      check(holder.obj, obj, "unsafe.putReferenceOpaque");
      check(arr[length - 1], obj, "unsafe.putReferenceOpaque");
    }
    {
      Object obj = new Object();

      unsafe.putReferenceVolatile(holder, 8, obj);
      unsafe.putReferenceVolatile(arr, lastElementOffset, obj);
      check(holder.obj, obj, "unsafe.putReferenceVolatile");
      check(arr[length - 1], obj, "unsafe.putReferenceVolatile");
    }
  }

  private static class TestClass {
    public int intVar = 0;
    public long longVar = 0;
    public Object objectVar = null;
  }

  private static void testFinalFields() throws NoSuchFieldException {
    Field theUnsafeField = Unsafe.class.getDeclaredField("theUnsafe");
    theUnsafeField.setAccessible(true);

    try {
      theUnsafeField.set(null, null);
      throw new AssertionError("IAE was expected");
    } catch (IllegalAccessException expected) {}

    Field longArrayBaseOffsetField = Unsafe.class.getDeclaredField("ARRAY_LONG_BASE_OFFSET");
    longArrayBaseOffsetField.setAccessible(true);

    try {
      longArrayBaseOffsetField.set(null, 0);
      throw new AssertionError("IAE was expected");
    } catch (IllegalAccessException expected) {}
  }

  private static class TestVolatileClass {
    public volatile int volatileIntVar = 0;
    public volatile boolean volatileBooleanVar = false;
    public volatile byte volatileByteVar = 0;
    public volatile short volatileShortVar = 0;
    public volatile char volatileCharVar = 0;
    public volatile long volatileLongVar = 0;
    public volatile float volatileFloatVar = 0.0f;
    public volatile double volatileDoubleVar = 0.0d;
    public volatile Object volatileObjectVar = null;
  }

  private static class TestMemoryPtr implements AutoCloseable {
      private long ptr = 0;

      public TestMemoryPtr(int size) {
          ptr = jdkUnsafeTestMalloc(size);
      }

      public long get() {
          return ptr;
      }

      @Override
      public void close() {
          if (ptr != 0) {
              jdkUnsafeTestFree(ptr);
              ptr = 0;
          }
      }
  }

  private static native int vmJdkArrayBaseOffset(Class<?> clazz);
  private static native int vmJdkArrayIndexScale(Class<?> clazz);
  private static native long jdkUnsafeTestMalloc(long size);
  private static native void jdkUnsafeTestFree(long memory);

  private native static void ensureJitCompiled(Class<?> clazz, String method);
}
