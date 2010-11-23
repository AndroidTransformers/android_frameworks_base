
ContextFinish {
	handcodeApi
	}

ContextBindRootScript {
	param RsScript sampler
	}

ContextBindProgramStore {
	param RsProgramStore pgm
	}

ContextBindProgramFragment {
	param RsProgramFragment pgm
	}

ContextBindProgramVertex {
	param RsProgramVertex pgm
	}

ContextBindProgramRaster {
	param RsProgramRaster pgm
	}

ContextBindFont {
	param RsFont pgm
	}

ContextPause {
	}

ContextResume {
	}

ContextSetSurface {
	param uint32_t width
	param uint32_t height
	param ANativeWindow *sur
	}

ContextDump {
	param int32_t bits
}

ContextSetPriority {
	param int32_t priority
	}

AssignName {
	param void *obj
	param const char *name
	param size_t len
	}

ObjDestroy {
	param RsAsyncVoidPtr objPtr
	}

ElementCreate {
	param RsDataType mType
	param RsDataKind mKind
	param bool mNormalized
	param uint32_t mVectorSize
	ret RsElement
	}

ElementCreate2 {
	param size_t count
	param const RsElement * elements
	param const char ** names
	param const size_t * nameLengths
	param const uint32_t * arraySize
	ret RsElement
	}

AllocationUpdateFromBitmap {
	param RsAllocation alloc
	param RsElement srcFmt
	param const void * data
	}

AllocationCreateBitmapRef {
	param RsType type
	param RsAsyncVoidPtr bmpPtr
	param RsAsyncVoidPtr callbackData
	param RsBitmapCallback_t callback
	ret RsAllocation
	}

AllocationUploadToTexture {
	param RsAllocation alloc
	param bool genMipMaps
	param uint32_t baseMipLevel
	}

AllocationUploadToBufferObject {
	param RsAllocation alloc
	}


AllocationData {
	param RsAllocation va
	param const void * data
	param uint32_t bytes
	handcodeApi
	togglePlay
	}

Allocation1DSubData {
	param RsAllocation va
	param uint32_t xoff
	param uint32_t count
	param const void *data
	param uint32_t bytes
	handcodeApi
	togglePlay
	}

Allocation1DSubElementData {
	param RsAllocation va
	param uint32_t x
	param const void *data
	param uint32_t comp_offset
	param uint32_t bytes
	handcodeApi
	togglePlay
	}

Allocation2DSubData {
	param RsAllocation va
	param uint32_t xoff
	param uint32_t yoff
	param uint32_t w
	param uint32_t h
	param const void *data
	param uint32_t bytes
	}

Allocation2DSubElementData {
	param RsAllocation va
	param uint32_t x
	param uint32_t y
	param const void *data
	param uint32_t element_offset
	param uint32_t bytes
	}

AllocationRead {
	param RsAllocation va
	param void * data
	}

Adapter1DCreate {
	ret RsAdapter1D
	}

Adapter1DBindAllocation {
	param RsAdapter1D adapt
	param RsAllocation alloc
	}

Adapter1DSetConstraint {
	param RsAdapter1D adapter
	param RsDimension dim
	param uint32_t value
	}

Adapter1DData {
	param RsAdapter1D adapter
	param const void * data
	}

Adapter1DSubData {
	param RsAdapter1D adapter
	param uint32_t xoff
	param uint32_t count
	param const void *data
	}

Adapter2DCreate {
	ret RsAdapter2D
	}

Adapter2DBindAllocation {
	param RsAdapter2D adapt
	param RsAllocation alloc
	}

Adapter2DSetConstraint {
	param RsAdapter2D adapter
	param RsDimension dim
	param uint32_t value
	}

Adapter2DData {
	param RsAdapter2D adapter
	param const void *data
	}

Adapter2DSubData {
	param RsAdapter2D adapter
	param uint32_t xoff
	param uint32_t yoff
	param uint32_t w
	param uint32_t h
	param const void *data
	}

AllocationResize1D {
	param RsAllocation va
	param uint32_t dimX
	}

AllocationResize2D {
	param RsAllocation va
	param uint32_t dimX
	param uint32_t dimY
	}

SamplerBegin {
	}

SamplerSet {
	param RsSamplerParam p
	param RsSamplerValue value
	}

SamplerSet2 {
	param RsSamplerParam p
	param float value
	}

SamplerCreate {
	ret RsSampler
	}



ScriptBindAllocation {
	param RsScript vtm
	param RsAllocation va
	param uint32_t slot
	}


ScriptCBegin {
	}


ScriptSetTimeZone {
	param RsScript s
	param const char * timeZone
	param uint32_t length
	}


ScriptInvoke {
	param RsScript s
	param uint32_t slot
	}

ScriptInvokeV {
	param RsScript s
	param uint32_t slot
	param const void * data
	param uint32_t dataLen
	handcodeApi
	togglePlay
	}

ScriptSetVarI {
	param RsScript s
	param uint32_t slot
	param int value
	}

ScriptSetVarObj {
	param RsScript s
	param uint32_t slot
	param RsObjectBase value
	}

ScriptSetVarJ {
	param RsScript s
	param uint32_t slot
	param int64_t value
	}

ScriptSetVarF {
	param RsScript s
	param uint32_t slot
	param float value
	}

ScriptSetVarD {
	param RsScript s
	param uint32_t slot
	param double value
	}

ScriptSetVarV {
	param RsScript s
	param uint32_t slot
	param const void * data
	param uint32_t dataLen
	handcodeApi
	togglePlay
	}


ScriptCSetText {
	param const char * text
	param uint32_t length
	}

ScriptCCreate {
        param const char * resName
	ret RsScript
	}


ProgramStoreBegin {
	param RsElement in
	param RsElement out
	}

ProgramStoreColorMask {
	param bool r
	param bool g
	param bool b
	param bool a
	}

ProgramStoreBlendFunc {
	param RsBlendSrcFunc srcFunc
	param RsBlendDstFunc destFunc
	}

ProgramStoreDepthMask {
	param bool enable
}

ProgramStoreDither {
	param bool enable
}

ProgramStoreDepthFunc {
	param RsDepthFunc func
}

ProgramStoreCreate {
	ret RsProgramStore
	}

ProgramRasterCreate {
	param bool pointSmooth
	param bool lineSmooth
	param bool pointSprite
	ret RsProgramRaster
}

ProgramRasterSetLineWidth {
	param RsProgramRaster pr
	param float lw
}

ProgramRasterSetCullMode {
	param RsProgramRaster pr
	param RsCullMode mode
}

ProgramBindConstants {
	param RsProgram vp
	param uint32_t slot
	param RsAllocation constants
	}


ProgramBindTexture {
	param RsProgramFragment pf
	param uint32_t slot
	param RsAllocation a
	}

ProgramBindSampler {
	param RsProgramFragment pf
	param uint32_t slot
	param RsSampler s
	}

ProgramFragmentCreate {
	param const char * shaderText
	param uint32_t shaderLength
	param const uint32_t * params
	param uint32_t paramLength
	ret RsProgramFragment
	}

ProgramVertexCreate {
	param const char * shaderText
	param uint32_t shaderLength
	param const uint32_t * params
	param uint32_t paramLength
	ret RsProgramVertex
	}

FileOpen {
	ret RsFile
	param const char *name
	param size_t len
	}

FontCreateFromFile {
	param const char *name
	param uint32_t fontSize
	param uint32_t dpi
	ret RsFont
	}

MeshCreate {
	ret RsMesh
	param uint32_t vtxCount
	param uint32_t idxCount
	}

MeshBindIndex {
	param RsMesh mesh
	param RsAllocation idx
	param uint32_t primType
	param uint32_t slot
	}

MeshBindVertex {
	param RsMesh mesh
	param RsAllocation vtx
	param uint32_t slot
	}

MeshInitVertexAttribs {
	param RsMesh mesh
	}

AnimationCreate {
	param const float *inValues
	param const float *outValues
	param uint32_t valueCount
	param RsAnimationInterpolation interp
	param RsAnimationEdge pre
	param RsAnimationEdge post
	ret RsAnimation
	}

