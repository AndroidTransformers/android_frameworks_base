/*
 * Copyright (C) 2009 The Android Open Source Project
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

#ifndef ANDROID_RS_BUILD_FOR_HOST
#include "rsContext.h"
#include <GLES/gl.h>
#include <GLES2/gl2.h>
#else
#include "rsContextHostStub.h"
#include <OpenGL/gl.h>
#endif //ANDROID_RS_BUILD_FOR_HOST

using namespace android;
using namespace android::renderscript;


ShaderCache::ShaderCache()
{
    mEntries.setCapacity(16);
}

ShaderCache::~ShaderCache()
{
    for (uint32_t ct=0; ct < mEntries.size(); ct++) {
        glDeleteProgram(mEntries[ct]->program);
        free(mEntries[ct]);
    }
}

bool ShaderCache::lookup(Context *rsc, ProgramVertex *vtx, ProgramFragment *frag)
{
    if (!vtx->getShaderID()) {
        vtx->loadShader(rsc);
    }
    if (!frag->getShaderID()) {
        frag->loadShader(rsc);
    }

    // Don't try to cache if shaders failed to load
    if(!vtx->getShaderID() || !frag->getShaderID()) {
        return false;
    }
    //LOGV("ShaderCache lookup  vtx %i, frag %i", vtx->getShaderID(), frag->getShaderID());
    uint32_t entryCount = mEntries.size();
    for(uint32_t ct = 0; ct < entryCount; ct ++) {
        if ((mEntries[ct]->vtx == vtx->getShaderID()) &&
            (mEntries[ct]->frag == frag->getShaderID())) {

            //LOGV("SC using program %i", mEntries[ct]->program);
            glUseProgram(mEntries[ct]->program);
            mCurrent = mEntries[ct];
            //LOGV("ShaderCache hit, using %i", ct);
            rsc->checkError("ShaderCache::lookup (hit)");
            return true;
        }
    }

    //LOGV("ShaderCache miss");
    //LOGE("e0 %x", glGetError());
    entry_t *e = (entry_t *)malloc(sizeof(entry_t));
    mEntries.push(e);
    mCurrent = e;
    e->vtx = vtx->getShaderID();
    e->frag = frag->getShaderID();
    e->program = glCreateProgram();
    e->vtxAttrCount = vtx->getAttribCount();
    if (e->program) {
        GLuint pgm = e->program;
        glAttachShader(pgm, vtx->getShaderID());
        //LOGE("e1 %x", glGetError());
        glAttachShader(pgm, frag->getShaderID());

        if (!vtx->isUserProgram()) {
            glBindAttribLocation(pgm, 0, "ATTRIB_position");
            glBindAttribLocation(pgm, 1, "ATTRIB_color");
            glBindAttribLocation(pgm, 2, "ATTRIB_normal");
            glBindAttribLocation(pgm, 3, "ATTRIB_texture0");
        }

        //LOGE("e2 %x", glGetError());
        glLinkProgram(pgm);
        //LOGE("e3 %x", glGetError());
        GLint linkStatus = GL_FALSE;
        glGetProgramiv(pgm, GL_LINK_STATUS, &linkStatus);
        if (linkStatus != GL_TRUE) {
            GLint bufLength = 0;
            glGetProgramiv(pgm, GL_INFO_LOG_LENGTH, &bufLength);
            if (bufLength) {
                char* buf = (char*) malloc(bufLength);
                if (buf) {
                    glGetProgramInfoLog(pgm, bufLength, NULL, buf);
                    LOGE("Could not link program:\n%s\n", buf);
                    free(buf);
                }
            }
            glDeleteProgram(pgm);
            rsc->setError(RS_ERROR_BAD_SHADER, "Error linking GL Programs");
            return false;
        }

        for (uint32_t ct=0; ct < e->vtxAttrCount; ct++) {
            e->mVtxAttribSlots[ct] = glGetAttribLocation(pgm, vtx->getAttribName(ct));
            e->mVtxAttribNames[ct] = vtx->getAttribName(ct).string();
            if (rsc->props.mLogShaders) {
                LOGV("vtx A %i, %s = %d\n", ct, vtx->getAttribName(ct).string(), e->mVtxAttribSlots[ct]);
            }
        }

        for (uint32_t ct=0; ct < vtx->getUniformCount(); ct++) {
            e->mVtxUniformSlots[ct] = glGetUniformLocation(pgm, vtx->getUniformName(ct));
            if (rsc->props.mLogShaders) {
                LOGV("vtx U, %s = %d\n", vtx->getUniformName(ct).string(), e->mVtxUniformSlots[ct]);
            }
        }
        for (uint32_t ct=0; ct < frag->getUniformCount(); ct++) {
            e->mFragUniformSlots[ct] = glGetUniformLocation(pgm, frag->getUniformName(ct));
            if (rsc->props.mLogShaders) {
                LOGV("frag U, %s = %d\n", frag->getUniformName(ct).string(), e->mFragUniformSlots[ct]);
            }
        }
    }

    e->mIsValid = true;
    //LOGV("SC made program %i", e->program);
    glUseProgram(e->program);
    rsc->checkError("ShaderCache::lookup (miss)");
    return true;
}

int32_t ShaderCache::vtxAttribSlot(const String8 &attrName) const {
    for (uint32_t ct=0; ct < mCurrent->vtxAttrCount; ct++) {
        if(attrName == mCurrent->mVtxAttribNames[ct]) {
            return mCurrent->mVtxAttribSlots[ct];
        }
    }
    return -1;
}

void ShaderCache::cleanupVertex(uint32_t id)
{
    int32_t numEntries = (int32_t)mEntries.size();
    for(int32_t ct = 0; ct < numEntries; ct ++) {
        if (mEntries[ct]->vtx == id) {
            glDeleteProgram(mEntries[ct]->program);

            free(mEntries[ct]);
            mEntries.removeAt(ct);
            numEntries = (int32_t)mEntries.size();
            ct --;
        }
    }
}

void ShaderCache::cleanupFragment(uint32_t id)
{
    int32_t numEntries = (int32_t)mEntries.size();
    for(int32_t ct = 0; ct < numEntries; ct ++) {
        if (mEntries[ct]->frag == id) {
            glDeleteProgram(mEntries[ct]->program);

            free(mEntries[ct]);
            mEntries.removeAt(ct);
            numEntries = (int32_t)mEntries.size();
            ct --;
        }
    }
}

void ShaderCache::cleanupAll()
{
}

