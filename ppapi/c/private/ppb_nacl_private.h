/* Copyright (c) 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* From private/ppb_nacl_private.idl modified Fri Nov 16 05:20:17 2012. */

#ifndef PPAPI_C_PRIVATE_PPB_NACL_PRIVATE_H_
#define PPAPI_C_PRIVATE_PPB_NACL_PRIVATE_H_

#include "ppapi/c/pp_bool.h"
#include "ppapi/c/pp_instance.h"
#include "ppapi/c/pp_macros.h"
#include "ppapi/c/pp_stdint.h"

#define PPB_NACL_PRIVATE_INTERFACE_1_0 "PPB_NaCl_Private;1.0"
#define PPB_NACL_PRIVATE_INTERFACE PPB_NACL_PRIVATE_INTERFACE_1_0

/**
 * @file
 * This file contains NaCl private interfaces. */


#include "ppapi/c/private/pp_file_handle.h"

/**
 * @addtogroup Enums
 * @{
 */
/**
 * The <code>PP_NaClResult</code> enum contains NaCl result codes.
 */
typedef enum {
  /** Successful NaCl call */
  PP_NACL_OK = 0,
  /** Unspecified NaCl error */
  PP_NACL_FAILED = 1,
  /** Error creating the module */
  PP_NACL_ERROR_MODULE = 2,
  /** Error creating and initializing the instance */
  PP_NACL_ERROR_INSTANCE = 3,
  /** SRPC proxy should be used instead */
  PP_NACL_USE_SRPC = 128
} PP_NaClResult;
PP_COMPILE_ASSERT_SIZE_IN_BYTES(PP_NaClResult, 4);
/**
 * @}
 */

/**
 * @addtogroup Interfaces
 * @{
 */
/* PPB_NaCl_Private */
struct PPB_NaCl_Private_1_0 {
  /* Launches NaCl's sel_ldr process.  Returns PP_NACL_OK on success and writes
   * |socket_count| nacl::Handles to imc_handles. Returns PP_NACL_FAILED on
   * failure. The |enable_ppapi_dev| parameter controls whether GetInterface
   * returns 'Dev' interfaces to the NaCl plugin.
   */
  PP_NaClResult (*LaunchSelLdr)(PP_Instance instance,
                                const char* alleged_url,
                                PP_Bool enable_ppapi_dev,
                                int32_t socket_count,
                                void* imc_handles);
  /* This function starts the IPC proxy so the nexe can communicate with the
   * browser. Returns PP_NACL_OK on success, otherwise a result code indicating
   * the failure. PP_NACL_FAILED is returned if LaunchSelLdr wasn't called with
   * the instance. PP_NACL_ERROR_MODULE is returned if the module can't be
   * initialized. PP_NACL_ERROR_INSTANCE is returned if the instance can't be
   * initialized. PP_NACL_USE_SRPC is returned if the plugin should use SRPC.
   */
  PP_NaClResult (*StartPpapiProxy)(PP_Instance instance);
  /* On POSIX systems, this function returns the file descriptor of
   * /dev/urandom.  On non-POSIX systems, this function returns 0.
   */
  int32_t (*UrandomFD)();
  /* Whether the Pepper 3D interfaces should be disabled in the NaCl PPAPI
   * proxy. This is so paranoid admins can effectively prevent untrusted shader
   * code to be processed by the graphics stack.
   */
  PP_Bool (*Are3DInterfacesDisabled)();
  /* Enables the creation of sel_ldr processes off of the main thread.
   */
  void (*EnableBackgroundSelLdrLaunch)();
  /* This is Windows-specific.  This is a replacement for DuplicateHandle() for
   * use inside the Windows sandbox.  Note that we provide this via dependency
   * injection only to avoid the linkage problems that occur because the NaCl
   * plugin is built as a separate DLL/DSO
   * (see http://code.google.com/p/chromium/issues/detail?id=114439#c8).
   */
  int32_t (*BrokerDuplicateHandle)(PP_FileHandle source_handle,
                                   uint32_t process_id,
                                   PP_FileHandle* target_handle,
                                   uint32_t desired_access,
                                   uint32_t options);
  /* Returns a read-only file descriptor of a file rooted in the Pnacl
   * component directory, or -1 on error.
   * Do we want this to take a completion callback and be async, or
   * could we make this happen on another thread?
   */
  PP_FileHandle (*GetReadonlyPnaclFd)(const char* filename);
  /* This creates a temporary file that will be deleted by the time
   * the last handle is closed (or earlier on POSIX systems), and
   * returns a posix handle to that temporary file.
   */
  PP_FileHandle (*CreateTemporaryFile)(PP_Instance instance);
  /* Return true if we are off the record.
   */
  PP_Bool (*IsOffTheRecord)();
  /* Return true if PNaCl is turned on.
   */
  PP_Bool (*IsPnaclEnabled)();
};

typedef struct PPB_NaCl_Private_1_0 PPB_NaCl_Private;
/**
 * @}
 */

#endif  /* PPAPI_C_PRIVATE_PPB_NACL_PRIVATE_H_ */

