/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NSSBASE_H
#define NSSBASE_H

#ifdef DEBUG
static const char NSSBASE_CVS_ID[] = "@(#) $RCSfile: nssbase.h,v $ $Revision: 1.4 $ $Date: 2012/04/25 14:49:26 $";
#endif /* DEBUG */

/*
 * nssbase.h
 *
 * This header file contains the prototypes of the basic public
 * NSS routines.
 */

#ifndef NSSBASET_H
#include "nssbaset.h"
#endif /* NSSBASET_H */

PR_BEGIN_EXTERN_C

/*
 * NSSArena
 *
 * The public methods relating to this type are:
 *
 *  NSSArena_Create  -- constructor
 *  NSSArena_Destroy
 */

/*
 * NSSArena_Create
 *
 * This routine creates a new memory arena.  This routine may return
 * NULL upon error, in which case it will have created an error stack.
 *
 * The top-level error may be one of the following values:
 *  NSS_ERROR_NO_MEMORY
 *
 * Return value:
 *  NULL upon error
 *  A pointer to an NSSArena upon success
 */

NSS_EXTERN NSSArena *
NSSArena_Create
(
  void
);

extern const NSSError NSS_ERROR_NO_MEMORY;

/*
 * NSSArena_Destroy
 *
 * This routine will destroy the specified arena, freeing all memory
 * allocated from it.  This routine returns a PRStatus value; if 
 * successful, it will return PR_SUCCESS.  If unsuccessful, it will
 * create an error stack and return PR_FAILURE.
 *
 * The top-level error may be one of the following values:
 *  NSS_ERROR_INVALID_ARENA
 *
 * Return value:
 *  PR_SUCCESS upon success
 *  PR_FAILURE upon failure
 */

NSS_EXTERN PRStatus
NSSArena_Destroy
(
  NSSArena *arena
);

extern const NSSError NSS_ERROR_INVALID_ARENA;

/*
 * The error stack
 *
 * The public methods relating to the error stack are:
 *
 *  NSS_GetError
 *  NSS_GetErrorStack
 */

/*
 * NSS_GetError
 *
 * This routine returns the highest-level (most general) error set
 * by the most recent NSS library routine called by the same thread
 * calling this routine.
 *
 * This routine cannot fail.  It may return NSS_ERROR_NO_ERROR, which
 * indicates that the previous NSS library call did not set an error.
 *
 * Return value:
 *  0 if no error has been set
 *  A nonzero error number
 */

NSS_EXTERN NSSError
NSS_GetError
(
  void
);

extern const NSSError NSS_ERROR_NO_ERROR;

/*
 * NSS_GetErrorStack
 *
 * This routine returns a pointer to an array of NSSError values, 
 * containingthe entire sequence or "stack" of errors set by the most 
 * recent NSS library routine called by the same thread calling this 
 * routine.  NOTE: the caller DOES NOT OWN the memory pointed to by 
 * the return value.  The pointer will remain valid until the calling 
 * thread calls another NSS routine.  The lowest-level (most specific) 
 * error is first in the array, and the highest-level is last.  The 
 * array is zero-terminated.  This routine may return NULL upon error; 
 * this indicates a low-memory situation.
 *
 * Return value:
 *  NULL upon error, which is an implied NSS_ERROR_NO_MEMORY
 *  A NON-caller-owned pointer to an array of NSSError values
 */

NSS_EXTERN NSSError *
NSS_GetErrorStack
(
  void
);

PR_END_EXTERN_C

#endif /* NSSBASE_H */
