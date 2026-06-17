/*
 * Copyright (C) 2026 The Android Open Source Project
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

// TODO: Due to missing flagging support in checker, checker tests for
// `com::android::art::rw::flags::packed_switch_simplification()` are disabled.
// Enable them when we implement flagging support or during the flag cleanup.
public class PackedSwitch {
    public static void main() {
        // Test creating constant tables of different types with the default case throwing `Error`.
        // Note: For narrow types, we're using `int` as the return type
        // but still expect the table to use a narrow type to save space.
        $noinline$testSwitchToTableBoolean();
        $noinline$testSwitchToTableByte();
        $noinline$testSwitchToTableUint8();
        $noinline$testSwitchToTableShort();
        $noinline$testSwitchToTableChar();
        $noinline$testSwitchToTableInt();
        $noinline$testSwitchToTableLong();
        $noinline$testSwitchToTableFloat();
        $noinline$testSwitchToTableDouble();

        // A variation of the above tests with `Return`s. Some of these tests merge results
        // with a phi feeding a single return, other tests have multiple returns.
        $noinline$testSwitchReturnToTableBoolean();
        $noinline$testSwitchReturnToTableByte();
        $noinline$testSwitchReturnToTableUint8();
        $noinline$testSwitchReturnToTableShort();
        $noinline$testSwitchReturnToTableChar();
        $noinline$testSwitchReturnToTableInt();
        $noinline$testSwitchReturnToTableLong();
        $noinline$testSwitchReturnToTableFloat();
        $noinline$testSwitchReturnToTableDouble();

        // Test creating constant tables (`int` only) with the default case included.
        $noinline$testSwitchToTableIntWithDefault0();
        $noinline$testSwitchToTableIntWithDefault1();
        $noinline$testSwitchToTableIntWithDefault2();

        // A variation of the above tests with `Return`s in various configurations.
        $noinline$testSwitchReturnToTableIntWithDefault0();
        $noinline$testSwitchReturnToTableIntWithDefault1();
        $noinline$testSwitchReturnToTableIntWithDefault2();
    }

    public static void $noinline$testSwitchToTableBoolean() {
        try {
            $noinline$switchToTableBoolean(0);
        } catch (Error expected) {}
        assertEquals(1, $noinline$switchToTableBoolean(1));
        assertEquals(0, $noinline$switchToTableBoolean(2));
        assertEquals(0, $noinline$switchToTableBoolean(3));
        assertEquals(1, $noinline$switchToTableBoolean(4));
        assertEquals(0, $noinline$switchToTableBoolean(5));
        try {
            $noinline$switchToTableBoolean(6);
        } catch (Error expected) {}
    }

    /// CHECK-START: int PackedSwitch.$noinline$switchToTableBoolean(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch
    /// CHECK:              Phi

    // CHECK-START: int PackedSwitch.$noinline$switchToTableBoolean(int) control_flow_simplifier (after)
    // CHECK: {{z\d+}}     LoadConstantTableEntry

    // CHECK-START: int PackedSwitch.$noinline$switchToTableBoolean(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static int $noinline$switchToTableBoolean(int value) {
        final int result;
        switch (value) {
            case 1: result = 1; break;
            case 2: result = 0; break;
            case 3: result = 0; break;
            case 4: result = 1; break;
            case 5: result = 0; break;
            default: throw new Error();
        }
        $noinline$nop();  // Avoid the return pattern.
        return result;
    }

    public static void $noinline$testSwitchToTableByte() {
        try {
            $noinline$switchToTableByte(0);
        } catch (Error expected) {}
        assertEquals(42, $noinline$switchToTableByte(1));
        assertEquals(88, $noinline$switchToTableByte(2));
        assertEquals(-7, $noinline$switchToTableByte(3));
        assertEquals(11, $noinline$switchToTableByte(4));
        assertEquals(123, $noinline$switchToTableByte(5));
        try {
            $noinline$switchToTableByte(6);
        } catch (Error expected) {}
    }

    /// CHECK-START: int PackedSwitch.$noinline$switchToTableByte(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch
    /// CHECK:              Phi

    // CHECK-START: int PackedSwitch.$noinline$switchToTableByte(int) control_flow_simplifier (after)
    // CHECK: {{b\d+}}     LoadConstantTableEntry

    // CHECK-START: int PackedSwitch.$noinline$switchToTableByte(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static int $noinline$switchToTableByte(int value) {
        final int result;
        switch (value) {
            case 1: result = 42; break;
            case 2: result = 88; break;
            case 3: result = -7; break;
            case 4: result = 11; break;
            case 5: result = 123; break;
            default: throw new Error();
        }
        $noinline$nop();  // Avoid the return pattern.
        return result;
    }

    public static void $noinline$testSwitchToTableUint8() {
        try {
            $noinline$switchToTableUint8(0);
        } catch (Error expected) {}
        assertEquals(42, $noinline$switchToTableUint8(1));
        assertEquals(88, $noinline$switchToTableUint8(2));
        assertEquals(7, $noinline$switchToTableUint8(3));
        assertEquals(11, $noinline$switchToTableUint8(4));
        assertEquals(255, $noinline$switchToTableUint8(5));
        try {
            $noinline$switchToTableUint8(6);
        } catch (Error expected) {}
    }

    /// CHECK-START: int PackedSwitch.$noinline$switchToTableUint8(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch
    /// CHECK:              Phi

    // CHECK-START: int PackedSwitch.$noinline$switchToTableUint8(int) control_flow_simplifier (after)
    // CHECK: {{a\d+}}     LoadConstantTableEntry

    // CHECK-START: int PackedSwitch.$noinline$switchToTableUint8(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static int $noinline$switchToTableUint8(int value) {
        final int result;
        switch (value) {
            case 1: result = 42; break;
            case 2: result = 88; break;
            case 3: result = 7; break;
            case 4: result = 11; break;
            case 5: result = 255; break;
            default: throw new Error();
        }
        $noinline$nop();  // Avoid the return pattern.
        return result;
    }

    public static void $noinline$testSwitchToTableShort() {
        try {
            $noinline$switchToTableShort(0);
        } catch (Error expected) {}
        assertEquals(42, $noinline$switchToTableShort(1));
        assertEquals(88, $noinline$switchToTableShort(2));
        assertEquals(-7, $noinline$switchToTableShort(3));
        assertEquals(11, $noinline$switchToTableShort(4));
        assertEquals(12345, $noinline$switchToTableShort(5));
        try {
            $noinline$switchToTableShort(6);
        } catch (Error expected) {}
    }

    /// CHECK-START: int PackedSwitch.$noinline$switchToTableShort(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch
    /// CHECK:              Phi

    // CHECK-START: int PackedSwitch.$noinline$switchToTableShort(int) control_flow_simplifier (after)
    // CHECK: {{s\d+}}     LoadConstantTableEntry

    // CHECK-START: int PackedSwitch.$noinline$switchToTableShort(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static int $noinline$switchToTableShort(int value) {
        final int result;
        switch (value) {
            case 1: result = 42; break;
            case 2: result = 88; break;
            case 3: result = -7; break;
            case 4: result = 11; break;
            case 5: result = 12345; break;
            default: throw new Error();
        }
        $noinline$nop();  // Avoid the return pattern.
        return result;
    }

    public static void $noinline$testSwitchToTableChar() {
        try {
            $noinline$switchToTableChar(0);
        } catch (Error expected) {}
        assertEquals(42, $noinline$switchToTableChar(1));
        assertEquals(88, $noinline$switchToTableChar(2));
        assertEquals(7, $noinline$switchToTableChar(3));
        assertEquals(11, $noinline$switchToTableChar(4));
        assertEquals(54321, $noinline$switchToTableChar(5));
        try {
            $noinline$switchToTableChar(6);
        } catch (Error expected) {}
    }

    /// CHECK-START: int PackedSwitch.$noinline$switchToTableChar(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch
    /// CHECK:              Phi

    // CHECK-START: int PackedSwitch.$noinline$switchToTableChar(int) control_flow_simplifier (after)
    // CHECK: {{c\d+}}     LoadConstantTableEntry

    // CHECK-START: int PackedSwitch.$noinline$switchToTableChar(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static int $noinline$switchToTableChar(int value) {
        final int result;
        switch (value) {
            case 1: result = 42; break;
            case 2: result = 88; break;
            case 3: result = 7; break;
            case 4: result = 11; break;
            case 5: result = 54321; break;
            default: throw new Error();
        }
        $noinline$nop();  // Avoid the return pattern.
        return result;
    }

    public static void $noinline$testSwitchToTableInt() {
        try {
            $noinline$switchToTableInt(0);
        } catch (Error expected) {}
        assertEquals(42, $noinline$switchToTableInt(1));
        assertEquals(88, $noinline$switchToTableInt(2));
        assertEquals(-7, $noinline$switchToTableInt(3));
        assertEquals(11, $noinline$switchToTableInt(4));
        assertEquals(123456789, $noinline$switchToTableInt(5));
        try {
            $noinline$switchToTableInt(6);
        } catch (Error expected) {}
    }

    /// CHECK-START: int PackedSwitch.$noinline$switchToTableInt(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch
    /// CHECK:              Phi

    // CHECK-START: int PackedSwitch.$noinline$switchToTableInt(int) control_flow_simplifier (after)
    // CHECK: {{i\d+}}     LoadConstantTableEntry

    // CHECK-START: int PackedSwitch.$noinline$switchToTableInt(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static int $noinline$switchToTableInt(int value) {
        final int result;
        switch (value) {
            case 1: result = 42; break;
            case 2: result = 88; break;
            case 3: result = -7; break;
            case 4: result = 11; break;
            case 5: result = 123456789; break;
            default: throw new Error();
        }
        $noinline$nop();  // Avoid the return pattern.
        return result;
    }

    public static void $noinline$testSwitchToTableLong() {
        try {
            $noinline$switchToTableLong(0);
        } catch (Error expected) {}
        assertEquals(42L, $noinline$switchToTableLong(1));
        assertEquals(88L, $noinline$switchToTableLong(2));
        assertEquals(-7L, $noinline$switchToTableLong(3));
        assertEquals(11L, $noinline$switchToTableLong(4));
        assertEquals(123456789987654321L, $noinline$switchToTableLong(5));
        try {
            $noinline$switchToTableLong(6);
        } catch (Error expected) {}
    }

    /// CHECK-START: long PackedSwitch.$noinline$switchToTableLong(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch
    /// CHECK:              Phi

    // CHECK-START: long PackedSwitch.$noinline$switchToTableLong(int) control_flow_simplifier (after)
    // CHECK: {{j\d+}}     LoadConstantTableEntry

    // CHECK-START: long PackedSwitch.$noinline$switchToTableLong(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static long $noinline$switchToTableLong(int value) {
        final long result;
        switch (value) {
            case 1: result = 42L; break;
            case 2: result = 88L; break;
            case 3: result = -7L; break;
            case 4: result = 11L; break;
            case 5: result = 123456789987654321L; break;
            default: throw new Error();
        }
        $noinline$nop();  // Avoid the return pattern.
        return result;
    }

    public static void $noinline$testSwitchToTableFloat() {
        try {
            $noinline$switchToTableFloat(0);
        } catch (Error expected) {}
        assertEquals(42.0f, $noinline$switchToTableFloat(1));
        assertEquals(88.0f, $noinline$switchToTableFloat(2));
        assertEquals(-7.0f, $noinline$switchToTableFloat(3));
        assertEquals(11.0f, $noinline$switchToTableFloat(4));
        assertEquals(123456789.0f, $noinline$switchToTableFloat(5));
        assertTrue(Float.isNaN($noinline$switchToTableFloat(6)));
        try {
            $noinline$switchToTableFloat(7);
        } catch (Error expected) {}
    }

    /// CHECK-START: float PackedSwitch.$noinline$switchToTableFloat(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch
    /// CHECK:              Phi

    // CHECK-START: float PackedSwitch.$noinline$switchToTableFloat(int) control_flow_simplifier (after)
    // CHECK: {{f\d+}}     LoadConstantTableEntry

    // CHECK-START: float PackedSwitch.$noinline$switchToTableFloat(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static float $noinline$switchToTableFloat(int value) {
        final float result;
        switch (value) {
            case 1: result = 42.0f; break;
            case 2: result = 88.0f; break;
            case 3: result = -7.0f; break;
            case 4: result = 11.0f; break;
            case 5: result = 123456789.0f; break;
            case 6: result = Float.NaN; break;
            default: throw new Error();
        }
        $noinline$nop();  // Avoid the return pattern.
        return result;
    }

    public static void $noinline$testSwitchToTableDouble() {
        try {
            $noinline$switchToTableDouble(0);
        } catch (Error expected) {}
        assertEquals(42.0, $noinline$switchToTableDouble(1));
        assertEquals(88.0, $noinline$switchToTableDouble(2));
        assertEquals(-7.0, $noinline$switchToTableDouble(3));
        assertEquals(11.0, $noinline$switchToTableDouble(4));
        assertEquals(123456789.0, $noinline$switchToTableDouble(5));
        assertTrue(Double.isNaN($noinline$switchToTableDouble(6)));
        try {
            $noinline$switchToTableDouble(7);
        } catch (Error expected) {}
    }

    /// CHECK-START: double PackedSwitch.$noinline$switchToTableDouble(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch
    /// CHECK:              Phi

    // CHECK-START: double PackedSwitch.$noinline$switchToTableDouble(int) control_flow_simplifier (after)
    // CHECK: {{d\d+}}     LoadConstantTableEntry

    // CHECK-START: double PackedSwitch.$noinline$switchToTableDouble(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static double $noinline$switchToTableDouble(int value) {
        final double result;
        switch (value) {
            case 1: result = 42.0; break;
            case 2: result = 88.0; break;
            case 3: result = -7.0; break;
            case 4: result = 11.0; break;
            case 5: result = 123456789.0; break;
            case 6: result = Double.NaN; break;
            default: throw new Error();
        }
        $noinline$nop();  // Avoid the return pattern.
        return result;
    }

    public static void $noinline$testSwitchReturnToTableBoolean() {
        try {
            $noinline$switchReturnToTableBoolean(0);
        } catch (Error expected) {}
        assertEquals(1, $noinline$switchReturnToTableBoolean(1));
        assertEquals(0, $noinline$switchReturnToTableBoolean(2));
        assertEquals(0, $noinline$switchReturnToTableBoolean(3));
        assertEquals(1, $noinline$switchReturnToTableBoolean(4));
        assertEquals(0, $noinline$switchReturnToTableBoolean(5));
        try {
            $noinline$switchReturnToTableBoolean(6);
        } catch (Error expected) {}
    }

    /// CHECK-START: int PackedSwitch.$noinline$switchReturnToTableBoolean(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch
    /// CHECK:              Phi

    // CHECK-START: int PackedSwitch.$noinline$switchReturnToTableBoolean(int) control_flow_simplifier (after)
    // CHECK: {{z\d+}}     LoadConstantTableEntry

    // CHECK-START: int PackedSwitch.$noinline$switchReturnToTableBoolean(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static int $noinline$switchReturnToTableBoolean(int value) {
        final int result;
        switch (value) {
            case 1: return 1;  // Standalone return.
            case 2:
            case 3: return 0;  // Return in a merged block without a phi.
            case 4: result = 1; break;
            case 5: result = 0; break;
            default: throw new Error();
        }
        return result;  // Return in a merged block with a phi.
    }

    public static void $noinline$testSwitchReturnToTableByte() {
        try {
            $noinline$switchReturnToTableByte(0);
        } catch (Error expected) {}
        assertEquals(42, $noinline$switchReturnToTableByte(1));
        assertEquals(88, $noinline$switchReturnToTableByte(2));
        assertEquals(-7, $noinline$switchReturnToTableByte(3));
        assertEquals(11, $noinline$switchReturnToTableByte(4));
        assertEquals(123, $noinline$switchReturnToTableByte(5));
        try {
            $noinline$switchReturnToTableByte(6);
        } catch (Error expected) {}
    }

    /// CHECK-START: int PackedSwitch.$noinline$switchReturnToTableByte(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch
    /// CHECK:              Phi

    // CHECK-START: int PackedSwitch.$noinline$switchReturnToTableByte(int) control_flow_simplifier (after)
    // CHECK: {{b\d+}}     LoadConstantTableEntry

    // CHECK-START: int PackedSwitch.$noinline$switchReturnToTableByte(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static int $noinline$switchReturnToTableByte(int value) {
        final int result;
        switch (value) {
            case 1: result = 42; break;
            case 2: result = 88; break;
            case 3: result = -7; break;
            case 4: result = 11; break;
            case 5: result = 123; break;
            default: throw new Error();
        }
        return result;
    }

    public static void $noinline$testSwitchReturnToTableUint8() {
        try {
            $noinline$switchReturnToTableUint8(0);
        } catch (Error expected) {}
        assertEquals(42, $noinline$switchReturnToTableUint8(1));
        assertEquals(88, $noinline$switchReturnToTableUint8(2));
        assertEquals(7, $noinline$switchReturnToTableUint8(3));
        assertEquals(11, $noinline$switchReturnToTableUint8(4));
        assertEquals(255, $noinline$switchReturnToTableUint8(5));
        try {
            $noinline$switchReturnToTableUint8(6);
        } catch (Error expected) {}
    }

    /// CHECK-START: int PackedSwitch.$noinline$switchReturnToTableUint8(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch
    /// CHECK:              Phi

    // CHECK-START: int PackedSwitch.$noinline$switchReturnToTableUint8(int) control_flow_simplifier (after)
    // CHECK: {{a\d+}}     LoadConstantTableEntry

    // CHECK-START: int PackedSwitch.$noinline$switchReturnToTableUint8(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static int $noinline$switchReturnToTableUint8(int value) {
        final int result;
        switch (value) {
            case 1: result = 42; break;
            case 2: return 88;
            case 3: result = 7; break;
            case 4: result = 11; break;
            case 5: result = 255; break;
            default: throw new Error();
        }
        return result;
    }

    public static void $noinline$testSwitchReturnToTableShort() {
        try {
            $noinline$switchReturnToTableShort(0);
        } catch (Error expected) {}
        assertEquals(42, $noinline$switchReturnToTableShort(1));
        assertEquals(88, $noinline$switchReturnToTableShort(2));
        assertEquals(-7, $noinline$switchReturnToTableShort(3));
        assertEquals(11, $noinline$switchReturnToTableShort(4));
        assertEquals(12345, $noinline$switchReturnToTableShort(5));
        try {
            $noinline$switchReturnToTableShort(6);
        } catch (Error expected) {}
    }

    /// CHECK-START: int PackedSwitch.$noinline$switchReturnToTableShort(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch
    /// CHECK:              Phi

    // CHECK-START: int PackedSwitch.$noinline$switchReturnToTableShort(int) control_flow_simplifier (after)
    // CHECK: {{s\d+}}     LoadConstantTableEntry

    // CHECK-START: int PackedSwitch.$noinline$switchReturnToTableShort(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static int $noinline$switchReturnToTableShort(int value) {
        final int result;
        switch (value) {
            case 1: result = 42; break;
            case 2: result = 88; break;
            case 3: result = -7; break;
            case 4: result = 11; break;
            case 5: result = 12345; break;
            default: throw new Error();
        }
        return result;
    }

    public static void $noinline$testSwitchReturnToTableChar() {
        try {
            $noinline$switchReturnToTableChar(0);
        } catch (Error expected) {}
        assertEquals(42, $noinline$switchReturnToTableChar(1));
        assertEquals(88, $noinline$switchReturnToTableChar(2));
        assertEquals(7, $noinline$switchReturnToTableChar(3));
        assertEquals(11, $noinline$switchReturnToTableChar(4));
        assertEquals(54321, $noinline$switchReturnToTableChar(5));
        try {
            $noinline$switchReturnToTableChar(6);
        } catch (Error expected) {}
    }

    /// CHECK-START: int PackedSwitch.$noinline$switchReturnToTableChar(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch

    /// CHECK-START: int PackedSwitch.$noinline$switchReturnToTableChar(int) control_flow_simplifier (before)
    /// CHECK-NOT:          Phi

    // CHECK-START: int PackedSwitch.$noinline$switchReturnToTableChar(int) control_flow_simplifier (after)
    // CHECK: {{c\d+}}     LoadConstantTableEntry

    // CHECK-START: int PackedSwitch.$noinline$switchReturnToTableChar(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static int $noinline$switchReturnToTableChar(int value) {
        switch (value) {
            case 1: return 42;
            case 2: return 88;
            case 3: return 7;
            case 4: return 11;
            case 5: return 54321;
            default: throw new Error();
        }
    }

    public static void $noinline$testSwitchReturnToTableInt() {
        try {
            $noinline$switchReturnToTableInt(0);
        } catch (Error expected) {}
        assertEquals(42, $noinline$switchReturnToTableInt(1));
        assertEquals(88, $noinline$switchReturnToTableInt(2));
        assertEquals(-7, $noinline$switchReturnToTableInt(3));
        assertEquals(11, $noinline$switchReturnToTableInt(4));
        assertEquals(123456789, $noinline$switchReturnToTableInt(5));
        try {
            $noinline$switchReturnToTableInt(6);
        } catch (Error expected) {}
    }

    /// CHECK-START: int PackedSwitch.$noinline$switchReturnToTableInt(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch
    /// CHECK:              Phi

    // CHECK-START: int PackedSwitch.$noinline$switchReturnToTableInt(int) control_flow_simplifier (after)
    // CHECK: {{i\d+}}     LoadConstantTableEntry

    // CHECK-START: int PackedSwitch.$noinline$switchReturnToTableInt(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static int $noinline$switchReturnToTableInt(int value) {
        final int result;
        switch (value) {
            case 1: result = 42; break;
            case 2: result = 88; break;
            case 3: result = -7; break;
            case 4: result = 11; break;
            case 5: return 123456789;
            default: throw new Error();
        }
        return result;
    }

    public static void $noinline$testSwitchReturnToTableLong() {
        try {
            $noinline$switchReturnToTableLong(0);
        } catch (Error expected) {}
        assertEquals(42L, $noinline$switchReturnToTableLong(1));
        assertEquals(88L, $noinline$switchReturnToTableLong(2));
        assertEquals(-7L, $noinline$switchReturnToTableLong(3));
        assertEquals(11L, $noinline$switchReturnToTableLong(4));
        assertEquals(123456789987654321L, $noinline$switchReturnToTableLong(5));
        try {
            $noinline$switchReturnToTableLong(6);
        } catch (Error expected) {}
    }

    /// CHECK-START: long PackedSwitch.$noinline$switchReturnToTableLong(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch
    /// CHECK:              Phi

    // CHECK-START: long PackedSwitch.$noinline$switchReturnToTableLong(int) control_flow_simplifier (after)
    // CHECK: {{j\d+}}     LoadConstantTableEntry

    // CHECK-START: long PackedSwitch.$noinline$switchReturnToTableLong(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static long $noinline$switchReturnToTableLong(int value) {
        final long result;
        switch (value) {
            case 1: result = 42L; break;
            case 2: result = 88L; break;
            case 3: result = -7L; break;
            case 4: result = 11L; break;
            case 5: result = 123456789987654321L; break;
            default: throw new Error();
        }
        return result;
    }

    public static void $noinline$testSwitchReturnToTableFloat() {
        try {
            $noinline$switchReturnToTableFloat(0);
        } catch (Error expected) {}
        assertEquals(42.0f, $noinline$switchReturnToTableFloat(1));
        assertEquals(88.0f, $noinline$switchReturnToTableFloat(2));
        assertEquals(-7.0f, $noinline$switchReturnToTableFloat(3));
        assertEquals(11.0f, $noinline$switchReturnToTableFloat(4));
        assertEquals(123456789.0f, $noinline$switchReturnToTableFloat(5));
        assertTrue(Float.isNaN($noinline$switchReturnToTableFloat(6)));
        try {
            $noinline$switchReturnToTableFloat(7);
        } catch (Error expected) {}
    }

    /// CHECK-START: float PackedSwitch.$noinline$switchReturnToTableFloat(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch
    /// CHECK:              Phi

    // CHECK-START: float PackedSwitch.$noinline$switchReturnToTableFloat(int) control_flow_simplifier (after)
    // CHECK: {{f\d+}}     LoadConstantTableEntry

    // CHECK-START: float PackedSwitch.$noinline$switchReturnToTableFloat(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static float $noinline$switchReturnToTableFloat(int value) {
        final float result;
        switch (value) {
            case 1: result = 42.0f; break;
            case 2: result = 88.0f; break;
            case 3: result = -7.0f; break;
            case 4: result = 11.0f; break;
            case 5: result = 123456789.0f; break;
            case 6: result = Float.NaN; break;
            default: throw new Error();
        }
        return result;
    }

    public static void $noinline$testSwitchReturnToTableDouble() {
        try {
            $noinline$switchReturnToTableDouble(0);
        } catch (Error expected) {}
        assertEquals(42.0, $noinline$switchReturnToTableDouble(1));
        assertEquals(88.0, $noinline$switchReturnToTableDouble(2));
        assertEquals(-7.0, $noinline$switchReturnToTableDouble(3));
        assertEquals(11.0, $noinline$switchReturnToTableDouble(4));
        assertEquals(123456789.0, $noinline$switchReturnToTableDouble(5));
        assertTrue(Double.isNaN($noinline$switchReturnToTableDouble(6)));
        try {
            $noinline$switchReturnToTableDouble(7);
        } catch (Error expected) {}
    }

    /// CHECK-START: double PackedSwitch.$noinline$switchReturnToTableDouble(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch

    /// CHECK-START: double PackedSwitch.$noinline$switchReturnToTableDouble(int) control_flow_simplifier (before)
    /// CHECK-NOT:          Phi

    // CHECK-START: double PackedSwitch.$noinline$switchReturnToTableDouble(int) control_flow_simplifier (after)
    // CHECK: {{d\d+}}     LoadConstantTableEntry

    // CHECK-START: double PackedSwitch.$noinline$switchReturnToTableDouble(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static double $noinline$switchReturnToTableDouble(int value) {
        switch (value) {
            case 1: return 42.0;
            case 2: return 88.0;
            case 3: return -7.0;
            case 4: return 11.0;
            case 5: return 123456789.0;
            case 6: return Double.NaN;
            default: throw new Error();
        }
    }

    public static void $noinline$testSwitchToTableIntWithDefault0() {
        assertEquals(-987654321, $noinline$switchToTableIntWithDefault0(-1));
        assertEquals(42, $noinline$switchToTableIntWithDefault0(0));
        assertEquals(88, $noinline$switchToTableIntWithDefault0(1));
        assertEquals(-7, $noinline$switchToTableIntWithDefault0(2));
        assertEquals(11, $noinline$switchToTableIntWithDefault0(3));
        assertEquals(123456789, $noinline$switchToTableIntWithDefault0(4));
        assertEquals(-987654321, $noinline$switchToTableIntWithDefault0(5));
    }

    /// CHECK-START: int PackedSwitch.$noinline$switchToTableIntWithDefault0(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch
    /// CHECK:              Phi

    // CHECK-START: int PackedSwitch.$noinline$switchToTableIntWithDefault0(int) control_flow_simplifier (after)
    // CHECK: <<Sel:i\d+>> Select
    // CHECK: {{i\d+}}     LoadConstantTableEntry [<<Sel>>]

    // CHECK-START: int PackedSwitch.$noinline$switchToTableIntWithDefault0(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static int $noinline$switchToTableIntWithDefault0(int value) {
        final int result;
        switch (value) {
            case 0: result = 42; break;
            case 1: result = 88; break;
            case 2: result = -7; break;
            case 3: result = 11; break;
            case 4: result = 123456789; break;
            default: result = -987654321; break;
        }
        $noinline$nop();  // Avoid the return pattern.
        return result;
    }

    public static void $noinline$testSwitchToTableIntWithDefault1() {
        assertEquals(-987654321, $noinline$switchToTableIntWithDefault1(0));
        assertEquals(42, $noinline$switchToTableIntWithDefault1(1));
        assertEquals(88, $noinline$switchToTableIntWithDefault1(2));
        assertEquals(-7, $noinline$switchToTableIntWithDefault1(3));
        assertEquals(11, $noinline$switchToTableIntWithDefault1(4));
        assertEquals(123456789, $noinline$switchToTableIntWithDefault1(5));
        assertEquals(-987654321, $noinline$switchToTableIntWithDefault1(6));
    }

    /// CHECK-START: int PackedSwitch.$noinline$switchToTableIntWithDefault1(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch
    /// CHECK:              Phi

    // CHECK-START: int PackedSwitch.$noinline$switchToTableIntWithDefault1(int) control_flow_simplifier (after)
    // CHECK: <<Sel:i\d+>> Select
    // CHECK: {{i\d+}}     LoadConstantTableEntry [<<Sel>>]

    // CHECK-START: int PackedSwitch.$noinline$switchToTableIntWithDefault1(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static int $noinline$switchToTableIntWithDefault1(int value) {
        final int result;
        switch (value) {
            case 1: result = 42; break;
            case 2: result = 88; break;
            case 3: result = -7; break;
            case 4: result = 11; break;
            case 5: result = 123456789; break;
            default: result = -987654321; break;
        }
        $noinline$nop();  // Avoid the return pattern.
        return result;
    }

    public static void $noinline$testSwitchToTableIntWithDefault2() {
        assertEquals(-987654321, $noinline$switchToTableIntWithDefault2(1));
        assertEquals(42, $noinline$switchToTableIntWithDefault2(2));
        assertEquals(88, $noinline$switchToTableIntWithDefault2(3));
        assertEquals(-7, $noinline$switchToTableIntWithDefault2(4));
        assertEquals(11, $noinline$switchToTableIntWithDefault2(5));
        assertEquals(123456789, $noinline$switchToTableIntWithDefault2(6));
        assertEquals(-987654321, $noinline$switchToTableIntWithDefault2(7));
    }

    /// CHECK-START: int PackedSwitch.$noinline$switchToTableIntWithDefault2(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch
    /// CHECK:              Phi

    // CHECK-START: int PackedSwitch.$noinline$switchToTableIntWithDefault2(int) control_flow_simplifier (after)
    // CHECK: <<Sel:i\d+>> Select
    // CHECK: {{i\d+}}     LoadConstantTableEntry [<<Sel>>]

    // CHECK-START: int PackedSwitch.$noinline$switchToTableIntWithDefault2(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static int $noinline$switchToTableIntWithDefault2(int value) {
        final int result;
        switch (value) {
            case 2: result = 42; break;
            case 3: result = 88; break;
            case 4: result = -7; break;
            case 5: result = 11; break;
            case 6: result = 123456789; break;
            default: result = -987654321; break;
        }
        $noinline$nop();  // Avoid the return pattern.
        return result;
    }

    public static void $noinline$testSwitchReturnToTableIntWithDefault0() {
        assertEquals(-987654321, $noinline$switchReturnToTableIntWithDefault0(-1));
        assertEquals(42, $noinline$switchReturnToTableIntWithDefault0(0));
        assertEquals(88, $noinline$switchReturnToTableIntWithDefault0(1));
        assertEquals(-7, $noinline$switchReturnToTableIntWithDefault0(2));
        assertEquals(11, $noinline$switchReturnToTableIntWithDefault0(3));
        assertEquals(123456789, $noinline$switchReturnToTableIntWithDefault0(4));
        assertEquals(-987654321, $noinline$switchReturnToTableIntWithDefault0(5));
    }

    /// CHECK-START: int PackedSwitch.$noinline$switchReturnToTableIntWithDefault0(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch
    /// CHECK:              Phi

    // CHECK-START: int PackedSwitch.$noinline$switchReturnToTableIntWithDefault0(int) control_flow_simplifier (after)
    // CHECK: <<Sel:i\d+>> Select
    // CHECK: {{i\d+}}     LoadConstantTableEntry [<<Sel>>]

    // CHECK-START: int PackedSwitch.$noinline$switchReturnToTableIntWithDefault0(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static int $noinline$switchReturnToTableIntWithDefault0(int value) {
        final int result;
        switch (value) {
            case 0: result = 42; break;
            case 1: result = 88; break;
            case 2: result = -7; break;
            case 3: result = 11; break;
            case 4: result = 123456789; break;
            default: result = -987654321; break;
        }
        return result;
    }

    public static void $noinline$testSwitchReturnToTableIntWithDefault1() {
        assertEquals(-987654321, $noinline$switchReturnToTableIntWithDefault1(0));
        assertEquals(42, $noinline$switchReturnToTableIntWithDefault1(1));
        assertEquals(88, $noinline$switchReturnToTableIntWithDefault1(2));
        assertEquals(-7, $noinline$switchReturnToTableIntWithDefault1(3));
        assertEquals(11, $noinline$switchReturnToTableIntWithDefault1(4));
        assertEquals(123456789, $noinline$switchReturnToTableIntWithDefault1(5));
        assertEquals(-987654321, $noinline$switchReturnToTableIntWithDefault1(6));
    }

    /// CHECK-START: int PackedSwitch.$noinline$switchReturnToTableIntWithDefault1(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch
    /// CHECK:              Phi

    // CHECK-START: int PackedSwitch.$noinline$switchReturnToTableIntWithDefault1(int) control_flow_simplifier (after)
    // CHECK: <<Sel:i\d+>> Select
    // CHECK: {{i\d+}}     LoadConstantTableEntry [<<Sel>>]

    // CHECK-START: int PackedSwitch.$noinline$switchReturnToTableIntWithDefault1(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static int $noinline$switchReturnToTableIntWithDefault1(int value) {
        final int result;
        switch (value) {
            case 1: return 42;
            case 2: result = 88; break;
            case 3: result = -7; break;
            case 4: return 11;
            case 5: result = 123456789; break;
            default: result = -987654321; break;
        }
        return result;
    }

    public static void $noinline$testSwitchReturnToTableIntWithDefault2() {
        assertEquals(-987654321, $noinline$switchReturnToTableIntWithDefault2(1));
        assertEquals(42, $noinline$switchReturnToTableIntWithDefault2(2));
        assertEquals(88, $noinline$switchReturnToTableIntWithDefault2(3));
        assertEquals(-7, $noinline$switchReturnToTableIntWithDefault2(4));
        assertEquals(11, $noinline$switchReturnToTableIntWithDefault2(5));
        assertEquals(123456789, $noinline$switchReturnToTableIntWithDefault2(6));
        assertEquals(-987654321, $noinline$switchReturnToTableIntWithDefault2(7));
    }

    /// CHECK-START: int PackedSwitch.$noinline$switchReturnToTableIntWithDefault2(int) control_flow_simplifier (before)
    /// CHECK:              PackedSwitch

    /// CHECK-START: int PackedSwitch.$noinline$switchReturnToTableIntWithDefault2(int) control_flow_simplifier (before)
    /// CHECK-NOT:          Phi

    // CHECK-START: int PackedSwitch.$noinline$switchReturnToTableIntWithDefault2(int) control_flow_simplifier (after)
    // CHECK: <<Sel:i\d+>> Select
    // CHECK: {{i\d+}}     LoadConstantTableEntry [<<Sel>>]

    // CHECK-START: int PackedSwitch.$noinline$switchReturnToTableIntWithDefault2(int) control_flow_simplifier (after)
    // CHECK-NOT:          PackedSwitch
    // CHECK-NOT:          Phi
    public static int $noinline$switchReturnToTableIntWithDefault2(int value) {
        switch (value) {
            case 2: return 42;
            case 3: return 88;
            case 4: return -7;
            case 5: return 11;
            case 6: return 123456789;
            default: return -987654321;
        }
    }

    public static void $noinline$nop() {}

    public static void assertEquals(int expected, int actual) {
        if (expected != actual) {
            throw new AssertionError("Expected " + expected + " got " + actual);
        }
    }

    public static void assertEquals(long expected, long actual) {
        if (expected != actual) {
            throw new AssertionError("Expected " + expected + " got " + actual);
        }
    }

    public static void assertEquals(float expected, float actual) {
        if (expected != actual) {
            throw new AssertionError("Expected " + expected + " got " + actual);
        }
    }

    public static void assertEquals(double expected, double actual) {
        if (expected != actual) {
            throw new AssertionError("Expected " + expected + " got " + actual);
        }
    }

    public static void assertTrue(boolean condition) {
        if (!condition) {
            throw new AssertionError("Expected true");
        }
    }
}
