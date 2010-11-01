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

#include "rsContext.h"
#include "rsScriptC.h"
#include "rsMatrix.h"

#include "utils/Timers.h"

#include <time.h>

using namespace android;
using namespace android::renderscript;

#define GET_TLS()  Context::ScriptTLSStruct * tls = \
    (Context::ScriptTLSStruct *)pthread_getspecific(Context::gThreadTLSKey); \
    Context * rsc = tls->mContext; \
    ScriptC * sc = (ScriptC *) tls->mScript


//////////////////////////////////////////////////////////////////////////////
// Math routines
//////////////////////////////////////////////////////////////////////////////

static float SC_sinf_fast(float x)
{
    const float A =   1.0f / (2.0f * M_PI);
    const float B = -16.0f;
    const float C =   8.0f;

    // scale angle for easy argument reduction
    x *= A;

    if (fabsf(x) >= 0.5f) {
        // argument reduction
        x = x - ceilf(x + 0.5f) + 1.0f;
    }

    const float y = B * x * fabsf(x) + C * x;
    return 0.2215f * (y * fabsf(y) - y) + y;
}

static float SC_cosf_fast(float x)
{
    x += float(M_PI / 2);

    const float A =   1.0f / (2.0f * M_PI);
    const float B = -16.0f;
    const float C =   8.0f;

    // scale angle for easy argument reduction
    x *= A;

    if (fabsf(x) >= 0.5f) {
        // argument reduction
        x = x - ceilf(x + 0.5f) + 1.0f;
    }

    const float y = B * x * fabsf(x) + C * x;
    return 0.2215f * (y * fabsf(y) - y) + y;
}


static float SC_randf(float max)
{
    float r = (float)rand();
    return r / RAND_MAX * max;
}

static float SC_randf2(float min, float max)
{
    float r = (float)rand();
    return r / RAND_MAX * (max - min) + min;
}

static int SC_randi(int max)
{
    return (int)SC_randf(max);
}

static int SC_randi2(int min, int max)
{
    return (int)SC_randf2(min, max);
}

static float SC_frac(float v)
{
    int i = (int)floor(v);
    return fmin(v - i, 0x1.fffffep-1f);
}

//////////////////////////////////////////////////////////////////////////////
// Time routines
//////////////////////////////////////////////////////////////////////////////

static int32_t SC_second()
{
    GET_TLS();

    time_t rawtime;
    time(&rawtime);

    struct tm *timeinfo;
    timeinfo = localtime(&rawtime);
    return timeinfo->tm_sec;
}

static int32_t SC_minute()
{
    GET_TLS();

    time_t rawtime;
    time(&rawtime);

    struct tm *timeinfo;
    timeinfo = localtime(&rawtime);
    return timeinfo->tm_min;
}

static int32_t SC_hour()
{
    GET_TLS();

    time_t rawtime;
    time(&rawtime);

    struct tm *timeinfo;
    timeinfo = localtime(&rawtime);
    return timeinfo->tm_hour;
}

static int32_t SC_day()
{
    GET_TLS();

    time_t rawtime;
    time(&rawtime);

    struct tm *timeinfo;
    timeinfo = localtime(&rawtime);
    return timeinfo->tm_mday;
}

static int32_t SC_month()
{
    GET_TLS();

    time_t rawtime;
    time(&rawtime);

    struct tm *timeinfo;
    timeinfo = localtime(&rawtime);
    return timeinfo->tm_mon;
}

static int32_t SC_year()
{
    GET_TLS();

    time_t rawtime;
    time(&rawtime);

    struct tm *timeinfo;
    timeinfo = localtime(&rawtime);
    return timeinfo->tm_year;
}

static int64_t SC_uptimeMillis()
{
    return nanoseconds_to_milliseconds(systemTime(SYSTEM_TIME_MONOTONIC));
}

static int64_t SC_uptimeNanos()
{
    return systemTime(SYSTEM_TIME_MONOTONIC);
}

static float SC_getDt()
{
    GET_TLS();
    int64_t l = sc->mEnviroment.mLastDtTime;
    sc->mEnviroment.mLastDtTime = systemTime(SYSTEM_TIME_MONOTONIC);
    return ((float)(sc->mEnviroment.mLastDtTime - l)) / 1.0e9;
}


//////////////////////////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////////////////////////

static uint32_t SC_allocGetDimX(RsAllocation va)
{
    const Allocation *a = static_cast<const Allocation *>(va);
    CHECK_OBJ(a);
    //LOGE("SC_allocGetDimX a=%p  type=%p", a, a->getType());
    return a->getType()->getDimX();
}

static uint32_t SC_allocGetDimY(RsAllocation va)
{
    const Allocation *a = static_cast<const Allocation *>(va);
    CHECK_OBJ(a);
    return a->getType()->getDimY();
}

static uint32_t SC_allocGetDimZ(RsAllocation va)
{
    const Allocation *a = static_cast<const Allocation *>(va);
    CHECK_OBJ(a);
    return a->getType()->getDimZ();
}

static uint32_t SC_allocGetDimLOD(RsAllocation va)
{
    const Allocation *a = static_cast<const Allocation *>(va);
    CHECK_OBJ(a);
    return a->getType()->getDimLOD();
}

static uint32_t SC_allocGetDimFaces(RsAllocation va)
{
    const Allocation *a = static_cast<const Allocation *>(va);
    CHECK_OBJ(a);
    return a->getType()->getDimFaces();
}

static const void * SC_getElementAtX(RsAllocation va, uint32_t x)
{
    const Allocation *a = static_cast<const Allocation *>(va);
    CHECK_OBJ(a);
    const Type *t = a->getType();
    CHECK_OBJ(t);
    const uint8_t *p = (const uint8_t *)a->getPtr();
    return &p[t->getElementSizeBytes() * x];
}

static const void * SC_getElementAtXY(RsAllocation va, uint32_t x, uint32_t y)
{
    const Allocation *a = static_cast<const Allocation *>(va);
    CHECK_OBJ(a);
    const Type *t = a->getType();
    CHECK_OBJ(t);
    const uint8_t *p = (const uint8_t *)a->getPtr();
    return &p[t->getElementSizeBytes() * (x + y*t->getDimX())];
}

static const void * SC_getElementAtXYZ(RsAllocation va, uint32_t x, uint32_t y, uint32_t z)
{
    const Allocation *a = static_cast<const Allocation *>(va);
    CHECK_OBJ(a);
    const Type *t = a->getType();
    CHECK_OBJ(t);
    const uint8_t *p = (const uint8_t *)a->getPtr();
    return &p[t->getElementSizeBytes() * (x + y*t->getDimX())];
}

static void SC_setObject(void **vdst, void * vsrc) {
    //LOGE("SC_setObject  %p,%p  %p", vdst, *vdst, vsrc);
    if (vsrc) {
        CHECK_OBJ(vsrc);
        static_cast<ObjectBase *>(vsrc)->incSysRef();
    }
    if (vdst[0]) {
        CHECK_OBJ(vdst[0]);
        static_cast<ObjectBase *>(vdst[0])->decSysRef();
    }
    *vdst = vsrc;
    //LOGE("SC_setObject *");
}
static void SC_clearObject(void **vdst) {
    //LOGE("SC_clearObject  %p,%p", vdst, *vdst);
    if (vdst[0]) {
        CHECK_OBJ(vdst[0]);
        static_cast<ObjectBase *>(vdst[0])->decSysRef();
    }
    *vdst = NULL;
    //LOGE("SC_clearObject *");
}
static bool SC_isObject(RsAllocation vsrc) {
    return vsrc != NULL;
}



static void SC_debugF(const char *s, float f) {
    LOGE("%s %f, 0x%08x", s, f, *((int *) (&f)));
}
static void SC_debugFv2(const char *s, float f1, float f2) {
    LOGE("%s {%f, %f}", s, f1, f2);
}
static void SC_debugFv3(const char *s, float f1, float f2, float f3) {
    LOGE("%s {%f, %f, %f}", s, f1, f2, f3);
}
static void SC_debugFv4(const char *s, float f1, float f2, float f3, float f4) {
    LOGE("%s {%f, %f, %f, %f}", s, f1, f2, f3, f4);
}
static void SC_debugD(const char *s, double d) {
    LOGE("%s %f, 0x%08llx", s, d, *((long long *) (&d)));
}
static void SC_debugFM4v4(const char *s, const float *f) {
    LOGE("%s {%f, %f, %f, %f", s, f[0], f[4], f[8], f[12]);
    LOGE("%s  %f, %f, %f, %f", s, f[1], f[5], f[9], f[13]);
    LOGE("%s  %f, %f, %f, %f", s, f[2], f[6], f[10], f[14]);
    LOGE("%s  %f, %f, %f, %f}", s, f[3], f[7], f[11], f[15]);
}
static void SC_debugFM3v3(const char *s, const float *f) {
    LOGE("%s {%f, %f, %f", s, f[0], f[3], f[6]);
    LOGE("%s  %f, %f, %f", s, f[1], f[4], f[7]);
    LOGE("%s  %f, %f, %f}",s, f[2], f[5], f[8]);
}
static void SC_debugFM2v2(const char *s, const float *f) {
    LOGE("%s {%f, %f", s, f[0], f[2]);
    LOGE("%s  %f, %f}",s, f[1], f[3]);
}

static void SC_debugI32(const char *s, int32_t i) {
    LOGE("%s %i  0x%x", s, i, i);
}
static void SC_debugU32(const char *s, uint32_t i) {
    LOGE("%s %u  0x%x", s, i, i);
}
static void SC_debugLL64(const char *s, long long ll) {
    LOGE("%s %lld  0x%llx", s, ll, ll);
}
static void SC_debugULL64(const char *s, unsigned long long ll) {
    LOGE("%s %llu  0x%llx", s, ll, ll);
}

static void SC_debugP(const char *s, const void *p) {
    LOGE("%s %p", s, p);
}

static uint32_t SC_toClient2(int cmdID, void *data, int len)
{
    GET_TLS();
    //LOGE("SC_toClient %i %i %i", cmdID, len);
    return rsc->sendMessageToClient(data, cmdID, len, false);
}

static uint32_t SC_toClient(int cmdID)
{
    GET_TLS();
    //LOGE("SC_toClient %i", cmdID);
    return rsc->sendMessageToClient(NULL, cmdID, 0, false);
}

static uint32_t SC_toClientBlocking2(int cmdID, void *data, int len)
{
    GET_TLS();
    //LOGE("SC_toClientBlocking %i %i", cmdID, len);
    return rsc->sendMessageToClient(data, cmdID, len, true);
}

static uint32_t SC_toClientBlocking(int cmdID)
{
    GET_TLS();
    //LOGE("SC_toClientBlocking %i", cmdID);
    return rsc->sendMessageToClient(NULL, cmdID, 0, true);
}

int SC_divsi3(int a, int b)
{
    return a / b;
}

int SC_getAllocation(const void *ptr)
{
    GET_TLS();
    const Allocation *alloc = sc->ptrToAllocation(ptr);
    return (int)alloc;
}

void SC_allocationMarkDirty(RsAllocation a)
{
    Allocation *alloc = static_cast<Allocation *>(a);
    alloc->sendDirty();
}

void SC_ForEach(RsScript vs,
                RsAllocation vin,
                RsAllocation vout,
                const void *usr)
{
    GET_TLS();
    const Allocation *ain = static_cast<const Allocation *>(vin);
    Allocation *aout = static_cast<Allocation *>(vout);
    Script *s = static_cast<Script *>(vs);
    s->runForEach(rsc, ain, aout, usr);
}

void SC_ForEach2(RsScript vs,
                RsAllocation vin,
                RsAllocation vout,
                const void *usr,
                const RsScriptCall *call)
{
    GET_TLS();
    const Allocation *ain = static_cast<const Allocation *>(vin);
    Allocation *aout = static_cast<Allocation *>(vout);
    Script *s = static_cast<Script *>(vs);
    s->runForEach(rsc, ain, aout, usr, call);
}

//////////////////////////////////////////////////////////////////////////////
// Class implementation
//////////////////////////////////////////////////////////////////////////////

// llvm name mangling ref
//  <builtin-type> ::= v  # void
//                 ::= b  # bool
//                 ::= c  # char
//                 ::= a  # signed char
//                 ::= h  # unsigned char
//                 ::= s  # short
//                 ::= t  # unsigned short
//                 ::= i  # int
//                 ::= j  # unsigned int
//                 ::= l  # long
//                 ::= m  # unsigned long
//                 ::= x  # long long, __int64
//                 ::= y  # unsigned long long, __int64
//                 ::= f  # float
//                 ::= d  # double

static ScriptCState::SymbolTable_t gSyms[] = {
    { "__divsi3", (void *)&SC_divsi3, true },

    // allocation
    { "_Z19rsAllocationGetDimX13rs_allocation", (void *)&SC_allocGetDimX, true },
    { "_Z19rsAllocationGetDimY13rs_allocation", (void *)&SC_allocGetDimY, true },
    { "_Z19rsAllocationGetDimZ13rs_allocation", (void *)&SC_allocGetDimZ, true },
    { "_Z21rsAllocationGetDimLOD13rs_allocation", (void *)&SC_allocGetDimLOD, true },
    { "_Z23rsAllocationGetDimFaces13rs_allocation", (void *)&SC_allocGetDimFaces, true },
    { "_Z15rsGetAllocationPKv", (void *)&SC_getAllocation, true },

    { "_Z14rsGetElementAt13rs_allocationj", (void *)&SC_getElementAtX, true },
    { "_Z14rsGetElementAt13rs_allocationjj", (void *)&SC_getElementAtXY, true },
    { "_Z14rsGetElementAt13rs_allocationjjj", (void *)&SC_getElementAtXYZ, true },

    { "_Z11rsSetObjectP10rs_elementS_", (void *)&SC_setObject, true },
    { "_Z13rsClearObjectP10rs_element", (void *)&SC_clearObject, true },
    { "_Z10rsIsObject10rs_element", (void *)&SC_isObject, true },

    { "_Z11rsSetObjectP7rs_typeS_", (void *)&SC_setObject, true },
    { "_Z13rsClearObjectP7rs_type", (void *)&SC_clearObject, true },
    { "_Z10rsIsObject7rs_type", (void *)&SC_isObject, true },

    { "_Z11rsSetObjectP13rs_allocationS_", (void *)&SC_setObject, true },
    { "_Z13rsClearObjectP13rs_allocation", (void *)&SC_clearObject, true },
    { "_Z10rsIsObject13rs_allocation", (void *)&SC_isObject, true },

    { "_Z11rsSetObjectP10rs_samplerS_", (void *)&SC_setObject, true },
    { "_Z13rsClearObjectP10rs_sampler", (void *)&SC_clearObject, true },
    { "_Z10rsIsObject10rs_sampler", (void *)&SC_isObject, true },

    { "_Z11rsSetObjectP9rs_scriptS_", (void *)&SC_setObject, true },
    { "_Z13rsClearObjectP9rs_script", (void *)&SC_clearObject, true },
    { "_Z10rsIsObject9rs_script", (void *)&SC_isObject, true },

    { "_Z11rsSetObjectP7rs_meshS_", (void *)&SC_setObject, true },
    { "_Z13rsClearObjectP7rs_mesh", (void *)&SC_clearObject, true },
    { "_Z10rsIsObject7rs_mesh", (void *)&SC_isObject, true },

    { "_Z11rsSetObjectP19rs_program_fragmentS_", (void *)&SC_setObject, true },
    { "_Z13rsClearObjectP19rs_program_fragment", (void *)&SC_clearObject, true },
    { "_Z10rsIsObject19rs_program_fragment", (void *)&SC_isObject, true },

    { "_Z11rsSetObjectP17rs_program_vertexS_", (void *)&SC_setObject, true },
    { "_Z13rsClearObjectP17rs_program_vertex", (void *)&SC_clearObject, true },
    { "_Z10rsIsObject17rs_program_vertex", (void *)&SC_isObject, true },

    { "_Z11rsSetObjectP17rs_program_rasterS_", (void *)&SC_setObject, true },
    { "_Z13rsClearObjectP17rs_program_raster", (void *)&SC_clearObject, true },
    { "_Z10rsIsObject17rs_program_raster", (void *)&SC_isObject, true },

    { "_Z11rsSetObjectP16rs_program_storeS_", (void *)&SC_setObject, true },
    { "_Z13rsClearObjectP16rs_program_store", (void *)&SC_clearObject, true },
    { "_Z10rsIsObject16rs_program_store", (void *)&SC_isObject, true },

    { "_Z11rsSetObjectP7rs_fontS_", (void *)&SC_setObject, true },
    { "_Z13rsClearObjectP7rs_font", (void *)&SC_clearObject, true },
    { "_Z10rsIsObject7rs_font", (void *)&SC_isObject, true },


    { "_Z21rsAllocationMarkDirty13rs_allocation", (void *)&SC_allocationMarkDirty, true },


    // Debug
    { "_Z7rsDebugPKcf", (void *)&SC_debugF, true },
    { "_Z7rsDebugPKcff", (void *)&SC_debugFv2, true },
    { "_Z7rsDebugPKcfff", (void *)&SC_debugFv3, true },
    { "_Z7rsDebugPKcffff", (void *)&SC_debugFv4, true },
    { "_Z7rsDebugPKcd", (void *)&SC_debugD, true },
    { "_Z7rsDebugPKcPK12rs_matrix4x4", (void *)&SC_debugFM4v4, true },
    { "_Z7rsDebugPKcPK12rs_matrix3x3", (void *)&SC_debugFM3v3, true },
    { "_Z7rsDebugPKcPK12rs_matrix2x2", (void *)&SC_debugFM2v2, true },
    { "_Z7rsDebugPKci", (void *)&SC_debugI32, true },
    { "_Z7rsDebugPKcj", (void *)&SC_debugU32, true },
    // Both "long" and "unsigned long" need to be redirected to their
    // 64-bit counterparts, since we have hacked Slang to use 64-bit
    // for "long" on Arm (to be similar to Java).
    { "_Z7rsDebugPKcl", (void *)&SC_debugLL64, true },
    { "_Z7rsDebugPKcm", (void *)&SC_debugULL64, true },
    { "_Z7rsDebugPKcx", (void *)&SC_debugLL64, true },
    { "_Z7rsDebugPKcy", (void *)&SC_debugULL64, true },
    { "_Z7rsDebugPKcPKv", (void *)&SC_debugP, true },

    // RS Math
    { "_Z6rsRandi", (void *)&SC_randi, true },
    { "_Z6rsRandii", (void *)&SC_randi2, true },
    { "_Z6rsRandf", (void *)&SC_randf, true },
    { "_Z6rsRandff", (void *)&SC_randf2, true },
    { "_Z6rsFracf", (void *)&SC_frac, true },

    // time
    { "_Z8rsSecondv", (void *)&SC_second, true },
    { "_Z8rsMinutev", (void *)&SC_minute, true },
    { "_Z6rsHourv", (void *)&SC_hour, true },
    { "_Z5rsDayv", (void *)&SC_day, true },
    { "_Z7rsMonthv", (void *)&SC_month, true },
    { "_Z6rsYearv", (void *)&SC_year, true },
    { "_Z14rsUptimeMillisv", (void*)&SC_uptimeMillis, true },
    { "_Z13rsUptimeNanosv", (void*)&SC_uptimeNanos, true },
    { "_Z7rsGetDtv", (void*)&SC_getDt, false },

    { "_Z14rsSendToClienti", (void *)&SC_toClient, false },
    { "_Z14rsSendToClientiPKvj", (void *)&SC_toClient2, false },
    { "_Z22rsSendToClientBlockingi", (void *)&SC_toClientBlocking, false },
    { "_Z22rsSendToClientBlockingiPKvj", (void *)&SC_toClientBlocking2, false },

    { "_Z9rsForEach9rs_script13rs_allocationS0_PKv", (void *)&SC_ForEach, false },
    //{ "_Z9rsForEach9rs_script13rs_allocationS0_PKv", (void *)&SC_ForEach2, true },

////////////////////////////////////////////////////////////////////

    //{ "sinf_fast", (void *)&SC_sinf_fast, true },
    //{ "cosf_fast", (void *)&SC_cosf_fast, true },

    { NULL, NULL, false }
};

const ScriptCState::SymbolTable_t * ScriptCState::lookupSymbol(const char *sym)
{
    ScriptCState::SymbolTable_t *syms = gSyms;

    while (syms->mPtr) {
        if (!strcmp(syms->mName, sym)) {
            return syms;
        }
        syms++;
    }
    return NULL;
}

