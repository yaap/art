%def op_invoke_custom():
   EXPORT_PC
   FETCH w0, 1 // call_site index, first argument of runtime call.
   b NterpCommonInvokeCustom

%def op_invoke_custom_range():
   EXPORT_PC
   FETCH w0, 1 // call_site index, first argument of runtime call.
   b NterpCommonInvokeCustomRange

%def get_receiver(dest, args, range):
%  if range:
     GET_VREG ${dest}, ${args}
%  else:
     and ${dest}, ${args}, #0xf
     GET_VREG ${dest}, ${dest}

%def invoke_direct_or_super(helper="", range=False, is_super=False):
   // The slow path fits in the handler for `invoke-super/range`.
%  slow_path = '1f' if is_super and range else add_slow_path( \
%      invoke_direct_or_super_slow_path, range, is_super)
   EXPORT_PC
   // Fast-path which gets the method from thread-local cache.
%  fetch_from_thread_cache("x0", miss_label=slow_path)
.L${opcode}_resume:
   // Load the C-F arguments to wARGS (w8) and get the receiver to x1 (the 'this' pointer).
   FETCH wARGS, 2
%  get_receiver("w1", "wARGS", range)
   cbz w1, common_errNullObject    // bail if null
   b $helper
%  if is_super and range:
1:
%    invoke_direct_or_super_slow_path(range, is_super)

%def invoke_direct_or_super_slow_path(range, is_super):
   mov x0, xSELF
   ldr x1, [sp]
   mov x2, xPC
   mov x3, xFP
   bl nterp_get_method
%  if is_super:
     b .L${opcode}_resume
%  else:
     tbz x0, #0, .L${opcode}_resume
     FETCH wARGS, 2
     and x0, x0, #-2 // Remove the extra bit that marks it's a String.<init> method.
%    if range:
       b NterpHandleStringInitRange
%    else:
       b NterpHandleStringInit

%def op_invoke_direct():
%  invoke_direct_or_super(helper="NterpCommonInvokeInstance")

%def op_invoke_direct_range():
%  invoke_direct_or_super(helper="NterpCommonInvokeInstanceRange", range=True)

%def op_invoke_super():
%  invoke_direct_or_super(helper="NterpCommonInvokeInstance", is_super=True)

%def op_invoke_super_range():
%  invoke_direct_or_super(helper="NterpCommonInvokeInstanceRange", range=True, is_super=True)

%def op_invoke_polymorphic(range=False, helper="NterpCommonInvokePolymorphic"):
   EXPORT_PC
   // No need to fetch the target method.
   // Load the C-F arguments to wARGS (w8) and get the receiver to x1 (the 'this' pointer).
   FETCH wARGS, 2
%  get_receiver("w1", "wARGS", range)
   cbz w1, common_errNullObject    // bail if null
   b ${helper}

%def op_invoke_polymorphic_range():
%  op_invoke_polymorphic(range=True, helper="NterpCommonInvokePolymorphicRange")

%def invoke_interface(range=False):
%  slow_path = add_slow_path(invoke_interface_slow_path, range)
   EXPORT_PC
   // Fast-path which gets the method from thread-local cache.
%  fetch_from_thread_cache("xHIDDEN_ARG", miss_label=slow_path)
.L${opcode}_resume:
   // Load the C-F arguments to wARGS (w8) and get the receiver to x1 (the 'this' pointer).
   FETCH wARGS, 2
%  get_receiver("w1", "wARGS", range)
   // Note: if w1 is null, this will be handled by our SIGSEGV handler.
   ldr w2, [x1, #MIRROR_OBJECT_CLASS_OFFSET]
   UNPOISON_HEAP_REF w2
   // Test the first two bits of the fetched ArtMethod:
   // - If the first bit is set, this is a method on j.l.Object
   // - If the second bit is set, this is a default method.
   tst xHIDDEN_ARG, #0x3
   b.ne .L${opcode}_slow_path_edge_cases
   ldrh w3, [xHIDDEN_ARG, #ART_METHOD_IMT_INDEX_OFFSET]
.L${opcode}_resume_with_imt_index:
   ldr x2, [x2, #MIRROR_CLASS_IMT_PTR_OFFSET_64]
%  if range:
     ldr x0, [x2, w3, uxtw #3]
     b NterpCommonInvokeInstanceRange
%  else:
#ifdef USE_HEAP_POISONING
     // The unpoisoning above means we need to move the LDR to the
     // slow path to keep the handler size 64B.
     b .L${opcode}_slow_path_load_method_for_heap_poisoning
#else
     ldr x0, [x2, w3, uxtw #3]
     b NterpCommonInvokeInstance
#endif
%    pass

%def invoke_interface_slow_path(range):
   mov x0, xSELF
   ldr x1, [sp]
   mov x2, xPC
   mov x3, xFP
   bl nterp_get_method
   mov xHIDDEN_ARG, x0
   b .L${opcode}_resume
.L${opcode}_slow_path_edge_cases:
   tbnz xHIDDEN_ARG, #0, 2f
   and xHIDDEN_ARG, xHIDDEN_ARG, #-4
   ldrh w3, [xHIDDEN_ARG, #ART_METHOD_METHOD_INDEX_OFFSET]
   and w3, w3, #ART_METHOD_IMT_MASK
   b .L${opcode}_resume_with_imt_index
2:
   // The method resolved to a non-interface method in j.l.Object.
   // Perform virtual call with the vtable index in bits 31:16 of xHIDDEN_ARG.
   lsr ip, xHIDDEN_ARG, #16
   add w2, w2, #MIRROR_CLASS_VTABLE_OFFSET_64
   ldr x0, [x2, ip, lsl #3]
%  if range:
     b NterpCommonInvokeInstanceRange
%  else:
     b NterpCommonInvokeInstance
%  pass
#ifdef USE_HEAP_POISONING
.L${opcode}_slow_path_load_method_for_heap_poisoning:
   ldr x0, [x2, w3, uxtw #3]
   b NterpCommonInvokeInstance
#endif
%  pass

%def op_invoke_interface():
%  invoke_interface()

%def op_invoke_interface_range():
%  invoke_interface(range=True)

%def invoke_static(range):
%  slow_path = add_slow_path(invoke_static_slow_path)
   EXPORT_PC
   // Fast-path which gets the method from thread-local cache.
%  fetch_from_thread_cache("x0", miss_label=slow_path)
.L${opcode}_resume:
   FETCH wARGS, 2
%  helper = r"NterpCommonInvokeStatic" + (r"Range" if range else r"")
%  n2n_entry_point_check("x2", entrypoint="lr", call_compiled_code=helper)
%  nterp_to_nterp = r"nterp_to_nterp_static_" + (r"range" if range else r"non_range")
   bl ${nterp_to_nterp}
   FETCH_ADVANCE_INST 3
   PREPARE_OPCODE_DISPATCH
   GOTO_OPCODE

%def invoke_static_slow_path():
   mov x0, xSELF
   ldr x1, [sp]
   mov x2, xPC
   mov x3, xFP
   bl nterp_get_method
   b .L${opcode}_resume

%def op_invoke_static():
%  invoke_static(range=False)

%def op_invoke_static_range():
%  invoke_static(range=True)

%def invoke_virtual(helper="", range=False):
%  slow_path = add_slow_path(invoke_virtual_slow_path)
   EXPORT_PC
   // Fast-path which gets the method from thread-local cache.
%  fetch_from_thread_cache("x2", miss_label=slow_path)
.L${opcode}_resume:
   // Load the C-F arguments to wARGS (w8) and get the receiver to x1 (the 'this' pointer).
   FETCH wARGS, 2
%  get_receiver("w1", "wARGS", range)
   // Note: if w1 is null, this will be handled by our SIGSEGV handler.
   ldr w0, [x1, #MIRROR_OBJECT_CLASS_OFFSET]
   UNPOISON_HEAP_REF w0
   add w0, w0, #MIRROR_CLASS_VTABLE_OFFSET_64
   ldr x0, [x0, w2, uxtw #3]
   b $helper

%def invoke_virtual_slow_path():
   mov x0, xSELF
   ldr x1, [sp]
   mov x2, xPC
   mov x3, xFP
   bl nterp_get_method
   mov x2, x0
   b .L${opcode}_resume

%def op_invoke_virtual():
%  invoke_virtual(helper="NterpCommonInvokeInstance")

%def op_invoke_virtual_range():
%  invoke_virtual(helper="NterpCommonInvokeInstanceRange", range=True)
