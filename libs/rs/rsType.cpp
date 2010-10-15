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
#else
#include "rsContextHostStub.h"
#include <OpenGL/gl.h>
#endif

using namespace android;
using namespace android::renderscript;

Type::Type(Context *rsc) : ObjectBase(rsc)
{
    mAllocFile = __FILE__;
    mAllocLine = __LINE__;
    mLODs = 0;
    mLODCount = 0;
    mAttribs = NULL;
    mAttribsSize = 0;
    clear();
}

Type::~Type()
{
    for (uint32_t ct = 0; ct < mRSC->mStateType.mTypes.size(); ct++) {
        if (mRSC->mStateType.mTypes[ct] == this) {
            mRSC->mStateType.mTypes.removeAt(ct);
            break;
        }
    }
    if (mLODs) {
        delete [] mLODs;
        mLODs = NULL;
    }
    if(mAttribs) {
        delete [] mAttribs;
        mAttribs = NULL;
    }
}

void Type::clear()
{
    if (mLODs) {
        delete [] mLODs;
        mLODs = NULL;
    }
    mDimX = 0;
    mDimY = 0;
    mDimZ = 0;
    mDimLOD = 0;
    mFaces = false;
    mElement.clear();
}

TypeState::TypeState()
{
}

TypeState::~TypeState()
{
}

size_t Type::getOffsetForFace(uint32_t face) const
{
    rsAssert(mFaces);
    return 0;
}

void Type::compute()
{
    uint32_t oldLODCount = mLODCount;
    if (mDimLOD) {
        uint32_t l2x = rsFindHighBit(mDimX) + 1;
        uint32_t l2y = rsFindHighBit(mDimY) + 1;
        uint32_t l2z = rsFindHighBit(mDimZ) + 1;

        mLODCount = rsMax(l2x, l2y);
        mLODCount = rsMax(mLODCount, l2z);
    } else {
        mLODCount = 1;
    }
    if (mLODCount != oldLODCount) {
        if(mLODs){
            delete [] mLODs;
        }
        mLODs = new LOD[mLODCount];
    }

    uint32_t tx = mDimX;
    uint32_t ty = mDimY;
    uint32_t tz = mDimZ;
    size_t offset = 0;
    for (uint32_t lod=0; lod < mLODCount; lod++) {
        mLODs[lod].mX = tx;
        mLODs[lod].mY = ty;
        mLODs[lod].mZ = tz;
        mLODs[lod].mOffset = offset;
        offset += tx * rsMax(ty, 1u) * rsMax(tz, 1u) * mElement->getSizeBytes();
        if (tx > 1) tx >>= 1;
        if (ty > 1) ty >>= 1;
        if (tz > 1) tz >>= 1;
    }

    // At this point the offset is the size of a mipmap chain;
    mMipChainSizeBytes = offset;

    if (mFaces) {
        offset *= 6;
    }
    mTotalSizeBytes = offset;

    makeGLComponents();
}

uint32_t Type::getLODOffset(uint32_t lod, uint32_t x) const
{
    uint32_t offset = mLODs[lod].mOffset;
    offset += x * mElement->getSizeBytes();
    return offset;
}

uint32_t Type::getLODOffset(uint32_t lod, uint32_t x, uint32_t y) const
{
    uint32_t offset = mLODs[lod].mOffset;
    offset += (x + y * mLODs[lod].mX) * mElement->getSizeBytes();
    return offset;
}

uint32_t Type::getLODOffset(uint32_t lod, uint32_t x, uint32_t y, uint32_t z) const
{
    uint32_t offset = mLODs[lod].mOffset;
    offset += (x + y*mLODs[lod].mX + z*mLODs[lod].mX*mLODs[lod].mY) * mElement->getSizeBytes();
    return offset;
}

bool Type::isValidGLComponent(uint32_t fieldIdx) {
    // Do not create attribs for padding
    if(mElement->getFieldName(fieldIdx)[0] == '#') {
        return false;
    }

    // Only GL_BYTE, GL_UNSIGNED_BYTE, GL_SHORT, GL_UNSIGNED_SHORT, GL_FIXED, GL_FLOAT are accepted.
    // Filter rs types accordingly
    RsDataType dt = mElement->getField(fieldIdx)->getComponent().getType();
    if(dt != RS_TYPE_FLOAT_32 && dt != RS_TYPE_UNSIGNED_8 &&
       dt != RS_TYPE_UNSIGNED_16 && dt != RS_TYPE_SIGNED_8 &&
       dt != RS_TYPE_SIGNED_16) {
        return false;
    }

    // Now make sure they are not arrays
    uint32_t arraySize = mElement->getFieldArraySize(fieldIdx);
    if(arraySize != 1) {
        return false;
    }

    return true;
}

void Type::makeGLComponents()
{
    // Count the number of gl attrs to initialize
    mAttribsSize = 0;

    for (uint32_t ct=0; ct < mElement->getFieldCount(); ct++) {
        if(isValidGLComponent(ct)) {
            mAttribsSize ++;
        }
    }
    if(mAttribs) {
        delete [] mAttribs;
        mAttribs = NULL;
    }
    if(mAttribsSize) {
        mAttribs = new VertexArray::Attrib[mAttribsSize];
    }

    uint32_t userNum = 0;
    for (uint32_t ct=0; ct < mElement->getFieldCount(); ct++) {
        const Component &c = mElement->getField(ct)->getComponent();

        if(!isValidGLComponent(ct)) {
            continue;
        }

        mAttribs[userNum].size = c.getVectorSize();
        mAttribs[userNum].offset = mElement->getFieldOffsetBytes(ct);
        mAttribs[userNum].type = c.getGLType();
        mAttribs[userNum].normalized = c.getType() != RS_TYPE_FLOAT_32;//c.getIsNormalized();
        String8 tmp(RS_SHADER_ATTR);
        tmp.append(mElement->getFieldName(ct));
        mAttribs[userNum].name.setTo(tmp.string());

        userNum ++;
    }
}


void Type::enableGLVertexBuffer(VertexArray *va) const
{
    uint32_t stride = mElement->getSizeBytes();
    for (uint32_t ct=0; ct < mAttribsSize; ct++) {
        // Load up to RS_MAX_ATTRIBS inputs
        // TODO: grow vertexarray dynamically
        if(ct >= RS_MAX_ATTRIBS) {
            LOGE("More GL attributes than we can handle");
            break;
        }
        if (mAttribs[ct].size) {
            va->add(mAttribs[ct], stride);
        }
    }
}



void Type::dumpLOGV(const char *prefix) const
{
    char buf[1024];
    ObjectBase::dumpLOGV(prefix);
    LOGV("%s   Type: x=%i y=%i z=%i mip=%i face=%i", prefix, mDimX, mDimY, mDimZ, mDimLOD, mFaces);
    sprintf(buf, "%s element: ", prefix);
    mElement->dumpLOGV(buf);
}

void Type::serialize(OStream *stream) const
{
    // Need to identify ourselves
    stream->addU32((uint32_t)getClassId());

    String8 name(getName());
    stream->addString(&name);

    mElement->serialize(stream);

    stream->addU32(mDimX);
    stream->addU32(mDimY);
    stream->addU32(mDimZ);

    stream->addU8((uint8_t)(mDimLOD ? 1 : 0));
    stream->addU8((uint8_t)(mFaces ? 1 : 0));
}

Type *Type::createFromStream(Context *rsc, IStream *stream)
{
    // First make sure we are reading the correct object
    RsA3DClassID classID = (RsA3DClassID)stream->loadU32();
    if(classID != RS_A3D_CLASS_ID_TYPE) {
        LOGE("type loading skipped due to invalid class id\n");
        return NULL;
    }

    String8 name;
    stream->loadString(&name);

    Element *elem = Element::createFromStream(rsc, stream);
    if(!elem) {
        return NULL;
    }

    Type *type = new Type(rsc);
    type->mDimX = stream->loadU32();
    type->mDimY = stream->loadU32();
    type->mDimZ = stream->loadU32();

    uint8_t temp = stream->loadU8();
    type->mDimLOD = temp != 0;

    temp = stream->loadU8();
    type->mFaces = temp != 0;

    type->setElement(elem);

    return type;
}

bool Type::getIsNp2() const
{
    uint32_t x = getDimX();
    uint32_t y = getDimY();
    uint32_t z = getDimZ();

    if (x && (x & (x-1))) {
        return true;
    }
    if (y && (y & (y-1))) {
        return true;
    }
    if (z && (z & (z-1))) {
        return true;
    }
    return false;
}

bool Type::isEqual(const Type *other) const {
    if(other == NULL) {
        return false;
    }
    if (other->getElement()->isEqual(getElement()) &&
        other->getDimX() == mDimX &&
        other->getDimY() == mDimY &&
        other->getDimZ() == mDimZ &&
        other->getDimLOD() == mDimLOD &&
        other->getDimFaces() == mFaces) {
        return true;
    }
    return false;
}

Type * Type::cloneAndResize1D(Context *rsc, uint32_t dimX) const
{
    TypeState * stc = &rsc->mStateType;
    for (uint32_t ct=0; ct < stc->mTypes.size(); ct++) {
        Type *t = stc->mTypes[ct];
        if (t->getElement() != mElement.get()) continue;
        if (t->getDimX() != dimX) continue;
        if (t->getDimY() != mDimY) continue;
        if (t->getDimZ() != mDimZ) continue;
        if (t->getDimLOD() != mDimLOD) continue;
        if (t->getDimFaces() != mFaces) continue;
        t->incUserRef();
        return t;
    }

    Type *nt = new Type(rsc);
    nt->mElement.set(mElement);
    nt->mDimX = dimX;
    nt->mDimY = mDimY;
    nt->mDimZ = mDimZ;
    nt->mDimLOD = mDimLOD;
    nt->mFaces = mFaces;
    nt->compute();
    return nt;
}

Type * Type::cloneAndResize2D(Context *rsc, uint32_t dimX, uint32_t dimY) const
{
    TypeState * stc = &rsc->mStateType;
    for (uint32_t ct=0; ct < stc->mTypes.size(); ct++) {
        Type *t = stc->mTypes[ct];
        if (t->getElement() != mElement.get()) continue;
        if (t->getDimX() != dimX) continue;
        if (t->getDimY() != dimY) continue;
        if (t->getDimZ() != mDimZ) continue;
        if (t->getDimLOD() != mDimLOD) continue;
        if (t->getDimFaces() != mFaces) continue;
        t->incUserRef();
        return t;
    }

    Type *nt = new Type(rsc);
    nt->mElement.set(mElement);
    nt->mDimX = dimX;
    nt->mDimY = dimY;
    nt->mDimZ = mDimZ;
    nt->mDimLOD = mDimLOD;
    nt->mFaces = mFaces;
    nt->compute();
    return nt;
}


//////////////////////////////////////////////////
//
namespace android {
namespace renderscript {

void rsi_TypeGetNativeData(Context *rsc, RsType type, uint32_t *typeData, uint32_t typeDataSize)
{
    rsAssert(typeDataSize == 6);
    // Pack the data in the follofing way mDimX; mDimY; mDimZ;
    // mDimLOD; mDimFaces; mElement; into typeData
    Type *t = static_cast<Type *>(type);

    (*typeData++) = t->getDimX();
    (*typeData++) = t->getDimY();
    (*typeData++) = t->getDimZ();
    (*typeData++) = t->getDimLOD();
    (*typeData++) = t->getDimFaces() ? 1 : 0;
    (*typeData++) = (uint32_t)t->getElement();

}


}
}

void * rsaTypeCreate(RsContext con, RsElement _e, uint32_t dimCount,
                     const RsDimension *dims, const uint32_t *vals)
{
    Context *rsc = static_cast<Context *>(con);
    Element *e = static_cast<Element *>(_e);
    TypeState * stc = &rsc->mStateType;

    uint32_t dimX = 0;
    uint32_t dimY = 0;
    uint32_t dimZ = 0;
    uint32_t dimLOD = 0;
    uint32_t dimFaces = 0;

    for (uint32_t ct=0; ct < dimCount; ct++) {
        switch(dims[ct]) {
        case RS_DIMENSION_X: dimX = vals[ct]; break;
        case RS_DIMENSION_Y: dimY = vals[ct]; break;
        case RS_DIMENSION_Z: dimZ = vals[ct]; break;
        case RS_DIMENSION_LOD: dimLOD = vals[ct]; break;
        case RS_DIMENSION_FACE: dimFaces = vals[ct]; break;

        default:
            LOGE("rsaTypeCreate: Bad dimension");
            rsAssert(0);
        }
    }

    ObjectBase::lockUserRef();
    for (uint32_t ct=0; ct < stc->mTypes.size(); ct++) {
        Type *t = stc->mTypes[ct];
        if (t->getElement() != e) continue;
        if (t->getDimX() != dimX) continue;
        if (t->getDimY() != dimY) continue;
        if (t->getDimZ() != dimZ) continue;
        if (t->getDimLOD() != dimLOD) continue;
        if (t->getDimFaces() != dimFaces) continue;
        t->prelockedIncUserRef();
        ObjectBase::unlockUserRef();
        return t;
    }
    ObjectBase::unlockUserRef();

    Type * st = new Type(rsc);
    st->incUserRef();
    st->setDimX(dimX);
    st->setDimY(dimY);
    st->setDimZ(dimZ);
    st->setElement(e);
    st->setDimLOD(dimLOD);
    st->setDimFaces(dimFaces);
    st->compute();

    ObjectBase::lockUserRef();
    stc->mTypes.push(st);
    ObjectBase::unlockUserRef();
    return st;
}

