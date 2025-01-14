/*
 * Copyright (c) 2012 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Implement the Validator API for the x86-64 architecture. */
#include <errno.h>
#include <string.h>

#include "native_client/src/trusted/validator_ragel/unreviewed/dfa_validate_common.h"
#include "native_client/src/trusted/validator_ragel/unreviewed/validator_internal.h"

/* Be sure the correct compile flags are defined for this. */
#if NACL_ARCH(NACL_TARGET_ARCH) != NACL_x86
# error("Can't compile, target is for x86-64")
#else
# if NACL_TARGET_SUBARCH != 64
#  error("Can't compile, target is for x86-64")
# endif
#endif


static NaClValidationStatus ApplyDfaValidator_x86_64(
    uintptr_t guest_addr,
    uint8_t *data,
    size_t size,
    int stubout_mode,
    int readonly_text,
    const NaClCPUFeaturesX86 *cpu_features,
    struct NaClValidationCache *cache) {
  enum NaClValidationStatus status = NaClValidationFailed;
  UNREFERENCED_PARAMETER(guest_addr);
  UNREFERENCED_PARAMETER(cache);

  if (stubout_mode) {
    return NaClValidationFailedNotImplemented;
  }
  if (!NaClArchSupported(cpu_features))
    return NaClValidationFailedCpuNotSupported;
  if (size & kBundleMask)
    return NaClValidationFailed;
  if (ValidateChunkAMD64(data, size, 0 /*options*/, cpu_features,
                         readonly_text ?
                           ProcessError :
                           StubOutCPUUnsupportedInstruction,
                         &status))
    return NaClValidationSucceeded;
  else if (errno == ENOMEM)
    return NaClValidationFailedOutOfMemory;
  else
    return status;
}


static NaClValidationStatus ValidatorCodeCopy_x86_64(
    uintptr_t guest_addr,
    uint8_t *data_existing,
    uint8_t *data_new,
    size_t size,
    const NaClCPUFeatures *cpu_features,
    NaClCopyInstructionFunc copy_func) {
  struct CodeCopyCallbackData callback_data;
  UNREFERENCED_PARAMETER(guest_addr);

  if (size & kBundleMask) {
    return NaClValidationFailed;
  }
  callback_data.copy_func = copy_func;
  callback_data.delta = data_existing - data_new;
  if (ValidateChunkAMD64(data_new, size, CALL_USER_CALLBACK_ON_EACH_INSTRUCTION,
                         cpu_features, ProcessCodeCopyInstruction,
                         &callback_data))
    return NaClValidationSucceeded;
  else if (errno == ENOMEM)
    return NaClValidationFailedOutOfMemory;
  else
    return NaClValidationFailed;
}


static Bool ProcessCodeReplacementInstruction(const uint8_t *begin_new,
                                              const uint8_t *end_new,
                                              uint32_t info,
                                              void *callback_data) {
  ptrdiff_t delta = (ptrdiff_t)callback_data;
  size_t instruction_length = end_new - begin_new + 1;
  const uint8_t *begin_existing = begin_new + delta;
  const uint8_t *end_existing = end_new + delta;

  /* Sanity check: instruction must be shorter then 15 bytes.  */
  CHECK(instruction_length <= MAX_INSTRUCTION_LENGTH);

  /* Unsupported instruction must have been replaced with HLTs.  */
  if ((info & VALIDATION_ERRORS_MASK) == CPUID_UNSUPPORTED_INSTRUCTION) {
    return CodeReplacementIsStubouted(begin_existing, instruction_length);
  /* If we have jump which jumps out of it's range...  */
  } else if (info & DIRECT_JUMP_OUT_OF_RANGE) {
    /* then everything is fine if it's the only error and jump is unchanged!  */
    if ((info & (VALIDATION_ERRORS_MASK & ~DIRECT_JUMP_OUT_OF_RANGE)) ||
        memcmp(begin_new, begin_existing, instruction_length) != 0)
      return FALSE;
  /* If instruction is not accepted then we have nothing to do here.  */
  } else if (info & (VALIDATION_ERRORS_MASK | BAD_JUMP_TARGET)) {
    return FALSE;
  /* Instruction is untouched: we are done.  */
  } else if (memcmp(begin_new, begin_existing, instruction_length) == 0) {
    return TRUE;
  /* Only some instructions can be modified.  */
  } else if (!(info & MODIFIABLE_INSTRUCTION)) {
    return FALSE;
  /* Instruction with two-bit immediate can only change these two bits.  */
  } else if ((info & IMMEDIATE_2BIT) == IMMEDIATE_2BIT) {
    if (memcmp(begin_new, begin_existing,
                 instruction_length - (info & IMMEDIATES_SIZE_MASK) - 1) != 0 ||
        (*end_new & 0xfc) != (*end_existing & 0xfc)) {
      return FALSE;
    }
  /* Instruction's last byte is not immediate, thus it must be unchanged.  */
  } else if (info & LAST_BYTE_IS_NOT_IMMEDIATE) {
    if (memcmp(begin_new, begin_existing,
                 instruction_length - (info & IMMEDIATES_SIZE_MASK) - 1) != 0 ||
        (*end_new) != (*end_existing)) {
      return FALSE;
    }
  /* Normal instruction can only change an immediate.  */
  } else if (memcmp(begin_new, begin_existing,
                     instruction_length - (info & IMMEDIATES_SIZE_MASK)) != 0) {
    return FALSE;
  }
  return TRUE;
}

static NaClValidationStatus ValidatorCodeReplacement_x86_64(
    uintptr_t guest_addr,
    uint8_t *data_existing,
    uint8_t *data_new,
    size_t size,
    const NaClCPUFeatures *cpu_features) {
  UNREFERENCED_PARAMETER(guest_addr);

  if (size & kBundleMask) {
    return NaClValidationFailed;
  }
  if (ValidateChunkAMD64(data_new, size, CALL_USER_CALLBACK_ON_EACH_INSTRUCTION,
                         cpu_features, ProcessCodeReplacementInstruction,
                         (void *)(data_existing - data_new)))
    return NaClValidationSucceeded;
  else if (errno == ENOMEM)
    return NaClValidationFailedOutOfMemory;
  else
    return NaClValidationFailed;
}

static const struct NaClValidatorInterface validator = {
  ApplyDfaValidator_x86_64,
  ValidatorCodeCopy_x86_64,
  ValidatorCodeReplacement_x86_64,
};

const struct NaClValidatorInterface *NaClDfaValidatorCreate_x86_64(void) {
  return &validator;
}
