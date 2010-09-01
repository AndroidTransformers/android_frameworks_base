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

#ifndef ANDROID_UI_SNAPSHOT_H
#define ANDROID_UI_SNAPSHOT_H

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <utils/RefBase.h>

#include <SkCanvas.h>
#include <SkRegion.h>

#include "Layer.h"
#include "Matrix.h"
#include "Rect.h"

namespace android {
namespace uirenderer {

/**
 * A snapshot holds information about the current state of the rendering
 * surface. A snapshot is usually created whenever the user calls save()
 * and discarded when the user calls restore(). Once a snapshot is created,
 * it can hold information for deferred rendering.
 *
 * Each snapshot has a link to a previous snapshot, indicating the previous
 * state of the renderer.
 */
class Snapshot: public LightRefBase<Snapshot> {
public:
    Snapshot(): invisible(false), flags(0), previous(NULL), layer(NULL), fbo(0) {
        transform = &mTransformRoot;
        clipRect = &mClipRectRoot;
    }

    /**
     * Copies the specified snapshot/ The specified snapshot is stored as
     * the previous snapshot.
     */
    Snapshot(const sp<Snapshot>& s, int saveFlags):
            height(s->height),
            invisible(s->invisible),
            flags(0),
            previous(s),
            layer(NULL),
            fbo(s->fbo),
            viewport(s->viewport) {
        if (saveFlags & SkCanvas::kMatrix_SaveFlag) {
            mTransformRoot.load(*s->transform);
            transform = &mTransformRoot;
        } else {
            transform = s->transform;
        }

        if (saveFlags & SkCanvas::kClip_SaveFlag) {
            mClipRectRoot.set(*s->clipRect);
            clipRect = &mClipRectRoot;
        } else {
            clipRect = s->clipRect;
        }

        if ((s->flags & Snapshot::kFlagClipSet) &&
                !(s->flags & Snapshot::kFlagDirtyLocalClip)) {
            mLocalClip.set(s->mLocalClip);
        } else {
            flags |= Snapshot::kFlagDirtyLocalClip;
        }
    }

    /**
     * Various flags set on #flags.
     */
    enum Flags {
        /**
         * Indicates that the clip region was modified. When this
         * snapshot is restored so must the clip.
         */
        kFlagClipSet = 0x1,
        /**
         * Indicates that this snapshot was created when saving
         * a new layer.
         */
        kFlagIsLayer = 0x2,
        /**
         * Indicates that this snapshot has changed the ortho matrix.
         */
        kFlagDirtyOrtho = 0x4,
        /**
         * Indicates that the local clip should be recomputed.
         */
        kFlagDirtyLocalClip = 0x8,
    };

    /**
     * Intersects the current clip with the new clip rectangle.
     */
    bool clip(float left, float top, float right, float bottom, SkRegion::Op op) {
        bool clipped = false;

        Rect r(left, top, right, bottom);
        transform->mapRect(r);

        switch (op) {
            case SkRegion::kDifference_Op:
                break;
            case SkRegion::kIntersect_Op:
                clipped = clipRect->intersect(r);
                break;
            case SkRegion::kUnion_Op:
                clipped = clipRect->unionWith(r);
                break;
            case SkRegion::kXOR_Op:
                break;
            case SkRegion::kReverseDifference_Op:
                break;
            case SkRegion::kReplace_Op:
                clipRect->set(r);
                clipped = true;
                break;
        }

        if (clipped) {
            flags |= Snapshot::kFlagClipSet | Snapshot::kFlagDirtyLocalClip;
        }

        return clipped;
    }

    /**
     * Sets the current clip.
     */
    void setClip(float left, float top, float right, float bottom) {
        clipRect->set(left, top, right, bottom);
        flags |= Snapshot::kFlagClipSet | Snapshot::kFlagDirtyLocalClip;
    }

    const Rect& getLocalClip() {
        if (flags & Snapshot::kFlagDirtyLocalClip) {
            mat4 inverse;
            inverse.loadInverse(*transform);

            mLocalClip.set(*clipRect);
            inverse.mapRect(mLocalClip);

            flags &= ~Snapshot::kFlagDirtyLocalClip;
        }
        return mLocalClip;
    }

    // TODO: Temporary
    void resetTransform(float x, float y, float z) {
        transform = &mTransformRoot;
        transform->loadTranslate(x, y, z);
    }

    // TODO: Temporary
    void resetClip(float left, float top, float right, float bottom) {
        clipRect = &mClipRectRoot;
        clipRect->set(left, top, right, bottom);
        flags |= Snapshot::kFlagClipSet | Snapshot::kFlagDirtyLocalClip;
    }

    /**
     * Height of the framebuffer the snapshot is rendering into.
     */
    int height;

    /**
     * If true, the layer won't be rendered.
     */
    bool invisible;

    /**
     * Dirty flags.
     */
    int flags;

    /**
     * Previous snapshot.
     */
    sp<Snapshot> previous;

    /**
     * Only set when the flag kFlagIsLayer is set.
     */
    Layer* layer;
    GLuint fbo;

    /**
     * Current viewport.
     */
    Rect viewport;

    /**
     * Contains the previous ortho matrix.
     */
    mat4 orthoMatrix;

    /**
     * Local transformation. Holds the current translation, scale and
     * rotation values.
     */
    mat4* transform;

    /**
     * Current clip region. The clip is stored in canvas-space coordinates,
     * (screen-space coordinates in the regular case.)
     */
    Rect* clipRect;

private:
    mat4 mTransformRoot;
    Rect mClipRectRoot;
    Rect mLocalClip;

}; // class Snapshot

}; // namespace uirenderer
}; // namespace android

#endif // ANDROID_UI_SNAPSHOT_H
