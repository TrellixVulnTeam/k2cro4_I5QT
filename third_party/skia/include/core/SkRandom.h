
/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#ifndef SkRandom_DEFINED
#define SkRandom_DEFINED

#include "Sk64.h"
#include "SkScalar.h"

/** \class SkRandom

    Utility class that implements pseudo random 32bit numbers using a fast
    linear equation. Unlike rand(), this class holds its own seed (initially
    set to 0), so that multiple instances can be used with no side-effects.
*/
class SkRandom {
public:
    SkRandom() : fSeed(0) {}
    SkRandom(uint32_t seed) : fSeed(seed) {}

    /** Return the next pseudo random number as an unsigned 32bit value.
    */
    uint32_t nextU() { uint32_t r = fSeed * kMul + kAdd; fSeed = r; return r; }

    /** Return the next pseudo random number as a signed 32bit value.
    */
    int32_t nextS() { return (int32_t)this->nextU(); }

    /** Return the next pseudo random number as an unsigned 16bit value.
    */
    U16CPU nextU16() { return this->nextU() >> 16; }

    /** Return the next pseudo random number as a signed 16bit value.
    */
    S16CPU nextS16() { return this->nextS() >> 16; }

    /** Return the next pseudo random number, as an unsigned value of
        at most bitCount bits.
        @param bitCount The maximum number of bits to be returned
    */
    uint32_t nextBits(unsigned bitCount) {
        SkASSERT(bitCount > 0 && bitCount <= 32);
        return this->nextU() >> (32 - bitCount);
    }

    /** Return the next pseudo random unsigned number, mapped to lie within
        [min, max] inclusive.
    */
    uint32_t nextRangeU(uint32_t min, uint32_t max) {
        SkASSERT(min <= max);
        return min + this->nextU() % (max - min + 1);
    }

    /** Return the next pseudo random unsigned number, mapped to lie within
        [0, count).
     */
    uint32_t nextULessThan(uint32_t count) {
        SkASSERT(count > 0);
        return this->nextRangeU(0, count - 1);
    }

    /** Return the next pseudo random number expressed as an unsigned SkFixed
        in the range [0..SK_Fixed1).
    */
    SkFixed nextUFixed1() { return this->nextU() >> 16; }

    /** Return the next pseudo random number expressed as a signed SkFixed
        in the range (-SK_Fixed1..SK_Fixed1).
    */
    SkFixed nextSFixed1() { return this->nextS() >> 15; }

    /** Return the next pseudo random number expressed as a SkScalar
        in the range [0..SK_Scalar1).
    */
    SkScalar nextUScalar1() { return SkFixedToScalar(this->nextUFixed1()); }

    /** Return the next pseudo random number expressed as a SkScalar
        in the range [min..max).
    */
    SkScalar nextRangeScalar(SkScalar min, SkScalar max) {
        return SkScalarMul(this->nextUScalar1(), (max - min)) + min;
    }

    /** Return the next pseudo random number expressed as a SkScalar
        in the range (-SK_Scalar1..SK_Scalar1).
    */
    SkScalar nextSScalar1() { return SkFixedToScalar(this->nextSFixed1()); }

    /** Return the next pseudo random number as a bool.
    */
    bool nextBool() { return this->nextU() >= 0x80000000; }

    /** Return the next pseudo random number as a signed 64bit value.
    */
    void next64(Sk64* a) {
        SkASSERT(a);
        a->set(this->nextS(), this->nextU());
    }

    /**
     *  Return the current seed. This allows the caller to later reset to the
     *  same seed (using setSeed) so it can generate the same sequence.
     */
    int32_t getSeed() const { return fSeed; }

    /** Set the seed of the random object. The seed is initialized to 0 when the
        object is first created, and is updated each time the next pseudo random
        number is requested.
    */
    void setSeed(int32_t seed) { fSeed = (uint32_t)seed; }

private:
    //  See "Numerical Recipes in C", 1992 page 284 for these constants
    enum {
        kMul = 1664525,
        kAdd = 1013904223
    };
    uint32_t fSeed;
};

#endif

