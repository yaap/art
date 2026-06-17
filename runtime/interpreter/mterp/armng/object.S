%def op_check_cast():
%  slow_path = add_slow_path_for_last_prefix(op_check_cast_slow_path)
   // Fast-path which gets the class from thread-local cache.
%  fetch_from_thread_cache("r0", miss_label="3f")
1:
#ifdef USE_HEAP_POISONING
   lsrs    r2, rINST, #8               // r2<- A; set flags to use 16-bit encoding
#else  // USE_HEAP_POISONING
   lsr     r2, rINST, #8               // r2<- A
#endif  // USE_HEAP_POISONING
   GET_VREG r3, r2                     // r3<- vA (object)
   cbz     r3, 2f
   ldr     r2, [r3, #MIRROR_OBJECT_CLASS_OFFSET]
   UNPOISON_HEAP_REF r2, s="s"
   // Fast path: do a comparison without read barrier.
   cmp     r0, r2
   bne     ${slow_path}
2:
   FETCH_ADVANCE_INST 2
   PREPARE_OPCODE_DISPATCH
   GOTO_OPCODE
3:
   EXPORT_PC
   mov     r0, rSELF
   ldr     r1, [sp]
   mov     r2, rPC
   bl      nterp_get_class
   b       1b

%def op_check_cast_slow_path():
   ldr     r1, [r0, #MIRROR_CLASS_ACCESS_FLAGS_OFFSET]
   tst     r1, #MIRROR_CLASS_IS_INTERFACE_FLAG
   bne     2f
   ldr     r1, [r0, #MIRROR_CLASS_COMPONENT_TYPE_OFFSET]
   UNPOISON_HEAP_REF r1
   cbnz    r1, 5f
1:
   ldr     r2, [r2, #MIRROR_CLASS_SUPER_CLASS_OFFSET]
   UNPOISON_HEAP_REF r2
   cmp     r0, r2
   beq     6f
   cmp     r2, #0
   bne     1b
2:
   TEST_IF_MARKING 4f
3:
   EXPORT_PC
   mov     r1, r0
   mov     r0, r3
   bl      art_quick_check_instance_of
   b       6f
4:
   bl      art_quick_read_barrier_mark_reg00
   b       3b
5:
   // Class in r0 is an array, r1 is the component type.
   ldr     r2, [r2, #MIRROR_CLASS_COMPONENT_TYPE_OFFSET]
   UNPOISON_HEAP_REF r2
   // Check if object is an array.
   cmp     r2, #0
   beq     2b
   ldr     r4, [r1, #MIRROR_CLASS_SUPER_CLASS_OFFSET]
   UNPOISON_HEAP_REF r4
   cmp     r4, #0
   // If the super class of the component type is not null, go slow path.
   bne     2b
   ldrh    r1, [r1, #MIRROR_CLASS_OBJECT_PRIMITIVE_TYPE_OFFSET]
   // Check if the object is a primitive array.
   ldrh    r2, [r2, #MIRROR_CLASS_OBJECT_PRIMITIVE_TYPE_OFFSET]
   orrs    r2, r1
   // Go slow path for throwing the exception.
   bne     2b
6:
   FETCH_ADVANCE_INST 2
   PREPARE_OPCODE_DISPATCH
   GOTO_OPCODE

%def op_instance_of():
%  slow_path = add_slow_path(op_instance_of_slow_path)
%  check_hierarchy = add_slow_path_for_last_prefix( \
%      op_instance_check_hierarchy, suffix="_check_hierarchy")
   /* instance-of vA, vB, class@CCCC */
   // Fast-path which gets the class from thread-local cache.
%  fetch_from_thread_cache("r1", miss_label=slow_path)
.L${opcode}_resume:
#ifdef USE_HEAP_POISONING
   lsrs    r2, rINST, #12              // r2<- B; set flags to use 16-bit encoding
#else  // USE_HEAP_POISONING
   lsr     r2, rINST, #12              // r2<- B
#endif  // USE_HEAP_POISONING
   GET_VREG r0, r2                     // r0<- vB (object)
   cbz     r0, 1f
   ldr     r2, [r0, #MIRROR_OBJECT_CLASS_OFFSET]
   UNPOISON_HEAP_REF r2, s="s"
   // Fast path: do a comparison without read barrier.
   cmp     r1, r2
   bne     ${check_hierarchy}
   mov     r0, #1
1:
   ubfx    r1, rINST, #(8-2), #(4+2)   // r1<- A << 2; opcode 20 has highest two bits clear
   SET_VREG_PRESCALED r0, r1, s="s"    // set flags to use 16-bit encoding
   FETCH_ADVANCE_INST 2
   PREPARE_OPCODE_DISPATCH
   GOTO_OPCODE

%def op_instance_of_slow_path():
   EXPORT_PC
   mov     r0, rSELF
   ldr     r1, [sp]
   mov     r2, rPC
   bl      nterp_get_class
   mov     r1, r0
   b       .L${opcode}_resume

%def op_instance_check_hierarchy():
   // Go slow path if we are marking. Checking now allows
   // not going to slow path if the super class hierarchy check fails.
   TEST_IF_MARKING 4f
   ldr     r3, [r1, #MIRROR_CLASS_ACCESS_FLAGS_OFFSET]
   tst     r3, #MIRROR_CLASS_IS_INTERFACE_FLAG
   bne     5f
   ldr     r3, [r1, #MIRROR_CLASS_COMPONENT_TYPE_OFFSET]
   UNPOISON_HEAP_REF r3
   cmp     r3, #0
   bne     3f
   // Prepare result value 1 for a match in the loop below.
   mov     r0, #1
1:
   ldr     r2, [r2, #MIRROR_CLASS_SUPER_CLASS_OFFSET]
   UNPOISON_HEAP_REF r2
   cmp     r1, r2
   beq     6f
   cmp     r2, #0
   bne     1b
2:
   mov     r0, #0
   b       6f
3:
   // Class in r1 is an array, r3 is the component type.
   ldr     r2, [r2, #MIRROR_CLASS_COMPONENT_TYPE_OFFSET]
   UNPOISON_HEAP_REF r2
   // Check if object is an array.
   cmp     r2, #0
   beq     2b
   ldr     r4, [r3, #MIRROR_CLASS_SUPER_CLASS_OFFSET]
   UNPOISON_HEAP_REF r4
   cmp     r4, #0
   bne     5f
   ldrh    r3, [r3, #MIRROR_CLASS_OBJECT_PRIMITIVE_TYPE_OFFSET]
   // Check if the object is a primitive array.
   ldrh    r2, [r2, #MIRROR_CLASS_OBJECT_PRIMITIVE_TYPE_OFFSET]
   orr     r0, r2, r3
   clz     r0, r0
   lsrs    r0, r0, #5
   b       6f
4:
   bl      art_quick_read_barrier_mark_reg01
5:
   EXPORT_PC
   bl      artInstanceOfFromCode
6:
   ubfx    r1, rINST, #8, #4           // r1<- A
   SET_VREG r0, r1
   FETCH_ADVANCE_INST 2
   PREPARE_OPCODE_DISPATCH
   GOTO_OPCODE

%def op_iget_boolean():
%  op_iget(load="ldrb")

%def op_iget_byte():
%  op_iget(load="ldrsb")

%def op_iget_char():
%  op_iget(load="ldrh")

%def op_iget_short():
%  op_iget(load="ldrsh")

%def op_iget(load="ldr", wide=False, is_object=False):
%  slow_path = add_slow_path(op_iget_slow_path, load, wide, is_object)
   // Fast-path which gets the field from thread-local cache.
%  fetch_from_thread_cache("r1", miss_label=slow_path, wb=wide or is_object, tmp="r0", tmp2="r2")
.L${opcode}_resume:
%  if wide or is_object:
     lsrs    r2, rINST, #12            // r2<- B; set flags to use 16-bit encoding
%  else:
     lsr     r2, rINST, #12            // r2<- B
   GET_VREG r3, r2                     // r3<- object we're operating on
   ubfx    r2, rINST, #8, #4           // r2<- A
   cmp     r3, #0
   beq     common_errNullObject        // object was null
%  if wide:
     PRESCALE_VREG r2, r2, s="s"       // set flags to use 16-bit encoding
     add     r1, r1, r3
     CLEAR_SHADOW_PAIR_PRESCALED r2, r0, r3, s="s"  // set flags to use 16-bit encoding
     ldm     r1, {r0, r1}              // use 16-bit LDM instead of 32-bit LDRD
     VREG_INDEX_TO_ADDR_PRESCALED r2, r2
     SET_VREG_WIDE_BY_ADDR r0, r1, r2, wb="!"  // fp[A] <- value; write-back to use 16-bit encoding
%  elif is_object:
     $load   r0, [r3, r1]
#ifdef USE_HEAP_POISONING
     UNPOISON_HEAP_REF r0, s="s"
     TEST_IF_MARKING .L${opcode}_read_barrier
#else  // USE_HEAP_POISONING
     mov     r3, rMR                   // MOV+CBZ is 4B (`cmp rMR, #0` + BNE is 6B).
     TEST_IF_MARKING_CBZ r3, .L${opcode}_read_barrier
#endif  // USE_HEAP_POISONING
.L${opcode}_resume_after_read_barrier:
     PRESCALE_VREG r2, r2, s="s"       // set flags to use 16-bit encoding
     SET_VREG_OBJECT_PRESCALED r0, r2  // fp[A] <- value
%  else:
     $load   r0, [r3, r1]
     SET_VREG r0, r2                   // fp[A] <- value
   FETCH_ADVANCE_INST 2
   PREPARE_OPCODE_DISPATCH
   GOTO_OPCODE
%  if is_object:
#ifndef USE_HEAP_POISONING
.L${opcode}_read_barrier:
     bl      art_quick_read_barrier_mark_reg00
     b       .L${opcode}_resume_after_read_barrier
#endif  // !USE_HEAP_POISONING
%    pass

%def op_iget_slow_path(load, wide, is_object):
   ldr     r0, [sp]
   ldr     r1, [r0, #ART_METHOD_ACCESS_FLAGS_OFFSET]
   tst     r1, #ART_METHOD_IS_OBSOLETE_FLAG
   bne     .L${opcode}_slow_path_continue
   // We don't do a read barrier for faster execution.
   // See comment in `NterpGetLocalStaticFieldInternal`.
   ldr     r0, [r0, ART_METHOD_DECLARING_CLASS_OFFSET]
   mov     r1, rPC
   bl      NterpGetLocalInstanceField
   movs    r1, r0
   bpl     .L${opcode}_resume
.L${opcode}_slow_path_continue:
   mov     r0, rSELF
   ldr     r1, [sp]
   mov     r2, rPC
   mov     r3, rFP
   EXPORT_PC
   bl      nterp_get_instance_field_offset
   movs    r1, r0
   bpl     .L${opcode}_resume
   CLEAR_VOLATILE_MARKER r0
   lsr     r2, rINST, #12              // r2<- B
   GET_VREG r3, r2                     // r3<- object we're operating on
   ubfx    r2, rINST, #8, #4           // r2<- A
   cmp     r3, #0
   beq     common_errNullObject            // object was null
%  if wide:
     add     ip, r3, r0
     ATOMIC_LOAD64 ip, r0, r1, r3, .L${opcode}_slow_path_atomic_load
     dmb     ish
     CLEAR_SHADOW_PAIR r2, ip, lr
     VREG_INDEX_TO_ADDR r2, r2
     SET_VREG_WIDE_BY_ADDR r0, r1, r2  // fp[A] <- value
%  else:
     $load   r0, [r3, r0]
     dmb     ish
%    if is_object:
       UNPOISON_HEAP_REF r0
       TEST_IF_MARKING .L${opcode}_read_barrier
       SET_VREG_OBJECT r0, r2          // fp[A] <- value
%    else:
       SET_VREG r0, r2                 // fp[A] <- value
   FETCH_ADVANCE_INST 2
   PREPARE_OPCODE_DISPATCH
   GOTO_OPCODE
%  if is_object:
#ifdef USE_HEAP_POISONING
.L${opcode}_read_barrier:
     bl      art_quick_read_barrier_mark_reg00
     b       .L${opcode}_resume_after_read_barrier
#endif  // USE_HEAP_POISONING
%    pass

%def op_iget_wide():
%  op_iget(load="ldr", wide=True)

%def op_iget_object():
%  op_iget(load="ldr", is_object=True)

%def op_iput_boolean():
%  op_iput(store="strb")

%def op_iput_byte():
%  op_iput(store="strb")

%def op_iput_char():
%  op_iput(store="strh")

%def op_iput_short():
%  op_iput(store="strh")

%def op_iput(store="str", wide=False, is_object=False):
%  slow_path = add_slow_path(op_iput_slow_path, store, wide, is_object)
%  if not wide:
     ubfx    r4, rINST, #8, #4         // r4<- A
     GET_VREG r4, r4                   // r4 <- v[A]
%  pass
   // Fast-path which gets the field from thread-local cache.
#ifdef USE_HEAP_POISONING
%  fetch_from_thread_cache("r1", miss_label=slow_path, wb=is_object, tmp="r0", tmp2="r2")
#else  // USE_HEAP_POISONING
%  fetch_from_thread_cache("r1", miss_label=slow_path)
#endif  // USE_HEAP_POISONING
.L${opcode}_resume:
%  if is_object:
     lsrs    r0, rINST, #12            // r0<- B; set flags to use 16-bit encoding
%  else:
     lsr     r0, rINST, #12            // r0<- B
   GET_VREG r0, r0                     // r0<- vB (object we're operating on)
   cmp     r0, #0
   beq     common_errNullObject
%  if wide:
     ubfx    r4, rINST, #8, #4         // r4<- A
     VREG_INDEX_TO_ADDR r4, r4
     GET_VREG_WIDE_BY_ADDR r2, r3, r4  // fp[A] <- value
     add     r0, r0, r1
     strd    r2, r3, [r0]
%  else:
     POISON_HEAP_REF_IF_OBJECT $is_object, r4, s="s"
     $store  r4, [r0, r1]
     // Use low register temps and set flags to use 16-bit encodings.
     WRITE_BARRIER_IF_OBJECT $is_object, r4, r0, .L${opcode}_skip_write_barrier, r1, r2, s="s"
   FETCH_ADVANCE_INST 2
   PREPARE_OPCODE_DISPATCH
   GOTO_OPCODE

%def op_iput_slow_path(store, wide, is_object):
   ldr     r0, [sp]
   ldr     r1, [r0, #ART_METHOD_ACCESS_FLAGS_OFFSET]
   tst     r1, #ART_METHOD_IS_OBSOLETE_FLAG
   bne     .L${opcode}_slow_path_continue
   // We don't do a read barrier for faster execution.
   // See comment in `NterpGetLocalStaticFieldInternal`.
   ldr     r0, [r0, ART_METHOD_DECLARING_CLASS_OFFSET]
   mov     r1, rPC
%  if is_object:
     bl      NterpGetLocalInstanceFieldForIPutObject
%  else:
     bl      NterpGetLocalInstanceField
   movs    r1, r0
   bpl     .L${opcode}_resume
.L${opcode}_slow_path_continue:
   mov     r0, rSELF
   ldr     r1, [sp]
   mov     r2, rPC
   mov     r3, rFP
   EXPORT_PC
   bl      nterp_get_instance_field_offset
%  if is_object:
     // Reload the value as it may have moved.
     ubfx    r4, rINST, #8, #4         // r4<- A
     GET_VREG r4, r4                   // r4 <- v[A]
   movs    r1, r0
   bpl     .L${opcode}_resume
   CLEAR_VOLATILE_MARKER r0
%  if wide:
     lsr     r4, rINST, #12              // r4<- B
     ubfx    r1, rINST, #8, #4           // r1<- A
     GET_VREG r4, r4                     // vB (object we're operating on)
     cmp     r4, #0
     beq     common_errNullObject
     VREG_INDEX_TO_ADDR r1, r1
     GET_VREG_WIDE_BY_ADDR r2, r3, r1
     add     ip, r4, r0
     dmb     ish
     ATOMIC_STORE64 ip, r2, r3, r0, r1, .L${opcode}_slow_path_atomic_store
     dmb     ish
%  else:
     lsr     r1, rINST, #12              // r4<- B
     GET_VREG r1, r1                     // vB (object we're operating on)
     cmp     r1, #0
     beq     common_errNullObject
     dmb     ish
     POISON_HEAP_REF_IF_OBJECT $is_object, r4
     $store  r4, [r1, r0]
     dmb     ish
     WRITE_BARRIER_IF_OBJECT $is_object, r4, r1, .L${opcode}_slow_path_skip_write_barrier, r0
   FETCH_ADVANCE_INST 2
   PREPARE_OPCODE_DISPATCH
   GOTO_OPCODE

%def op_iput_wide():
%  op_iput(store="str", wide=True)

%def op_iput_object():
%  op_iput(store="str", is_object=True)

%def op_sget_boolean():
%  op_sget(load="ldrb")

%def op_sget_byte():
%  op_sget(load="ldrsb")

%def op_sget_char():
%  op_sget(load="ldrh")

%def op_sget_short():
%  op_sget(load="ldrsh")

%def op_sget(load="ldr", wide=False, is_object=False):
%  slow_path = add_slow_path(op_sget_slow_path, load, wide, is_object)
   // Fast-path which gets the field from thread-local cache.
%  fetch_uint64_from_thread_cache(miss_label=slow_path)
.L${opcode}_resume:
   lsrs    r2, rINST, #8               // r2 <- A; set flags to use 16-bit encoding
%  if wide:
     movs    r4, #0                    // r3 <- 0; set flags to use 16-bit encoding
     TEST_IF_MARKING_RZERO r4, .L${opcode}_read_barrier
%  else:
     TEST_IF_MARKING .L${opcode}_read_barrier
%  pass
.L${opcode}_resume_after_read_barrier:
%  if wide:
     add     r0, r3, r1
     PRESCALE_VREG r2, r2, s="s"       // r2 <- A << 2; set flags to use 16-bit encoding
     ldrd    r0, r1, [r0]
     CLEAR_SHADOW_PAIR_PRESCALED_RZERO r2, r4, r3, s="s"  // set flags to use 16-bit encoding
     VREG_INDEX_TO_ADDR_PRESCALED r2, r2
     SET_VREG_WIDE_BY_ADDR r0, r1, r2, wb="!"  // fp[A] <- value; write-back to use 16-bit encoding
%  elif is_object:
     $load   r0, [r3, r1]
     UNPOISON_HEAP_REF r0
     // No need to check the marking register, we know it's not set here.
.L${opcode}_after_reference_load:
     SET_VREG_OBJECT r0, r2            // fp[A] <- value
%  else:
     $load   r0, [r3, r1]
     SET_VREG r0, r2                   // fp[A] <- value
   FETCH_ADVANCE_INST 2
   PREPARE_OPCODE_DISPATCH
   GOTO_OPCODE
%  if not (is_object or wide):
.L${opcode}_read_barrier:
     bl      art_quick_read_barrier_mark_reg03
     b       .L${opcode}_resume_after_read_barrier

%def op_sget_slow_path(load, wide, is_object):
   ldr     r0, [sp]
   ldr     r1, [r0, #ART_METHOD_ACCESS_FLAGS_OFFSET]
   tst     r1, #ART_METHOD_IS_OBSOLETE_FLAG
   bne     .L${opcode}_slow_path_continue
   // We don't do a read barrier for faster execution.
   // See comment in `NterpGetLocalStaticFieldInternal`.
   ldr     r0, [r0, ART_METHOD_DECLARING_CLASS_OFFSET]
   mov     r1, rPC
   bl      NterpGetLocalStaticField
   mov     r3, r1
   movs    r1, r0
   bne     .L${opcode}_resume
.L${opcode}_slow_path_continue:
   mov     r0, rSELF
   ldr     r1, [sp]
   mov     r2, rPC
   mov     r3, #0
   EXPORT_PC
   bl      nterp_get_static_field
   mov     r3, r1
   movs    r1, r0
   bpl     .L${opcode}_resume
   CLEAR_VOLATILE_MARKER r1
   lsr     r2, rINST, #8               // r2 <- A
   TEST_IF_MARKING .L${opcode}_slow_path_read_barrier
.L${opcode}_slow_path_resume_after_read_barrier:
%  if wide:
     add     ip, r3, r1
     ATOMIC_LOAD64 ip, r0, r1, r3, .L${opcode}_slow_path_atomic_load
     dmb     ish
     CLEAR_SHADOW_PAIR r2, ip, lr
     VREG_INDEX_TO_ADDR r2, r2
     SET_VREG_WIDE_BY_ADDR r0, r1, r2    // fp[A] <- value
%  else:
     $load   r0, [r3, r1]
     dmb     ish
%    if is_object:
       UNPOISON_HEAP_REF r0
       TEST_IF_MARKING .L${opcode}_mark_after_load
       SET_VREG_OBJECT r0, r2            // fp[A] <- value
%    else:
       SET_VREG r0, r2                   // fp[A] <- value
   FETCH_ADVANCE_INST 2
   PREPARE_OPCODE_DISPATCH
   GOTO_OPCODE
.L${opcode}_slow_path_read_barrier:
   bl      art_quick_read_barrier_mark_reg03
   b       .L${opcode}_slow_path_resume_after_read_barrier
%  if is_object or wide:
.L${opcode}_read_barrier:
     bl      art_quick_read_barrier_mark_reg03
%    if is_object:
       ldr     r0, [r1, r3]
       UNPOISON_HEAP_REF r0
.L${opcode}_mark_after_load:
       // Here, we know the marking register is set.
       bl      art_quick_read_barrier_mark_reg00
       b       .L${opcode}_after_reference_load
%    else:
       b       .L${opcode}_resume_after_read_barrier

%def op_sget_wide():
%  op_sget(load="ldr", wide=True)

%def op_sget_object():
%  op_sget(load="ldr", is_object=True)

%def op_sput_boolean():
%  op_sput(store="strb")

%def op_sput_byte():
%  op_sput(store="strb")

%def op_sput_char():
%  op_sput(store="strh")

%def op_sput_short():
%  op_sput(store="strh")

%def op_sput(store="str", wide=False, is_object=False):
%  slow_path = add_slow_path(op_sput_slow_path, store, wide, is_object)
%  if not wide:
     lsr     r4, rINST, #8             // r4 <- A
     GET_VREG r4, r4                   // r4 <- v[A]
   // Fast-path which gets the field from thread-local cache.
%  fetch_uint64_from_thread_cache(miss_label=slow_path)
.L${opcode}_resume:
   TEST_IF_MARKING .L${opcode}_read_barrier
.L${opcode}_resume_after_read_barrier:
%  if wide:
     lsr     r2, rINST, #8             // r2 <- A
     VREG_INDEX_TO_ADDR r2, r2
     add     r0, r1, r3
     GET_VREG_WIDE_BY_ADDR r1, r2, r2  // fp[A] <- value
     strd    r1, r2, [r0]
%  else:
     POISON_HEAP_REF_IF_OBJECT $is_object, r4, s="s"
     $store  r4, [r1, r3]
     // Use low register temps and set flags to use 16-bit encodings.
     WRITE_BARRIER_IF_OBJECT $is_object, r4, r3, .L${opcode}_skip_write_barrier, r1, r2, s="s"
   FETCH_ADVANCE_INST 2
   PREPARE_OPCODE_DISPATCH
   GOTO_OPCODE

%def op_sput_slow_path(store, wide, is_object):
   ldr     r0, [sp]
   ldr     r1, [r0, #ART_METHOD_ACCESS_FLAGS_OFFSET]
   tst     r1, #ART_METHOD_IS_OBSOLETE_FLAG
   bne     .L${opcode}_slow_path_continue
   // We don't do a read barrier for faster execution.
   // See comment in `NterpGetLocalStaticFieldInternal`.
   ldr     r0, [r0, ART_METHOD_DECLARING_CLASS_OFFSET]
   mov     r1, rPC
%  if is_object:
     bl      NterpGetLocalStaticFieldForSPutObject
%  else:
     bl      NterpGetLocalStaticField
   mov     r3, r1
   movs    r1, r0
   bne     .L${opcode}_resume
.L${opcode}_slow_path_continue:
   mov     r0, rSELF
   ldr     r1, [sp]
   mov     r2, rPC
%  if is_object:
     mov     r3, r4
%  else:
     mov     r3, #0
   EXPORT_PC
   bl      nterp_get_static_field
%  if is_object:
     // Reload the value as it may have moved.
     lsr     r4, rINST, #8             // r4 <- A
     GET_VREG r4, r4                   // r4 <- v[A]
   mov     r3, r1
   movs    r1, r0
   bpl     .L${opcode}_resume
   CLEAR_VOLATILE_MARKER r1
   TEST_IF_MARKING .L${opcode}_slow_path_read_barrier
.L${opcode}_slow_path_resume_after_read_barrier:
%  if wide:
     lsr     r2, rINST, #8             // r2 <- A
     VREG_INDEX_TO_ADDR r2, r2
     add     ip, r1, r3
     GET_VREG_WIDE_BY_ADDR r1, r2, r2
     dmb     ish
     ATOMIC_STORE64 ip, r1, r2, r0, r3, .L${opcode}_slow_path_atomic_store
     dmb     ish
%  else:
     dmb     ish
     POISON_HEAP_REF_IF_OBJECT $is_object r4
     $store  r4, [r1, r3]
     dmb     ish
     WRITE_BARRIER_IF_OBJECT $is_object, r4, r3, .L${opcode}_slow_path_skip_write_barrier, r1
   FETCH_ADVANCE_INST 2
   PREPARE_OPCODE_DISPATCH
   GOTO_OPCODE
.L${opcode}_slow_path_read_barrier:
   bl      art_quick_read_barrier_mark_reg03
   b       .L${opcode}_slow_path_resume_after_read_barrier
.L${opcode}_read_barrier:
   bl      art_quick_read_barrier_mark_reg03
   b       .L${opcode}_resume_after_read_barrier

%def op_sput_wide():
%  op_sput(store="str", wide=True)

%def op_sput_object():
%  op_sput(store="str", is_object=True)

%def op_new_instance():
%  slow_path = add_slow_path(op_new_instance_slow_path)
   EXPORT_PC
   // Fast-path which gets the class from thread-local cache.
%  fetch_from_thread_cache("r0", miss_label=slow_path)
   TEST_IF_MARKING 2f
1:
   ldr     lr, [rSELF, #THREAD_ALLOC_OBJECT_ENTRYPOINT_OFFSET]
   blx     lr
   dmb     ishst                        // need fence for making object's class visible
.L${opcode}_resume:
   lsrs    r1, rINST, #(8-2)            // r1 <- A << 2; opcode 22 has highest two bits clear;
                                        // set flags to use 16-bit encoding
   // `new-array` handler jumps here to shorten the code.
.L${opcode}_store_r0_to_prescaled_r1_fetch_advance_2_dispatch:
   SET_VREG_OBJECT_PRESCALED r0, r1     // fp[A] <- value
   FETCH_ADVANCE_INST 2
   PREPARE_OPCODE_DISPATCH
   GOTO_OPCODE
2:
   bl      art_quick_read_barrier_mark_reg00
   b       1b

%def op_new_instance_slow_path():
   mov     r0, rSELF
   ldr     r1, [sp]
   mov     r2, rPC
   bl      nterp_allocate_object
   b       .L${opcode}_resume
