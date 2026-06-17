/*
 * Copyright (C) 2020 The Android Open Source Project
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

/*
 * This is a #include, not a %include, because we want the C pre-processor
 * to expand the macros into assembler assignment statements.
 */
#include "asm_support.h"
#include "arch/arm64/asm_support_arm64.S"

/**
 * ARM64 Runtime register usage conventions.
 *
 *   r0     : w0 is 32-bit return register and x0 is 64-bit.
 *   r0-r7  : Argument registers.
 *   r8-r15 : Caller save registers (used as temporary registers).
 *   r16-r17: Also known as ip0-ip1, respectively. Used as scratch registers by
 *            the linker, by the trampolines and other stubs (the compiler uses
 *            these as temporary registers).
 *   r18    : Reserved for platform (SCS, shadow call stack)
 *   r19    : Pointer to thread-local storage.
 *   r20-r29: Callee save registers.
 *   r30    : (lr) is reserved (the link register).
 *   rsp    : (sp) is reserved (the stack pointer).
 *   rzr    : (zr) is reserved (the zero register).
 *
 *   Floating-point registers
 *   v0-v31
 *
 *   v0     : s0 is return register for singles (32-bit) and d0 for doubles (64-bit).
 *            This is analogous to the C/C++ (hard-float) calling convention.
 *   v0-v7  : Floating-point argument registers in both Dalvik and C/C++ conventions.
 *            Also used as temporary and codegen scratch registers.
 *
 *   v0-v7 and v16-v31 : Caller save registers (used as temporary registers).
 *   v8-v15 : bottom 64-bits preserved across C calls (d8-d15 are preserved).
 *
 *   v16-v31: Used as codegen temp/scratch.
 *   v8-v15 : Can be used for promotion.
 *
 *   Must maintain 16-byte stack alignment.
 *
 * Nterp notes:
 *
 * The following registers have fixed assignments:
 *
 *   reg nick      purpose
 *   x19  xSELF     self (Thread) pointer
 *   x20  wMR       marking register
 *   x21  xSUSPEND  suspend check register
 *   x29  xFP       interpreted frame pointer, used for accessing locals and args
 *   x22  xPC       interpreted program counter, used for fetching instructions
 *   x23  xINST     first 16-bit code unit of current instruction
 *   x24  xIBASE    interpreted instruction handler pointer, used for computed goto;
 *                  we initialize this register roughly (except the replacable bits) with
 *                  the 16-KiB aligned address of one of the `nterp*_op_nop` symbols and
 *                  then keep inserting opcodes to bits 6-13 to form the handler address.
 *   x25  xREFS     base of object references of dex registers.
 *   x26  xCACHE    pointer to the interpreter cache.
 *   x27  xDEXFILE  pointer to the `DexFile` defining the current method.
 *   x28  xCSTMP    callee-save scratch register (with 32-bit version wCSTMP).
 *   x16  ip        scratch reg
 *   x17  ip2       scratch reg (used by macros)
 *
 * Macros are provided for common operations.  They MUST NOT alter unspecified registers or
 * condition codes.
*/

/* single-purpose registers, given names for clarity */
#define CFI_DEX  22 // DWARF register number of the register holding dex-pc (xPC).
#define CFI_TMP  0  // DWARF register number of the first argument register (r0).
#define xPC      x22
#define xINST    x23
#define wINST    w23
#define xIBASE   x24
#define xREFS    x25
#define CFI_REFS 25
#define xCACHE   x26
#define xDEXFILE x27
#define wDEXFILE w27  // Used for loading heap references to retrieve the `DexFile` pointer.
#define xCSTMP   x28
#define wCSTMP   w28
#define ip       x16
#define ip2      x17
#define wip      w16
#define wip2     w17

// To avoid putting ifdefs arond the use of wMR, make sure it's defined.
// IsNterpSupported returns false for configurations that don't have wMR (typically CMS).
#ifndef wMR
#define wMR w20
#endif

// Temporary registers while setting up a frame for calls.
#define wARGS        w8  // args C-F for non-range calls, first arg for range calls.
#define xARGS        x8  // 64-bit register version of the wARGS register.
#define xSHORTY     x10  // Shorty pointer during nterp-to-non-nterp calls.
#define wSHORTY_LEN w11  // Shorty length during nterp-to-non-nterp calls.
#define xLOADW      x12  // Pointer to W register loading code during nterp-to-non-nterp calls.
#define xLOADS      x13  // Pointer to S register loading code during nterp-to-non-nterp calls.
#define xNEW_FP     x12  // New FP during nterp-to-nterp calls.
#define xNEW_REFS   x13  // New REFS during nterp-to-nterp calls.
#define CFI_NEW_REFS 13
#define xHIDDEN_ARG x15  // The hidden argument register for invoke-interface/-range.

// +8 for the ArtMethod of the caller.
#define OFFSET_TO_FIRST_ARGUMENT_IN_STACK (CALLEE_SAVES_SIZE + 8)

/*
 * Fetch the next instruction from xPC into wINST.  Does not advance xPC.
 */
.macro FETCH_INST
    ldrh    wINST, [xPC]
.endm

/*
 * Fetch the next instruction from the specified offset.  Advances xPC
 * to point to the next instruction.  "count" is in 16-bit code units.
 *
 * Because of the limited size of immediate constants on ARM, this is only
 * suitable for small forward movements (i.e. don't try to implement "goto"
 * with this).
 *
 * This must come AFTER anything that can throw an exception, or the
 * exception catch may miss.  (This also implies that it must come after
 * EXPORT_PC.)
 */
.macro FETCH_ADVANCE_INST count
    ldrh    wINST, [xPC, #((\count)*2)]!
.endm

/*
 * Similar to FETCH_ADVANCE_INST, but does not update xPC.  Used to load
 * xINST ahead of possible exception point.  Be sure to manually advance xPC
 * later.
 */
.macro PREFETCH_INST count
    ldrh    wINST, [xPC, #((\count)*2)]
.endm

/* Advance xPC by some number of code units. */
.macro ADVANCE count
  add  xPC, xPC, #((\count)*2)
.endm

/*
 * Fetch a half-word code unit from an offset past the current PC.  The
 * "count" value is in 16-bit code units.  Does not advance xPC.
 *
 * The "_S" variant works the same but treats the value as signed.
 */
.macro FETCH reg, count
    ldrh    \reg, [xPC, #((\count)*2)]
.endm

.macro FETCH_S reg, count
    ldrsh   \reg, [xPC, #((\count)*2)]
.endm

/*
 * Fetch one byte from an offset past the current PC.  Pass in the same
 * "count" as you would for FETCH, and an additional 0/1 indicating which
 * byte of the halfword you want (lo/hi).
 */
.macro FETCH_B reg, count, byte
    ldrb     \reg, [xPC, #((\count)*2+(\byte))]
.endm

/*
 * Insert the instruction's opcode field into the xIBASE register.
 */
.macro PREPARE_OPCODE_DISPATCH
    bfi     xIBASE, xINST, #NTERP_HANDLER_SIZE_LOG2, #8
.endm

/*
 * Begin executing the opcode in _reg.  Clobbers reg
 */

.macro GOTO_OPCODE
    br      xIBASE
.endm

/*
 * Get/set the 32-bit value from a Dalvik register.
 */
.macro GET_VREG reg, vreg
    ldr     \reg, [xFP, \vreg, uxtw #2]
.endm
.macro GET_VREG_PRESCALED reg, scaled_vreg
    ldr     \reg, [xFP, \scaled_vreg, uxtw]
.endm
.macro GET_VREG_OBJECT reg, vreg
    ldr     \reg, [xREFS, \vreg, uxtw #2]
.endm
.macro SET_VREG reg, vreg
    str     \reg, [xFP, \vreg, uxtw #2]
    str     wzr, [xREFS, \vreg, uxtw #2]
.endm
.macro SET_VREG_OBJECT reg, vreg
    str     \reg, [xFP, \vreg, uxtw #2]
    str     \reg, [xREFS, \vreg, uxtw #2]
.endm
.macro SET_VREG_FLOAT reg, vreg
    str     \reg, [xFP, \vreg, uxtw #2]
    str     wzr, [xREFS, \vreg, uxtw #2]
.endm

/*
 * Get/set the 64-bit value from a Dalvik register.
 */
.macro LOAD_SCALED_VREG_MASK scaled_mask_reg, unscaled_mask
    mov     \scaled_mask_reg, \unscaled_mask << 2
.endm
.macro EXTRACT_SCALED_VREG scaled_vreg, scaled_mask_reg, src_reg, lsb
    .if \lsb < 2
      and     \scaled_vreg, \scaled_mask_reg, \src_reg, lsl #(2 - \lsb)
    .else
      and     \scaled_vreg, \scaled_mask_reg, \src_reg, lsr #(\lsb - 2)
    .endif
.endm
.macro GET_VREG_WIDE_PRESCALED reg, scaled_vreg
    ldr     \reg, [xFP, \scaled_vreg, uxtw]
.endm
.macro GET_VREG_WIDE reg, vreg
    lsl     wip2, \vreg, #2
    GET_VREG_WIDE_PRESCALED \reg, wip2
.endm
.macro SET_VREG_WIDE_PRESCALED reg, scaled_vreg
    str     \reg, [xFP, \scaled_vreg, uxtw]
    str     xzr, [xREFS, \scaled_vreg, uxtw]
.endm
.macro SET_VREG_WIDE reg, vreg
    lsl     wip2, \vreg, #2
    SET_VREG_WIDE_PRESCALED \reg, wip2
.endm
.macro GET_VREG_DOUBLE_PRESCALED reg, scaled_vreg
    GET_VREG_WIDE_PRESCALED \reg, \scaled_vreg
.endm
.macro GET_VREG_DOUBLE reg, vreg
    GET_VREG_WIDE \reg, \vreg
.endm
.macro SET_VREG_DOUBLE reg, vreg
    SET_VREG_WIDE \reg, \vreg
.endm

/*
 * Get the 32-bit value from a Dalvik register and sign-extend to 64-bit.
 * Used to avoid an extra instruction in int-to-long.
 */
.macro GET_VREG_S reg, vreg
    ldrsw   \reg, [xFP, \vreg, uxtw #2]
.endm

// An assembly entry for nterp.
.macro OAT_ENTRY name
    .type \name, #function
    .hidden \name
    .global \name
    .balign 16
\name:
%   pass
.endm

.macro SIZE name
    .size \name, .-\name
.endm

.macro NAME_START name
    .type \name, #function
    .hidden \name  // Hide this as a global symbol, so we do not incur plt calls.
    .global \name
    /* Cache alignment for function entry */
    .balign 16
\name:
%   pass
.endm

.macro NAME_END name
  SIZE \name
.endm

// Macro for defining entrypoints into runtime. We don't need to save registers
// (we're not holding references there), but there is no
// kDontSave runtime method. So just use the kSaveRefsOnly runtime method.
.macro NTERP_TRAMPOLINE name, helper
  ENTRY \name
    SETUP_SAVE_REFS_ONLY_FRAME
    bl \helper
    RESTORE_SAVE_REFS_ONLY_FRAME
    REFRESH_MARKING_REGISTER
    ldr xIP0, [xSELF, # THREAD_EXCEPTION_OFFSET]   // Get exception field.
    cbnz xIP0, nterp_deliver_pending_exception
    ret
  END \name
.endm

.macro CLEAR_VOLATILE_MARKER reg
  neg \reg, \reg
.endm

.macro EXPORT_PC
    str    xPC, [xREFS, #-16]
.endm

.macro BRANCH
    add     xPC, xPC, wINST, sxtw #1    // update xPC
    // Update method counter and do a suspend check if the branch is negative or zero.
    cmp wINST, #0
    b.le NterpHotnessCheck
    FETCH wINST, 0                      // load wINST
    PREPARE_OPCODE_DISPATCH             // insert opcode to xIBASE
    GOTO_OPCODE                         // jump to next instruction
.endm

.macro TEST_IF_MARKING label
    cbnz wMR, \label
.endm

.macro POISON_HEAP_REF_IF_OBJECT is_object, rRef
   .if \is_object
     POISON_HEAP_REF \rRef
   .endif
.endm

.macro WRITE_BARRIER_NOT_NULL holder
   ldr     ip, [xSELF, #THREAD_CARD_TABLE_OFFSET]
   lsr     wip2, \holder, #CARD_TABLE_CARD_SHIFT
   strb    wip, [ip, ip2]
.endm

.macro WRITE_BARRIER_IF_OBJECT is_object, value, holder, label
   .if \is_object
     cbz     \value, \label
     WRITE_BARRIER_NOT_NULL \holder
\label:
%    pass
   .endif
.endm

.macro CHECK_AND_UPDATE_SHARED_MEMORY_METHOD if_hot, if_not_hot
    ldr wip, [x0, #ART_METHOD_ACCESS_FLAGS_OFFSET]
    tbz wip, #ART_METHOD_IS_MEMORY_SHARED_FLAG_BIT, \if_hot
    // Intrinsics are always in the boot image and considered hot.
    tbnz wip, #ART_METHOD_IS_INTRINSIC_FLAG_BIT, \if_hot
    ldr wip, [xSELF, #THREAD_SHARED_METHOD_HOTNESS_OFFSET]
    cbz wip, \if_hot
    add wip, wip, #-1
    str wip, [xSELF, #THREAD_SHARED_METHOD_HOTNESS_OFFSET]
    b \if_not_hot
.endm

// We don't use implicit suspend check in nterp because it wouldn't work
// with execute-only mapped binaries with the current implementation.
.macro DO_SUSPEND_CHECK continue_label
    ldr wip, [xSELF, #THREAD_FLAGS_OFFSET]
    tst wip, #THREAD_SUSPEND_OR_CHECKPOINT_REQUEST
    b.eq \continue_label
    EXPORT_PC
    bl    art_quick_test_suspend
.endm

// Setup the stack to start executing the method. Expects:
// - x0 to contain the ArtMethod
//
// Outputs
// - \regs_ins contains the dex registers size in bits 15:0 and inputs size in bits 31:15
// - \old_sp contains the old stack pointer.
// - if \ins is not "none", \ins contains the ins size
//
// Uses ip and ip2 as temporaries.
.macro SETUP_STACK_FRAME code_item, refs, fp, cfi_refs, old_sp, regs_ins=wip, access_flags=none
    // Fetch dex register size, ins size, outs size and (unnecessary) tries size with a single LDP.
#if CODE_ITEM_INS_SIZE_OFFSET != CODE_ITEM_REGISTERS_SIZE_OFFSET + 2 || \
    CODE_ITEM_OUTS_SIZE_OFFSET != CODE_ITEM_REGISTERS_SIZE_OFFSET + 4
#error Unexpected offsets.
#endif
    ldp \regs_ins, wip2, [\code_item, #CODE_ITEM_REGISTERS_SIZE_OFFSET]

    // Calculate the new SP and \refs offset from the new SP.
    // Frame size: 24 + (4 * outs_size) + padding1 + (2 * 4 * registers_size) + padding2.
    // 24 is for saving the previous frame, pc, and method being executed.
    //
    // Interleave with initializing the xDEXFILE (and \access_flags if requested) via the
    // declaring class and `DexCache` without read barrier (see `ArtMethod::GetDexFile()`,
    // note that we never enter an obsolete method and nterp is not used for proxy methods).
    mov \refs, #(24 + 4)                      // +4 for rounding up \refs offset.
#if ART_METHOD_ACCESS_FLAGS_OFFSET != ART_METHOD_DECLARING_CLASS_OFFSET + 4
#error Unexpected offsets, cannot use LDP.
#endif
    .ifnc \access_flags, none
      ldp wDEXFILE, \access_flags, [x0, #ART_METHOD_DECLARING_CLASS_OFFSET]
    .else
      ldr wDEXFILE, [x0, #ART_METHOD_DECLARING_CLASS_OFFSET]
    .endif
    add \refs, \refs, wip2, uxth #2           // Add `4 * outs_size`.
    sub ip, sp, \regs_ins, uxth #3            // Subtract `(2 * 4 * registers_size)` from SP.
    and \refs, \refs, #(-__SIZEOF_POINTER__)  // \refs offset = 24 + (4 * outs_size) + padding1.
    ldr wDEXFILE, [xDEXFILE, #MIRROR_CLASS_DEX_CACHE_OFFSET]
    UNPOISON_HEAP_REF wDEXFILE
    sub ip, ip, \refs                         // Subtract \refs offset.
    ldr xDEXFILE, [xDEXFILE, #MIRROR_DEX_CACHE_DEX_FILE_OFFSET]
    and ip, ip, #-16                          // Align new SP (add padding2).

    // Remember the old SP.
    mov \old_sp, sp
    .cfi_def_cfa_register \old_sp

    // Calculate \refs pointer from the \refs offset and new SP.
    add \refs, \refs, ip

    // Update the SP.
    mov sp, ip

    // Store the old SP and change CFA to use it.
    str \old_sp, [\refs, #-8]
    CFI_DEF_CFA_BREG_PLUS_UCONST \cfi_refs, -8, CALLEE_SAVES_SIZE

    // Calculate \fp pointer from \refs pointer and registers_size.
    add \fp, \refs, \regs_ins, uxth #2

    // Put nulls in reference frame.
    cbz \regs_ins, 2f  // Note: Zero regs in bits 15:0 implies zero inputs in bits 31:16.
    mov ip2, \refs
1:
    str xzr, [ip2], #8  // May clear vreg[0].
    cmp ip2, \fp
    b.lo 1b
2:
    // Save the ArtMethod.
    str x0, [sp]
.endm

// Increase method hotness and do suspend check before starting executing the method.
.macro START_EXECUTING_INSTRUCTIONS
    ldr x0, [sp]
    ldrh w2, [x0, #ART_METHOD_HOTNESS_COUNT_OFFSET]
#if (NTERP_HOTNESS_VALUE != 0)
#error Expected 0 for hotness value
#endif
    // If the counter is at zero, handle this in the runtime.
    cbz w2, 3f
    add x2, x2, #-1
    strh w2, [x0, #ART_METHOD_HOTNESS_COUNT_OFFSET]
1:
    DO_SUSPEND_CHECK continue_label=2f
2:
    FETCH_INST
    PREPARE_OPCODE_DISPATCH
    GOTO_OPCODE
3:
    CHECK_AND_UPDATE_SHARED_MEMORY_METHOD if_hot=4f, if_not_hot=1b
4:
    mov x1, xzr
    mov x2, xFP
    bl nterp_hot_method
    b 2b
%   pass
.endm

.macro SPILL_ALL_CALLEE_SAVES
    INCREASE_FRAME CALLEE_SAVES_SIZE
    // Note: we technically don't need to save x19 and x20,
    // but the runtime will expect those values to be there when unwinding
    // (see Arm64Context::DoLongJump checking for the thread register).
    SAVE_ALL_CALLEE_SAVES 0
.endm

.macro RESTORE_ALL_CALLEE_SAVES
    // FP callee-saves
    ldp d8, d9, [sp, #0]
    ldp d10, d11, [sp, #16]
    ldp d12, d13, [sp, #32]
    ldp d14, d15, [sp, #48]

    // GP callee-saves.
    // No need to restore x19 (it's always the thread), and
    // don't restore x20 (the marking register) as it may have been updated,
    // don't restore x21 (the suspend check register) as it may have been updated.
    RESTORE_REG      x22, 88
    RESTORE_TWO_REGS x23, x24, 96
    RESTORE_TWO_REGS x25, x26, 112
    RESTORE_TWO_REGS x27, x28, 128
    RESTORE_TWO_REGS x29, lr, 144

    DECREASE_FRAME CALLEE_SAVES_SIZE
.endm

.macro SPILL_ALL_ARGUMENTS
    stp x0, x1, [sp, #-128]!
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp d0, d1, [sp, #64]
    stp d2, d3, [sp, #80]
    stp d4, d5, [sp, #96]
    stp d6, d7, [sp, #112]
.endm

.macro RESTORE_ALL_ARGUMENTS
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldp d0, d1, [sp, #64]
    ldp d2, d3, [sp, #80]
    ldp d4, d5, [sp, #96]
    ldp d6, d7, [sp, #112]
    ldp x0, x1, [sp], #128
.endm

// Helper to setup the stack after doing a nterp to nterp call. This will setup:
// - xNEW_FP: the new pointer to dex registers
// - xNEW_REFS: the new pointer to references
// - xPC: the new PC pointer to execute
// - x3: the dex registers size in bits 15:0 and inputs size in bits 31:15
//
// The method expects:
// - x0 to contain the ArtMethod
// - x1 may contain the receiver and must not be clobbered
// - x2 to contain the code item
// - x4-x7 can be used as temporaries.
.macro SETUP_STACK_FOR_INVOKE
   // We do the same stack overflow check as the compiler. See CanMethodUseNterp
   // in how we limit the maximum nterp frame size.
   sub x16, sp, #STACK_OVERFLOW_RESERVED_BYTES
   ldr wzr, [x16]

   // Spill all callee saves to have a consistent stack frame whether we
   // are called by compiled code or nterp.
   SPILL_ALL_CALLEE_SAVES

   // Setup the frame. Use x4 for `old_sp` as the old SP is not needed after the setup.
   SETUP_STACK_FRAME x2, xNEW_REFS, xNEW_FP, CFI_NEW_REFS, x4, w3

   // Set the dex pc pointer to point to instructions.
   add xPC, x2, #CODE_ITEM_INSNS_OFFSET
   CFI_DEFINE_DEX_PC_WITH_OFFSET(CFI_TMP, CFI_DEX, 0)
.endm

// Copy one argument dex register. Emits exactly 4 instructions (16 bytes).
.macro N2N_COPY_NON_RANGE_ARG arg, refs, fp, offset, t0, t1
   GET_VREG_OBJECT \t1, \arg
   GET_VREG    \t0, \arg
   str         \t1, [\refs, #\offset]
   str         \t0, [\fp, #\offset]
.endm

// Setup arguments based on a non-range nterp to nterp call, and start executing
// the method. We expect:
// - xNEW_FP: the new pointer to dex registers
// - xNEW_REFS: the new pointer to references
// - xPC: the new PC pointer to execute
// - wINST: the invoke instruction with G arg in bits 11:8
// - x3: the dex registers size in bits 15:0 and inputs size in bits 31:15
// - x8: arguments C-F
// - x1: receiver if non-static.
// Note: the number of arguments in bits 15:12 of wINST is off by one for string init
.macro SETUP_NON_RANGE_ARGUMENTS_AND_EXECUTE is_static=0
   // /* op vA, vB, {vC...vG} */
   // The inputs size from the invoke in wINST bits 15:12 is one higher than the inputs size
   // in x3 bits 31:16 for string init. To avoid adjusting the `N2N_COPY_NON_RANGE_ARG` offsets
   // below, use the former for the inputs start calculation while using the latter for the
   // computed goto.
   .if \is_static
     adr x9, 1f
   .else
     adr x9, 1f + 16  // One less copy than the number of arguments.
   .endif
   sub wip, w3, w3, lsr #16           // inputs start index in low 16 bits
   ubfx w7, wINST, #8, #4             // Extract G
   // Note: The top 4 bits of dex registers size must be clear, otherwise the frame would
   // be too large and we would not use nterp (>=16KiB dex registers even without REFS).
   sub x9, x9, x3, lsr #(16 - 4)      // Target code to copy arguments
   ubfx w6, wARGS, #12, #4            // Extract F
   // Calculate input dex register addresses in xNEW_REFS and xNEW_FP.
   add x10, xNEW_REFS, wip, uxth #2
   ubfx w5, wARGS, #8, #4             // Extract E
   add x11, xNEW_FP, wip, uxth #2
   ubfx w4, wARGS, #4, #4             // Extract D
   .if \is_static
     ubfx w3, wARGS, #0, #4           // Extract C
   .endif
   // Jump to the code that performs the argument copying.
   br  x9

   .balign 16
   // Each `N2N_COPY_NON_RANGE_ARG` emits 16 bytes of code.
   N2N_COPY_NON_RANGE_ARG w7, x10, x11, 16, wip, wip2
   N2N_COPY_NON_RANGE_ARG w6, x10, x11, 12, wip, wip2
   N2N_COPY_NON_RANGE_ARG w5, x10, x11, 8, wip, wip2
   N2N_COPY_NON_RANGE_ARG w4, x10, x11, 4, wip, wip2
   .if \is_static
     N2N_COPY_NON_RANGE_ARG w3, x10, x11, 0, wip, wip2
   .endif
1:
   .if !\is_static
     str         w1, [x10]
     str         w1, [x11]
   .endif

   // Start executing the method.
   mov xFP, xNEW_FP
   mov xREFS, xNEW_REFS
   CFI_DEF_CFA_BREG_PLUS_UCONST CFI_REFS, -8, CALLEE_SAVES_SIZE
   START_EXECUTING_INSTRUCTIONS
.endm

// Setup arguments based on a range nterp to nterp call, and start executing
// the method.
// - xNEW_FP: the new pointer to dex registers
// - xNEW_REFS: the new pointer to references
// - xPC: the new PC pointer to execute
// - x3: the dex registers size in bits 15:0 and inputs size in bits 31:15
// - x8: first dex register
// - x1: receiver if non-static.
// Note: the number of arguments in bits 15:12 of wINST is off by one for string init
//
// Uses ip, ip2, x5, x6 as temporaries.
.macro SETUP_RANGE_ARGUMENTS_AND_EXECUTE is_static=0
   lsr w3, w3, #16
   sub x2, xNEW_FP, xNEW_REFS
   add x8, x8, x3             // index past the last argument
   .if !\is_static
     sub x3, x3, #1
   .endif
   sub x2, x2, #4

   cbz x3, 2f
   add x5, xREFS, x8, lsl #2  // pointer past the last argument in reference array
   add x6, xFP, x8, lsl #2    // pointer past the last argument in register array
1:
   ldr  wip, [x5, #-4]!
   ldr  wip2, [x6, #-4]!
   sub  x3, x3, 1
   str  wip, [xNEW_REFS, x2]
   str  wip2, [xNEW_FP, x2]
   sub x2, x2, 4
   cbnz x3, 1b
2:
   .if !\is_static
     str w1, [xNEW_REFS, x2]
     str w1, [xNEW_FP, x2]
   .endif
   mov xFP, xNEW_FP
   mov xREFS, xNEW_REFS
   CFI_DEF_CFA_BREG_PLUS_UCONST CFI_REFS, -8, CALLEE_SAVES_SIZE
   START_EXECUTING_INSTRUCTIONS
.endm

// Get shorty from method index (default) or proto index (with \is_proto_index=1).
.macro GET_SHORTY_AND_LENGTH shorty, length, index, slow_path, is_proto_index=0
   .if \is_proto_index
     // Load proto ids pointer.
     ldr ip2, [xDEXFILE, #DEX_FILE_PROTO_IDS_OFFSET]
   .else
     // The \index is a method index.
     // Load method ids and proto ids pointers.
#if DEX_FILE_PROTO_IDS_OFFSET != DEX_FILE_METHOD_IDS_OFFSET + 8
#error Unexpected offsets, cannot use LDP.
#endif
     ldp ip, ip2, [xDEXFILE, #DEX_FILE_METHOD_IDS_OFFSET]
     // Load proto index from `MethodId`.
#if DEX_FILE_METHOD_ID_SIZE != 8
#error Unexpected size of MethodId.
#endif
     add ip, ip, \index, lsl #3
     ldrh wip, [ip, #DEX_FILE_METHOD_ID_PROTO_INDEX_OFFSET]
   .endif
   // Load shorty index from `ProtoId`
#if DEX_FILE_PROTO_ID_SIZE != 12
#error Unexpected size of ProtoId.
#endif
#if DEX_FILE_PROTO_ID_SHORTY_INDEX_OFFSET != 0
#error Unexpected shorty index offset.
#endif
   .if \is_proto_index
     add ip, \index, \index, lsl #1
   .else
     add ip, ip, ip, lsl #1
   .endif
   ldr wip, [ip2, ip, lsl #2]
   // Load string ids and data begin.
   ldr ip2, [xDEXFILE, #DEX_FILE_STRING_IDS_OFFSET]
   ldr \shorty, [xDEXFILE, #DEX_FILE_DATA_BEGIN_OFFSET]
   // Load string data offset.
#if DEX_FILE_STRING_ID_SIZE != 4
#error Unexpected size of StringId.
#endif
   ldr wip, [ip2, ip, lsl #2]
   // Load shorty and first byte of length.
   add \shorty, \shorty, wip, uxtw
   ldrb \length, [\shorty], #1
   // Go to slow path if the shorty length is encoded in more than one byte.
   tbnz \length, #7, \slow_path
.endm

.macro GET_SHORTY_AND_LENGTH_SLOW_PATH shorty, length, resume
   // Note: `DexFileVerifier` does not prevent longer-than-necessary ULEB128
   // encodings, so even shorty length 1 can be encoded in up to five bytes.
   ldrb wip, [\shorty], #1
   bfi \length, wip, #7, #7
   tbz wip, #7, \resume
   ldrb wip, [\shorty], #1
   bfi \length, wip, #14, #7
   tbz wip, #7, \resume
   ldrb wip, [\shorty], #1
   bfi \length, wip, #21, #7
   tbz wip, #7, \resume
   ldrb wip, [\shorty], #1
   bfi \length, wip, #28, #4
   b \resume
.endm

.macro GET_CODE_ITEM dest, method
   ldr \dest, [\method, #ART_METHOD_DATA_OFFSET_64]
.endm

// Check if method entrypoint is the nterp entrypoint for quick nterp-to-nterp transition.
// Loads the code item and entrypoint to specified registers for potential reuse.
%def n2n_entry_point_check(code_item, entrypoint, call_compiled_code):
   // On entry, the method is x0, the instance is x1
#if ART_METHOD_QUICK_CODE_OFFSET_64 != ART_METHOD_DATA_OFFSET_64 + 8
#error Unexpected offsets, cannot use LDP.
#endif
   ldp ${code_item}, ${entrypoint}, [x0, #ART_METHOD_DATA_OFFSET_64]
   adr ip, ExecuteNterpImpl
   cmp ${entrypoint}, ip
   b.ne ${call_compiled_code}

.macro UPDATE_REGISTERS_FOR_STRING_INIT old_value, new_value
   mov wip, wzr
1:
   GET_VREG_OBJECT wip2, wip
   cmp wip2, \old_value
   b.ne 2f
   SET_VREG_OBJECT \new_value, wip
2:
   add wip, wip, #1
   add ip2, xREFS, wip, uxtw #2
   cmp ip2, xFP
   b.ne 1b
.endm

%def setup_return_value(shorty):
   // Note: We shall move d0 to x0 for both 'D' and 'F'. For 'F', this means moving garbage
   // into the higher 32 bits of x0 but the only way that x0 can be used for return shorty 'F'
   // is by `move-result` which shall ignore the high 32 bits anyway.
   sub wip, ${shorty}, #'D'
   fmov ip2, d0
   tst wip, #~2      // Test if result type char == 'D' or 'F'.
   csel x0, x0, ip2, ne

%def common_invoke_non_range_fast_path_setup(is_static, suffix):
   ubfx wip, wINST, #12, #4
%  if is_static:
     cbz wip, .Linvoke_fast_path_${suffix}
   cmp wip, #2
   b.lt .Lone_arg_fast_path_${suffix}
   b.eq .Ltwo_args_fast_path_${suffix}
   cmp wip, #4
   b.lt .Lthree_args_fast_path_${suffix}
   b.eq .Lfour_args_fast_path_${suffix}

   ubfx        wip, wINST, #8, #4
   GET_VREG    w5, wip
.Lfour_args_fast_path_${suffix}:
   ubfx        wip, wARGS, #12, #4
   GET_VREG    w4, wip
.Lthree_args_fast_path_${suffix}:
   ubfx        wip, wARGS, #8, #4
   GET_VREG    w3, wip
.Ltwo_args_fast_path_${suffix}:
   ubfx        wip, wARGS, #4, #4
   GET_VREG    w2, wip
.Lone_arg_fast_path_${suffix}:
%  if is_static:
     ubfx        wip, wARGS, #0, #4
     GET_VREG    w1, wip
%  else:
     // First argument already in w1.
%    pass
%  pass
.Linvoke_fast_path_${suffix}:
%  pass

%def common_invoke_non_range(is_static=False, suffix="", is_polymorphic=False, is_custom=False):
%  if is_polymorphic:
     // No fast path for polymorphic calls.
%    pass
%  elif is_custom:
     // No fast path for custom calls.
%    pass
%  else:
     ldr wip, [x0, #ART_METHOD_ACCESS_FLAGS_OFFSET]
     tbz wip, #ART_METHOD_NTERP_INVOKE_FAST_PATH_FLAG_BIT, .Lfast_path_with_few_args_${suffix}
%    common_invoke_non_range_fast_path_setup(is_static, suffix)
     FETCH_ADVANCE_INST 3
     blr lr
     PREPARE_OPCODE_DISPATCH
     GOTO_OPCODE

.Lfast_path_with_few_args_${suffix}:
     // Fast path when we have zero or one argument (modulo 'this'). If there
     // is one argument, we can put it in both floating point and core register.
%    if is_static:
       cmp wINST, #(2 << 12)
%    else:
       cmp wINST, #(3 << 12)
     b.ge .Lget_shorty_${suffix}
%    if is_static:
       tbz wINST, #12, .Linvoke_with_few_args_${suffix}
%    else:
       tbnz wINST, #12, .Linvoke_with_few_args_${suffix}
%    if is_static:
       ubfx w2, wARGS, #0, #4  // dex register of first argument
       GET_VREG w1, w2
       fmov s0, w1
%    else:
       ubfx w2, wARGS, #4, #4  // dex register of second argument
       GET_VREG w2, w2
       fmov s0, w2
%    pass
.Linvoke_with_few_args_${suffix}:
     // Check if the next instruction is move-result or move-result-wide.
     // If it is, we fetch the shorty and jump to the regular invocation.
     FETCH wINST, 3
     and wip, wINST, #0xfe
     cmp wip, #0x0a
     b.eq .Lget_shorty_and_invoke_${suffix}
     blr lr
     ADVANCE 3
     PREPARE_OPCODE_DISPATCH
     GOTO_OPCODE
.Lget_shorty_and_invoke_${suffix}:
     FETCH w7, 1
     GET_SHORTY_AND_LENGTH xSHORTY, wSHORTY_LEN, x7, \
         slow_path=.Lget_shorty_and_invoke_slow_path_${suffix}
.Lget_shorty_and_invoke_resume_${suffix}:
     ldrb wCSTMP, [xSHORTY]  // load return arg character
     b .Lsetup_finished_${suffix}
.Lget_shorty_and_invoke_slow_path_${suffix}:
     GET_SHORTY_AND_LENGTH_SLOW_PATH xSHORTY, wSHORTY_LEN, \
         resume=.Lget_shorty_and_invoke_resume_${suffix}
%  pass

.Lget_shorty_${suffix}:
%  if is_custom:
     stp x0, x1, [sp, #-16]!
     ldr x0, [sp, #16]
     mov x1, xPC
     bl NterpGetShortyFromInvokeCustom
     mov xSHORTY, x0
     mov wSHORTY_LEN, w1
     ldp x0, x1, [sp], #16
     FETCH wARGS, 2
%  else:
%    index_code_unit = 3 if is_polymorphic else 1
     FETCH w7, ${index_code_unit}
     GET_SHORTY_AND_LENGTH xSHORTY, wSHORTY_LEN, x7, \
         slow_path=.Lget_shorty_slow_path_${suffix}, is_proto_index=${is_polymorphic}
%  pass
.Lget_shorty_resume_${suffix}:
   // At this point:
   // - x0 contains method
   // - x1 contains 'this' pointer for instance method.
   // - wARGS (x8) contains arguments C-F.
   // - xSHORTY (x10) contains the shorty pointer.
   // - xSHORTY_LEN (x11) contains the shorty length.
   // - xHIDDEN_ARG (x15) contains the interface method for interface calls.
   // - wINST (w23) contains the number of arguments in bits 15:12 and the argument G in bits 11:8.
   // - lr contains the entrypoint, except for polymorphic and custom invokes.
   ldrb wCSTMP, [xSHORTY], #1  // load return arg character, advance to args at shorty + 1
   sub wSHORTY_LEN, wSHORTY_LEN, 1  // reduce the remaining shorty length.
   // Check for zero args for invoke-custom, invoke-polymorphic or string-init.
   // Zero args for invoke-static are handled above.
%  if is_custom or is_polymorphic:
     cbz wSHORTY_LEN, .Lsetup_finished_${suffix}
%  if is_static:
     adr xLOADW, .Lnon_range_load_w1_${suffix}
%  else:
     adr xLOADW, .Lnon_range_load_w2_${suffix}
%  if is_static:
     ubfiz wINST, wINST, #8, #12  // Move G to bits 19:16. Opcode bits shall be replaced below.
%  else:
     ubfiz wINST, wINST, #4, #12  // Move G to bits 15:12. Opcode bits shall be replaced below.
   adr xLOADS, .Lnon_range_load_s0_${suffix}
%  if is_static:
     bfxil wINST, wARGS, #0, #16  // Insert args C-F to bits 15:0.
%  else:
     bfxil wINST, wARGS, #4, #12  // Insert args D-F to bits 11:0.

   // Each load handler has 16 bytes, except for the "LOAD_LAST_VREG" handler which has 32 bytes.
%  load_w_to_x_offset = 16 * (4 if is_static else 3) + 32
%  load_s_to_d_offset = 16 * (4 if is_static else 3) + 32
.Lnon_range_load_regs_${suffix}:
   ldrb wip2, [xSHORTY], #1       // Load next character in signature, and increment.
   ubfiz wip, wINST, #2, #4       // Extract and prescale current argument vreg.
   sub wSHORTY_LEN, wSHORTY_LEN, #1
   cmp wip2, #'J'                 // Is this a long?
   beq .Lnon_range_load_long_${suffix}
   cmp wip2, #'D'                 // Is this a double?
   beq .Lnon_range_load_double_${suffix}
   lsr wINST, wINST, #4
   cmp wip2, #'F'                 // Is this a float?
   beq .Lnon_range_load_float_${suffix}
   // Everything else uses a 4-byte GPR.
   br xLOADW
.Lnon_range_load_long_${suffix}:
   add ip2, xLOADW, ${load_w_to_x_offset}
   lsr wINST, wINST, #8
   br ip2
.Lnon_range_load_float_${suffix}:
   br xLOADS
.Lnon_range_load_double_${suffix}:
   add ip2, xLOADS, ${load_s_to_d_offset}
   lsr wINST, wINST, #8
   br ip2

   // The `cont` can have a special value "LOAD_LAST_VREG" to specify that the last vreg
   // should be loaded to both W register and S register without looking at the shorty.
   // This is an optimization for invokes with 5 vregs with non-wide last argument.
   // Without "LOAD_LAST_VREG", the handler has 16 bytes (or less if `cont == done`),
   // with "LOAD_LAST_VREG" it has over 16 bytes but no more than 32 bytes, so
   // `.balign 16` shall round it up to 32 bytes for the following handler.
%  def non_range_load_vreg(reg, done, cont):
%    narrow = 'w' if reg[0] == 'w' or reg[0] == 'x' else 's'
%    reg_number = int(reg[1:])
%    if reg[0] == 'x' or reg[0] == 'd':
%      load_narrow_to_wide_offset = load_w_to_x_offset if reg[0] == 'x' else load_s_to_d_offset
       .org .Lnon_range_load_${narrow}${reg_number}_${suffix} + ${load_narrow_to_wide_offset}
%    else:
       .balign 16
%    pass
.Lnon_range_load_${reg}_${suffix}:
     // Note: `GET_VREG_DOUBLE_PRESCALED` just forwards args to `GET_WREG_WIDE_PRESCALED`.
%    get_vreg_suffix = "_WIDE" if reg[0] == 'x' or reg[0] == 'd' else ""
     GET_VREG${get_vreg_suffix}_PRESCALED ${reg}, wip
%    if done != cont:
       cbz wSHORTY_LEN, ${done}_${suffix}
%    next_reg_number = reg_number + 1
%    if cont == "LOAD_LAST_VREG":
       // Load the last vreg to both W register and S register.
       // We take this path only if all previous arguments have the same type (all FP
       // or all non-FP), so the register of other type is known (w1/w2, s0).
       ubfx wip, wINST, #0, #4    // Extract last argument vreg.
%      if reg[0] == 'w' or reg[0] == 'x':
         GET_VREG w${next_reg_number}, wip
         fmov s0, w${next_reg_number}
%      else:
%        if is_static:
           GET_VREG w1, wip
           fmov s${next_reg_number}, w1
%        else:
           GET_VREG w2, wip
           fmov s${next_reg_number}, w2
       b ${done}_${suffix}
%    else:
%      if done != cont:
%        narrow_upper = narrow.upper()
         adr xLOAD${narrow_upper}, .Lnon_range_load_${narrow}${next_reg_number}_${suffix}
       b ${cont}_${suffix}
%  pass

   // Note: To keep the distances from wN to xN (and from sN to dN) handlers identical in the
   // presence of 32-byte handlers with "LOAD_LAST_VREG", we reorder the last narrow handler
   // (which does not have a corresponding wide handler) after the narrow handler corresponding
   // to the wide "LOAD_LAST_VREG" handler. The narrow "LOAD_LAST_VREG" handler ends up last,
   // corresponding to the last wide handler.

%  if is_static:
%    non_range_load_vreg("w1", ".Lsetup_finished", ".Lnon_range_load_regs")
%  non_range_load_vreg("w2", ".Lsetup_finished", ".Lnon_range_load_regs")
%  non_range_load_vreg("w3", ".Lsetup_finished", ".Lnon_range_load_regs")
%  non_range_load_vreg("w5", ".Lsetup_finished", ".Lsetup_finished")  // See above for ordering.
%  non_range_load_vreg("w4", ".Lsetup_finished", "LOAD_LAST_VREG")

%  if is_static:
%    non_range_load_vreg("x1", ".Lsetup_finished", ".Lnon_range_load_regs")
%  non_range_load_vreg("x2", ".Lsetup_finished", ".Lnon_range_load_regs")
%  non_range_load_vreg("x3", ".Lsetup_finished", "LOAD_LAST_VREG")
%  non_range_load_vreg("x4", ".Lsetup_finished", ".Lsetup_finished")

%  non_range_load_vreg("s0", ".Lsetup_finished", ".Lnon_range_load_regs")
%  non_range_load_vreg("s1", ".Lsetup_finished", ".Lnon_range_load_regs")
%  if is_static:
%    non_range_load_vreg("s2", ".Lsetup_finished", ".Lnon_range_load_regs")
%    non_range_load_vreg("s4", ".Lsetup_finished", ".Lsetup_finished")  // See above for ordering.
%    non_range_load_vreg("s3", ".Lsetup_finished", "LOAD_LAST_VREG")
%  else:
%    non_range_load_vreg("s3", ".Lsetup_finished", ".Lsetup_finished")  // See above for ordering.
%    non_range_load_vreg("s2", ".Lsetup_finished", "LOAD_LAST_VREG")

%  non_range_load_vreg("d0", ".Lsetup_finished", ".Lnon_range_load_regs")
%  if is_static:
%    non_range_load_vreg("d1", ".Lsetup_finished", ".Lnon_range_load_regs")
%    non_range_load_vreg("d2", ".Lsetup_finished", "LOAD_LAST_VREG")
%    non_range_load_vreg("d3", ".Lsetup_finished", ".Lsetup_finished")
%  else:
%    non_range_load_vreg("d1", ".Lsetup_finished", "LOAD_LAST_VREG")
%    non_range_load_vreg("d2", ".Lsetup_finished", ".Lsetup_finished")
%  pass

.Lsetup_finished_${suffix}:
%  invoke_code_units = 4 if is_polymorphic else 3
   FETCH_ADVANCE_INST ${invoke_code_units}
%  if is_polymorphic:
     bl art_quick_invoke_polymorphic
%  elif is_custom:
     bl art_quick_invoke_custom
%  else:
     blr lr
   PREPARE_OPCODE_DISPATCH
%  setup_return_value("wCSTMP")
   GOTO_OPCODE

%  if not is_custom:
.Lget_shorty_slow_path_${suffix}:
     GET_SHORTY_AND_LENGTH_SLOW_PATH xSHORTY, wSHORTY_LEN, resume=.Lget_shorty_resume_${suffix}

%def common_invoke_range_fast_path_setup(is_static, suffix):
   lsr wip2, wINST, #8  // Number of arguments
%  if is_static:
     cbz ip2, .Linvoke_fast_path_range_${suffix}
   add x9, xFP, wARGS, uxtw #2  // location of first dex register value
   cmp ip2, #2
   b.lt .Lone_arg_fast_path_range_${suffix}
   b.eq .Ltwo_args_fast_path_range_${suffix}
   cmp ip2, #4
   b.lt .Lthree_args_fast_path_range_${suffix}
   b.eq .Lfour_args_fast_path_range_${suffix}
   cmp ip2, #6
   b.lt .Lfive_args_fast_path_range_${suffix}
   b.eq .Lsix_args_fast_path_range_${suffix}
   cmp ip2, #7
   b.eq .Lseven_args_fast_path_range_${suffix}
   // Setup x10 to point to the stack location of parameters. We do not need to copy
   // parameters that are passed in registers to their reserved stack slots.
   add x10, sp, #8  // Add space for the ArtMethod

.Lloop_over_fast_path_range_${suffix}:
   sub ip2, ip2, #1
   ldr wip, [x9, ip2, lsl #2]
   str wip, [x10, ip2, lsl #2]
   cmp ip2, #7
   b.ne .Lloop_over_fast_path_range_${suffix}

.Lseven_args_fast_path_range_${suffix}:
   ldr w7, [x9, #24]
.Lsix_args_fast_path_range_${suffix}:
   ldr w6, [x9, #20]
.Lfive_args_fast_path_range_${suffix}:
   ldr w5, [x9, #16]
.Lfour_args_fast_path_range_${suffix}:
   ldr w4, [x9, #12]
.Lthree_args_fast_path_range_${suffix}:
   ldr w3, [x9, #8]
.Ltwo_args_fast_path_range_${suffix}:
   ldr w2, [x9, #4]
.Lone_arg_fast_path_range_${suffix}:
%  if is_static:
     ldr w1, [x9, #0]
%  else:
     // First argument already in w1.
%    pass
%  pass
.Linvoke_fast_path_range_${suffix}:
%  pass

%def common_invoke_range(is_static=False, suffix="", is_polymorphic=False, is_custom=False):
%  if is_polymorphic:
     // No fast path for polymorphic calls.
%    pass
%  elif is_custom:
     // No fast path for custom calls.
%    pass
%  else:
     ldr wip, [x0, #ART_METHOD_ACCESS_FLAGS_OFFSET]
     tbz wip, #ART_METHOD_NTERP_INVOKE_FAST_PATH_FLAG_BIT, .Lfast_path_with_few_args_range_${suffix}
%    common_invoke_range_fast_path_setup(is_static, suffix)
     FETCH_ADVANCE_INST 3
     blr lr
     PREPARE_OPCODE_DISPATCH
     GOTO_OPCODE

.Lfast_path_with_few_args_range_${suffix}:
     // Fast path when we have zero or one argument (modulo 'this'). If there
     // is one argument, we can put it in both floating point and core register.
     lsr w2, wINST, #8 // number of arguments
%    if is_static:
       cmp w2, #1
%    else:
       cmp w2, #2
     b.lt .Linvoke_with_few_args_range_${suffix}
     b.ne .Lget_shorty_range_${suffix}
%    if is_static:
       GET_VREG w1, wARGS
       fmov s0, w1
%    else:
       add w2, wARGS, #1  // Add 1 for next argument
       GET_VREG w2, w2
       fmov s0, w2
%    pass
.Linvoke_with_few_args_range_${suffix}:
     // Check if the next instruction is move-result or move-result-wide.
     // If it is, we fetch the shorty and jump to the regular invocation.
     FETCH wINST, 3
     and wip, wINST, #0xfe
     cmp wip, #0x0a
     b.eq .Lget_shorty_and_invoke_range_${suffix}
     blr lr
     ADVANCE 3
     PREPARE_OPCODE_DISPATCH
     GOTO_OPCODE
.Lget_shorty_and_invoke_range_${suffix}:
     FETCH w7, 1
     GET_SHORTY_AND_LENGTH xSHORTY, wSHORTY_LEN, x7, \
         slow_path=.Lget_shorty_and_invoke_range_slow_path_${suffix}
.Lget_shorty_and_invoke_range_resume_${suffix}:
     ldrb wCSTMP, [xSHORTY]  // load return arg character
     b .Lsetup_finished_range_${suffix}
.Lget_shorty_and_invoke_range_slow_path_${suffix}:
     GET_SHORTY_AND_LENGTH_SLOW_PATH xSHORTY, wSHORTY_LEN, \
         resume=.Lget_shorty_and_invoke_range_resume_${suffix}
%  pass

.Lget_shorty_range_${suffix}:
%  if is_custom:
     stp x0, x1, [sp, #-16]!
     ldr x0, [sp, #16]
     mov x1, xPC
     bl NterpGetShortyFromInvokeCustom
     mov xSHORTY, x0
     mov wSHORTY_LEN, w1
     ldp x0, x1, [sp], #16
     FETCH wARGS, 2
%  else:
%    index_code_unit = 3 if is_polymorphic else 1
     FETCH w7, ${index_code_unit}
     GET_SHORTY_AND_LENGTH xSHORTY, wSHORTY_LEN, x7, \
         slow_path=.Lget_shorty_range_slow_path_${suffix}, is_proto_index=${is_polymorphic}
%  pass
.Lget_shorty_range_resume_${suffix}:
   // At this point:
   // - x0 contains method
   // - x1 contains 'this' pointer for instance method.
   // - wARGS (x8) contains the first arg
   // - xSHORTY (x10) contains the shorty pointer.
   // - xSHORTY_LEN (x11) contains the shorty length.
   // - xHIDDEN_ARG (x15) contains the interface method for interface calls.
   // - wINST (w23) contains the number of arguments in bits 15:8.
   // - lr contains the entrypoint, except for polymorphic and custom invokes.
   ldrb wCSTMP, [xSHORTY], #1  // load return arg character, advance to args at shorty + 1
   sub wSHORTY_LEN, wSHORTY_LEN, 1  // reduce the remaining shorty length.
   // Check for zero args for invoke-custom, invoke-polymorphic or string-init.
   // Zero args for invoke-static are handled above.
%  if is_custom or is_polymorphic:
     cbz wSHORTY_LEN, .Lsetup_finished_range_${suffix}
%  if is_static:
     adr xLOADW, .Lrange_load_w1_${suffix}
%  else:
     adr xLOADW, .Lrange_load_w2_${suffix}
   adr xLOADS, .Lrange_load_s0_${suffix}
   add xARGS, xFP, wARGS, uxtw #2 // args pointer in the FP array.
%  if is_static:
     add xINST, sp, #8            // args pointer in the stack out area.
%  else:
     add xINST, sp, #(8 + 4)      // args pointer in the stack out area; skip `this`
     add xARGS, xARGS, 4          // skip `this`
%  pass

   // Each load handler has 20 bytes and the copy handler has 16 bytes.
%  load_w_to_x_offset = 20 * (7 if is_static else 6) + 16
%  load_s_to_d_offset = 20 * 8 + 16
.Lrange_load_regs_${suffix}:
   ldrb wip2, [xSHORTY], #1       // Load next character in signature, and increment.
   sub wSHORTY_LEN, wSHORTY_LEN, #1
   cmp wip2, #'J'                 // Is this a long?
   beq .Lrange_load_long_${suffix}
   cmp wip2, #'D'                 // Is this a double?
   beq .Lrange_load_double_${suffix}
   cmp wip2, #'F'                 // Is this a float?
   beq .Lrange_load_float_${suffix}
   // Everything else uses a 4-byte GPR.
   br xLOADW
.Lrange_load_long_${suffix}:
   add ip2, xLOADW, #${load_w_to_x_offset}
   br ip2
.Lrange_load_float_${suffix}:
   br xLOADS
.Lrange_load_double_${suffix}:
   add ip2, xLOADS, #${load_s_to_d_offset}
   br ip2

   // Each handler has exactly five instructions (20 bytes).
%  def range_load_vreg(reg, done, cont, last=False):
%    narrow = 'w' if reg[0] == 'w' or reg[0] == 'x' else 's'
%    reg_number = int(reg[1:])
%    if reg[0] == 'x' or reg[0] == 'd':
%      load_narrow_to_wide_offset = load_w_to_x_offset if reg[0] == 'x' else load_s_to_d_offset
       .org .Lrange_load_${narrow}${reg_number}_${suffix} + ${load_narrow_to_wide_offset}
%    pass
.Lrange_load_${reg}_${suffix}:
%    reg_size = 8 if reg[0] == 'x' or reg[0] == 'd' else 4
     ldr ${reg}, [xARGS], #${reg_size}
     cbz wSHORTY_LEN, ${done}_${suffix}
     add xINST, xINST, ${reg_size}
%    narrow_upper = narrow.upper()
%    label = r"copy_{}".format(narrow) if last else r"load_{}{}".format(narrow, reg_number + 1)
     adr xLOAD${narrow_upper}, .Lrange_${label}_${suffix}
     b ${cont}_${suffix}
%  pass

%  if is_static:
%    range_load_vreg("w1", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("w2", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("w3", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("w4", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("w5", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("w6", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("w7", ".Lsetup_finished_range", ".Lrange_load_regs", last=True)
.Lrange_copy_w_${suffix}:
   ldr wip, [xARGS], #4
   str wip, [xINST], #4
   cbz wSHORTY_LEN, .Lsetup_finished_range_${suffix}
   b .Lrange_load_regs_${suffix}

%  if is_static:
%    range_load_vreg("x1", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("x2", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("x3", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("x4", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("x5", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("x6", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("x7", ".Lsetup_finished_range", ".Lrange_load_regs", last=True)
   .org .Lrange_copy_w_${suffix} + ${load_w_to_x_offset}
.Lrange_copy_x_${suffix}:
   ldr ip, [xARGS], #8
   str ip, [xINST], #8
   cbz wSHORTY_LEN, .Lsetup_finished_range_${suffix}
   b .Lrange_load_regs_${suffix}

%  range_load_vreg("s0", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("s1", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("s2", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("s3", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("s4", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("s5", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("s6", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("s7", ".Lsetup_finished_range", ".Lrange_load_regs", last=True)
.Lrange_copy_s_${suffix}:
   ldr wip, [xARGS], #4
   str wip, [xINST], #4
   cbz wSHORTY_LEN, .Lsetup_finished_range_${suffix}
   b .Lrange_load_regs_${suffix}

%  range_load_vreg("d0", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("d1", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("d2", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("d3", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("d4", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("d5", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("d6", ".Lsetup_finished_range", ".Lrange_load_regs")
%  range_load_vreg("d7", ".Lsetup_finished_range", ".Lrange_load_regs", last=True)
   .org .Lrange_copy_s_${suffix} + ${load_s_to_d_offset}
.Lrange_copy_d_${suffix}:
   ldr ip, [xARGS], #8
   str ip, [xINST], #8
   cbz wSHORTY_LEN, .Lsetup_finished_range_${suffix}
   b .Lrange_load_regs_${suffix}

.Lsetup_finished_range_${suffix}:
%  invoke_code_units = 4 if is_polymorphic else 3
   FETCH_ADVANCE_INST ${invoke_code_units}
%  if is_polymorphic:
     bl art_quick_invoke_polymorphic
%  elif is_custom:
     bl art_quick_invoke_custom
%  else:
     blr lr
   PREPARE_OPCODE_DISPATCH
%  setup_return_value("wCSTMP")
   GOTO_OPCODE

%  if not is_custom:
.Lget_shorty_range_slow_path_${suffix}:
     GET_SHORTY_AND_LENGTH_SLOW_PATH \
         xSHORTY, wSHORTY_LEN, resume=.Lget_shorty_range_resume_${suffix}

%def define_execute_nterp():
  // Puts the next int/long/object parameter passed in physical register
  // in the expected dex register array entry, and in case of object in the
  // expected reference array entry.
  .macro LOOP_OVER_SHORTY_STORING_GPRS gpr_64, gpr_32, shorty, arg_offset, regs, refs, finished
1: // LOOP
    ldrb wip, [\shorty], #1       // Load next character in shorty, and increment.
    cbz wip, \finished            // if (wip == '\0') goto finished
    cmp wip, #74                  // if (wip == 'J') goto FOUND_LONG
    b.eq 2f
    cmp wip, #70                  // if (wip == 'F') goto SKIP_FLOAT
    b.eq 3f
    cmp wip, #68                  // if (wip == 'D') goto SKIP_DOUBLE
    b.eq 4f
    str \gpr_32, [\regs, \arg_offset]
    cmp wip, #76                  // if (wip != 'L') goto NOT_REFERENCE
    b.ne 6f
    str \gpr_32, [\refs, \arg_offset]
6:  // NOT_REFERENCE
    add \arg_offset, \arg_offset, #4
    b 5f
2:  // FOUND_LONG
    str \gpr_64, [\regs, \arg_offset]
    add \arg_offset, \arg_offset, #8
    b 5f
3:  // SKIP_FLOAT
    add \arg_offset, \arg_offset, #4
    b 1b
4:  // SKIP_DOUBLE
    add \arg_offset, \arg_offset, #8
    b 1b
5:
%   pass
  .endm

  // Puts the next floating point parameter passed in physical register
  // in the expected dex register array entry.
  // Uses ip as temporary.
  .macro LOOP_OVER_SHORTY_STORING_FPS dreg, sreg, shorty, arg_offset, fp, finished
1: // LOOP
    ldrb wip, [\shorty], #1                 // Load next character in shorty, and increment.
    cbz wip, \finished                      // if (wip == '\0') goto finished
    cmp wip, #68                            // if (wip == 'D') goto FOUND_DOUBLE
    b.eq 2f
    cmp wip, #70                            // if (wip == 'F') goto FOUND_FLOAT
    b.eq 3f
    add \arg_offset, \arg_offset, #4
    //  Handle extra argument in arg array taken by a long.
    cmp wip, #74                            // if (wip != 'J') goto LOOP
    b.ne 1b
    add \arg_offset, \arg_offset, #4
    b 1b                        // goto LOOP
2:  // FOUND_DOUBLE
    str \dreg, [\fp, \arg_offset]
    add \arg_offset, \arg_offset, #8
    b 4f
3:  // FOUND_FLOAT
    str \sreg, [\fp, \arg_offset]
    add \arg_offset, \arg_offset, #4
4:
%   pass
  .endm

  // Puts the next floating point parameter passed in stack
  // in the expected dex register array entry.
  // Uses ip as temporary.
  //
  // TODO: Or we could just spill regs to the reserved slots in the caller's
  // frame and copy all regs in a simple loop. This time, however, we would
  // need to look at the shorty anyway to look for the references.
  // (The trade-off is different for passing arguments and receiving them.)
  .macro LOOP_OVER_FPs shorty, arg_offset, regs, stack_ptr, finished
1: // LOOP
    ldrb wip, [\shorty], #1                 // Load next character in shorty, and increment.
    cbz wip, \finished                      // if (wip == '\0') goto finished
    cmp wip, #68                            // if (wip == 'D') goto FOUND_DOUBLE
    b.eq 2f
    cmp wip, #70                            // if (wip == 'F') goto FOUND_FLOAT
    b.eq 3f
    add \arg_offset, \arg_offset, #4
    //  Handle extra argument in arg array taken by a long.
    cmp wip, #74                            // if (wip != 'J') goto LOOP
    b.ne 1b
    add \arg_offset, \arg_offset, #4
    b 1b                        // goto LOOP
2:  // FOUND_DOUBLE
    add ip, \stack_ptr, \arg_offset
    ldr ip, [ip,  #OFFSET_TO_FIRST_ARGUMENT_IN_STACK]
    str ip, [\regs, \arg_offset]
    add \arg_offset, \arg_offset, #8
    b 1b
3:  // FOUND_FLOAT
    add ip, \stack_ptr, \arg_offset
    ldr wip, [ip,  #OFFSET_TO_FIRST_ARGUMENT_IN_STACK]
    str wip, [\regs, \arg_offset]
    add \arg_offset, \arg_offset, #4
    b 1b
  .endm

  // Puts the next int/long/object parameter passed in stack
  // in the expected dex register array entry, and in case of object in the
  // expected reference array entry.
  // Uses ip and ip2 as temporary.
  .macro LOOP_OVER_INTs shorty, arg_offset, regs, refs, stack_ptr, finished
1: // LOOP
    ldrb wip, [\shorty], #1       // Load next character in shorty, and increment.
    cbz wip, \finished            // if (wip == '\0') goto finished
    cmp wip, #74                  // if (wip == 'J') goto FOUND_LONG
    b.eq 2f
    cmp wip, #70                  // if (wip == 'F') goto SKIP_FLOAT
    b.eq 3f
    cmp wip, #68                  // if (wip == 'D') goto SKIP_DOUBLE
    b.eq 4f
    add ip2, \stack_ptr, \arg_offset
    ldr wip2, [ip2,  #OFFSET_TO_FIRST_ARGUMENT_IN_STACK]
    str wip2, [\regs, \arg_offset]
    cmp wip, #76                  // if (wip != 'L') goto loop
    b.ne 3f
    str wip2, [\refs, \arg_offset]
    add \arg_offset, \arg_offset, #4
    b 1b
2:  // FOUND_LONG
    add ip, \stack_ptr, \arg_offset
    ldr ip, [ip,  #OFFSET_TO_FIRST_ARGUMENT_IN_STACK]
    str ip, [\regs, \arg_offset]
    add \arg_offset, \arg_offset, #8
    b 1b
3:  // SKIP_FLOAT
    add \arg_offset, \arg_offset, #4
    b 1b
4:  // SKIP_DOUBLE
    add \arg_offset, \arg_offset, #8
    b 1b
  .endm

  .macro SETUP_REFERENCE_PARAMETER_IN_GPR gpr32, regs, refs, ins, arg_offset, finished
    str \gpr32, [\regs, \arg_offset]
    sub \ins, \ins, #1
    str \gpr32, [\refs, \arg_offset]
    add \arg_offset, \arg_offset, #4
    cbz \ins, \finished
  .endm

  // Uses ip2 as temporary.
  .macro SETUP_REFERENCE_PARAMETERS_IN_STACK regs, refs, ins, stack_ptr, arg_offset
1:
    ldr wip2, [\stack_ptr, \arg_offset]
    sub \ins, \ins, #1
    str wip2, [\regs, \arg_offset]
    str wip2, [\refs, \arg_offset]
    add \arg_offset, \arg_offset, #4
    cbnz \ins, 1b
  .endm

  /*
   * ArtMethod entry point.
   *
   * On entry:
   *  x0   ArtMethod* callee
   *  rest  method parameters
   */

  OAT_ENTRY ExecuteNterpWithClinitImpl
    .cfi_startproc
    // For simplicity, we don't do a read barrier here, but instead rely
    // on art_quick_resolution_trampoline to always have a suspend point before
    // calling back here.
    ldr wip, [x0, #ART_METHOD_DECLARING_CLASS_OFFSET]
    ldr wip2, [ip, #MIRROR_CLASS_STATUS_OFFSET]
    lsr wip2, wip2, #MIRROR_CLASS_STATUS_SHIFT
    cmp wip2, #MIRROR_CLASS_STATUS_VISIBLY_INITIALIZED
    b.hs ExecuteNterpImpl
    cmp wip2, #MIRROR_CLASS_STATUS_INITIALIZED
    b.lo .Linitializing_check
    dmb ish
    b ExecuteNterpImpl
.Linitializing_check:
    cmp wip2, #MIRROR_CLASS_STATUS_INITIALIZING
    b.lo .Lresolution_trampoline
    ldr wip2, [ip, #MIRROR_CLASS_CLINIT_THREAD_ID_OFFSET]
    ldr wip, [xSELF, #THREAD_TID_OFFSET]
    cmp wip, wip2
    b.eq ExecuteNterpImpl
.Lresolution_trampoline:
    b art_quick_resolution_trampoline
    .cfi_endproc
    .type EndExecuteNterpWithClinitImpl, #function
    .hidden EndExecuteNterpWithClinitImpl
    .global EndExecuteNterpWithClinitImpl
EndExecuteNterpWithClinitImpl:
%   pass

  OAT_ENTRY ExecuteNterpImpl
    .cfi_startproc
    sub x16, sp, #STACK_OVERFLOW_RESERVED_BYTES
    ldr wzr, [x16]
    /* Spill callee save regs */
    SPILL_ALL_CALLEE_SAVES

    ldr xPC, [x0, #ART_METHOD_DATA_OFFSET_64]
    // Setup the stack for executing the method. Use xINST as a callee-save temporary.
    // Load access flags to x8 (wARGS is not used here).
    SETUP_STACK_FRAME xPC, xREFS, xFP, CFI_REFS, xINST, regs_ins=w14, access_flags=w8
    lsr w15, w14, #16  // Extract inputs size
    // Update xPC to point to instructions.
    add xPC, xPC, #CODE_ITEM_INSNS_OFFSET

    // Setup the parameters
    cbz w15, .Lxmm_setup_finished

    sub wip2, w14, w15  // inputs start index in low 16 bits (high bits have inputs size)
    ubfiz w11, wip2, #2, #16  // x11 is now the offset for inputs into the registers array.

    tbz w8, #ART_METHOD_NTERP_ENTRY_POINT_FAST_PATH_FLAG_BIT, .Lsetup_slow_path
    // Setup pointer to inputs in FP and pointer to inputs in REFS
    add x10, xFP, x11
    add x11, xREFS, x11
    mov x14, #0
    SETUP_REFERENCE_PARAMETER_IN_GPR w1, x10, x11, w15, x14, .Lxmm_setup_finished
    SETUP_REFERENCE_PARAMETER_IN_GPR w2, x10, x11, w15, x14, .Lxmm_setup_finished
    SETUP_REFERENCE_PARAMETER_IN_GPR w3, x10, x11, w15, x14, .Lxmm_setup_finished
    SETUP_REFERENCE_PARAMETER_IN_GPR w4, x10, x11, w15, x14, .Lxmm_setup_finished
    SETUP_REFERENCE_PARAMETER_IN_GPR w5, x10, x11, w15, x14, .Lxmm_setup_finished
    SETUP_REFERENCE_PARAMETER_IN_GPR w6, x10, x11, w15, x14, .Lxmm_setup_finished
    SETUP_REFERENCE_PARAMETER_IN_GPR w7, x10, x11, w15, x14, .Lxmm_setup_finished
    add xINST, xINST, #OFFSET_TO_FIRST_ARGUMENT_IN_STACK
    SETUP_REFERENCE_PARAMETERS_IN_STACK x10, x11, w15, xINST, x14
    b .Lxmm_setup_finished

.Lsetup_slow_path:
    // If the method is not static and there is one argument ('this'), we don't need to fetch the
    // shorty.
    tbnz w8, #ART_METHOD_IS_STATIC_FLAG_BIT, .Lsetup_with_shorty
    str w1, [xFP, x11]
    str w1, [xREFS, x11]
    cmp w15, #1
    b.eq .Lxmm_setup_finished

.Lsetup_with_shorty:
    ldr w10, [x0, #ART_METHOD_DEX_METHOD_INDEX_OFFSET]
    GET_SHORTY_AND_LENGTH xIBASE, w9, x10, slow_path=.Lsetup_with_shorty_slow_path
.Lsetup_with_shorty_continue:

    // Setup pointer to inputs in FP and pointer to inputs in REFS
    add x10, xFP, x11
    add x11, xREFS, x11
    mov x14, #0

    add x9, xIBASE, #1  // shorty + 1  ; ie skip return arg character
    tbnz w8, #ART_METHOD_IS_STATIC_FLAG_BIT, .Lhandle_static_method
    add x10, x10, #4
    add x11, x11, #4
    add xINST, xINST, #4
    b .Lcontinue_setup_gprs
.Lhandle_static_method:
    LOOP_OVER_SHORTY_STORING_GPRS x1, w1, x9, x14, x10, x11, .Lgpr_setup_finished
.Lcontinue_setup_gprs:
    LOOP_OVER_SHORTY_STORING_GPRS x2, w2, x9, x14, x10, x11, .Lgpr_setup_finished
    LOOP_OVER_SHORTY_STORING_GPRS x3, w3, x9, x14, x10, x11, .Lgpr_setup_finished
    LOOP_OVER_SHORTY_STORING_GPRS x4, w4, x9, x14, x10, x11, .Lgpr_setup_finished
    LOOP_OVER_SHORTY_STORING_GPRS x5, w5, x9, x14, x10, x11, .Lgpr_setup_finished
    LOOP_OVER_SHORTY_STORING_GPRS x6, w6, x9, x14, x10, x11, .Lgpr_setup_finished
    LOOP_OVER_SHORTY_STORING_GPRS x7, w7, x9, x14, x10, x11, .Lgpr_setup_finished
    LOOP_OVER_INTs x9, x14, x10, x11, xINST, .Lgpr_setup_finished
.Lgpr_setup_finished:
    add x9, xIBASE, #1  // shorty + 1  ; ie skip return arg character
    mov x14, #0  // reset counter
    LOOP_OVER_SHORTY_STORING_FPS d0, s0, x9, x14, x10, .Lxmm_setup_finished
    LOOP_OVER_SHORTY_STORING_FPS d1, s1, x9, x14, x10, .Lxmm_setup_finished
    LOOP_OVER_SHORTY_STORING_FPS d2, s2, x9, x14, x10, .Lxmm_setup_finished
    LOOP_OVER_SHORTY_STORING_FPS d3, s3, x9, x14, x10, .Lxmm_setup_finished
    LOOP_OVER_SHORTY_STORING_FPS d4, s4, x9, x14, x10, .Lxmm_setup_finished
    LOOP_OVER_SHORTY_STORING_FPS d5, s5, x9, x14, x10, .Lxmm_setup_finished
    LOOP_OVER_SHORTY_STORING_FPS d6, s6, x9, x14, x10, .Lxmm_setup_finished
    LOOP_OVER_SHORTY_STORING_FPS d7, s7, x9, x14, x10, .Lxmm_setup_finished
    LOOP_OVER_FPs x9, x14, x10, xINST, .Lxmm_setup_finished
.Lxmm_setup_finished:
    CFI_DEFINE_DEX_PC_WITH_OFFSET(CFI_TMP, CFI_DEX, 0)

    // Prepare xIBASE.
    //
    // A full set of opcode handlers has 16KiB. We can quickly construct the handler address by
    // inserting the opcode from the low 8 bits of the 16-bit instruction into bits 13:6 of a
    // register holding a 16KiB-aligned start of the handlers. However, with 4KiB pages,
    // `libart.so` is loaded only on a 4KiB-aligned address. Therefore we emit four sets of nterp
    // handlers, 20KiB apart, and one of them starts at a 16KiB-aligned address. We fill the 4KiB
    // gaps with slow-path code. The bits 12-13 of the last handler set address tell us how many
    // times we need to subtract the 20KiB distance to get the address of the aligned handler set,
    //     nterp3_op_nop - ((nterp3_op_nop >> 12) & 3) * 20KiB .
    // To shorten the initialization sequence, we subtract only 16KiB per handler set as the
    // remaining 4KiB only clears bits 13:12 which shall be replaced by the opcode anyway. Since
    // the low 12 bits are clear, we use UBFIZ to merge extraction and multiplication by 16KiB.
#if NTERP_HANDLER_SIZE_LOG2 != 6
#error "Unexpected handler size."
#endif
    adr xIBASE, nterp3_op_nop
    ubfiz ip, xIBASE, #2, #14  // (nterp3_op_nop >> 12) & 3) * 16KiB
    sub xIBASE, xIBASE, ip

    // Set xCACHE
    add xCACHE, xSELF, #THREAD_INTERPRETER_CACHE_OFFSET
    /* start executing the instruction at xPC */
    START_EXECUTING_INSTRUCTIONS
    /* NOTE: no fallthrough */

.Lsetup_with_shorty_slow_path:
    GET_SHORTY_AND_LENGTH_SLOW_PATH xIBASE, w9, resume=.Lsetup_with_shorty_continue

    // cfi info continues, and covers the whole nterp implementation.
    SIZE ExecuteNterpImpl

%define_execute_nterp()

%def opcode_pre():
%  pass

%def fetch_from_thread_cache(dest_reg, miss_label):
   // Fetch some information from the thread cache.
   // Uses ip and ip2 as temporaries.
   ubfx     ip2, xPC, #THREAD_INTERPRETER_CACHE_KEY_LOW_BIT, #THREAD_INTERPRETER_CACHE_SIZE_LOG2
                                           // entry index
   add      ip, xCACHE, ip2, lsl #4        // entry address within the cache
   ldp      ip, ${dest_reg}, [ip]          // entry key (pc) and value (offset)
   cmp      ip, xPC
   b.ne     ${miss_label}

%def footer():
/*
 * ===========================================================================
 *  Common subroutines and data
 * ===========================================================================
 */

  .text
  .align  2

  // Enclose all code below in a symbol (which gets printed in backtraces).
  NAME_START nterp_helper

// Note: mterp also uses the common_* names below for helpers, but that's OK
// as the assembler compiled each interpreter separately.
common_errDivideByZero:
    EXPORT_PC
    bl art_quick_throw_div_zero

// Expect index in w1, length in w3.
common_errArrayIndex:
    EXPORT_PC
    mov x0, x1
    mov x1, x3
    bl art_quick_throw_array_bounds

common_errNullObject:
    EXPORT_PC
    bl art_quick_throw_null_pointer_exception

NterpCommonInvokeStatic:
%   common_invoke_non_range(is_static=True, suffix="invokeStatic")

NterpCommonInvokeStaticRange:
%   common_invoke_range(is_static=True, suffix="invokeStatic")

NterpCommonInvokeInstance:
%   n2n_entry_point_check("x2", entrypoint="lr", call_compiled_code="1f")
    bl nterp_to_nterp_instance_non_range
    FETCH_ADVANCE_INST 3
    PREPARE_OPCODE_DISPATCH
    GOTO_OPCODE
1:
%   common_invoke_non_range(suffix="invokeInstance")

NterpCommonInvokeInstanceRange:
%   n2n_entry_point_check("x2", entrypoint="lr", call_compiled_code="1f")
    bl nterp_to_nterp_instance_range
    FETCH_ADVANCE_INST 3
    PREPARE_OPCODE_DISPATCH
    GOTO_OPCODE
1:
%   common_invoke_range(suffix="invokeInstance")

NterpCommonInvokePolymorphic:
%   common_invoke_non_range(is_polymorphic=True, suffix="invokePolymorphic")

NterpCommonInvokePolymorphicRange:
%   common_invoke_range(is_polymorphic=True, suffix="invokePolymorphic")

NterpCommonInvokeCustom:
%   common_invoke_non_range(is_static=True, is_custom=True, suffix="invokeCustom")

NterpCommonInvokeCustomRange:
%   common_invoke_range(is_static=True, is_custom=True, suffix="invokeCustom")

NterpHandleStringInit:
    ubfx wCSTMP, wARGS, #0, #4        // Remember `this` vreg number in a callee-save register.
    ubfx wip, wINST, #8, #4           // Extract G
    lsr wARGS, wARGS, #4              // Shift args D-F to C-E for string init.
    sub wINST, wINST, (1 << 12)       // Adjust the number of arguments.
    bfi wARGS, wip, #12, #4           // Insert arg G as F for string init.
%   n2n_entry_point_check("x2", entrypoint="lr", call_compiled_code="1f")
    bl nterp_to_nterp_static_non_range
    b .Ldone_return_stringInit
1:
    // No string-init method has any 'D', 'F' or 'J' args, so they can all use the fast path.
#ifndef NDEBUG
    ldr wip, [x0, #ART_METHOD_ACCESS_FLAGS_OFFSET]
    tbnz wip, #ART_METHOD_NTERP_INVOKE_FAST_PATH_FLAG_BIT, 9f
    brk 0
9:
#endif  // NDEBUG
%   common_invoke_non_range_fast_path_setup(is_static=True, suffix="stringInit")
    blr lr
.Ldone_return_stringInit:
    FETCH_ADVANCE_INST 3
    GET_VREG w1, wCSTMP               // Get `this`.
    UPDATE_REGISTERS_FOR_STRING_INIT w1, w0
    PREPARE_OPCODE_DISPATCH
    GOTO_OPCODE

NterpHandleStringInitRange:
    mov wCSTMP, wARGS                 // Remember `this` vreg number in a callee-save register.
    add wARGS, wARGS, #1              // Adjust the first argument number.
    sub wINST, wINST, (1 << 8)        // Adjust the number of arguments.
%   n2n_entry_point_check("x2", entrypoint="lr", call_compiled_code="1f")
    bl nterp_to_nterp_static_range
    b .Ldone_return_range_stringInit
1:
    // No string-init method has any 'D', 'F' or 'J' args, so they can all use the fast path.
#ifndef NDEBUG
    ldr wip, [x0, #ART_METHOD_ACCESS_FLAGS_OFFSET]
    tbnz wip, #ART_METHOD_NTERP_INVOKE_FAST_PATH_FLAG_BIT, 9f
    brk 0
9:
#endif  // NDEBUG
%   common_invoke_range_fast_path_setup(is_static=True, suffix="stringInit")
    blr lr
.Ldone_return_range_stringInit:
    FETCH_ADVANCE_INST 3
    GET_VREG w1, wCSTMP               // Get `this`.
    UPDATE_REGISTERS_FOR_STRING_INIT w1, w0
    PREPARE_OPCODE_DISPATCH
    GOTO_OPCODE

NterpHotnessCheck:
    ldr x0, [sp]
    ldrh w2, [x0, #ART_METHOD_HOTNESS_COUNT_OFFSET]
#if (NTERP_HOTNESS_VALUE != 0)
#error Expected 0 for hotness value
#endif
    // If the counter is at zero, handle this in the runtime.
    cbz w2, 3f
    add x2, x2, #-1
    strh w2, [x0, #ART_METHOD_HOTNESS_COUNT_OFFSET]
1:
    DO_SUSPEND_CHECK continue_label=2f
2:
    FETCH wINST, 0                      // load wINST
    PREPARE_OPCODE_DISPATCH             // insert opcode to xIBASE
    GOTO_OPCODE                         // jump to next instruction

3:
    CHECK_AND_UPDATE_SHARED_MEMORY_METHOD if_hot=4f, if_not_hot=1b
4:
    mov x1, xPC
    mov x2, xFP
    bl nterp_hot_method
    cbz x0, 2b

    // Drop the current frame.
    ldr ip, [xREFS, #-8]
    mov sp, ip
    .cfi_def_cfa sp, CALLEE_SAVES_SIZE

    // The transition frame of type SaveAllCalleeSaves saves x19 and x20,
    // but not managed ABI. So we need to restore callee-saves of the nterp frame,
    // and save managed ABI callee saves, which will be restored by the callee upon
    // return.
    RESTORE_ALL_CALLEE_SAVES
    INCREASE_FRAME ((CALLEE_SAVES_SIZE) - 16)

    // FP callee-saves
    stp d8, d9, [sp, #0]
    stp d10, d11, [sp, #16]
    stp d12, d13, [sp, #32]
    stp d14, d15, [sp, #48]

    // GP callee-saves.
    SAVE_TWO_REGS x21, x22, 64
    SAVE_TWO_REGS x23, x24, 80
    SAVE_TWO_REGS x25, x26, 96
    SAVE_TWO_REGS x27, x28, 112
    SAVE_TWO_REGS x29, lr, 128

    // Setup the new frame
    ldr x1, [x0, #OSR_DATA_FRAME_SIZE]
    // Given stack size contains all callee saved registers, remove them.
    sub x1, x1, #(CALLEE_SAVES_SIZE - 16)

    // We know x1 cannot be 0, as it at least contains the ArtMethod.

    // Remember CFA in a callee-save register.
    mov xINST, sp
    .cfi_def_cfa_register xINST

    sub sp, sp, x1

    add x2, x0, #OSR_DATA_MEMORY
5:
    sub x1, x1, #8
    ldr ip, [x2, x1]
    str ip, [sp, x1]
    cbnz x1, 5b

    // Fetch the native PC to jump to and save it in a callee-save register.
    ldr xFP, [x0, #OSR_DATA_NATIVE_PC]

    // Free the memory holding OSR Data.
    bl free

    // Jump to the compiled code.
    br xFP

// This is the logical end of ExecuteNterpImpl, where the frame info applies.
// EndExecuteNterpImpl includes the methods below as we want the runtime to
// see them as part of the Nterp PCs.
    .cfi_endproc

nterp_to_nterp_static_non_range:
    .cfi_startproc
    SETUP_STACK_FOR_INVOKE
    SETUP_NON_RANGE_ARGUMENTS_AND_EXECUTE is_static=1
    .cfi_endproc

nterp_to_nterp_instance_non_range:
    .cfi_startproc
    SETUP_STACK_FOR_INVOKE
    SETUP_NON_RANGE_ARGUMENTS_AND_EXECUTE is_static=0
    .cfi_endproc

nterp_to_nterp_static_range:
    .cfi_startproc
    SETUP_STACK_FOR_INVOKE
    SETUP_RANGE_ARGUMENTS_AND_EXECUTE is_static=1
    .cfi_endproc

nterp_to_nterp_instance_range:
    .cfi_startproc
    SETUP_STACK_FOR_INVOKE
    SETUP_RANGE_ARGUMENTS_AND_EXECUTE is_static=0
    .cfi_endproc

  NAME_END nterp_helper

  // This is the end of PCs contained by the OatQuickMethodHeader created for the interpreter
  // entry point.
  .type EndExecuteNterpImpl, #function
  .hidden EndExecuteNterpImpl
  .global EndExecuteNterpImpl
  EndExecuteNterpImpl:

  // Entrypoints into runtime.
  NTERP_TRAMPOLINE nterp_get_static_field, NterpGetStaticField
  NTERP_TRAMPOLINE nterp_get_instance_field_offset, NterpGetInstanceFieldOffset
  NTERP_TRAMPOLINE nterp_filled_new_array, NterpFilledNewArray
  NTERP_TRAMPOLINE nterp_filled_new_array_range, NterpFilledNewArrayRange
  NTERP_TRAMPOLINE nterp_get_class, NterpGetClass
  NTERP_TRAMPOLINE nterp_allocate_object, NterpAllocateObject
  NTERP_TRAMPOLINE nterp_get_method, NterpGetMethod
  NTERP_TRAMPOLINE nterp_hot_method, NterpHotMethod
  NTERP_TRAMPOLINE nterp_load_object, NterpLoadObject

  ENTRY nterp_deliver_pending_exception
    DELIVER_PENDING_EXCEPTION
  END nterp_deliver_pending_exception

  // gen_mterp.py will inline the following definitions
  // within [ExecuteNterpImpl, EndExecuteNterpImpl).

%def instruction_end():
%   prefix_number = int(re.sub(r"nterp([0-3])_", r"\1", opcode_name_prefix))
%   if prefix_number == 3:
      .type artNterpAsmInstructionEnd, #function
      .hidden artNterpAsmInstructionEnd
      .global artNterpAsmInstructionEnd
artNterpAsmInstructionEnd:
      // artNterpAsmInstructionEnd is used as landing pad for exception handling.
      FETCH_INST
      PREPARE_OPCODE_DISPATCH
      GOTO_OPCODE

%def instruction_start():
%   prefix_number = int(re.sub(r"nterp([0-3])_", r"\1", opcode_name_prefix))
%   if prefix_number == 0:
      .type artNterpAsmInstructionStart, #function
      .hidden artNterpAsmInstructionStart
      .global artNterpAsmInstructionStart
      artNterpAsmInstructionStart = nterp0_op_nop
      .text
      .balign 4096
%   else:
%     prev_op_nop = "nterp{}_op_nop".format(int(prefix_number) - 1)
      .org ${prev_op_nop} + 20480

%def add_slow_path_for_last_prefix(write_fn, *write_args, suffix="_slow_path"):
%   if opcode_name_prefix != "nterp3_":
%     return re.sub(r"nterp._", r"nterp3_", opcode) + suffix
%   else:
%     return add_slow_path(write_fn=write_fn, *write_args, suffix=suffix)

%def opcode_name_prefixes():
%   return ["nterp0_", "nterp1_", "nterp2_", "nterp3_"]
%def opcode_start():
  NAME_START ${opcode}
    # Explicitly restore CFA, just in case the previous opcode clobbered it (by .cfi_def_*).
    CFI_DEF_CFA_BREG_PLUS_UCONST CFI_REFS, -8, CALLEE_SAVES_SIZE
%def opcode_end():
  NAME_END ${opcode}
  // Advance to the end of this handler. Causes error if we are past that point.
  .org ${opcode} + NTERP_HANDLER_SIZE  // ${opcode} handler is too big!
%def opcode_slow_path_start(name):
  NAME_START ${name}
%   pass
%def opcode_slow_path_end(name):
  NAME_END ${name}
