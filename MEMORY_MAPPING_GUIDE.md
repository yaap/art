# Analyzing ART Memory Mappings (`showmap` & `meminfo`)

This guide covers how to measure and interpret Android Runtime (ART)-related
memory usage in managed processes, focusing on key artifacts like `.dex`,
`.odex`, `.art`, and the Java heap. Understanding these mappings can help
quantify resource costs and impact, and guide related optimizations to app
bytecode, compiled code, and allocation behavior.

## 1. Tools Overview

*   **`showmap`**: Displays the virtual memory map of a process. It provides the
    most detailed view of what files and anonymous memory regions are mapped,
    their permissions, and their memory consumption (PSS/RSS).
*   **`dumpsys meminfo`**: Provides a high-level summary of memory usage,
    categorizing it into buckets like "Java Heap", "Code", "Stack", and specific
    mmap types.

## 2. Analyzing Memory Mappings with `showmap`

### Capturing a Map

1.  **Identify the Process ID (PID):**

    ```bash
    adb shell pidof system_server
    # Output example: 1543
    ```

2.  **Run `showmap` (Root may be required for full details):**

    ```bash
    adb root
    adb shell showmap 1543 > system_server_showmap.txt`
    ```

    >    *Tip*: To merge same-named maps and sort by their total private memory (clean + dirty), you can pipe the output as follows:
    >    ```bash
    >    adb shell showmap 1543 \
    >       | awk -e '{ printf("%5d %s\n", $6 + $7, $0) }' \
    >       | sort -k1 -rn \
    >       > system_server_showmap_sorted.txt
    >    ```


### Interpreting Map output

Let's look at an example subset of the output you might see from a `showmap`
command:

```none
virtual                     shared   shared  private  private
   size      RSS      PSS    clean    dirty    clean    dirty     swap  swapPSS object
------- -------- -------- -------- -------- -------- -------- -------- -------- ----------------
...
  38832     6440      430     6380        0       60        0        0        0 /system/framework/framework-res.apk
  11796    11072      888    10740        0      332        0        0        0 /apex/com.android.art/javalib/core-oj.jar
   8300     8300     4200     8200        0       88       12        0        0 /system/framework/oat/arm64/services.odex
    648      644      324      640        0        4        0        0        0 /system/framework/oat/arm64/services.vdex
   3072     3064      285        0     2796        0      268        0        0 [anon:dalvik-/system/framework/boot.art]
1048576    65652    65652        0        0        0    65652        0        0 [anon:dalvik-main space]
  58836      300      300        0        0        0      300        0        0 [anon:dalvik-non moving space]
   6700     6700      456        0     6284        0      416        0        0 [anon:dalvik-zygote space]
...
```

*   **Virtual size**: The address space reserved. This does not represent
    physical memory usage.
*   **RSS (Resident Set Size)**: The amount of physical RAM currently holding
    pages for this mapping.
*   **PSS (Proportional Set Size)**: A key metric for proportional "cost". It is
    **RSS** but with shared pages divided by the number of processes sharing
    them.
    *   *Formula*: `Private pages + (Shared pages / Number of sharers)`
*   **Clean vs. Dirty**:
    *   **Clean**: Pages backed by a file on disk that have *not* been modified
        in memory. These can be evicted by the kernel under memory pressure
        without swapping (just re-read from disk later).
    *   **Dirty**: Pages that have been modified. These *must* remain in RAM, or
        be written out to swap space before eviction.
    *   **Private Dirty**: Usually the most expensive and impactful bucket of
        memory. This includes heap allocations, written global variables, JIT
        caches, app images, etc.
*   **swap**: Pages that have been evicted from RAM and are currently stored in
    the swap (typically zram) area.
*   **swapPSS**: Proportional cost of swapped pages, similar to PSS.

## 3. Interpreting ART File Mappings

ART maps various file types into memory. Understanding them helps diagnose if
memory usage comes from code, resources, or the heap.

### 3.1 `.dex` / `.apk` / `.jar` (Bytecode & Resources)

*   **Description**:
    *   **`.apk`**: By default, DEX bytecode should be stored uncompressed in
        the APK. This allows the runtime to `mmap` the dex directly from the
        APK, creating a clean mapping backed by the APK file itself, saving
        memory compared to extracting it. This mapping will also contain other
        pages for app resources (e.g., `resources.arsc`) and assets.
    *   **`.jar`**: Boot classpath or other system libraries.
*   **Mapping**: Mostly clean (read-only), though not always for certain non-ART
    components of APKs.
*   **Why it matters**: Large mappings of dex/apk/jar files often point to
    unoptimized or poorly optimized app bytecode and resources. See
    [this guide](https://developer.android.com/topic/performance/app-optimization/enable-app-optimization/)
    for best practices on related optimizations. Reducing the size of shipped
    bytecode and resources can dramatically reduce the app's memory footprint at
    runtime.
*   **Example**:

    ```none
    virtual                     shared   shared  private  private
       size      RSS      PSS    clean    dirty    clean    dirty     swap  swapPSS object
    ------- -------- -------- -------- -------- -------- -------- -------- -------- ----------------
      38832     6440      430     6380        0       60        0        0        0 /system/framework/framework-res.apk
      11796    11072      888    10740        0      332        0        0        0 /apex/com.android.art/javalib/core-oj.jar
    ```

### 3.2 `.art` (App Image / Boot Image)

*   **Description**: Pre-initialized class/object "snapshots" to improve app
    startup. Instead of loading commonly used classes at every launch, ART maps
    these objects directly into the heap.
*   **Mapping**: Typically `private dirty` or `shared dirty` (if shared with
    zygote). The image files are stored compressed on disk by default, and must
    be decompressed into the heap and patched at runtime for security (ASLR).
*   **Why it matters**: As app images are dirty mappings, care should be taken
    to avoid unnecessarily increasing their size. Follow
    [best practices](https://developer.android.com/topic/performance/app-optimization/enable-app-optimization/)
    to ensure classes are lean and minified, and avoid bundling
    [overly broad or poorly tuned baseline profiles](https://developer.android.com/topic/performance/baselineprofiles/overview).

*   **Example**:

    ```none
    virtual                     shared   shared  private  private
       size      RSS      PSS    clean    dirty    clean    dirty     swap  swapPSS object
    ------- -------- -------- -------- -------- -------- -------- -------- -------- ----------------
    # Boot image (shared across apps from the zygote)
       3072     3064      285        0     2796        0      268        0        0 [anon:dalvik-/system/framework/boot.art]

    # App image (private to the app) - Note for a background app, this may be entirely in swap as below!
      22944        0        0        0        0        0        0    22940    22940 [anon:dalvik-/data/user/0/com.android.foo/cache/oat_primary/arm64/base.art]
    ```

    > *Note*: You may also see a small, clean file mapping for these images (for
    > referencing the header), but the actual heap data appears as an anonymous
    > mapping named `[anon:dalvik-...art]`.*

### 3.3 `.oat` / `.odex` (Compiled Code)

*   **Description**: Contains AOT-compiled native machine code (ELF format) and
    some metadata.
*   **Mapping**: Mostly `clean` (executable code).
*   **Why it matters**: The size of these artifacts in memory is a function of how much code is compiled (typically dictated by the profile with `speed-profile` compilation), and how much is needed at runtime for execution. The best way to minimize memory impact is to ensure that:
    *   The underlying `.dex` has been [fully optimized and minified](https://developer.android.com/topic/performance/baselineprofiles/overview).
    *   Any bundled [baseline profiles](https://developer.android.com/topic/performance/baselineprofiles/overview) are tuned and compact.
*   **Example**:

    ```none
    virtual                     shared   shared  private  private
       size      RSS      PSS    clean    dirty    clean    dirty     swap  swapPSS object
    ------- -------- -------- -------- -------- -------- -------- -------- -------- ----------------
       8300     8300     4200     8200        0       88       12        0        0 /system/framework/oat/arm64/services.odex
    ```

### 3.4 `.vdex` (Verified Dex)

*   **Description**: Contains verification dependencies. If the APK ships with
    compressed DEX (which prevents direct mapping), the system extracts the
    uncompressed DEX bytecode into the `.vdex` file during installation to allow
    for mapping.
*   **Mapping**: Mostly `clean` (read-only)
*   **Why it matters**: A large vdex mapping often indicates that the app is
    shipping compressed dex, which *should be avoided* in favor of clean
    uncompressed dex bundled in the APK.
*   **Example**

    ```none
    virtual                      shared   shared  private  private
       size      RSS      PSS    clean    dirty    clean    dirty     swap  swapPSS object
    ------- -------- -------- -------- -------- -------- -------- -------- -------- ----------------
        648      644      324      640        0        4        0        0        0 /system/framework/oat/arm64/services.vdex
    ```

## 4. Interpreting ART Anonymous Mappings (Heap & JIT)

Anonymous memory isn't backed by a file. In Android/ART, these have specific
names, typically prefixed with `[anon:dalvik-...]`.

### 4.1 Java Heap (`[anon:dalvik-...]`)

*   **Description**: The Java heap is a function of runtime allocations and the
    garbage collector. See
    [this guide](https://source.android.com/docs/core/runtime/gc-debug) for a
    more exhaustive overview of the GC, how it operates, and what the means for
    app development. Allocations can end up in one of the following buckets:
    *   **`[anon:dalvik-main space]`**: The primary Java heap. This is where
        standard objects live.
    *   **`[anon:dalvik-large object space]`**: For very large primitive arrays and
        strings (LOS).
    *   **`[anon:dalvik-zygote space]`**: The heap inherited from Zygote.
        Typically "shared dirty".
    *   **`[anon:dalvik-non moving space]`**: Memory for objects that must not
        move or where moving should be avoided (e.g., certain JNI critical
        objects, some internal primitive array allocations).
*   **Why it matters**: High heap utilization, excessive heap churn, and memory
    leaks, can all lead to poor app performance.
    [This guide](https://developer.android.com/topic/performance/memory)
    outlines several best practices for minimizing heap usage.
*   **Example**:

    ```none
    virtual                     shared   shared  private  private
       size      RSS      PSS    clean    dirty    clean    dirty     swap  swapPSS object
    ------- -------- -------- -------- -------- -------- -------- -------- -------- ----------------
    1048576    65652    65652        0        0        0    65652        0        0 [anon:dalvik-main space]
      58836      300      300        0        0        0      300        0        0 [anon:dalvik-non moving space]
       6700     6700      456        0     6284        0      416        0        0 [anon:dalvik-zygote space]
    ```
    > *Note*: The `virtual size` (1GB here) is the *reserved* maximum heap size,
    > but `RSS` (65MB) is what's actually used.*

### 4.2 Internal ART Structures

*   **Description**: Internal data structures used by ART for bookkeeping. These
    aren't directly actionable for most developers.
    *   **`[anon:dalvik-LinearAlloc]`**: Internal storage for loaded classes and
        methods.
    *   **`[anon:dalvik-card table]`**: GC accounting structure.
    *   **`[anon:dalvik-bitmap...]`**: GC live object bitmaps.

### 4.3 JIT Cache (`/memfd:jit-cache`)

*   **Description**: Holds native code compiled *at runtime* by the JIT
    (Just-In-Time) compiler.
*   **Mapping**: Executable, anonymous (or memfd backed).
*   **Why it matters**: ART generally tries to find the right balance between
    AOT and JIT using profile-guided optimization. A large JIT cache can occur
    if the app has not been `speed-profile` compiled (check via `dumpsys package
    dexopt`).
*   **Example**:

    ```none
    virtual                     shared   shared  private  private
       size      RSS      PSS    clean    dirty    clean    dirty     swap  swapPSS object
    ------- -------- -------- -------- -------- -------- -------- -------- -------- ----------------
     131072     1308      656        0     1304        0        4        0        0 /memfd:jit-cache (deleted)
    ```

### 4.4 Native Heap (`[anon:scudo:...]` or `[anon:libc_malloc]`)

*   **Description**: Allocations from C++ `malloc`/`new`. Android 11+ uses the
    Scudo allocator.
*   **Mapping**: Mostly private dirty, with shared dirty bits from the zygote.
    While most of ART's memory usage is bucketed into the mappings outlined
    above, the runtime itself (implemented in C++) can use the native heap for
    basic execution.
*   **Example**:

    ```none
    virtual                     shared   shared  private  private
       size      RSS      PSS    clean    dirty    clean    dirty     swap  swapPSS object
    ------- -------- -------- -------- -------- -------- -------- -------- -------- ----------------
     180480    50224    46690        0     3548        0    46676        0        0 [anon:scudo:primary]
    ```

## 5. Summary View: `dumpsys meminfo`

See the official
[dumpsys meminfo documentation](https://developer.android.com/tools/dumpsys#meminfo)
for more details.

For a quick summarized view of process memory usage, `dumpsys meminfo` is a
useful tool that aggregates memory usage into logical buckets and types. The
`-d` argument enables more granular details on ART-specific memory usage. As an
example, an abbreviated view for system_server (using `adb shell dumpsys meminfo
-d system_server`) might look like:

```none
                   Pss  Private  Private     Swap      Rss
                 Total    Dirty    Clean    Dirty    Total
                ------   ------   ------   ------   ------
  Native Heap    51005    50988        0        0    55460
  Dalvik Heap    75478    75428        0        0    83340
 Dalvik Other    13020    12300        0        0    14176
 ...
     .so mmap    29999     2344    17540        0   116256  # Native libraries
    .jar mmap    50250        0    19348        0   177644  # Boot classpath or system JARs (DEX)
    .apk mmap    89373        0    51412        0   148888  # APK resources + code
    .dex mmap    18945      148    13988        0    25408  # Extracted or mapped DEX
    .oat mmap     1220        0      460        0    26092  # AOT Code
    .art mmap     3399     3180       20        0    34100  # Boot/App Images
...
        TOTAL   391182   191124   110416        0   750724
```

The next section gives a more granular breakdown of ART memory usage by mapping
region:

```none
 Dalvik Details
        .Heap    74084    74084        0        0    74084  # 100% dirty!
         .LOS      638      628        0        0     2256  # Primitive array allocs > 12KB.
      .Zygote      456      416        0        0     6700  # Zygote space
   .NonMoving      300      300        0        0      300  # Internal non-movable data structures
 .LinearAlloc     9700     9700        0        0     9700  # Class/Method metadata
          .GC     2564     2564        0        0     3000  # Overhead for GC
      .AppJIT      720        0        0        0     1440  # JIT cache + data
 .IndirectRef       36       36        0        0       36  # Includes JNI references
   .Boot vdex      585        0      428        0     2156  # Clean!
     .App dex    17768      148    13520        0    22100  # Mostly clean!
    .App vdex      592        0       40        0     1152  # Clean!
     .App art      368      364        4        0      368  # Mostly dirty!
    .Boot art     3031     2816       16        0    33732  # Mostly dirty!
```

Finally, we get a summary of overall memory usage for the process:

```none
 App Summary
                       Pss(KB)                     Rss(KB)
                        ------                      ------
           Java Heap:    78628                      117440  # [anon:dalvik-main/zygote/etc]
         Native Heap:    50988                       55460  # [anon:scudo/malloc]
                Code:   105300                      496164  # .so + .jar + .apk + .dex + .oat + .art
....
       Private Other:    21816                              # LinearAlloc, indirect ref, etc.
....
           TOTAL PSS:   391182         TOTAL RSS:   750724
```

## 6. Links and Resources

*   **[Perfetto Memory Case Studies](https://perfetto.dev/docs/case-studies/memory)**:
    An exhaustive guide for capturing and analyzing memory behavior using
    Perfetto, including how to collect Java heap dumps and native/Java profiles.
*   **[Memory allocation](https://developer.android.com/topic/performance/memory-management)**:
    High level overview of memory management on Android.
*   **[Android runtime](https://source.android.com/docs/core/runtime)**: High
    level documentation for the Android runtime.
*   **`dexdump`**: A (host/device) tool for inspecting and analyzing DEX and JAR
    files. It provides detailed info on classes, methods, and bytecode.
*   **`oatdump`**: A (host/device) tool for analyzing OAT and ODEX files,
    providing a view into the compiled native code and metadata. See
    [the companion disassembly guide](DISASSEMBLY_GUIDE.md) for more details.
*   **[`apkanalyzer`](https://developer.android.com/tools/apkanalyzer)**: A
    (host) tool for analyzing APKs, including resource size, DEX contents, and
    manifest details.
*   **`dumpsys package dexopt`**: A (device) command providing details on the
    compilation state for installed apps.
