# Disassembling Android Platform Code (`oatdump` & `dex2oat`)

This guide covers workflows for inspecting compiled Android platform code (boot
image, system server, apps) using `oatdump` and analyzing compiler decisions
using `dex2oat`.

## 1. Tools Overview

*   **`oatdump`**: Dumps the content of `.oat` (ELF) and `.vdex` files. Use this
    to inspect code that **has already been compiled** on the build server or
    device.
*   **`dex2oat` (and `dex2oatd`)**: The on-device compiler. Use this locally to
    **simulate compilation** with debug flags (like `--dump-cfg`) to understand
    *why* and *how* code is being compiled (or not).

## 2. Building and Running the Tools

Before using these tools on your host, you must build them from your local
checkout.

### 2.1. Building the Tools

Run the following command from the root of your Android tree:

```bash
m oatdump dex2oat
```

Other commonly used targets include:

*   **`m oatdumpd dex2oatd`**: Builds the **debug** versions of these tools.
    These are recommended for analysis as they contain additional assertions and
    detailed logging.
*   **`m build-art-host`**: A high-level target that builds most ART host tools,
    their dependencies (like ICU and TZData), and core boot images.

### 2.2. Running Binaries from Output Directory

After building, the binaries are located in the host output directory. You can
run them directly using their relative paths from the root of the checkout:

```bash
# General path format: ./out/host/linux-x86/bin/<tool_name>
./out/host/linux-x86/bin/oatdump --help
./out/host/linux-x86/bin/dex2oat --help
```

If you have initialized your environment using `lunch`, you can use the
`$ANDROID_HOST_OUT` environment variable:

```bash
$ANDROID_HOST_OUT/bin/oatdump --help
```

**Tip:** If you see errors related to missing shared libraries (`.so` files),
ensure your `LD_LIBRARY_PATH` includes the host library directory:

```bash
export LD_LIBRARY_PATH=$ANDROID_HOST_OUT/lib64:$LD_LIBRARY_PATH
```

## 3. Inspecting Compiled Code with `oatdump`

### Locating Files

*   **Host (Build Output):**
    *   OAT files: `$OUT/system/framework/<arch>/boot.oat`
    *   VDEX files: `$OUT/system/framework/<arch>/boot.vdex`
    *   *Note*: `$OUT` (e.g., `out/target/product/raven`) is set after running
        the `lunch` command. `<arch>` (e.g., `arm64`) is a placeholder for your
        specific build architecture.
    *   *Note*: On newer Android versions (Android 11+), core libraries
        (java.lang, java.util) are in the ART APEX:
        `$OUT/apex/com.android.art/javalib/<arch>/boot.oat`.
*   **Device:**
    *   Path: `/system/framework/<arch>/boot.oat` (and `.vdex`)
    *   *Note: `<arch>` is a placeholder for the device architecture (e.g.,
        `arm64`).*
    *   Note: On newer Android versions, some artifacts might be in
        `/data/dalvik-cache` if updated or compiled on-device.

### Workflow A: Running on Host

Run `oatdump` against the build artifacts. This is faster and allows using
standard linux tools (`grep`, `less`) easily.

```bash
# Setup environment
source build/envsetup.sh
lunch <your_target>  # e.g., lunch raven-userdebug

# Basic header dump (check compiler filter, etc.)
$ANDROID_HOST_OUT/bin/oatdump --oat-file=$OUT/system/framework/arm64/boot.oat --header-only

# Disassemble a specific class
# Note: "android.os.Looper" is in framework.jar (boot extension).
# For java.util.* (core-oj), check the ART APEX path.
$ANDROID_HOST_OUT/bin/oatdump --oat-file=$OUT/system/framework/arm64/boot.oat \
                              --class-filter=android.os.Looper
```

**Tip:** If you see "NO CODE", the method might not have been compiled
(interpreted only) or compiled in a different image (e.g., the app image vs boot
image).

### Workflow B: Running on Device

Useful if you don't have a matching local build or want to inspect the exact
state of a running device.

```bash
# Shell into device
# Note: 'adb root' requires a userdebug or eng build.
adb root && adb shell

# Dump to a file (recommended for large outputs)
oatdump --oat-file=/system/framework/arm64/boot.oat \
        --class-filter=android.os.Looper > /data/local/tmp/dump.txt

# Exit and pull
exit
adb pull /data/local/tmp/dump.txt
```

### Filtering Output

*   `--class-filter=<package.Class>`: Dumps only the specified class.
*   `--method-filter=<method_name>`: Dumps only methods with this name (e.g.,
    `get`).
*   `--no-disassemble`: Skips native code disassembly (useful for just checking
    OAT/Dex structure).
*   `--header-only`: Just the file header (checksums, key-value store,
    compiler-filter).

## 4. Analyzing Compiler Decisions with `dex2oat`

If `oatdump` shows "NO CODE" or suboptimal code, you can use `dex2oat` to
**recompile** the DEX file locally with verbose logging or CFG dumping.

### Workflow: Generating Control Flow Graphs (CFG)

The `--dump-cfg` flag generates a `.cfg` file that can be visualized using tools
like **c1visualizer** or **IR Hydra**. This shows the Intermediate
Representation (IR) at each compilation pass (inlining, constant folding,
register allocation, etc.).

1.  **Locate the Input DEX/JAR:**

    *   Example:
        `out/soong/.intermediates/libcore/core-oj/android_common/aligned/core-oj.jar`

2.  **Run `dex2oatd` (Debug Version):** Use `dex2oatd` (debug build) for better
    assertions and logging. Note that manually running `dex2oat` often requires
    specifying the boot class path if the code has dependencies.

    ```bash
    # Output file for the CFG
    CFG_OUTPUT=output.cfg

    $ANDROID_HOST_OUT/bin/dex2oatd \
      --dex-file=out/soong/.intermediates/libcore/core-oj/android_common/aligned/core-oj.jar \
      --oat-file=/dev/null \
      --boot-image=$OUT/system/framework/arm64/boot.art \
      --instruction-set=arm64 \
      --compiler-filter=speed \
      --dump-cfg=$CFG_OUTPUT \
      --verbose-methods=ArrayList.add,ArrayList.addAll
    ```

    *Note: If the compilation fails due to missing dependencies, you may need to
    provide `--boot-image` (e.g.,
    `--boot-image=$OUT/system/framework/arm64/boot.art`) or a full
    `--class-loader-context`.*

3.  **Analyze:**

    *   Open `output.cfg` in **c1visualizer** or **IR Hydra**.
    *   Look for passes like `Inliner` to see if a method was inlined or
        rejected (and why).

## 5. Common Recipes

### Check "Why wasn't this method inlined?"

1.  Verify with `oatdump` that it is indeed a call (`bl`/`blr`) and not inlined
    code.
2.  Use the `dex2oat` CFG workflow above.
3.  In the visualization tool (e.g., **c1visualizer**), find the `Inliner` pass.
4.  Look for the call site. The graph or side-panel usually logs failure reasons
    (e.g., "recursive", "too big", "cold", "always throws").

### Check "Is this method compiled hot?"

1.  Run `oatdump --header-only` to check the global `compiler-filter`.
    *   `verify`: No compilation (interpreter/JIT only).
    *   `speed-profile`: Profile-guided compilation.
    *   `speed`: AOT compiled.
2.  If `speed-profile`, dump the method.
    *   If `NO CODE`, it wasn't hot enough in the profile.
    *   If code exists, it was profiled as hot.

### Disassembling System Server

The System Server contains most of the core Android services (ActivityManager,
WindowManager, etc.).

```bash
# On device
adb shell oatdump --oat-file=/system/framework/oat/arm64/services.odex \
                  --boot-image=/system/framework/boot.art \
                  --class-filter=com.android.server.am.ActivityManagerService
```

**Important:** When dumping `.odex` or app `.oat` files, you often must provide
`--boot-image` so `oatdump` can resolve dependencies (like base classes or
methods in the boot classpath) to disassemble the code correctly.

### Disassembling Application Code

For installed APKs:

1.  Find the base.odex/vdex:

    ```bash
    adb shell pm path com.example.app
    ```

    (Usually in `/data/app/.../oat/arm64/`).

    *Note*: The command output starts with `package:`. You must remove this
    prefix when using the path.

2.  Run `oatdump`:

    ```bash
    bash adb shell oatdump --oat-file=/data/app/.../base.odex
    ```

## 6. ARM64 Disassembly Guide

This section provides a brief overview of the ARM64 architecture specifics
relevant when disassembling code compiled by `dex2oat`.

### 6.1 Registers

ART uses the standard ARM64 register set, but reserves some registers for
special purposes.

*   **General Purpose Registers (GPRs):** `x0`-`x30` (64-bit) and `w0`-`w30`
    (32-bit).

    *   `x29` (`fp`): Frame Pointer.
    *   `x30` (`lr`): Link Register (Return Address).

*   `xzr`/`wzr`: The zero register.

*   `sp`: Stack Pointer.

*   **Special & ART-Managed Registers:**

    *   `x0`: Used to pass the `ArtMethod*` pointer to a called method. For
        `@CriticalNative` JNI calls, it's used as a regular argument register.
        It also holds the return value for methods returning references or
        integral types up to 64 bits.
    *   `x15`: Used to pass the hidden argument (the interface method) for
        `invoke-interface` calls.
    *   `x16` (`ip0`), `x17` (`ip1`): Intra-procedural call scratch registers.
        Not guaranteed to be preserved across calls.
    *   `x19` (`tr`): The Thread Register, holds a pointer to the current
        `art::Thread` object. It is callee-saved.
    *   `x20` (`mr`): The Marking Register, used by the Baker read barrier for
        GC. It holds `Thread::Current()->IsGcMarking()`. It is callee-saved.
    *   `x21`: Used for implicit suspend checks. `x21` points to a memory
        location that normally contains a pointer to itself. When a suspend is
        requested, this pointer is set to null. The first `ldr x21, [x21]` will
        then load null into `x21`, and the second such instruction will trap. It
        is callee-saved.

### 6.2 Calling Convention (Managed)

ART uses a custom calling convention for `dex2oat` compiled managed code.

*   **Argument Passing:**

    *   The `ArtMethod*` of the callee is passed in `x0`.
    *   The first integral/reference arguments are passed in registers `x1`
        through `x7`. For instance methods, `x1` holds the `this` reference, and
        subsequent arguments are passed in `x2`, `x3`, etc. For static methods,
        the arguments start directly in `x1`.
    *   The first 8 floating-point arguments are passed in registers `d0`
        through `d7`.
    *   Any remaining arguments are passed on the stack.

*   **Return Values:**

    *   **Integral/Reference:** `w0` (32-bit) or `x0` (64-bit).
    *   **Floating-Point:** `s0` (32-bit float) or `d0` (64-bit double).

*   **Callee-Saved Registers:**

    *   Core registers: `x19` through `x28`, `x29` (`fp`), `x30` (`lr`). Note
        that `x19`, `x20`, and `x21` have special roles in ART.
    *   FP registers: `d8` through `d15`.

*   **Stack Layout:**

    *   The stack grows downwards.
    *   `sp[0]` holds the `ArtMethod*` of the current method.
    *   The compiler may perform a stack overflow check at the beginning of the
        method prologue:

        ```asm
        sub x16, sp, #0x2000
        ldr wzr, [x16]
        ```

### 6.3 Common Code Patterns

When inspecting disassembly, you will frequently encounter these patterns. The
following examples are based on compiling the code below:

```java
class AnotherClass {
    public static int static_field;
}

class AllPatternsExample {
    public int instance_field;

    public static void demonstratePatterns(AllPatternsExample obj, Object[] array, Object lock) {
        // Pattern: Implicit Null Check
        int x = obj.instance_field;

        // Pattern: Class Initialization Check
        AnotherClass.static_field = x;

        // Pattern: Suspend Check and Write Barrier
        for (int i = 0; i < array.length; i++) {
            array[i] = lock;
        }

        // Pattern: Explicit Null Check (principle)
        synchronized (lock) {
            lock.hashCode();
        }
    }
}
```

*   **Implicit Null Pointer Checks:** The `ldr` instruction to access
    `obj.instance_field` also serves as the null check. If `x1` (holding `obj`)
    is null, the load will fault. The runtime will trap the fault and throw a
    `NullPointerException`.

    ```java
    int x = obj.instance_field;
    ```

    ```asm
    ; x1 holds 'obj', the first argument to demonstratePatterns()
    ldr w0, [x1, #8]  ; This load acts as the null check
    ```

    Note that if `x1` is `null` then the attempted load above will be done from
    the address 8, since the `ldr` is reading from `x1` with an offset of `#8`.
    But this is still within the null guard page, and will be trapped and
    handled in the same way as dereferencing a null pointer would.

*   **Class Initialization Checks:** Before storing to
    `AnotherClass.static_field`, the code loads the class's status and branches
    to a slow path if it's not yet initialized.

    ```java
    AnotherClass.static_field = x;
    ```

    ```asm
    ; x1 holds a pointer to the AnotherClass java.lang.Class object
    ldrb w16, [x1, #107]             ; Load Class->status byte
    cmp w16, #0xf0                   ; Compare with visibly initialized state
    b.lo .Lslow_path_for_clinit      ; Branch if not visibly initialized
    str w0, [x1, #208]               ; Store to static field
    ```

*   **Suspend Checks:** On the back-edge of a loop, the compiler inserts a check
    to allow the GC to suspend the thread. This is often an implicit check using
    a load from `x21`.

    ```java
    for (int i = 0; i < array.length; i++) { ... }
    ```

    ```asm
    ; Inside the loop, right before branching back to the header
    add w23, w23, #0x1                ; i++
    ldr x21, [x21]                    ; Implicit suspend check
    b .Lloop_header                   ; Branch to loop header
    ```

*   **Write Barriers (Card Marking):** When storing a reference into an array
    (`array[i] = lock`), the compiler emits a write barrier to inform the GC.
    This involves marking a "card" in the card table as dirty.

    ```java
    array[i] = lock;
    ```

    ```asm
    ; x19=tr, x2=array, x3=lock, x23=i
    ldr x16, [x19, #144]              ; Load card table address from Thread
    lsr w17, w2, #10                  ; Get card index for 'array'
    strb w16, [x16, x17]              ; Mark card as dirty
    add w16, w2, #0xc                 ; Calculate array data start
    str w3, [x16, x23, lsl #2]        ; The actual store into the array
    ```

*   **Virtual Method Invocation** A virtual call involves loading the object's
    class, finding the target method in the class's virtual method table
    (vtable), loading the method's entry point, and finally branching to it.

    ```java
    lock.hashCode();
    ```

    ```asm
    ; x1 holds 'lock', which is the 'this' for the instance method call.
    ldr w0, [x1]                     ; w0 = lock->klass_
    ldr x0, [x0, #152]               ; x0 = klass->vtable[hashCode_index] (ArtMethod*)
    ldr lr, [x0, #24]                ; lr = ArtMethod->EntryPointFromQuickCompiledCode
    blr lr                           ; Call the method
    ```

*   **Interface Method Invocation** An interface call uses the Interface Method
    Table (IMT) to find the target method. It is similar to a virtual call, but
    it also passes the target interface method in `x15` as a hidden argument.
    This hidden argument is used by the `art_quick_imt_conflict_trampoline` if
    the IMT slot contains a conflict marker instead of a direct method pointer.

    ```java
    interface Greeter { void greet(); }
    void callGreet(Greeter g) {
        g.greet();
    }
    ```

    ```asm
    ; x1 holds 'g', the 'this' for the interface method call.
    ; x15 holds the Greeter.greet ArtMethod* (hidden argument).
    ldr w0, [x1]                         ; w0 = g->klass_
    ldr x0, [x0, #<ImtPtr_offset>]       ; x0 = klass->ImtPtr
    ldr x0, [x0, #<IMT_slot_offset>]     ; x0 = ImtPtr[slot_index] (resolved ArtMethod* or conflict trampoline)
    ldr lr, [x0, #24]                    ; lr = ArtMethod->EntryPointFromQuickCompiledCode
    blr lr                               ; Call the method
    ```
