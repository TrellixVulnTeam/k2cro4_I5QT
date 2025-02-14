/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* $Id: rijndael.h,v 1.12 2012/04/25 14:49:43 gerv%gerv.net Exp $ */

#ifndef _RIJNDAEL_H_
#define _RIJNDAEL_H_ 1

#define RIJNDAEL_MIN_BLOCKSIZE 16 /* bytes */
#define RIJNDAEL_MAX_BLOCKSIZE 32 /* bytes */

typedef SECStatus AESFunc(AESContext *cx, unsigned char *output,
                          unsigned int *outputLen, unsigned int maxOutputLen,
                          const unsigned char *input, unsigned int inputLen, 
                          unsigned int blocksize);

typedef SECStatus AESBlockFunc(AESContext *cx, 
                               unsigned char *output,
                               const unsigned char *input);

/* RIJNDAEL_NUM_ROUNDS
 *
 * Number of rounds per execution
 * Nk - number of key bytes
 * Nb - blocksize (in bytes)
 */
#define RIJNDAEL_NUM_ROUNDS(Nk, Nb) \
    (PR_MAX(Nk, Nb) + 6)

/* RIJNDAEL_MAX_STATE_SIZE 
 *
 * Maximum number of bytes in the state (spec includes up to 256-bit block
 * size)
 */
#define RIJNDAEL_MAX_STATE_SIZE 32

/*
 * This magic number is (Nb_max * (Nr_max + 1))
 * where Nb_max is the maximum block size in 32-bit words,
 *       Nr_max is the maximum number of rounds, which is Nb_max + 6
 */
#define RIJNDAEL_MAX_EXP_KEY_SIZE (8 * 15)

/* AESContextStr
 *
 * Values which maintain the state for Rijndael encryption/decryption.
 *
 * iv          - initialization vector for CBC mode
 * Nb          - the number of bytes in a block, specified by user
 * Nr          - the number of rounds, specified by a table
 * expandedKey - the round keys in 4-byte words, the length is Nr * Nb
 * worker      - the encryption/decryption function to use with this context
 */
struct AESContextStr
{
    unsigned int   Nb;
    unsigned int   Nr;
    AESFunc       *worker;
    unsigned char iv[RIJNDAEL_MAX_BLOCKSIZE];
    PRUint32      expandedKey[RIJNDAEL_MAX_EXP_KEY_SIZE];
};

#endif /* _RIJNDAEL_H_ */
