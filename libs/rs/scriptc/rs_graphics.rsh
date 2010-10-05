#ifndef __RS_GRAPHICS_RSH__
#define __RS_GRAPHICS_RSH__

#include "rs_math.rsh"


// Bind a ProgramFragment to the RS context.
extern void __attribute__((overloadable))
    rsgBindProgramFragment(rs_program_fragment);
extern void __attribute__((overloadable))
    rsgBindProgramStore(rs_program_store);
extern void __attribute__((overloadable))
    rsgBindProgramVertex(rs_program_vertex);
extern void __attribute__((overloadable))
    rsgBindProgramRaster(rs_program_raster);

extern void __attribute__((overloadable))
    rsgBindSampler(rs_program_fragment, uint slot, rs_sampler);
extern void __attribute__((overloadable))
    rsgBindTexture(rs_program_fragment, uint slot, rs_allocation);

extern void __attribute__((overloadable))
    rsgProgramVertexLoadProjectionMatrix(const rs_matrix4x4 *);
extern void __attribute__((overloadable))
    rsgProgramVertexLoadModelMatrix(const rs_matrix4x4 *);
extern void __attribute__((overloadable))
    rsgProgramVertexLoadTextureMatrix(const rs_matrix4x4 *);

extern void __attribute__((overloadable))
    rsgProgramVertexGetProjectionMatrix(rs_matrix4x4 *);

extern void __attribute__((overloadable))
    rsgProgramFragmentConstantColor(rs_program_fragment, float, float, float, float);

extern uint __attribute__((overloadable))
    rsgGetWidth(void);
extern uint __attribute__((overloadable))
    rsgGetHeight(void);

extern void __attribute__((overloadable))
    rsgUploadToTexture(rs_allocation);
extern void __attribute__((overloadable))
    rsgUploadToTexture(rs_allocation, uint mipLevel);
extern void __attribute__((overloadable))
    rsgUploadToBufferObject(rs_allocation);

extern void __attribute__((overloadable))
    rsgDrawRect(float x1, float y1, float x2, float y2, float z);
extern void __attribute__((overloadable))
    rsgDrawQuad(float x1, float y1, float z1,
                float x2, float y2, float z2,
                float x3, float y3, float z3,
                float x4, float y4, float z4);
extern void __attribute__((overloadable))
    rsgDrawQuadTexCoords(float x1, float y1, float z1, float u1, float v1,
                         float x2, float y2, float z2, float u2, float v2,
                         float x3, float y3, float z3, float u3, float v3,
                         float x4, float y4, float z4, float u4, float v4);
extern void __attribute__((overloadable))
    rsgDrawSpriteScreenspace(float x, float y, float z, float w, float h);

extern void __attribute__((overloadable))
    rsgDrawMesh(rs_mesh ism);
extern void __attribute__((overloadable))
    rsgDrawMesh(rs_mesh ism, uint primitiveIndex);
extern void __attribute__((overloadable))
    rsgDrawMesh(rs_mesh ism, uint primitiveIndex, uint start, uint len);

extern void __attribute__((overloadable))
    rsgClearColor(float, float, float, float);
extern void __attribute__((overloadable))
    rsgClearDepth(float);

extern void __attribute__((overloadable))
    rsgDrawText(const char *, int x, int y);
extern void __attribute__((overloadable))
    rsgDrawText(rs_allocation, int x, int y);
extern void __attribute__((overloadable))
    rsgBindFont(rs_font);
extern void __attribute__((overloadable))
    rsgFontColor(float, float, float, float);
// Returns the bounding box of the text relative to (0, 0)
// Any of left, right, top, bottom could be NULL
extern void __attribute__((overloadable))
    rsgMeasureText(const char *, int *left, int *right, int *top, int *bottom);
extern void __attribute__((overloadable))
    rsgMeasureText(rs_allocation, int *left, int *right, int *top, int *bottom);

extern void __attribute__((overloadable))
    rsgMeshComputeBoundingBox(rs_mesh mesh, float *minX, float *minY, float *minZ,
                                                float *maxX, float *maxY, float *maxZ);
void __attribute__((overloadable))
rsgMeshComputeBoundingBox(rs_mesh mesh, float3 *bBoxMin, float3 *bBoxMax) {
    float x1, y1, z1, x2, y2, z2;
    rsgMeshComputeBoundingBox(mesh, &x1, &y1, &z1, &x2, &y2, &z2);
    bBoxMin->x = x1;
    bBoxMin->y = y1;
    bBoxMin->z = z1;
    bBoxMax->x = x2;
    bBoxMax->y = y2;
    bBoxMax->z = z2;
}

///////////////////////////////////////////////////////
// misc

// Depricated
extern void __attribute__((overloadable))
    color(float, float, float, float);

#endif

