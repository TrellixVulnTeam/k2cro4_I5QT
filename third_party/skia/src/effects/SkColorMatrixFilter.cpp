
/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "SkColorMatrixFilter.h"
#include "SkColorMatrix.h"
#include "SkColorPriv.h"
#include "SkFlattenableBuffers.h"
#include "SkUnPreMultiply.h"

static int32_t rowmul4(const int32_t array[], unsigned r, unsigned g,
                          unsigned b, unsigned a) {
    return array[0] * r + array[1] * g  + array[2] * b + array[3] * a + array[4];
}

static int32_t rowmul3(const int32_t array[], unsigned r, unsigned g,
                       unsigned b) {
    return array[0] * r + array[1] * g  + array[2] * b + array[4];
}

static void General(SkColorMatrixFilter::State* state,
                    unsigned r, unsigned g, unsigned b, unsigned a) {
    const int32_t* SK_RESTRICT array = state->fArray;
    const int shift = state->fShift;
    int32_t* SK_RESTRICT result = state->fResult;

    result[0] = rowmul4(&array[0], r, g, b, a) >> shift;
    result[1] = rowmul4(&array[5], r, g, b, a) >> shift;
    result[2] = rowmul4(&array[10], r, g, b, a) >> shift;
    result[3] = rowmul4(&array[15], r, g, b, a) >> shift;
}

static void General16(SkColorMatrixFilter::State* state,
                      unsigned r, unsigned g, unsigned b, unsigned a) {
    const int32_t* SK_RESTRICT array = state->fArray;
    int32_t* SK_RESTRICT result = state->fResult;

    result[0] = rowmul4(&array[0], r, g, b, a) >> 16;
    result[1] = rowmul4(&array[5], r, g, b, a) >> 16;
    result[2] = rowmul4(&array[10], r, g, b, a) >> 16;
    result[3] = rowmul4(&array[15], r, g, b, a) >> 16;
}

static void AffineAdd(SkColorMatrixFilter::State* state,
                      unsigned r, unsigned g, unsigned b, unsigned a) {
    const int32_t* SK_RESTRICT array = state->fArray;
    const int shift = state->fShift;
    int32_t* SK_RESTRICT result = state->fResult;

    result[0] = rowmul3(&array[0], r, g, b) >> shift;
    result[1] = rowmul3(&array[5], r, g, b) >> shift;
    result[2] = rowmul3(&array[10], r, g, b) >> shift;
    result[3] = a;
}

static void AffineAdd16(SkColorMatrixFilter::State* state,
                        unsigned r, unsigned g, unsigned b, unsigned a) {
    const int32_t* SK_RESTRICT array = state->fArray;
    int32_t* SK_RESTRICT result = state->fResult;

    result[0] = rowmul3(&array[0], r, g, b) >> 16;
    result[1] = rowmul3(&array[5], r, g, b) >> 16;
    result[2] = rowmul3(&array[10], r, g, b) >> 16;
    result[3] = a;
}

static void ScaleAdd(SkColorMatrixFilter::State* state,
                     unsigned r, unsigned g, unsigned b, unsigned a) {
    const int32_t* SK_RESTRICT array = state->fArray;
    const int shift = state->fShift;
    int32_t* SK_RESTRICT result = state->fResult;

    // cast to (int) to keep the expression signed for the shift
    result[0] = (array[0] * (int)r + array[4]) >> shift;
    result[1] = (array[6] * (int)g + array[9]) >> shift;
    result[2] = (array[12] * (int)b + array[14]) >> shift;
    result[3] = a;
}

static void ScaleAdd16(SkColorMatrixFilter::State* state,
                       unsigned r, unsigned g, unsigned b, unsigned a) {
    const int32_t* SK_RESTRICT array = state->fArray;
    int32_t* SK_RESTRICT result = state->fResult;

    // cast to (int) to keep the expression signed for the shift
    result[0] = (array[0] * (int)r + array[4]) >> 16;
    result[1] = (array[6] * (int)g + array[9]) >> 16;
    result[2] = (array[12] * (int)b + array[14]) >> 16;
    result[3] = a;
}

static void Add(SkColorMatrixFilter::State* state,
                unsigned r, unsigned g, unsigned b, unsigned a) {
    const int32_t* SK_RESTRICT array = state->fArray;
    const int shift = state->fShift;
    int32_t* SK_RESTRICT result = state->fResult;

    result[0] = r + (array[4] >> shift);
    result[1] = g + (array[9] >> shift);
    result[2] = b + (array[14] >> shift);
    result[3] = a;
}

static void Add16(SkColorMatrixFilter::State* state,
                  unsigned r, unsigned g, unsigned b, unsigned a) {
    const int32_t* SK_RESTRICT array = state->fArray;
    int32_t* SK_RESTRICT result = state->fResult;

    result[0] = r + (array[4] >> 16);
    result[1] = g + (array[9] >> 16);
    result[2] = b + (array[14] >> 16);
    result[3] = a;
}

#define kNO_ALPHA_FLAGS (SkColorFilter::kAlphaUnchanged_Flag |  \
                         SkColorFilter::kHasFilter16_Flag)

// src is [20] but some compilers won't accept __restrict__ on anything
// but an raw pointer or reference
void SkColorMatrixFilter::initState(const SkScalar* SK_RESTRICT src) {
    int32_t* array = fState.fArray;
    SkFixed max = 0;
    for (int i = 0; i < 20; i++) {
        SkFixed value = SkScalarToFixed(src[i]);
        array[i] = value;
        value = SkAbs32(value);
        max = SkMax32(max, value);
    }

    /*  All of fArray[] values must fit in 23 bits, to safely allow me to
        multiply them by 8bit unsigned values, and get a signed answer without
        overflow. This means clz needs to be 9 or bigger
    */
    int bits = SkCLZ(max);
    int32_t one = SK_Fixed1;

    fState.fShift = 16; // we are starting out as fixed 16.16
    if (bits < 9) {
        bits = 9 - bits;
        fState.fShift -= bits;
        for (int i = 0; i < 20; i++) {
            array[i] >>= bits;
        }
        one >>= bits;
    }

    // check if we have to munge Alpha
    int32_t changesAlpha = (array[15] | array[16] | array[17] |
                            (array[18] - one) | array[19]);
    int32_t usesAlpha = (array[3] | array[8] | array[13]);
    bool shiftIs16 = (16 == fState.fShift);

    if (changesAlpha | usesAlpha) {
        fProc = shiftIs16 ? General16 : General;
        fFlags = changesAlpha ? 0 : SkColorFilter::kAlphaUnchanged_Flag;
    } else {
        fFlags = kNO_ALPHA_FLAGS;

        int32_t needsScale = (array[0] - one) |       // red axis
                             (array[6] - one) |       // green axis
                             (array[12] - one);       // blue axis

        int32_t needs3x3 =  array[1] | array[2] |     // red off-axis
                            array[5] | array[7] |     // green off-axis
                            array[10] | array[11];    // blue off-axis

        if (needs3x3) {
            fProc = shiftIs16 ? AffineAdd16 : AffineAdd;
        } else if (needsScale) {
            fProc = shiftIs16 ? ScaleAdd16 : ScaleAdd;
        } else if (array[4] | array[9] | array[14]) {   // needs add
            fProc = shiftIs16 ? Add16 : Add;
        } else {
            fProc = NULL;   // identity
        }
    }

    /*  preround our add values so we get a rounded shift. We do this after we
        analyze the array, so we don't miss the case where the caller has zeros
        which could make us accidentally take the General or Add case.
    */
    if (NULL != fProc) {
        int32_t add = 1 << (fState.fShift - 1);
        array[4] += add;
        array[9] += add;
        array[14] += add;
        array[19] += add;
    }
}

///////////////////////////////////////////////////////////////////////////////

static int32_t pin(int32_t value, int32_t max) {
    if (value < 0) {
        value = 0;
    }
    if (value > max) {
        value = max;
    }
    return value;
}

SkColorMatrixFilter::SkColorMatrixFilter(const SkColorMatrix& cm) : fMatrix(cm) {
    this->initState(cm.fMat);
}

SkColorMatrixFilter::SkColorMatrixFilter(const SkScalar array[20]) {
    memcpy(fMatrix.fMat, array, 20 * sizeof(SkScalar));
    this->initState(array);
}

uint32_t SkColorMatrixFilter::getFlags() {
    return this->INHERITED::getFlags() | fFlags;
}

void SkColorMatrixFilter::filterSpan(const SkPMColor src[], int count,
                                     SkPMColor dst[]) {
    Proc proc = fProc;
    State* state = &fState;
    int32_t* result = state->fResult;

    if (NULL == proc) {
        if (src != dst) {
            memcpy(dst, src, count * sizeof(SkPMColor));
        }
        return;
    }

    const SkUnPreMultiply::Scale* table = SkUnPreMultiply::GetScaleTable();

    for (int i = 0; i < count; i++) {
        SkPMColor c = src[i];

        unsigned r = SkGetPackedR32(c);
        unsigned g = SkGetPackedG32(c);
        unsigned b = SkGetPackedB32(c);
        unsigned a = SkGetPackedA32(c);

        // need our components to be un-premultiplied
        if (255 != a) {
            SkUnPreMultiply::Scale scale = table[a];
            r = SkUnPreMultiply::ApplyScale(scale, r);
            g = SkUnPreMultiply::ApplyScale(scale, g);
            b = SkUnPreMultiply::ApplyScale(scale, b);

            SkASSERT(r <= 255);
            SkASSERT(g <= 255);
            SkASSERT(b <= 255);
        }

        proc(state, r, g, b, a);

        r = pin(result[0], SK_R32_MASK);
        g = pin(result[1], SK_G32_MASK);
        b = pin(result[2], SK_B32_MASK);
        a = pin(result[3], SK_A32_MASK);
        // re-prepremultiply if needed
        dst[i] = SkPremultiplyARGBInline(a, r, g, b);
    }
}

void SkColorMatrixFilter::filterSpan16(const uint16_t src[], int count,
                                       uint16_t dst[]) {
    SkASSERT(fFlags & SkColorFilter::kHasFilter16_Flag);

    Proc   proc = fProc;
    State* state = &fState;
    int32_t* result = state->fResult;

    if (NULL == proc) {
        if (src != dst) {
            memcpy(dst, src, count * sizeof(uint16_t));
        }
        return;
    }

    for (int i = 0; i < count; i++) {
        uint16_t c = src[i];

        // expand to 8bit components (since our matrix translate is 8bit biased
        unsigned r = SkPacked16ToR32(c);
        unsigned g = SkPacked16ToG32(c);
        unsigned b = SkPacked16ToB32(c);

        proc(state, r, g, b, 0);

        r = pin(result[0], SK_R32_MASK);
        g = pin(result[1], SK_G32_MASK);
        b = pin(result[2], SK_B32_MASK);

        // now packed it back down to 16bits (hmmm, could dither...)
        dst[i] = SkPack888ToRGB16(r, g, b);
    }
}

///////////////////////////////////////////////////////////////////////////////

void SkColorMatrixFilter::flatten(SkFlattenableWriteBuffer& buffer) const {
    this->INHERITED::flatten(buffer);
    SkASSERT(sizeof(fMatrix.fMat)/sizeof(SkScalar) == 20);
    buffer.writeScalarArray(fMatrix.fMat, 20);
}

SkColorMatrixFilter::SkColorMatrixFilter(SkFlattenableReadBuffer& buffer)
        : INHERITED(buffer) {
    SkASSERT(buffer.getArrayCount() == 20);
    buffer.readScalarArray(fMatrix.fMat);
    this->initState(fMatrix.fMat);
}

bool SkColorMatrixFilter::asColorMatrix(SkScalar matrix[20]) {
    if (matrix) {
        memcpy(matrix, fMatrix.fMat, 20 * sizeof(SkScalar));
    }
    return true;
}

#if SK_SUPPORT_GPU
#include "GrEffect.h"
#include "GrTBackendEffectFactory.h"
#include "gl/GrGLEffect.h"

class ColorMatrixEffect : public GrEffect {
public:
    static const char* Name() { return "Color Matrix"; }

    ColorMatrixEffect(const SkColorMatrix& matrix) : GrEffect(0), fMatrix(matrix) {}

    virtual const GrBackendEffectFactory& getFactory() const SK_OVERRIDE {
        return GrTBackendEffectFactory<ColorMatrixEffect>::getInstance();
    }

    virtual bool isEqual(const GrEffect& s) const {
        const ColorMatrixEffect& cme = static_cast<const ColorMatrixEffect&>(s);
        return cme.fMatrix == fMatrix;
    }

    GR_DECLARE_EFFECT_TEST;

    class GLEffect : public GrGLEffect {
    public:
        // this class always generates the same code.
        static EffectKey GenKey(const GrEffectStage&, const GrGLCaps&) { return 0; }

        GLEffect(const GrBackendEffectFactory& factory,
                 const GrEffect& effect)
        : INHERITED(factory)
        , fMatrixHandle(GrGLUniformManager::kInvalidUniformHandle)
        , fVectorHandle(GrGLUniformManager::kInvalidUniformHandle) {}

        virtual void emitCode(GrGLShaderBuilder* builder,
                              const GrEffectStage&,
                              EffectKey,
                              const char* vertexCoords,
                              const char* outputColor,
                              const char* inputColor,
                              const TextureSamplerArray&) SK_OVERRIDE {
            fMatrixHandle = builder->addUniform(GrGLShaderBuilder::kFragment_ShaderType,
                                                kMat44f_GrSLType,
                                                "ColorMatrix");
            fVectorHandle = builder->addUniform(GrGLShaderBuilder::kFragment_ShaderType,
                                                kVec4f_GrSLType,
                                                "ColorMatrixVector");

            if (NULL == inputColor) {
                // could optimize this case, but we aren't for now.
                inputColor = GrGLSLOnesVecf(4);
            }
            // The max() is to guard against 0 / 0 during unpremul when the incoming color is
            // transparent black.
            builder->fFSCode.appendf("\tfloat nonZeroAlpha = max(%s.a, 0.00001);\n", inputColor);
            builder->fFSCode.appendf("\t%s = %s * vec4(%s.rgb / nonZeroAlpha, nonZeroAlpha) + %s;\n",
                                     outputColor,
                                     builder->getUniformCStr(fMatrixHandle),
                                     inputColor,
                                     builder->getUniformCStr(fVectorHandle));
            builder->fFSCode.appendf("\t%s.rgb *= %s.a;\n", outputColor, outputColor);
        }

        virtual void setData(const GrGLUniformManager& uniManager,
                             const GrEffectStage& stage) SK_OVERRIDE {
            const ColorMatrixEffect& cme =
                static_cast<const ColorMatrixEffect&>(*stage.getEffect());
            const float* m = cme.fMatrix.fMat;
            // The GL matrix is transposed from SkColorMatrix.
            GrGLfloat mt[]  = {
                m[0], m[5], m[10], m[15],
                m[1], m[6], m[11], m[16],
                m[2], m[7], m[12], m[17],
                m[3], m[8], m[13], m[18],
            };
            static const float kScale = 1.0f / 255.0f;
            GrGLfloat vec[] = {
                m[4] * kScale, m[9] * kScale, m[14] * kScale, m[19] * kScale,
            };
            uniManager.setMatrix4fv(fMatrixHandle, 0, 1, mt);
            uniManager.set4fv(fVectorHandle, 0, 1, vec);
        }

    private:
        GrGLUniformManager::UniformHandle fMatrixHandle;
        GrGLUniformManager::UniformHandle fVectorHandle;
    };

private:
    SkColorMatrix fMatrix;

    typedef GrGLEffect INHERITED;
};

GR_DEFINE_EFFECT_TEST(ColorMatrixEffect);

GrEffect* ColorMatrixEffect::TestCreate(SkRandom* random,
                                        GrContext*,
                                        GrTexture* dummyTextures[2]) {
    SkColorMatrix colorMatrix;
    for (int i = 0; i < SK_ARRAY_COUNT(colorMatrix.fMat); ++i) {
        colorMatrix.fMat[i] = random->nextSScalar1();
    }
    return SkNEW_ARGS(ColorMatrixEffect, (colorMatrix));
}

GrEffect* SkColorMatrixFilter::asNewEffect(GrContext*) const {
    return SkNEW_ARGS(ColorMatrixEffect, (fMatrix));
}

#endif
