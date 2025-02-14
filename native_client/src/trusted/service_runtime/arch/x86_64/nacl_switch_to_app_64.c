/*
 * Copyright (c) 2012 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * NaCl Service Runtime, C-level context switch code.
 */

#include "native_client/src/trusted/service_runtime/sel_ldr.h"
#include "native_client/src/trusted/service_runtime/arch/x86/sel_rt.h"
#include "native_client/src/trusted/service_runtime/nacl_app_thread.h"
#include "native_client/src/trusted/service_runtime/nacl_globals.h"
#include "native_client/src/trusted/service_runtime/nacl_switch_to_app.h"
#include "native_client/src/trusted/validator/x86/nacl_cpuid.h"

#if NACL_WINDOWS
# define NORETURN_PTR
#else
# define NORETURN_PTR NORETURN
#endif

static NORETURN_PTR void (*NaClSwitch)(struct NaClThreadContext *context);

void NaClInitSwitchToApp(struct NaClApp *nap) {
  NaClCPUFeaturesX86 cpu_features;

  UNREFERENCED_PARAMETER(nap);

  /*
   * TODO(mcgrathr): This call is repeated in platform qualification and
   * in every application of the validator.  It would be more efficient
   * to do it once and then reuse the same data.
   */
  NaClGetCurrentCPUFeatures(&cpu_features);
  if (NaClGetCPUFeature(&cpu_features, NaClCPUFeature_AVX)) {
    NaClSwitch = NaClSwitchAVX;
  } else {
    NaClSwitch = NaClSwitchSSE;
  }
}

NORETURN void NaClStartThreadInApp(struct NaClAppThread *natp,
                                   nacl_reg_t           new_prog_ctr) {
  struct NaClApp            *nap;
  struct NaClThreadContext  *context;

#if !NACL_WINDOWS
  /*
   * Ensure stack alignment.  Stack pointer must be -8 mod 16 when no
   * __m256 objects are passed (8 mod 32 if __m256), after the call.
   * Note the current doc (as of 2009-12-09) at
   *
   *   http://www.x86-64.org/documentation/abi.pdf
   *
   * is wrong since it claims (%rsp-8) should be 0 mod 16 or mod 32
   * after the call, and it should be (%rsp+8) == 0 mod 16 or 32.
   * Clearly it makes no difference since -8 and 8 are the same mod
   * 16, but there is a difference when mod 32.
   *
   * This is not suitable for Windows because we do not reserve 32
   * bytes for the shadow space.
   */
  nacl_reg_t  secure_stack_ptr = NaClGetStackPtr();

  NaClLog(6,
          "NaClStartThreadInApp: secure stack:   0x%"NACL_PRIxNACL_REG"\n",
          secure_stack_ptr);
  secure_stack_ptr = secure_stack_ptr & ~0x1f;
  NaClLog(6,
          "NaClStartThreadInApp: adjusted stack: 0x%"NACL_PRIxNACL_REG"\n",
          secure_stack_ptr);

  natp->user.trusted_stack_ptr = secure_stack_ptr;
#endif

  nap = natp->nap;
  context = &natp->user;
  context->new_prog_ctr = new_prog_ctr;
  context->sysret = 0;
  context->r15 = nap->mem_start;

  NaClLog(6,
          "NaClStackThreadInApp: user stack: 0x%"NACL_PRIxPTR"\n",
          NaClGetThreadCtxSp(context));
  NaClLog(6,
          "NaClStartThreadInApp: switching to untrusted code\n");

#if NACL_WINDOWS
  /* This sets up a stack containing a return address that has unwind info. */
  NaClSwitchSavingStackPtr(context, &context->trusted_stack_ptr, NaClSwitch);
#else
  NaClSwitch(context);
#endif
}

/*
 * Not really different that NaClStartThreadInApp, since when we start
 * a thread in x86_64 we do not need to save any extra state (e.g.,
 * segment registers) as in the x86_32 case.  We do not, however, save
 * the stack pointer, since o/w we will slowly exhaust the trusted
 * stack.
 */

NORETURN void NaClSwitchToApp(struct NaClAppThread *natp) {
  NaClSwitch(&natp->user);
}
