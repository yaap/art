/*
 * Copyright 2014 The Android Open Source Project
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

#include "jit_compiler.h"

#include "android-base/stringprintf.h"
#include "arch/instruction_set.h"
#include "arch/instruction_set_features.h"
#include "art_method-inl.h"
#include "base/logging.h"  // For VLOG
#include "base/systrace.h"
#include "base/time_utils.h"
#include "base/timing_logger.h"
#include "compiler.h"
#include "debug/elf_debug_writer.h"
#include "driver/compiler_options.h"
#include "export/jit_create.h"
#include "jit/debugger_interface.h"
#include "jit/jit.h"
#include "jit/jit_code_cache.h"
#include "jit/jit_logger.h"
#include "trace_common.h"

namespace art HIDDEN {
namespace jit {

JitCompiler* JitCompiler::Create() {
  return new JitCompiler();
}

void JitCompiler::SetDebuggableCompilerOption(bool value) {
  compiler_options_->SetDebuggable(value);
}

void JitCompiler::ParseCompilerOptions() {
  // Special case max code units for inlining, whose default is "unset" (implictly
  // meaning no limit). Do this before parsing the actual passed options.
  compiler_options_->SetInlineMaxCodeUnits(CompilerOptions::kDefaultInlineMaxCodeUnits);
  Runtime* runtime = Runtime::Current();
  {
    std::string error_msg;
    if (!compiler_options_->ParseCompilerOptions(runtime->GetCompilerOptions(),
                                                /*ignore_unrecognized=*/ true,
                                                &error_msg)) {
      LOG(FATAL) << error_msg;
      UNREACHABLE();
    }
  }
  // Set to appropriate JIT compiler type.
  compiler_options_->compiler_type_ = runtime->IsZygote()
      ? CompilerOptions::CompilerType::kSharedCodeJitCompiler
      : CompilerOptions::CompilerType::kJitCompiler;
  // JIT is never PIC, no matter what the runtime compiler options specify.
  compiler_options_->SetNonPic();

  // Set the appropriate read barrier option.
  compiler_options_->emit_read_barrier_ = gUseReadBarrier;

  // If the options don't provide whether we generate debuggable code, set
  // debuggability based on the runtime value.
  if (!compiler_options_->GetDebuggable()) {
    compiler_options_->SetDebuggable(runtime->IsJavaDebuggable());
  }

  compiler_options_->implicit_null_checks_ = runtime->GetImplicitNullChecks();
  compiler_options_->implicit_so_checks_ = runtime->GetImplicitStackOverflowChecks();
  compiler_options_->implicit_suspend_checks_ = runtime->GetImplicitSuspendChecks();

  const InstructionSet instruction_set = compiler_options_->GetInstructionSet();
  if (kRuntimeISA == InstructionSet::kArm) {
    DCHECK_EQ(instruction_set, InstructionSet::kThumb2);
  } else {
    DCHECK_EQ(instruction_set, kRuntimeISA);
  }
  std::unique_ptr<const InstructionSetFeatures> instruction_set_features;
  for (const std::string& option : runtime->GetCompilerOptions()) {
    VLOG(compiler) << "JIT compiler option " << option;
    std::string error_msg;
    if (option.starts_with("--instruction-set-variant=")) {
      const char* str = option.c_str() + strlen("--instruction-set-variant=");
      VLOG(compiler) << "JIT instruction set variant " << str;
      instruction_set_features = InstructionSetFeatures::FromVariantAndHwcap(
          instruction_set, str, &error_msg);
      if (instruction_set_features == nullptr) {
        LOG(WARNING) << "Error parsing " << option << " message=" << error_msg;
      }
    } else if (option.starts_with("--instruction-set-features=")) {
      const char* str = option.c_str() + strlen("--instruction-set-features=");
      VLOG(compiler) << "JIT instruction set features " << str;
      if (instruction_set_features == nullptr) {
        instruction_set_features = InstructionSetFeatures::FromVariant(
            instruction_set, "default", &error_msg);
        if (instruction_set_features == nullptr) {
          LOG(WARNING) << "Error parsing " << option << " message=" << error_msg;
        }
      }
      instruction_set_features =
          instruction_set_features->AddFeaturesFromString(str, &error_msg);
      if (instruction_set_features == nullptr) {
        LOG(WARNING) << "Error parsing " << option << " message=" << error_msg;
      }
    } else if (option.starts_with("--huge-method-max=")) {
      const char* str = option.c_str() + strlen("--huge-method-max=");
      VLOG(compiler) << "JIT huge method threshold " << str;
      int huge_method_threshold = static_cast<int>(strtol(str, NULL, 0));
      if (huge_method_threshold <= 0) {
        LOG(WARNING) << "Invalid huge method threshold: " << huge_method_threshold;
      } else {
        compiler_options_->SetHugeMethodThreshold(huge_method_threshold);
      }
    } else if (option.starts_with("--inline-max-code-units=")) {
      const char* str = option.c_str() + strlen("--inline-max-code-units=");
      VLOG(compiler) << "JIT inline max number of code units " << str;
      int inline_max_code_units = static_cast<int>(strtol(str, NULL, 0));
      if (inline_max_code_units <= 0) {
        LOG(WARNING) << "Invalid inline max code units: " << inline_max_code_units;
      } else {
        compiler_options_->SetInlineMaxCodeUnits(inline_max_code_units);
      }
    } else if (option.starts_with("--inline-max-total-instructions=")) {
      const char* str = option.c_str() + strlen("--inline-max-total-instructions=");
      VLOG(compiler) << "JIT inline max number of total instructions " << str;
      int inline_max_total_instructions = static_cast<int>(strtol(str, NULL, 0));
      if (inline_max_total_instructions <= 0) {
        LOG(WARNING) << "Invalid inline max total instructions: "
            << inline_max_total_instructions;
      } else {
        compiler_options_->SetInlineMaximumNumberOfTotalInstructions(inline_max_total_instructions);
      }
    } else if (option.starts_with("--inline-max-instructions-for-small-method=")) {
      const char* str = option.c_str() + strlen("--inline-max-instructions-for-small-method=");
      VLOG(compiler) << "JIT inline max number of instructions for small method " << str;
      int inline_max_instructions_for_small_method = static_cast<int>(strtol(str, NULL, 0));
      if (inline_max_instructions_for_small_method <= 0) {
        LOG(WARNING) << "Invalid inline max instructions for small method: "
            << inline_max_instructions_for_small_method;
      } else {
        compiler_options_->SetInlineMaximumNumberOfInstructionsForSmallMethod(
            inline_max_instructions_for_small_method);
      }
    } else if (option.starts_with("--inline-max-cumulated-dex-registers=")) {
      const char* str = option.c_str() + strlen("--inline-max-cumulated-dex-registers=");
      VLOG(compiler) << "JIT inline max number of cumulated dex registers " << str;
      int inline_max_cumulated_dex_registers = static_cast<int>(strtol(str, NULL, 0));
      if (inline_max_cumulated_dex_registers <= 0) {
        LOG(WARNING) << "Invalid inline max cumulated dex registers: "
            << inline_max_cumulated_dex_registers;
      } else {
        compiler_options_->SetInlineMaximumNumberOfCumulatedDexRegisters(
            inline_max_cumulated_dex_registers);
      }
    } else if (option.starts_with("--inline-max-recursive-calls=")) {
      const char* str = option.c_str() + strlen("--inline-max-recursive-calls=");
      VLOG(compiler) << "JIT inline max number of recursive calls " << str;
      int inline_max_recursive_calls = static_cast<int>(strtol(str, NULL, 0));
      if (inline_max_recursive_calls <= 0) {
        LOG(WARNING) << "Invalid inline max recursive calls: " << inline_max_recursive_calls;
      } else {
        compiler_options_->SetInlineMaximumNumberOfRecursiveCalls(inline_max_recursive_calls);
      }
    }
  }

  if (instruction_set_features == nullptr) {
    // '--instruction-set-features/--instruction-set-variant' were not used.
    // Use build-time defined features.
    instruction_set_features = InstructionSetFeatures::FromCppDefines();
  }
  compiler_options_->instruction_set_features_ = std::move(instruction_set_features);

  if (compiler_options_->GetGenerateDebugInfo()) {
    jit_logger_.reset(new JitLogger());
    jit_logger_->OpenLog();
  }

  compiler_options_->GetAssumeValueOptions().SetSdkInt(runtime->GetSdkVersion());

  if (ShouldEnableProfileCode()) {
    compiler_options_->enable_profile_code_ = true;
  }
}

JitCompilerInterface* jit_create() {
  VLOG(jit) << "Create jit compiler";
  auto* const jit_compiler = JitCompiler::Create();
  CHECK(jit_compiler != nullptr);
  VLOG(jit) << "Done creating jit compiler";
  return jit_compiler;
}

void JitCompiler::TypesLoaded(mirror::Class** types, size_t count) {
  const CompilerOptions& compiler_options = GetCompilerOptions();
  if (compiler_options.GetGenerateDebugInfo()) {
    InstructionSet isa = compiler_options.GetInstructionSet();
    const InstructionSetFeatures* features = compiler_options.GetInstructionSetFeatures();
    const ArrayRef<mirror::Class*> types_array(types, count);
    std::vector<uint8_t> elf_file =
        debug::WriteDebugElfFileForClasses(isa, features, types_array);

    // NB: Don't allow packing since it would remove non-backtrace data.
    MutexLock mu(Thread::Current(), *Locks::jit_lock_);
    AddNativeDebugInfoForJit(/*code_ptr=*/ nullptr, elf_file, /*allow_packing=*/ false);
  }
}

bool JitCompiler::GenerateDebugInfo() {
  return GetCompilerOptions().GetGenerateDebugInfo();
}

std::vector<uint8_t> JitCompiler::PackElfFileForJIT(ArrayRef<const JITCodeEntry*> elf_files,
                                                    ArrayRef<const void*> removed_symbols,
                                                    bool compress,
                                                    /*out*/ size_t* num_symbols) {
  return debug::PackElfFileForJIT(elf_files, removed_symbols, compress, num_symbols);
}

JitCompiler::JitCompiler() {
  compiler_options_.reset(new CompilerOptions());
  ParseCompilerOptions();
  compiler_.reset(Compiler::Create(*compiler_options_, /*storage=*/ nullptr));
}

JitCompiler::~JitCompiler() {
  if (compiler_options_->GetGenerateDebugInfo()) {
    jit_logger_->CloseLog();
  }
}

static const char* GetTimingLoggerMessage(CompilationKind compilation_kind) {
  switch (compilation_kind) {
    case CompilationKind::kOsr:
      return "Compiling OSR";
    case CompilationKind::kOptimized:
      return "Compiling optimized";
    case CompilationKind::kBaseline:
      return "Compiling baseline";
    case CompilationKind::kFast:
      return "Compiling fast";
  }
}

bool JitCompiler::CompileMethod(Thread* self,
                                JitMemoryRegion* region,
                                ArtMethod* method,
                                CompilationKind compilation_kind,
                                bool dynamic_instrumentation) {
  SCOPED_TRACE << "JIT compiling "
               << method->PrettyMethod()
               << " (kind=" << compilation_kind << ")"
               << " from " << method->GetDexFile()->GetLocation();

  DCHECK(!method->IsProxyMethod());
  DCHECK(method->GetDeclaringClass()->IsResolved());

  TimingLogger logger(
      "JIT compiler timing logger", true, VLOG_IS_ON(jit), TimingLogger::TimingKind::kThreadCpu);
  self->AssertNoPendingException();
  Runtime* runtime = Runtime::Current();

  // Do the compilation.
  bool success = false;
  Jit* jit = runtime->GetJit();
  {
    TimingLogger::ScopedTiming t2(GetTimingLoggerMessage(compilation_kind), &logger);
    JitCodeCache* const code_cache = jit->GetCodeCache();
    metrics::AutoTimer timer{runtime->GetMetrics()->JitMethodCompileTotalTime()};
    success = compiler_->JitCompile(self,
                                    code_cache,
                                    region,
                                    method,
                                    compilation_kind,
                                    jit_logger_.get(),
                                    dynamic_instrumentation);
    uint64_t duration_us = timer.Stop();
    VLOG(jit) << "Compilation of " << method->PrettyMethod() << " took "
              << PrettyDuration(UsToNs(duration_us));
    runtime->GetMetrics()->JitMethodCompileCount()->AddOne();
    runtime->GetMetrics()->JitMethodCompileTotalTimeDelta()->Add(duration_us);
    runtime->GetMetrics()->JitMethodCompileCountDelta()->AddOne();
  }

  // If we don't have a new task following this compile,
  // trim maps to reduce memory usage.
  if (jit->GetThreadPool() == nullptr || jit->GetThreadPool()->GetTaskCount(self) == 0) {
    TimingLogger::ScopedTiming t2("TrimMaps", &logger);
    runtime->GetJitArenaPool()->TrimMaps();
  }

  jit->AddTimingLogger(logger);
  return success;
}

bool JitCompiler::IsBaselineCompiler() const {
  return compiler_options_->IsBaseline();
}

uint32_t JitCompiler::GetInlineMaxCodeUnits() const {
  return compiler_options_->GetInlineMaxCodeUnits();
}

}  // namespace jit
}  // namespace art
