/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include "trace.h"

#include <sys/uio.h>
#include <unistd.h>

#include "android-base/macros.h"
#include "android-base/stringprintf.h"

#include "art_method-inl.h"
#include "base/casts.h"
#include "base/enums.h"
#include "base/os.h"
#include "base/stl_util.h"
#include "base/systrace.h"
#include "base/time_utils.h"
#include "base/unix_file/fd_file.h"
#include "base/utils.h"
#include "class_linker.h"
#include "common_throws.h"
#include "debugger.h"
#include "dex/descriptors_names.h"
#include "dex/dex_file-inl.h"
#include "entrypoints/quick/quick_entrypoints.h"
#include "gc/scoped_gc_critical_section.h"
#include "instrumentation.h"
#include "jit/jit.h"
#include "jit/jit_code_cache.h"
#include "mirror/class-inl.h"
#include "mirror/dex_cache-inl.h"
#include "mirror/object-inl.h"
#include "mirror/object_array-inl.h"
#include "nativehelper/scoped_local_ref.h"
#include "scoped_thread_state_change-inl.h"
#include "stack.h"
#include "thread.h"
#include "thread_list.h"

namespace art {

using android::base::StringPrintf;

static constexpr size_t TraceActionBits = MinimumBitsToStore(
    static_cast<size_t>(kTraceMethodActionMask));
static constexpr uint8_t kOpNewMethod = 1U;
static constexpr uint8_t kOpNewThread = 2U;
static constexpr uint8_t kOpTraceSummary = 3U;

static const char     kTraceTokenChar             = '*';
static const uint16_t kTraceHeaderLength          = 32;
static const uint32_t kTraceMagicValue            = 0x574f4c53;
static const uint16_t kTraceVersionSingleClock    = 2;
static const uint16_t kTraceVersionDualClock      = 3;
static const uint16_t kTraceRecordSizeSingleClock = 10;  // using v2
static const uint16_t kTraceRecordSizeDualClock   = 14;  // using v3 with two timestamps

TraceClockSource Trace::default_clock_source_ = kDefaultTraceClockSource;

Trace* volatile Trace::the_trace_ = nullptr;
pthread_t Trace::sampling_pthread_ = 0U;
std::unique_ptr<std::vector<ArtMethod*>> Trace::temp_stack_trace_;

// The key identifying the tracer to update instrumentation.
static constexpr const char* kTracerInstrumentationKey = "Tracer";

static TraceAction DecodeTraceAction(uint32_t tmid) {
  return static_cast<TraceAction>(tmid & kTraceMethodActionMask);
}

namespace {
// Scaling factor to convert timestamp counter into wall clock time reported in micro seconds.
// This is initialized at the start of tracing using the timestamp counter update frequency.
// See InitializeTimestampCounters for more details.
double tsc_to_microsec_scaling_factor = -1.0;

uint64_t GetTimestamp() {
  uint64_t t = 0;
#if defined(__arm__)
  // On ARM 32 bit, we don't always have access to the timestamp counters from user space. There is
  // no easy way to check if it is safe to read the timestamp counters. There is HWCAP_EVTSTRM which
  // is set when generic timer is available but not necessarily from the user space. Kernel disables
  // access to generic timer when there are known problems on the target CPUs. Sometimes access is
  // disabled only for 32-bit processes even when 64-bit processes can accesses the timer from user
  // space. These are not reflected in the HWCAP_EVTSTRM capability.So just fallback to
  // clock_gettime on these processes. See b/289178149 for more discussion.
  t = MicroTime();
#elif defined(__aarch64__)
  // See Arm Architecture Registers  Armv8 section System Registers
  asm volatile("mrs %0, cntvct_el0" : "=r"(t));
#elif defined(__i386__) || defined(__x86_64__)
  // rdtsc returns two 32-bit values in rax and rdx even on 64-bit architectures.
  unsigned int lo, hi;
  asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
  t = (static_cast<uint64_t>(hi) << 32) | lo;
#elif defined(__riscv)
  asm volatile("rdtime %0" : "=r"(t));
#else
  t = MicroTime();
#endif
  return t;
}

#if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__)
// Here we compute the scaling factor by sleeping for a millisecond. Alternatively, we could
// generate raw timestamp counter and also time using clock_gettime at the start and the end of the
// trace. We can compute the frequency of timestamp counter upadtes in the post processing step
// using these two samples. However, that would require a change in Android Studio which is the main
// consumer of these profiles. For now, just compute the frequency of tsc updates here.
double computeScalingFactor() {
  uint64_t start = MicroTime();
  uint64_t start_tsc = GetTimestamp();
  // Sleep for one millisecond.
  usleep(1000);
  uint64_t diff_tsc = GetTimestamp() - start_tsc;
  uint64_t diff_time = MicroTime() - start;
  double scaling_factor = static_cast<double>(diff_time) / diff_tsc;
  DCHECK(scaling_factor > 0.0) << scaling_factor;
  return scaling_factor;
}
#endif

#if defined(__i386__) || defined(__x86_64__)
double GetScalingFactorForX86() {
  uint32_t eax, ebx, ecx;
  asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx) : "a"(0x0), "c"(0));
  if (eax < 0x15) {
    // There is no 15H - Timestamp counter and core crystal clock information
    // leaf. Just compute the frequency.
    return computeScalingFactor();
  }

  // From Intel architecture-instruction-set-extensions-programming-reference:
  // EBX[31:0]/EAX[31:0] indicates the ratio of the TSC frequency and the
  // core crystal clock frequency.
  // If EBX[31:0] is 0, the TSC and "core crystal clock" ratio is not enumerated.
  // If ECX is 0, the nominal core crystal clock frequency is not enumerated.
  // "TSC frequency" = "core crystal clock frequency" * EBX/EAX.
  // The core crystal clock may differ from the reference clock, bus clock, or core clock
  // frequencies.
  // EAX Bits 31 - 00: An unsigned integer which is the denominator of the
  //                   TSC/"core crystal clock" ratio.
  // EBX Bits 31 - 00: An unsigned integer which is the numerator of the
  //                   TSC/"core crystal clock" ratio.
  // ECX Bits 31 - 00: An unsigned integer which is the nominal frequency of the core
  //                   crystal clock in Hz.
  // EDX Bits 31 - 00: Reserved = 0.
  asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx) : "a"(0x15), "c"(0));
  if (ebx == 0 || ecx == 0) {
    return computeScalingFactor();
  }
  double coreCrystalFreq = ecx;
  // frequency = coreCrystalFreq * (ebx / eax)
  // scaling_factor = seconds_to_microseconds / frequency
  //                = seconds_to_microseconds * eax / (coreCrystalFreq * ebx)
  double seconds_to_microseconds = 1000 * 1000;
  double scaling_factor = (seconds_to_microseconds * eax) / (coreCrystalFreq * ebx);
  return scaling_factor;
}
#endif

void InitializeTimestampCounters() {
  // It is sufficient to initialize this once for the entire execution. Just return if it is
  // already initialized.
  if (tsc_to_microsec_scaling_factor > 0.0) {
    return;
  }

#if defined(__arm__)
  // On ARM 32 bit, we don't always have access to the timestamp counters from
  // user space. Seem comment in GetTimestamp for more details.
  tsc_to_microsec_scaling_factor = 1.0;
#elif defined(__aarch64__)
  double seconds_to_microseconds = 1000 * 1000;
  uint64_t freq = 0;
  // See Arm Architecture Registers  Armv8 section System Registers
  asm volatile("mrs %0,  cntfrq_el0" : "=r"(freq));
  if (freq == 0) {
    // It is expected that cntfrq_el0 is correctly setup during system initialization but some
    // devices don't do this. In such cases fall back to computing the frequency. See b/315139000.
    tsc_to_microsec_scaling_factor = computeScalingFactor();
  } else {
    tsc_to_microsec_scaling_factor = seconds_to_microseconds / static_cast<double>(freq);
  }
#elif defined(__i386__) || defined(__x86_64__)
  tsc_to_microsec_scaling_factor = GetScalingFactorForX86();
#else
  tsc_to_microsec_scaling_factor = 1.0;
#endif
}

ALWAYS_INLINE uint64_t GetMicroTime(uint64_t counter) {
  DCHECK(tsc_to_microsec_scaling_factor > 0.0) << tsc_to_microsec_scaling_factor;
  return tsc_to_microsec_scaling_factor * counter;
}

}  // namespace

bool TraceWriter::HasMethodEncoding(ArtMethod* method) {
  return art_method_id_map_.find(method) != art_method_id_map_.end();
}

std::pair<uint32_t, bool> TraceWriter::GetMethodEncoding(ArtMethod* method) {
  auto it = art_method_id_map_.find(method);
  if (it != art_method_id_map_.end()) {
    return std::pair<uint32_t, bool>(it->second, false);
  } else {
    uint32_t idx = current_method_index_;
    art_method_id_map_.emplace(method, idx);
    current_method_index_++;
    return std::pair<uint32_t, bool>(idx, true);
  }
}

uint16_t TraceWriter::GetThreadEncoding(pid_t thread_id) {
  auto it = thread_id_map_.find(thread_id);
  if (it != thread_id_map_.end()) {
    return it->second;
  }

  uint16_t idx = current_thread_index_;
  thread_id_map_.emplace(thread_id, current_thread_index_);
  DCHECK_LT(current_thread_index_, (1 << 16) - 2);
  current_thread_index_++;
  return idx;
}

class TraceWriterTask final : public Task {
 public:
  TraceWriterTask(TraceWriter* trace_writer, uintptr_t* buffer, size_t cur_offset, size_t thread_id)
      : trace_writer_(trace_writer),
        buffer_(buffer),
        cur_offset_(cur_offset),
        thread_id_(thread_id) {}

  void Run(Thread* self ATTRIBUTE_UNUSED) override {
    std::unordered_map<ArtMethod*, std::string> method_infos;
    {
      ScopedObjectAccess soa(Thread::Current());
      trace_writer_->PreProcessTraceForMethodInfos(buffer_, cur_offset_, method_infos);
    }
    trace_writer_->FlushBuffer(buffer_, cur_offset_, thread_id_, method_infos);
    delete[] buffer_;
  }

 private:
  TraceWriter* trace_writer_;
  uintptr_t* buffer_;
  size_t cur_offset_;
  size_t thread_id_;
};

std::vector<ArtMethod*>* Trace::AllocStackTrace() {
  return (temp_stack_trace_.get() != nullptr)  ? temp_stack_trace_.release() :
      new std::vector<ArtMethod*>();
}

void Trace::FreeStackTrace(std::vector<ArtMethod*>* stack_trace) {
  stack_trace->clear();
  temp_stack_trace_.reset(stack_trace);
}

void Trace::SetDefaultClockSource(TraceClockSource clock_source) {
#if defined(__linux__)
  default_clock_source_ = clock_source;
#else
  if (clock_source != TraceClockSource::kWall) {
    LOG(WARNING) << "Ignoring tracing request to use CPU time.";
  }
#endif
}

static uint16_t GetTraceVersion(TraceClockSource clock_source) {
  return (clock_source == TraceClockSource::kDual) ? kTraceVersionDualClock
                                                   : kTraceVersionSingleClock;
}

static uint16_t GetRecordSize(TraceClockSource clock_source) {
  return (clock_source == TraceClockSource::kDual) ? kTraceRecordSizeDualClock
                                                   : kTraceRecordSizeSingleClock;
}

static uint16_t GetNumEntries(TraceClockSource clock_source) {
  return (clock_source == TraceClockSource::kDual) ? kNumEntriesForDualClock
                                                   : kNumEntriesForWallClock;
}

bool UseThreadCpuClock(TraceClockSource clock_source) {
  return (clock_source == TraceClockSource::kThreadCpu) ||
         (clock_source == TraceClockSource::kDual);
}

bool UseWallClock(TraceClockSource clock_source) {
  return (clock_source == TraceClockSource::kWall) || (clock_source == TraceClockSource::kDual);
}

void Trace::MeasureClockOverhead() {
  if (UseThreadCpuClock(clock_source_)) {
    Thread::Current()->GetCpuMicroTime();
  }
  if (UseWallClock(clock_source_)) {
    GetTimestamp();
  }
}

// Compute an average time taken to measure clocks.
uint32_t Trace::GetClockOverheadNanoSeconds() {
  Thread* self = Thread::Current();
  uint64_t start = self->GetCpuMicroTime();

  for (int i = 4000; i > 0; i--) {
    MeasureClockOverhead();
    MeasureClockOverhead();
    MeasureClockOverhead();
    MeasureClockOverhead();
    MeasureClockOverhead();
    MeasureClockOverhead();
    MeasureClockOverhead();
    MeasureClockOverhead();
  }

  uint64_t elapsed_us = self->GetCpuMicroTime() - start;
  return static_cast<uint32_t>(elapsed_us / 32);
}

// TODO: put this somewhere with the big-endian equivalent used by JDWP.
static void Append2LE(uint8_t* buf, uint16_t val) {
  *buf++ = static_cast<uint8_t>(val);
  *buf++ = static_cast<uint8_t>(val >> 8);
}

// TODO: put this somewhere with the big-endian equivalent used by JDWP.
static void Append4LE(uint8_t* buf, uint32_t val) {
  *buf++ = static_cast<uint8_t>(val);
  *buf++ = static_cast<uint8_t>(val >> 8);
  *buf++ = static_cast<uint8_t>(val >> 16);
  *buf++ = static_cast<uint8_t>(val >> 24);
}

// TODO: put this somewhere with the big-endian equivalent used by JDWP.
static void Append8LE(uint8_t* buf, uint64_t val) {
  *buf++ = static_cast<uint8_t>(val);
  *buf++ = static_cast<uint8_t>(val >> 8);
  *buf++ = static_cast<uint8_t>(val >> 16);
  *buf++ = static_cast<uint8_t>(val >> 24);
  *buf++ = static_cast<uint8_t>(val >> 32);
  *buf++ = static_cast<uint8_t>(val >> 40);
  *buf++ = static_cast<uint8_t>(val >> 48);
  *buf++ = static_cast<uint8_t>(val >> 56);
}

static void GetSample(Thread* thread, void* arg) REQUIRES_SHARED(Locks::mutator_lock_) {
  std::vector<ArtMethod*>* const stack_trace = Trace::AllocStackTrace();
  StackVisitor::WalkStack(
      [&](const art::StackVisitor* stack_visitor) REQUIRES_SHARED(Locks::mutator_lock_) {
        ArtMethod* m = stack_visitor->GetMethod();
        // Ignore runtime frames (in particular callee save).
        if (!m->IsRuntimeMethod()) {
          stack_trace->push_back(m);
        }
        return true;
      },
      thread,
      /* context= */ nullptr,
      art::StackVisitor::StackWalkKind::kIncludeInlinedFrames);
  Trace* the_trace = reinterpret_cast<Trace*>(arg);
  the_trace->CompareAndUpdateStackTrace(thread, stack_trace);
}

static void ClearThreadStackTraceAndClockBase(Thread* thread, [[maybe_unused]] void* arg) {
  thread->SetTraceClockBase(0);
  std::vector<ArtMethod*>* stack_trace = thread->GetStackTraceSample();
  thread->SetStackTraceSample(nullptr);
  delete stack_trace;
}

void Trace::CompareAndUpdateStackTrace(Thread* thread,
                                       std::vector<ArtMethod*>* stack_trace) {
  CHECK_EQ(pthread_self(), sampling_pthread_);
  std::vector<ArtMethod*>* old_stack_trace = thread->GetStackTraceSample();
  // Update the thread's stack trace sample.
  thread->SetStackTraceSample(stack_trace);
  // Read timer clocks to use for all events in this trace.
  uint32_t thread_clock_diff = 0;
  uint64_t timestamp_counter = 0;
  ReadClocks(thread, &thread_clock_diff, &timestamp_counter);
  if (old_stack_trace == nullptr) {
    // If there's no previous stack trace sample for this thread, log an entry event for all
    // methods in the trace.
    for (auto rit = stack_trace->rbegin(); rit != stack_trace->rend(); ++rit) {
      LogMethodTraceEvent(thread, *rit, kTraceMethodEnter, thread_clock_diff, timestamp_counter);
    }
  } else {
    // If there's a previous stack trace for this thread, diff the traces and emit entry and exit
    // events accordingly.
    auto old_rit = old_stack_trace->rbegin();
    auto rit = stack_trace->rbegin();
    // Iterate bottom-up over both traces until there's a difference between them.
    while (old_rit != old_stack_trace->rend() && rit != stack_trace->rend() && *old_rit == *rit) {
      old_rit++;
      rit++;
    }
    // Iterate top-down over the old trace until the point where they differ, emitting exit events.
    for (auto old_it = old_stack_trace->begin(); old_it != old_rit.base(); ++old_it) {
      LogMethodTraceEvent(thread, *old_it, kTraceMethodExit, thread_clock_diff, timestamp_counter);
    }
    // Iterate bottom-up over the new trace from the point where they differ, emitting entry events.
    for (; rit != stack_trace->rend(); ++rit) {
      LogMethodTraceEvent(thread, *rit, kTraceMethodEnter, thread_clock_diff, timestamp_counter);
    }
    FreeStackTrace(old_stack_trace);
  }
}

void* Trace::RunSamplingThread(void* arg) {
  Runtime* runtime = Runtime::Current();
  intptr_t interval_us = reinterpret_cast<intptr_t>(arg);
  CHECK_GE(interval_us, 0);
  CHECK(runtime->AttachCurrentThread("Sampling Profiler", true, runtime->GetSystemThreadGroup(),
                                     !runtime->IsAotCompiler()));

  while (true) {
    usleep(interval_us);
    ScopedTrace trace("Profile sampling");
    Thread* self = Thread::Current();
    Trace* the_trace;
    {
      MutexLock mu(self, *Locks::trace_lock_);
      the_trace = the_trace_;
      if (the_trace_->stop_tracing_) {
        break;
      }
    }
    {
      // Avoid a deadlock between a thread doing garbage collection
      // and the profile sampling thread, by blocking GC when sampling
      // thread stacks (see b/73624630).
      gc::ScopedGCCriticalSection gcs(self,
                                      art::gc::kGcCauseInstrumentation,
                                      art::gc::kCollectorTypeInstrumentation);
      ScopedSuspendAll ssa(__FUNCTION__);
      MutexLock mu(self, *Locks::thread_list_lock_);
      runtime->GetThreadList()->ForEach(GetSample, the_trace);
    }
  }

  runtime->DetachCurrentThread();
  return nullptr;
}

void Trace::Start(const char* trace_filename,
                  size_t buffer_size,
                  int flags,
                  TraceOutputMode output_mode,
                  TraceMode trace_mode,
                  int interval_us) {
  std::unique_ptr<File> file(OS::CreateEmptyFileWriteOnly(trace_filename));
  if (file == nullptr) {
    std::string msg = android::base::StringPrintf("Unable to open trace file '%s'", trace_filename);
    PLOG(ERROR) << msg;
    ScopedObjectAccess soa(Thread::Current());
    Thread::Current()->ThrowNewException("Ljava/lang/RuntimeException;", msg.c_str());
    return;
  }
  Start(std::move(file), buffer_size, flags, output_mode, trace_mode, interval_us);
}

void Trace::Start(int trace_fd,
                  size_t buffer_size,
                  int flags,
                  TraceOutputMode output_mode,
                  TraceMode trace_mode,
                  int interval_us) {
  if (trace_fd < 0) {
    std::string msg = android::base::StringPrintf("Unable to start tracing with invalid fd %d",
                                                  trace_fd);
    LOG(ERROR) << msg;
    ScopedObjectAccess soa(Thread::Current());
    Thread::Current()->ThrowNewException("Ljava/lang/RuntimeException;", msg.c_str());
    return;
  }
  std::unique_ptr<File> file(new File(trace_fd, /* path= */ "tracefile", /* check_usage= */ true));
  Start(std::move(file), buffer_size, flags, output_mode, trace_mode, interval_us);
}

void Trace::StartDDMS(size_t buffer_size,
                      int flags,
                      TraceMode trace_mode,
                      int interval_us) {
  Start(std::unique_ptr<File>(),
        buffer_size,
        flags,
        TraceOutputMode::kDDMS,
        trace_mode,
        interval_us);
}

void Trace::Start(std::unique_ptr<File>&& trace_file_in,
                  size_t buffer_size,
                  int flags,
                  TraceOutputMode output_mode,
                  TraceMode trace_mode,
                  int interval_us) {
  // We own trace_file now and are responsible for closing it. To account for error situations, use
  // a specialized unique_ptr to ensure we close it on the way out (if it hasn't been passed to a
  // Trace instance).
  auto deleter = [](File* file) {
    if (file != nullptr) {
      file->MarkUnchecked();  // Don't deal with flushing requirements.
      [[maybe_unused]] int result = file->Close();
      delete file;
    }
  };
  std::unique_ptr<File, decltype(deleter)> trace_file(trace_file_in.release(), deleter);

  Thread* self = Thread::Current();
  {
    MutexLock mu(self, *Locks::trace_lock_);
    if (the_trace_ != nullptr) {
      LOG(ERROR) << "Trace already in progress, ignoring this request";
      return;
    }
  }

  // Check interval if sampling is enabled
  if (trace_mode == TraceMode::kSampling && interval_us <= 0) {
    LOG(ERROR) << "Invalid sampling interval: " << interval_us;
    ScopedObjectAccess soa(self);
    ThrowRuntimeException("Invalid sampling interval: %d", interval_us);
    return;
  }

  // Initialize the frequency of timestamp counter updates here. This is needed
  // to get wallclock time from timestamp counter values.
  InitializeTimestampCounters();

  Runtime* runtime = Runtime::Current();

  // Enable count of allocs if specified in the flags.
  bool enable_stats = false;

  // Create Trace object.
  {
    // Suspend JIT here since we are switching runtime to debuggable. Debuggable runtimes cannot use
    // JITed code from before so we need to invalidated all JITed code here. Enter suspend JIT scope
    // to prevent any races with ongoing JIT compilations.
    jit::ScopedJitSuspend suspend_jit;
    // Required since EnableMethodTracing calls ConfigureStubs which visits class linker classes.
    gc::ScopedGCCriticalSection gcs(self,
                                    gc::kGcCauseInstrumentation,
                                    gc::kCollectorTypeInstrumentation);
    ScopedSuspendAll ssa(__FUNCTION__);
    MutexLock mu(self, *Locks::trace_lock_);
    if (the_trace_ != nullptr) {
      LOG(ERROR) << "Trace already in progress, ignoring this request";
    } else {
      enable_stats = (flags & kTraceCountAllocs) != 0;
      the_trace_ = new Trace(trace_file.release(), buffer_size, flags, output_mode, trace_mode);
      if (trace_mode == TraceMode::kSampling) {
        CHECK_PTHREAD_CALL(pthread_create, (&sampling_pthread_, nullptr, &RunSamplingThread,
                                            reinterpret_cast<void*>(interval_us)),
                                            "Sampling profiler thread");
        the_trace_->interval_us_ = interval_us;
      } else {
        if (!runtime->IsJavaDebuggable()) {
          art::jit::Jit* jit = runtime->GetJit();
          if (jit != nullptr) {
            jit->GetCodeCache()->InvalidateAllCompiledCode();
            jit->GetCodeCache()->TransitionToDebuggable();
            jit->GetJitCompiler()->SetDebuggableCompilerOption(true);
          }
          runtime->SetRuntimeDebugState(art::Runtime::RuntimeDebugState::kJavaDebuggable);
          runtime->GetInstrumentation()->UpdateEntrypointsForDebuggable();
          runtime->DeoptimizeBootImage();
        }
        // For thread cpu clocks, we need to make a kernel call and hence we call into c++ to
        // support them.
        bool is_fast_trace = !UseThreadCpuClock(the_trace_->GetClockSource());
#if defined(__arm__)
        // On ARM 32 bit, we don't always have access to the timestamp counters from
        // user space. Seem comment in GetTimestamp for more details.
        is_fast_trace = false;
#endif
        runtime->GetInstrumentation()->AddListener(
            the_trace_,
            instrumentation::Instrumentation::kMethodEntered |
                instrumentation::Instrumentation::kMethodExited |
                instrumentation::Instrumentation::kMethodUnwind,
            is_fast_trace);
        runtime->GetInstrumentation()->EnableMethodTracing(kTracerInstrumentationKey,
                                                           the_trace_,
                                                           /*needs_interpreter=*/false);
      }
    }
  }

  // Can't call this when holding the mutator lock.
  if (enable_stats) {
    runtime->SetStatsEnabled(true);
  }
}

void Trace::StopTracing(bool flush_entries) {
  Runtime* const runtime = Runtime::Current();
  Thread* const self = Thread::Current();

  pthread_t sampling_pthread = 0U;
  {
    MutexLock mu(self, *Locks::trace_lock_);
    if (the_trace_ == nullptr) {
      LOG(ERROR) << "Trace stop requested, but no trace currently running";
      return;
    }
    // Tell sampling_pthread_ to stop tracing.
    the_trace_->stop_tracing_ = true;
    sampling_pthread = sampling_pthread_;
  }

  // Make sure that we join before we delete the trace since we don't want to have
  // the sampling thread access a stale pointer. This finishes since the sampling thread exits when
  // the_trace_ is null.
  if (sampling_pthread != 0U) {
    CHECK_PTHREAD_CALL(pthread_join, (sampling_pthread, nullptr), "sampling thread shutdown");
  }

  // Make a copy of the_trace_, so it can be flushed later. We want to reset
  // the_trace_ to nullptr in suspend all scope to prevent any races
  Trace* the_trace = the_trace_;
  bool stop_alloc_counting = (the_trace->flags_ & Trace::kTraceCountAllocs) != 0;
  // Stop the trace sources adding more entries to the trace buffer and synchronise stores.
  {
    gc::ScopedGCCriticalSection gcs(
        self, gc::kGcCauseInstrumentation, gc::kCollectorTypeInstrumentation);
    jit::ScopedJitSuspend suspend_jit;
    ScopedSuspendAll ssa(__FUNCTION__);

    if (the_trace->trace_mode_ == TraceMode::kSampling) {
      MutexLock mu(self, *Locks::thread_list_lock_);
      runtime->GetThreadList()->ForEach(ClearThreadStackTraceAndClockBase, nullptr);
    } else {
      // For thread cpu clocks, we need to make a kernel call and hence we call into c++ to support
      // them.
      bool is_fast_trace = !UseThreadCpuClock(the_trace_->GetClockSource());
#if defined(__arm__)
        // On ARM 32 bit, we don't always have access to the timestamp counters from
        // user space. Seem comment in GetTimestamp for more details.
        is_fast_trace = false;
#endif
      runtime->GetInstrumentation()->RemoveListener(
          the_trace,
          instrumentation::Instrumentation::kMethodEntered |
              instrumentation::Instrumentation::kMethodExited |
              instrumentation::Instrumentation::kMethodUnwind,
          is_fast_trace);
      runtime->GetInstrumentation()->DisableMethodTracing(kTracerInstrumentationKey);
    }

    // Flush thread specific buffer from all threads before resetting the_trace_ to nullptr.
    // We also flush the buffer when destroying a thread which expects the_trace_ to be valid so
    // make sure that the per-thread buffer is reset before resetting the_trace_.
    {
      MutexLock tl_lock(Thread::Current(), *Locks::thread_list_lock_);
      for (Thread* thread : Runtime::Current()->GetThreadList()->GetList()) {
        if (thread->GetMethodTraceBuffer() != nullptr) {
          the_trace->trace_writer_->FlushBuffer(thread, /* is_sync= */ true);
          thread->ResetMethodTraceBuffer();
        }
      }
    }

    // Reset the_trace_ by taking a trace_lock
    MutexLock mu(self, *Locks::trace_lock_);
    the_trace_ = nullptr;
    sampling_pthread_ = 0U;
  }

  // At this point, code may read buf_ as its writers are shutdown
  // and the ScopedSuspendAll above has ensured all stores to buf_
  // are now visible.
  the_trace->trace_writer_->FinishTracing(the_trace->flags_, flush_entries);
  delete the_trace;

  if (stop_alloc_counting) {
    // Can be racy since SetStatsEnabled is not guarded by any locks.
    runtime->SetStatsEnabled(false);
  }
}

void Trace::FlushThreadBuffer(Thread* self) {
  MutexLock mu(self, *Locks::trace_lock_);
  the_trace_->trace_writer_->FlushBuffer(self, /* is_sync= */ false);
}

void Trace::Abort() {
  // Do not write anything anymore.
  StopTracing(/* flush_entries= */ false);
}

void Trace::Stop() {
  // Finish writing.
  StopTracing(/* flush_entries= */ true);
}

void Trace::Shutdown() {
  if (GetMethodTracingMode() != kTracingInactive) {
    Stop();
  }
}

TracingMode Trace::GetMethodTracingMode() {
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  if (the_trace_ == nullptr) {
    return kTracingInactive;
  } else {
    switch (the_trace_->trace_mode_) {
      case TraceMode::kSampling:
        return kSampleProfilingActive;
      case TraceMode::kMethodTracing:
        return kMethodTracingActive;
    }
    LOG(FATAL) << "Unreachable";
    UNREACHABLE();
  }
}

static constexpr size_t kMinBufSize = 18U;  // Trace header is up to 18B.
// Size of per-thread buffer size. The value is chosen arbitrarily. This value
// should be greater than kMinBufSize.
static constexpr size_t kPerThreadBufSize = 512 * 1024;
static_assert(kPerThreadBufSize > kMinBufSize);

namespace {

TraceClockSource GetClockSourceFromFlags(int flags) {
  bool need_wall = flags & Trace::TraceFlag::kTraceClockSourceWallClock;
  bool need_thread_cpu = flags & Trace::TraceFlag::kTraceClockSourceThreadCpu;
  if (need_wall && need_thread_cpu) {
    return TraceClockSource::kDual;
  } else if (need_wall) {
    return TraceClockSource::kWall;
  } else if (need_thread_cpu) {
    return TraceClockSource::kThreadCpu;
  } else {
    return kDefaultTraceClockSource;
  }
}

}  // namespace

TraceWriter::TraceWriter(File* trace_file,
                         TraceOutputMode output_mode,
                         TraceClockSource clock_source,
                         size_t buffer_size,
                         uint32_t clock_overhead_ns)
    : trace_file_(trace_file),
      trace_output_mode_(output_mode),
      clock_source_(clock_source),
      buf_(new uint8_t[std::max(kMinBufSize, buffer_size)]()),
      buffer_size_(std::max(kMinBufSize, buffer_size)),
      start_time_(GetMicroTime(GetTimestamp())),
      overflow_(false),
      clock_overhead_ns_(clock_overhead_ns),
      tracing_lock_("tracing lock", LockLevel::kTracingStreamingLock) {
  uint16_t trace_version = GetTraceVersion(clock_source_);
  if (output_mode == TraceOutputMode::kStreaming) {
    trace_version |= 0xF0U;
  }
  // Set up the beginning of the trace.
  memset(buf_.get(), 0, kTraceHeaderLength);
  Append4LE(buf_.get(), kTraceMagicValue);
  Append2LE(buf_.get() + 4, trace_version);
  Append2LE(buf_.get() + 6, kTraceHeaderLength);
  Append8LE(buf_.get() + 8, start_time_);
  if (trace_version >= kTraceVersionDualClock) {
    uint16_t record_size = GetRecordSize(clock_source_);
    Append2LE(buf_.get() + 16, record_size);
  }
  static_assert(18 <= kMinBufSize, "Minimum buffer size not large enough for trace header");

  cur_offset_ = kTraceHeaderLength;

  if (output_mode == TraceOutputMode::kStreaming) {
    // Flush the header information to the file. We use a per thread buffer, so
    // it is easier to just write the header information directly to file.
    if (!trace_file_->WriteFully(buf_.get(), kTraceHeaderLength)) {
      PLOG(WARNING) << "Failed streaming a tracing event.";
    }
    cur_offset_ = 0;
  }
  // Thread index of 0 is a special identifier used to distinguish between trace
  // event entries and thread / method info entries.
  current_thread_index_ = 1;

  // Don't create threadpool for a zygote. This would cause slowdown when forking because we need
  // to stop and start this thread pool. Method tracing on zygote isn't a frequent use case and
  // it is okay to flush on the main thread in such cases.
  if (!Runtime::Current()->IsZygote()) {
    thread_pool_.reset(new ThreadPool("Trace writer pool", 1));
    thread_pool_->StartWorkers(Thread::Current());
  }
}

Trace::Trace(File* trace_file,
             size_t buffer_size,
             int flags,
             TraceOutputMode output_mode,
             TraceMode trace_mode)
    : flags_(flags),
      trace_mode_(trace_mode),
      clock_source_(GetClockSourceFromFlags(flags)),
      interval_us_(0),
      stop_tracing_(false) {
  CHECK_IMPLIES(trace_file == nullptr, output_mode == TraceOutputMode::kDDMS);

  trace_writer_.reset(new TraceWriter(
      trace_file, output_mode, clock_source_, buffer_size, GetClockOverheadNanoSeconds()));
}

void TraceWriter::FinishTracing(int flags, bool flush_entries) {
  Thread* self = Thread::Current();
  if (flush_entries) {
    if (thread_pool_ != nullptr) {
      // Wait for any workers to be created. If we are stopping tracing as a part of runtime
      // shutdown, any unstarted workers can create problems if they try attaching while shutting
      // down.
      thread_pool_->WaitForWorkersToBeCreated();
      // Wait for any outstanding writer tasks to finish.
      thread_pool_->StopWorkers(self);
      thread_pool_->Wait(self, /* do_work= */ true, /* may_hold_locks= */ true);
    }

    size_t final_offset = 0;
    if (trace_output_mode_ != TraceOutputMode::kStreaming) {
      MutexLock mu(Thread::Current(), tracing_lock_);
      final_offset = cur_offset_;
    }

    // Compute elapsed time.
    uint64_t elapsed = GetMicroTime(GetTimestamp()) - start_time_;

    std::ostringstream os;

    os << StringPrintf("%cversion\n", kTraceTokenChar);
    os << StringPrintf("%d\n", GetTraceVersion(clock_source_));
    os << StringPrintf("data-file-overflow=%s\n", overflow_ ? "true" : "false");
    if (UseThreadCpuClock(clock_source_)) {
      if (UseWallClock(clock_source_)) {
        os << StringPrintf("clock=dual\n");
      } else {
        os << StringPrintf("clock=thread-cpu\n");
      }
    } else {
      os << StringPrintf("clock=wall\n");
    }
    os << StringPrintf("elapsed-time-usec=%" PRIu64 "\n", elapsed);
    if (trace_output_mode_ != TraceOutputMode::kStreaming) {
      size_t num_records = (final_offset - kTraceHeaderLength) / GetRecordSize(clock_source_);
      os << StringPrintf("num-method-calls=%zd\n", num_records);
    }
    os << StringPrintf("clock-call-overhead-nsec=%d\n", clock_overhead_ns_);
    os << StringPrintf("vm=art\n");
    os << StringPrintf("pid=%d\n", getpid());
    if ((flags & Trace::kTraceCountAllocs) != 0) {
      os << "alloc-count=" << Runtime::Current()->GetStat(KIND_ALLOCATED_OBJECTS) << "\n";
      os << "alloc-size=" << Runtime::Current()->GetStat(KIND_ALLOCATED_BYTES) << "\n";
      os << "gc-count=" <<  Runtime::Current()->GetStat(KIND_GC_INVOCATIONS) << "\n";
    }
    os << StringPrintf("%cthreads\n", kTraceTokenChar);
    {
      // TODO(b/280558212): Moving the Mutexlock out of DumpThreadList to try and
      // narrow down where seg fault is happening. Change this after the bug is
      // fixed.
      CHECK_NE(self, nullptr);
      MutexLock mu(self, tracing_lock_);
      DumpThreadList(os);
    }
    os << StringPrintf("%cmethods\n", kTraceTokenChar);
    DumpMethodList(os);
    os << StringPrintf("%cend\n", kTraceTokenChar);
    std::string header(os.str());

    if (trace_output_mode_ == TraceOutputMode::kStreaming) {
      DCHECK_NE(trace_file_.get(), nullptr);
      // It is expected that this method is called when all other threads are suspended, so there
      // cannot be any writes to trace_file_ after finish tracing.
      // Write a special token to mark the end of trace records and the start of
      // trace summary.
      uint8_t buf[7];
      Append2LE(buf, 0);
      buf[2] = kOpTraceSummary;
      Append4LE(buf + 3, static_cast<uint32_t>(header.length()));
      // Write the trace summary. The summary is identical to the file header when
      // the output mode is not streaming (except for methods).
      if (!trace_file_->WriteFully(buf, sizeof(buf)) ||
          !trace_file_->WriteFully(header.c_str(), header.length())) {
        PLOG(WARNING) << "Failed streaming a tracing event.";
      }
    } else {
      if (trace_file_.get() == nullptr) {
        std::vector<uint8_t> data;
        data.resize(header.length() + final_offset);
        memcpy(data.data(), header.c_str(), header.length());
        memcpy(data.data() + header.length(), buf_.get(), final_offset);
        Runtime::Current()->GetRuntimeCallbacks()->DdmPublishChunk(CHUNK_TYPE("MPSE"),
                                                                   ArrayRef<const uint8_t>(data));
      } else {
        if (!trace_file_->WriteFully(header.c_str(), header.length()) ||
            !trace_file_->WriteFully(buf_.get(), final_offset)) {
          std::string detail(StringPrintf("Trace data write failed: %s", strerror(errno)));
          PLOG(ERROR) << detail;
          ThrowRuntimeException("%s", detail.c_str());
        }
      }
    }
  } else {
    // This is only called from the child process post fork to abort the trace.
    // We shouldn't have any workers in the thread pool here.
    DCHECK_EQ(thread_pool_, nullptr);
  }

  if (trace_file_.get() != nullptr) {
    // Do not try to erase, so flush and close explicitly.
    if (flush_entries) {
      if (trace_file_->Flush() != 0) {
        PLOG(WARNING) << "Could not flush trace file.";
      }
    } else {
      trace_file_->MarkUnchecked();  // Do not trigger guard.
    }
    if (trace_file_->Close() != 0) {
      PLOG(ERROR) << "Could not close trace file.";
    }
  }
}

void Trace::DexPcMoved([[maybe_unused]] Thread* thread,
                       [[maybe_unused]] Handle<mirror::Object> this_object,
                       ArtMethod* method,
                       uint32_t new_dex_pc) {
  // We're not recorded to listen to this kind of event, so complain.
  LOG(ERROR) << "Unexpected dex PC event in tracing " << ArtMethod::PrettyMethod(method)
             << " " << new_dex_pc;
}

void Trace::FieldRead([[maybe_unused]] Thread* thread,
                      [[maybe_unused]] Handle<mirror::Object> this_object,
                      ArtMethod* method,
                      uint32_t dex_pc,
                      [[maybe_unused]] ArtField* field) REQUIRES_SHARED(Locks::mutator_lock_) {
  // We're not recorded to listen to this kind of event, so complain.
  LOG(ERROR) << "Unexpected field read event in tracing " << ArtMethod::PrettyMethod(method)
             << " " << dex_pc;
}

void Trace::FieldWritten([[maybe_unused]] Thread* thread,
                         [[maybe_unused]] Handle<mirror::Object> this_object,
                         ArtMethod* method,
                         uint32_t dex_pc,
                         [[maybe_unused]] ArtField* field,
                         [[maybe_unused]] const JValue& field_value)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  // We're not recorded to listen to this kind of event, so complain.
  LOG(ERROR) << "Unexpected field write event in tracing " << ArtMethod::PrettyMethod(method)
             << " " << dex_pc;
}

void Trace::MethodEntered(Thread* thread, ArtMethod* method) {
  uint32_t thread_clock_diff = 0;
  uint64_t timestamp_counter = 0;
  ReadClocks(thread, &thread_clock_diff, &timestamp_counter);
  LogMethodTraceEvent(thread, method, kTraceMethodEnter, thread_clock_diff, timestamp_counter);
}

void Trace::MethodExited(Thread* thread,
                         ArtMethod* method,
                         [[maybe_unused]] instrumentation::OptionalFrame frame,
                         [[maybe_unused]] JValue& return_value) {
  uint32_t thread_clock_diff = 0;
  uint64_t timestamp_counter = 0;
  ReadClocks(thread, &thread_clock_diff, &timestamp_counter);
  LogMethodTraceEvent(thread, method, kTraceMethodExit, thread_clock_diff, timestamp_counter);
}

void Trace::MethodUnwind(Thread* thread, ArtMethod* method, [[maybe_unused]] uint32_t dex_pc) {
  uint32_t thread_clock_diff = 0;
  uint64_t timestamp_counter = 0;
  ReadClocks(thread, &thread_clock_diff, &timestamp_counter);
  LogMethodTraceEvent(thread, method, kTraceUnroll, thread_clock_diff, timestamp_counter);
}

void Trace::ExceptionThrown([[maybe_unused]] Thread* thread,
                            [[maybe_unused]] Handle<mirror::Throwable> exception_object)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  LOG(ERROR) << "Unexpected exception thrown event in tracing";
}

void Trace::ExceptionHandled([[maybe_unused]] Thread* thread,
                             [[maybe_unused]] Handle<mirror::Throwable> exception_object)
    REQUIRES_SHARED(Locks::mutator_lock_) {
  LOG(ERROR) << "Unexpected exception thrown event in tracing";
}

void Trace::Branch(Thread* /*thread*/, ArtMethod* method,
                   uint32_t /*dex_pc*/, int32_t /*dex_pc_offset*/)
      REQUIRES_SHARED(Locks::mutator_lock_) {
  LOG(ERROR) << "Unexpected branch event in tracing" << ArtMethod::PrettyMethod(method);
}

void Trace::WatchedFramePop([[maybe_unused]] Thread* self,
                            [[maybe_unused]] const ShadowFrame& frame) {
  LOG(ERROR) << "Unexpected WatchedFramePop event in tracing";
}

void Trace::ReadClocks(Thread* thread, uint32_t* thread_clock_diff, uint64_t* timestamp_counter) {
  if (UseThreadCpuClock(clock_source_)) {
    uint64_t clock_base = thread->GetTraceClockBase();
    if (UNLIKELY(clock_base == 0)) {
      // First event, record the base time in the map.
      uint64_t time = thread->GetCpuMicroTime();
      thread->SetTraceClockBase(time);
    } else {
      *thread_clock_diff = thread->GetCpuMicroTime() - clock_base;
    }
  }
  if (UseWallClock(clock_source_)) {
    *timestamp_counter = GetTimestamp();
  }
}

std::string TraceWriter::GetMethodLine(const std::string& method_line, uint32_t method_index) {
  return StringPrintf("%#x\t%s", (method_index << TraceActionBits), method_line.c_str());
}

std::string TraceWriter::GetMethodInfoLine(ArtMethod* method) {
  method = method->GetInterfaceMethodIfProxy(kRuntimePointerSize);
  return StringPrintf("%s\t%s\t%s\t%s\n",
                      PrettyDescriptor(method->GetDeclaringClassDescriptor()).c_str(),
                      method->GetName(),
                      method->GetSignature().ToString().c_str(),
                      method->GetDeclaringClassSourceFile());
}

void TraceWriter::RecordThreadInfo(Thread* thread) {
  // This is the first event from this thread, so first record information about the thread.
  std::string thread_name;
  thread->GetThreadName(thread_name);

  // In tests, we destroy VM after already detaching the current thread. We re-attach the current
  // thread again as a "Shutdown thread" during the process of shutting down. So don't record
  // information about shutdown threads since it overwrites the actual thread_name.
  if (thread_name.compare("Shutdown thread") == 0) {
    return;
  }

  MutexLock mu(Thread::Current(), tracing_lock_);
  if (trace_output_mode_ != TraceOutputMode::kStreaming) {
    threads_list_.Overwrite(GetThreadEncoding(thread->GetTid()), thread_name);
    return;
  }

  static constexpr size_t kThreadNameHeaderSize = 7;
  uint8_t header[kThreadNameHeaderSize];
  Append2LE(header, 0);
  header[2] = kOpNewThread;
  Append2LE(header + 3, GetThreadEncoding(thread->GetTid()));
  DCHECK(thread_name.length() < (1 << 16));
  Append2LE(header + 5, static_cast<uint16_t>(thread_name.length()));

  if (!trace_file_->WriteFully(header, kThreadNameHeaderSize) ||
      !trace_file_->WriteFully(reinterpret_cast<const uint8_t*>(thread_name.c_str()),
                               thread_name.length())) {
    PLOG(WARNING) << "Failed streaming a tracing event.";
  }
}

void TraceWriter::PreProcessTraceForMethodInfos(
    uintptr_t* method_trace_entries,
    size_t current_offset,
    std::unordered_map<ArtMethod*, std::string>& method_infos) {
  // Compute the method infos before we process the entries. We don't want to assign an encoding
  // for the method here. The expectation is that once we assign a method id we write it to the
  // file before any other thread can see the method id. So we should assign method encoding while
  // holding the tracing_lock_ and not release it till we flush the method info to the file. We
  // don't want to flush entries to file while holding the mutator lock. We need the mutator lock to
  // get method info. So we just precompute method infos without assigning a method encoding here.
  // There may be a race and multiple threads computing the method info but only one of them would
  // actually put into the method_id_map_.
  MutexLock mu(Thread::Current(), tracing_lock_);
  size_t num_entries = GetNumEntries(clock_source_);
  DCHECK_EQ((kPerThreadBufSize - current_offset) % num_entries, 0u);
  for (size_t entry_index = kPerThreadBufSize; entry_index != current_offset;) {
    entry_index -= num_entries;
    uintptr_t method_and_action = method_trace_entries[entry_index];
    ArtMethod* method = reinterpret_cast<ArtMethod*>(method_and_action & kMaskTraceAction);
    if (!HasMethodEncoding(method) && method_infos.find(method) == method_infos.end()) {
      method_infos.emplace(method, std::move(GetMethodInfoLine(method)));
    }
  }
}

void TraceWriter::RecordMethodInfo(const std::string& method_info_line,
                                   uint32_t method_id,
                                   size_t* current_index,
                                   uint8_t* buffer,
                                   size_t buffer_size) {
  std::string method_line(GetMethodLine(method_info_line, method_id));
  // Write a special block with the name.
  static constexpr size_t kMethodNameHeaderSize = 5;
  uint8_t method_header[kMethodNameHeaderSize];
  DCHECK_LT(kMethodNameHeaderSize, kPerThreadBufSize);
  Append2LE(method_header, 0);
  method_header[2] = kOpNewMethod;

  uint16_t method_line_length = static_cast<uint16_t>(method_line.length());
  DCHECK(method_line.length() < (1 << 16));
  Append2LE(method_header + 3, method_line_length);

  EnsureSpace(buffer, current_index, buffer_size, kMethodNameHeaderSize);
  memcpy(buffer + *current_index, method_header, kMethodNameHeaderSize);
  *current_index += kMethodNameHeaderSize;

  EnsureSpace(buffer, current_index, buffer_size, method_line_length);
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(method_line.c_str());
  if (method_line_length < buffer_size) {
    memcpy(buffer + *current_index, ptr, method_line_length);
    *current_index += method_line_length;
  } else {
    // The data is larger than buffer, so write directly to the file. EnsureSpace should have
    // flushed any data in the buffer.
    DCHECK_EQ(*current_index, 0U);
    if (!trace_file_->WriteFully(ptr, method_line_length)) {
      PLOG(WARNING) << "Failed streaming a tracing event.";
    }
  }
}

void TraceWriter::FlushAllThreadBuffers() {
  ScopedThreadStateChange stsc(Thread::Current(), ThreadState::kSuspended);
  ScopedSuspendAll ssa(__FUNCTION__);
  MutexLock mu(Thread::Current(), *Locks::thread_list_lock_);
  for (Thread* thread : Runtime::Current()->GetThreadList()->GetList()) {
    if (thread->GetMethodTraceBuffer() != nullptr) {
      FlushBuffer(thread, /* is_sync= */ true);
      // We cannot flush anynore data, so just return.
      if (overflow_) {
        return;
      }
    }
  }
  return;
}

uintptr_t* TraceWriter::PrepareBufferForNewEntries(Thread* thread) {
  if (trace_output_mode_ == TraceOutputMode::kStreaming) {
    // In streaming mode, just flush the per-thread buffer and reuse the
    // existing buffer for new entries.
    FlushBuffer(thread, /* is_sync= */ false);
    DCHECK_EQ(overflow_, false);
  } else {
    // For non-streaming mode, flush all the threads to check if we have space in the common
    // buffer to record any future events.
    FlushAllThreadBuffers();
  }
  if (overflow_) {
    return nullptr;
  }
  return thread->GetMethodTraceBuffer();
}

void TraceWriter::FlushBuffer(Thread* thread, bool is_sync) {
  uintptr_t* method_trace_entries = thread->GetMethodTraceBuffer();
  size_t* current_offset = thread->GetMethodTraceIndexPtr();
  size_t tid = thread->GetTid();
  DCHECK(method_trace_entries != nullptr);

  if (is_sync || thread_pool_ == nullptr) {
    std::unordered_map<ArtMethod*, std::string> method_infos;
    PreProcessTraceForMethodInfos(method_trace_entries, *current_offset, method_infos);
    FlushBuffer(method_trace_entries, *current_offset, tid, method_infos);

    // This is a synchronous flush, so no need to allocate a new buffer. This is used either
    // when the tracing has finished or in non-streaming mode.
    // Just reset the buffer pointer to the initial value, so we can reuse the same buffer.
    *current_offset = kPerThreadBufSize;
  } else {
    // The TraceWriterTask takes the ownership of the buffer and delets the buffer once the
    // entries are flushed.
    thread_pool_->AddTask(Thread::Current(),
                          new TraceWriterTask(this, method_trace_entries, *current_offset, tid));

    // Create a new buffer and update the per-thread buffer so we don't have to wait for the
    // flushing to finish.
    uintptr_t* method_trace_buffer = new uintptr_t[std::max(kMinBufSize, kPerThreadBufSize)]();
    thread->SetMethodTraceBuffer(method_trace_buffer);
    *current_offset = kPerThreadBufSize;
  }

  return;
}

void TraceWriter::FlushBuffer(uintptr_t* method_trace_entries,
                              size_t current_offset,
                              size_t tid,
                              const std::unordered_map<ArtMethod*, std::string>& method_infos) {
  // Take a tracing_lock_ to serialize writes across threads. We also need to allocate a unique
  // method id for each method. We do that by maintaining a map from id to method for each newly
  // seen method. tracing_lock_ is required to serialize these.
  MutexLock mu(Thread::Current(), tracing_lock_);
  size_t current_index;
  uint8_t* buffer_ptr = nullptr;
  size_t buffer_size;
  std::unique_ptr<uint8_t[]> buffer;
  if (trace_output_mode_ == TraceOutputMode::kStreaming) {
    buffer_size = std::max(kMinBufSize, kPerThreadBufSize);
    buffer.reset(new uint8_t[buffer_size]);
    buffer_ptr = buffer.get();
    current_index = 0;
  } else {
    buffer_size = buffer_size_;
    buffer_ptr = buf_.get();
    current_index = cur_offset_;
  }
  uint16_t thread_id = GetThreadEncoding(tid);

  size_t num_entries = GetNumEntries(clock_source_);
  DCHECK_EQ((kPerThreadBufSize - current_offset) % num_entries, 0u);
  for (size_t entry_index = kPerThreadBufSize; entry_index != current_offset;) {
    entry_index -= num_entries;
    size_t record_index = entry_index;
    uintptr_t method_and_action = method_trace_entries[record_index++];
    ArtMethod* method = reinterpret_cast<ArtMethod*>(method_and_action & kMaskTraceAction);
    CHECK(method != nullptr);
    TraceAction action = DecodeTraceAction(method_and_action);
    uint32_t thread_time = 0;
    uint32_t wall_time = 0;
    if (UseThreadCpuClock(clock_source_)) {
      thread_time = method_trace_entries[record_index++];
    }
    if (UseWallClock(clock_source_)) {
      uint64_t timestamp = method_trace_entries[record_index++];
      if (art::kRuntimePointerSize == PointerSize::k32) {
        // On 32-bit architectures timestamp is stored as two 32-bit values.
        uint64_t high_timestamp = method_trace_entries[record_index++];
        timestamp = (high_timestamp << 32 | timestamp);
      }
      wall_time = GetMicroTime(timestamp) - start_time_;
    }

    auto [method_id, is_new_method] = GetMethodEncoding(method);
    if (is_new_method && trace_output_mode_ == TraceOutputMode::kStreaming) {
      RecordMethodInfo(
          method_infos.find(method)->second, method_id, &current_index, buffer_ptr, buffer_size);
    }

    const size_t record_size = GetRecordSize(clock_source_);
    DCHECK_LT(record_size, kPerThreadBufSize);
    if (trace_output_mode_ != TraceOutputMode::kStreaming &&
        current_index + record_size >= buffer_size) {
      cur_offset_ = current_index;
      overflow_ = true;
      return;
    }

    EnsureSpace(buffer_ptr, &current_index, buffer_size, record_size);
    EncodeEventEntry(
        buffer_ptr + current_index, thread_id, method_id, action, thread_time, wall_time);
    current_index += record_size;
  }

  if (trace_output_mode_ == TraceOutputMode::kStreaming) {
    // Flush the contents of buffer to file.
    if (!trace_file_->WriteFully(buffer_ptr, current_index)) {
      PLOG(WARNING) << "Failed streaming a tracing event.";
    }
  } else {
    // In non-streaming mode, we keep the data in the buffer and write to the
    // file when tracing has stopped. Just updated the offset of the buffer.
    cur_offset_ = current_index;
  }
  return;
}

void Trace::LogMethodTraceEvent(Thread* thread,
                                ArtMethod* method,
                                TraceAction action,
                                uint32_t thread_clock_diff,
                                uint64_t timestamp_counter) {
  // This method is called in both tracing modes (method and sampling). In sampling mode, this
  // method is only called by the sampling thread. In method tracing mode, it can be called
  // concurrently.

  // In non-streaming modes, we stop recoding events once the buffer is full.
  if (trace_writer_->HasOverflow()) {
    return;
  }

  uintptr_t* method_trace_buffer = thread->GetMethodTraceBuffer();
  size_t* current_index = thread->GetMethodTraceIndexPtr();
  // Initialize the buffer lazily. It's just simpler to keep the creation at one place.
  if (method_trace_buffer == nullptr) {
    method_trace_buffer = new uintptr_t[std::max(kMinBufSize, kPerThreadBufSize)]();
    thread->SetMethodTraceBuffer(method_trace_buffer);
    *current_index = kPerThreadBufSize;
    trace_writer_->RecordThreadInfo(thread);
  }

  size_t required_entries = GetNumEntries(clock_source_);
  if (*current_index < required_entries) {
    // This returns nullptr in non-streaming mode if there's an overflow and we cannot record any
    // more entries. In streaming mode, it returns nullptr if it fails to allocate a new buffer.
    method_trace_buffer = trace_writer_->PrepareBufferForNewEntries(thread);
    if (method_trace_buffer == nullptr) {
      return;
    }
  }

  // Record entry in per-thread trace buffer.
  // Update the offset
  int new_entry_index = *current_index - required_entries;
  *current_index = new_entry_index;

  // Ensure we always use the non-obsolete version of the method so that entry/exit events have the
  // same pointer value.
  method = method->GetNonObsoleteMethod();
  method_trace_buffer[new_entry_index++] = reinterpret_cast<uintptr_t>(method) | action;
  if (UseThreadCpuClock(clock_source_)) {
    method_trace_buffer[new_entry_index++] = thread_clock_diff;
  }
  if (UseWallClock(clock_source_)) {
    if (art::kRuntimePointerSize == PointerSize::k32) {
      // On 32-bit architectures store timestamp counter as two 32-bit values.
      method_trace_buffer[new_entry_index++] = static_cast<uint32_t>(timestamp_counter);
      method_trace_buffer[new_entry_index++] = timestamp_counter >> 32;
    } else {
      method_trace_buffer[new_entry_index++] = timestamp_counter;
    }
  }
}

void TraceWriter::EncodeEventEntry(uint8_t* ptr,
                                   uint16_t thread_id,
                                   uint32_t method_index,
                                   TraceAction action,
                                   uint32_t thread_clock_diff,
                                   uint32_t wall_clock_diff) {
  static constexpr size_t kPacketSize = 14U;  // The maximum size of data in a packet.
  DCHECK(method_index < (1 << (32 - TraceActionBits)));
  uint32_t method_value = (method_index << TraceActionBits) | action;
  Append2LE(ptr, thread_id);
  Append4LE(ptr + 2, method_value);
  ptr += 6;

  if (UseThreadCpuClock(clock_source_)) {
    Append4LE(ptr, thread_clock_diff);
    ptr += 4;
  }
  if (UseWallClock(clock_source_)) {
    Append4LE(ptr, wall_clock_diff);
  }
  static_assert(kPacketSize == 2 + 4 + 4 + 4, "Packet size incorrect.");
}

void TraceWriter::EnsureSpace(uint8_t* buffer,
                              size_t* current_index,
                              size_t buffer_size,
                              size_t required_size) {
  if (*current_index + required_size < buffer_size) {
    return;
  }

  if (!trace_file_->WriteFully(buffer, *current_index)) {
    PLOG(WARNING) << "Failed streaming a tracing event.";
  }
  *current_index = 0;
}

void TraceWriter::DumpMethodList(std::ostream& os) {
  MutexLock mu(Thread::Current(), tracing_lock_);
  for (auto const& entry : art_method_id_map_) {
    os << GetMethodLine(GetMethodInfoLine(entry.first), entry.second);
  }
}

void TraceWriter::DumpThreadList(std::ostream& os) {
  for (const auto& it : threads_list_) {
    os << it.first << "\t" << it.second << "\n";
  }
}

TraceOutputMode Trace::GetOutputMode() {
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  CHECK(the_trace_ != nullptr) << "Trace output mode requested, but no trace currently running";
  return the_trace_->trace_writer_->GetOutputMode();
}

Trace::TraceMode Trace::GetMode() {
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  CHECK(the_trace_ != nullptr) << "Trace mode requested, but no trace currently running";
  return the_trace_->trace_mode_;
}

int Trace::GetFlags() {
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  CHECK(the_trace_ != nullptr) << "Trace flags requested, but no trace currently running";
  return the_trace_->flags_;
}

int Trace::GetIntervalInMillis() {
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  CHECK(the_trace_ != nullptr) << "Trace interval requested, but no trace currently running";
  return the_trace_->interval_us_;
}

size_t Trace::GetBufferSize() {
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  CHECK(the_trace_ != nullptr) << "Trace buffer size requested, but no trace currently running";
  return the_trace_->trace_writer_->GetBufferSize();
}

bool Trace::IsTracingEnabled() {
  MutexLock mu(Thread::Current(), *Locks::trace_lock_);
  return the_trace_ != nullptr;
}

}  // namespace art
