/*
 * Copyright (C) 2010 The Android Open Source Project
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

#ifndef ANDROID_UI_DISPLAY_LIST_RENDERER_H
#define ANDROID_UI_DISPLAY_LIST_RENDERER_H

#include <SkChunkAlloc.h>
#include <SkFlattenable.h>
#include <SkMatrix.h>
#include <SkPaint.h>
#include <SkPath.h>
#include <SkPictureFlat.h>
#include <SkRefCnt.h>
#include <SkTDArray.h>
#include <SkTSearch.h>

#include "OpenGLRenderer.h"

namespace android {
namespace uirenderer {

///////////////////////////////////////////////////////////////////////////////
// Defines
///////////////////////////////////////////////////////////////////////////////

#define MIN_WRITER_SIZE 16384
#define HEAP_BLOCK_SIZE 4096

///////////////////////////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////////////////////////

class PathHeap: public SkRefCnt {
public:
    PathHeap();
    PathHeap(SkFlattenableReadBuffer& buffer);
    ~PathHeap();

    int append(const SkPath& path);

    int count() const { return mPaths.count(); }

    SkPath& operator[](int index) const {
        return *mPaths[index];
    }

    void flatten(SkFlattenableWriteBuffer& buffer) const;

private:
    SkChunkAlloc mHeap;
    SkTDArray<SkPath*> mPaths;
};

///////////////////////////////////////////////////////////////////////////////
// Display list
///////////////////////////////////////////////////////////////////////////////

class DisplayListRenderer;

/**
 * Replays recorded drawing commands.
 */
class DisplayList {
public:
    DisplayList(const DisplayListRenderer& recorder);
    ~DisplayList();

    enum Op {
        AcquireContext,
        ReleaseContext,
        Save,
        Restore,
        RestoreToCount,
        SaveLayer,
        Translate,
        Rotate,
        Scale,
        SetMatrix,
        ConcatMatrix,
        ClipRect,
        DrawBitmap,
        DrawBitmapMatrix,
        DrawBitmapRect,
        DrawPatch,
        DrawColor,
        DrawRect,
        DrawPath,
        DrawLines,
        DrawText,
        ResetShader,
        SetupShader,
        ResetColorFilter,
        SetupColorFilter,
        ResetShadow,
        SetupShadow
    };

    void replay(OpenGLRenderer& renderer);

private:
    void init();

    class TextContainer {
    public:
        size_t length() const {
            return mByteLength;
        }

        const char* text() const {
            return (const char*) mText;
        }

        size_t mByteLength;
        const char* mText;
    };

    SkBitmap* getBitmap() {
        return (SkBitmap*) getInt();
    }

    SkiaShader* getShader() {
        return (SkiaShader*) getInt();
    }

    inline int getIndex() {
        return mReader.readInt();
    }

    inline int getInt() {
        return mReader.readInt();
    }

    SkMatrix* getMatrix() {
        return (SkMatrix*) getInt();
    }

    SkPath* getPath() {
        return &(*mPathHeap)[getInt() - 1];
    }

    SkPaint* getPaint() {
        return (SkPaint*) getInt();
    }

    inline float getFloat() {
        return mReader.readScalar();
    }

    int32_t* getInts(uint32_t& count) {
        count = getInt();
        return (int32_t*) mReader.skip(count * sizeof(int32_t));
    }

    uint32_t* getUInts(int8_t& count) {
        count = getInt();
        return (uint32_t*) mReader.skip(count * sizeof(uint32_t));
    }

    float* getFloats(int& count) {
        count = getInt();
        return (float*) mReader.skip(count * sizeof(float));
    }

    void getText(TextContainer* text) {
        size_t length = text->mByteLength = getInt();
        text->mText = (const char*) mReader.skip(length);
    }

    PathHeap* mPathHeap;

    Vector<SkBitmap*> mBitmapResources;
    Vector<SkMatrix*> mMatrixResources;
    Vector<SkPaint*> mPaintResources;
    Vector<SkiaShader*> mShaderResources;

    mutable SkFlattenableReadBuffer mReader;

    SkRefCntPlayback mRCPlayback;
    SkTypefacePlayback mTFPlayback;
};

///////////////////////////////////////////////////////////////////////////////
// Renderer
///////////////////////////////////////////////////////////////////////////////

/**
 * Records drawing commands in a display list for latter playback.
 */
class DisplayListRenderer: public OpenGLRenderer {
public:
    DisplayListRenderer();
    ~DisplayListRenderer();

    void setViewport(int width, int height);
    void prepare(bool opaque);

    void acquireContext();
    void releaseContext();

    int save(int flags);
    void restore();
    void restoreToCount(int saveCount);

    int saveLayer(float left, float top, float right, float bottom,
            SkPaint* p, int flags);

    void translate(float dx, float dy);
    void rotate(float degrees);
    void scale(float sx, float sy);

    void setMatrix(SkMatrix* matrix);
    void concatMatrix(SkMatrix* matrix);

    bool clipRect(float left, float top, float right, float bottom, SkRegion::Op op);

    void drawBitmap(SkBitmap* bitmap, float left, float top, SkPaint* paint);
    void drawBitmap(SkBitmap* bitmap, SkMatrix* matrix, SkPaint* paint);
    void drawBitmap(SkBitmap* bitmap, float srcLeft, float srcTop,
            float srcRight, float srcBottom, float dstLeft, float dstTop,
            float dstRight, float dstBottom, SkPaint* paint);
    void drawPatch(SkBitmap* bitmap, const int32_t* xDivs, const int32_t* yDivs,
            const uint32_t* colors, uint32_t width, uint32_t height, int8_t numColors,
            float left, float top, float right, float bottom, SkPaint* paint);
    void drawColor(int color, SkXfermode::Mode mode);
    void drawRect(float left, float top, float right, float bottom, SkPaint* paint);
    void drawPath(SkPath* path, SkPaint* paint);
    void drawLines(float* points, int count, SkPaint* paint);
    void drawText(const char* text, int bytesCount, int count, float x, float y, SkPaint* paint);

    void resetShader();
    void setupShader(SkiaShader* shader);

    void resetColorFilter();
    void setupColorFilter(SkiaColorFilter* filter);

    void resetShadow();
    void setupShadow(float radius, float dx, float dy, int color);

    void reset();

    DisplayList* getDisplayList() const {
        return new DisplayList(*this);
    }

    const SkWriter32& writeStream() const {
        return mWriter;
    }

    const Vector<SkBitmap*>& getBitmapResources() const {
        return mBitmapResources;
    }

    const Vector<SkMatrix*>& getMatrixResources() const {
        return mMatrixResources;
    }

    const Vector<SkPaint*>& getPaintResources() const {
        return mPaintResources;
    }

    const Vector<SkiaShader*>& getShaderResources() const {
        return mShaderResources;
    }

private:
    inline void addOp(DisplayList::Op drawOp) {
        mWriter.writeInt(drawOp);
    }

    inline void addInt(int value) {
        mWriter.writeInt(value);
    }

    void addInts(const int32_t* values, uint32_t count) {
        mWriter.writeInt(count);
        for (uint32_t i = 0; i < count; i++) {
            mWriter.writeInt(values[i]);
        }
    }

    void addUInts(const uint32_t* values, int8_t count) {
        mWriter.writeInt(count);
        for (int8_t i = 0; i < count; i++) {
            mWriter.writeInt(values[i]);
        }
    }

    inline void addFloat(float value) {
        mWriter.writeScalar(value);
    }

    void addFloats(const float* values, int count) {
        mWriter.writeInt(count);
        for (int i = 0; i < count; i++) {
            mWriter.writeScalar(values[i]);
        }
    }

    inline void addPoint(float x, float y) {
        mWriter.writeScalar(x);
        mWriter.writeScalar(y);
    }

    inline void addBounds(float left, float top, float right, float bottom) {
        mWriter.writeScalar(left);
        mWriter.writeScalar(top);
        mWriter.writeScalar(right);
        mWriter.writeScalar(bottom);
    }

    inline void addText(const void* text, size_t byteLength) {
        mWriter.writeInt(byteLength);
        mWriter.writePad(text, byteLength);
    }

    inline void addPath(const SkPath* path) {
        if (mPathHeap == NULL) {
            mPathHeap = new PathHeap();
        }
        addInt(mPathHeap->append(*path));
    }

    inline void addPaint(SkPaint* paint) {
        addInt((int)paint);
        mPaintResources.add(paint);
        Caches& caches = Caches::getInstance();
        caches.resourceCache.incrementRefcount(paint);
    }

    inline void addMatrix(SkMatrix* matrix) {
        addInt((int)matrix);
        mMatrixResources.add(matrix);
        Caches& caches = Caches::getInstance();
        caches.resourceCache.incrementRefcount(matrix);
    }

    inline void addBitmap(SkBitmap* bitmap) {
        addInt((int)bitmap);
        mBitmapResources.add(bitmap);
        Caches& caches = Caches::getInstance();
        caches.resourceCache.incrementRefcount(bitmap);
    }

    inline void addShader(SkiaShader* shader) {
        addInt((int)shader);
        mShaderResources.add(shader);
        Caches& caches = Caches::getInstance();
        caches.resourceCache.incrementRefcount(shader);
    }

    SkChunkAlloc mHeap;

    Vector<SkBitmap*> mBitmapResources;
    Vector<SkMatrix*> mMatrixResources;
    Vector<SkPaint*> mPaintResources;
    Vector<SkiaShader*> mShaderResources;

    PathHeap* mPathHeap;
    SkWriter32 mWriter;

    SkRefCntRecorder mRCRecorder;
    SkRefCntRecorder mTFRecorder;

    friend class DisplayList;

}; // class DisplayListRenderer

}; // namespace uirenderer
}; // namespace android

#endif // ANDROID_UI_DISPLAY_LIST_RENDERER_H
