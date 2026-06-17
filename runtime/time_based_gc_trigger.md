# Time-Based GC Trigger

Time-based GC triggering is a strategy for triggering garbage collection (GC)
designed to make efficient use of overall GC CPU and memory across the device.
The more time has passed since the previous GC in an app, the lower the
threshold is for triggering the next GC. This decay in the GC trigger
threshold is chosen to optimize the heap size of to-be-collected objects
against the sustained per-second CPU cost of garbage collection across all
running applications.

Time-based GC triggering is different from the traditional
target-heap-utilization-based strategy used by ART (described in
`runtime/gc_configuration.md`), which was designed to keep GC cost per byte
allocated roughly constant.

The main motivations for switching from target-heap-utilization-based GC
triggering to time-based GC triggering are to save more memory across the
device with less overall CPU effort and simplify the tuning knob configuration
of GC.

## Motivations

### More efficient use of GC effort

Time-based GC triggering is designed to make efficient use of overall GC CPU
and memory use across the device. When it comes to memory use, we are
specifically interested in the heap size of to-be-collected objects, which we
refer to as `memory overhead` from here on.

In traditional heap-utilization-based triggering, memory overhead is
proportional to the live heap size of an application. The per-second CPU cost
of GC is proportional to the allocation rate of the application.

Consider two processes `A` and `B`. Process `A` has a small 10MB live heap
size and high allocation rate of 100MB per second. Process `B` has a large
100MB live heap size and small allocation rate of 10MB per second.

Assume target heap utilization is set to 0.5, it takes 50ms CPU time to do GC
in process `A`, and that it takes 500ms CPU time to do GC in process `B`
(because the live heap size of `B` is 10 times the live heap size of `A`). Say
we have 8GB total RAM on device.

With traditional heap-utilization-based triggering, GC will happen
100MB/10MB = 10 times per second in process `A`, and every
10MB/100MB = 0.1 seconds in process `B`. The total memory overhead across both
processes is 110MB with GC CPU cost of 10 * 50 + 0.1 * 500 = 550ms per second.

| Utilization Based Trigger (UBT)  | `A`      | `B`     | UBT Overall |
| :------------------------------- | :------- | :------ | :---------- |
| Live Heap                        |  10MB    | 100MB   | 110MB       |
| Alloc Rate                       | 100MB/s  |  10MB/s | 110MB/s     |
| Per-GC CPU                       |  50ms    | 500ms   |             |
| Memory Overhead                  |  10MB    | 100MB   | 110MB       |
| Memory Overhead % of total RAM   |   0.12%  |  1.2%   |  1.34%      |
| Heap Utilization                 |   0.5    |  0.5    |   0.5       |
| GCs per second                   |  10      |  0.1    |  10.1       |
| GC CPU per second                | 500ms/s  |  50ms/s | 550ms/s     |
| GC CPU per byte allocated        |   5ms/MB |  5ms/MB |   5ms/MB    |
| GC CPU % of 1 CPU core           |  50%     |  5%     |  55%        |
| Memory GC cost factor            | 417      |  4.17   |  41         |

The time-based GC trigger implicitly takes into account the allocation rate of
an app. The higher the allocation rate, the more memory the app can allocate
before triggering GC. For example, say GC triggers every 55MB allocated for
both process `A` and process `B`, because process `A` allocates at 10x the
rate despite having 10x smaller per-GC cost. In this case, GC will happen
100MB/55MB = 1.8 times per second in process `A` and 10MB/55MB = 0.18 times
per second in process `B`. The total memory overhead across device is still
110MB, but with GC CPU cost of only 1.8 * 50 + 0.18 * 500 = 180ms per second.
That's 1/3rd the GC CPU cost for the same overall memory overhead on device,
which is a much more efficient use of GC CPU effort.

| Time Based Trigger (TBT)       | `A`      | `B`     | TBT Overall | UBT Overall |
| :----------------------------- | :------- | :------ | :---------- | :---------- |
| Live Heap                      |  10MB    | 100MB   | 110MB       | 110MB       |
| Alloc Rate                     | 100MB/s  |  10MB/s | 110MB/s     | 110MB/s     |
| Per-GC CPU                     |  50ms    | 500ms   |             |             |
| Memory Overhead                |  55MB    |  55MB   | 110MB       | 110MB       |
| Memory Overhead % of total RAM |   0.67%  |  0.67%  |  1.34%      |  1.34%      |
| Heap Utilization               |   0.15   |  0.65   |   0.5       |   0.5       |
| GCs per second                 |   1.8    |  0.18   |   1.98      |  10.1       |
| GC CPU per second              |  90ms/s  |  90ms/s | 180ms/s     | 550ms/s     |
| GC CPU per byte allocated      | 0.9ms/MB |  9ms/MB |   1.6ms/MB  |   5ms/MB    |
| GC CPU % of 1 CPU core         |   9%     |  9%     |  18%        |  55%        |
| Memory GC cost factor          | 13.5     | 13.5    |  13.5       |  41         |

The approach to time-based GC triggering is based on the same idea as
[Marisa Kirisame et al, "Optimal heap limits for reducing browser memory
use"](https://dl.acm.org/doi/10.1145/3563323).

### Improve balance between GC memory overhead and CPU

The example in the previous section shows significant improvements in
efficiency because it involved processes whose live heap sizes were relatively
large or relatively small compared to their allocation rates.

In the traditional target heap utilization GC strategy, a process with small
live heap and high allocation rate uses a relatively high amount of GC CPU for
relatively little memory overhead. A process with large live heap and low
allocation has relatively high memory overhead for relatively little GC CPU.

This imbalance between memory overhead and GC CPU can be mitigated to some
extent with use of `heapminfree` and `heapmaxfree` to limit how extreme the
imbalance can get, but that's not what those knobs were designed for. Use of
`heapminfree` and `heapmaxfree` can introduce additional imbalances,
particularly as live heap size and allocation rates change over time in an
application.

Time-based GC triggering implicitly takes allocation rate into the decision
for when to trigger GC. An app will be given a larger heap allowance during
burst allocation activities and less of an allowance when it is idling to
maintain the balance of memory overhead to GC CPU overhead.

### Reduce lingering garbage in idle processes

In the heap target utilization approach to GC triggering, you can imagine a
case where an application allocates to just below the GC threshold and then
goes idle. For target utilization of 0.5, for example, that could mean just
shy of 50% of the Java heap consists of unreachable objects and will continue
to do so for the foreseeable future. The costs of leaving these unreachable
objects on the heap are further compounded if some of the unreachable objects
are pointers to "owned" native objects or binder objects causing memory to be
retained in other processes.

Time-based GC triggering addresses this case by limiting how long large
amounts of unreachable objects can stay on the heap before GC is triggered.

### More meaningful primary tuning knob

`heaptargetutilization` is the primary tuning knob to trade off space and time
in garbage collection in the traditional GC triggering approach. The
relationship between target heap utilization and memory use is indirect, and
you can't know how much space and time are being traded off without also
knowing the live heap size and allocation rates of the applications on device.

For example, for an app with live heap size of 50MB, a target heap utilization
of 0.5 means 50MB of memory overhead. Increasing target heap utilization to,
say, 0.75, reduces memory overhead in this case to 16.6MB.

If target heap utilization is tuned for a device empirically, the tuning value
can be thrown off if app allocation rates trend up or down or as GC
performance is improved release over release.

The time-based GC trigger specifies the main trade-off between space and time
using a `heap-memory-gc-cost-factor` parameter that directly represents the
trade-off between memory and space (see the section on tuning below). Changes
to application allocation rates or optimizations to GC impact how much GC and
memory overhead there is on device, without changing the balance between space
and time used by GC.

### Simplified GC tuning

The time-based GC trigger approach eliminates `heapminfree`, `heapmaxfree`,
and `foreground-heap-growth-multiplier` tuning knobs. The
`heaptargetheaputilization` knob is replaced with a
`heap-memory-gc-cost-factor` which has a natural default value of 1.

The ideal is that all devices can now rely on the default tuning knob setting
without requiring additional GC tuning, and that tuning knob values no longer
need to be updated every few years to adjust to increased memory capacity and
CPU capability of devices.

## Enabling time-based GC triggering

To enable time-based GC triggering, set the following system property:

```
dalvik.vm.enable_time_based_gc_trigger=true
```

For example, add the following to your device.mk file:

```
PRODUCT_VENDOR_PROPERTIES += dalvik.vm.enable_time_based_gc_trigger=true
```

Time-based GC triggering will not be enabled by default initially. As the
feature gains greater adoption and we have had a chance to see the broader
implications of the time-based approach to GC triggering, we will consider
switching it to be enabled by default.

## Tuning time-based GC triggering

When time-based GC triggering is enabled, the following GC tuning knobs will
no longer have any effect:

* `dalvik.vm.heaptargetutilization` (`-XX:HeapTargetUtilization`)
* `dalvik.vm.heapminfree` (`-XX:HeapMinFree`)
* `dalvik.vm.heapmaxfree` (`-XX:HeapMaxFree`)
* `dalvik.vm.foreground-heap-growth-multiplier` (`-XX:ForegroundHeapGrowthMultiplier`)

The following knobs continue to behave as before:

* `dalvik.vm.heapsize` (`-Xmx`)
* `dalvik.vm.heapgrowthlimit` (`-XX:HeapGrowthLimit`)
* `dalvik.vm.heapstartsize` (`-Xms`)

There is a single new tuning knob:

* `dalvik.vm.heap-memory-gc-cost-factor` (`-XX:HeapMemoryGcCostFactor`)

### Heap Memory GC Cost Factor

The more applications are running and allocating on device, the more GC is
required. How much GC is required varies over time depending on how the phone
is being used. The new tuning knob for time-based GC triggering controls how
GC costs are distributed between memory and GC CPU as the need for GC
increases on device.

`dalvik.vm.heap-memory-gc-cost-factor` (`-XX:HeapMemoryGcCostFactor`)
specifies the ratio of CPU overhead to memory overhead associated with garbage
collection on device. It can be thought of as specifying what percent of a
single CPU core's time is worth spending on garbage collection to save 1% of
total device memory. It defaults to 1.0.

For example, consider a device with 8GB total memory using the default tuning
knob value of 1.0. Time-based GC triggering balances memory and CPU so that
the ratio of memory to CPU overhead is 81.92MB per 10ms of CPU spent doing GC
per second wall clock time. Every 1% increase in required GC costs an
additional 81.92MB of memory and 10ms CPU per second.

Increasing the tuning knob value makes garbage collection more aggressive,
reducing memory overhead at the cost of additional GC. In theory, increasing
the value by 4x will result in around twice the GC CPU effort for half the
memory overhead.

Consider an extremely aggressive tuning knob value of 100.0. For every 1%
increase in required GC, the added cost is 10% of a CPU core but only 0.1% of
total device memory. At the margin you could save 5% of a CPU core for the low
cost of 0.1% of device memory by reducing the tuning knob value by 4x down to
25.0.

Consider an extremely relaxed tuning knob value of 0.01. For every 1% increase
in required GC, the added cost is 10% of total device memory but only 0.1% of
a CPU core. At the margin you could save 5% of device memory overhead for the
low cost of 0.1% of GC CPU by increasing the tuning knob value by 4x to 0.04.

To avoid such large imbalances in overhead of memory compared to CPU, we
recommend using the default tuning knob value of 1.0.

## Implications of time-based GC triggering

### Increased allocation rate leads to increased total heap size

Previously the total heap size of an application was a function of the
application's live heap size. Now the total heap size of an application
depends on both live heap size and allocation rate. The higher the allocation
rate, the larger the total heap size.

### Applications with higher allocation rates will do more GC

As with before, an application will do more GC the more it allocates. However,
with time-based GC triggering the increase in GC effort grows sublinearly
instead of linearly with the increase of allocation rate.

### Garbage collection can be triggered by the passage of time

Previously the GC trigger threshold was evaluated only when a new object was
allocated. With time-based GC triggering, there is additional logic to
trigger GC if the passage of time causes the threshold to drop below what has
already been allocated. This makes it possible for GC to be triggered in
applications that have stopped allocating entirely.

### Changes to java.lang.Runtime.totalMemory

The value returned by `java.lang.Runtime.totalMemory` now refers to the growth
limit for the heap rather than the target footprint. It cannot be used as a
good predictor for when the next garbage collection will be triggered, now
that the GC trigger depends in a more complicated way on both bytes allocated
and time passed since the previous GC.

### Future GC optimizations will result in memory and GC CPU savings

With time-based GC triggering, any optimizations that speed up the GC
algorithm will result in savings to both GC CPU and memory overhead on device.
The GC CPU savings comes from reduced per-GC cost, and the memory savings come
from increased rate of GC.

Previously, optimizations to the GC algorithm would result in GC CPU savings,
but not impact memory overhead.

## Time-based GC triggering in Perfetto traces

An easy way to check if time-based GC triggering is enabled via Perfetto trace
is to enable the `dalvik` atrace tag when taking the trace and search for
slices with `TimeBased` in the name. If any slices with `TimeBased` in the
name are found, time-based GC triggering is enabled. If there aren't any
slices with `TimeBased` in the name, it is unlikely that time-based GC
triggering is enabled.
