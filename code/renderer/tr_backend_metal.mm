#include "tr_local.h"

#import <Metal/Metal.h>
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

#include "../macosx/macosx_public.h"

// @NOTE: MAX_VERTEXES and MAX_INDEXES are *per frame*
#define LARGEBUFFER_MAX_FRAMES   4
#define LARGEBUFFER_MAX_VERTEXES 131072
#define LARGEBUFFER_MAX_INDEXES  (LARGEBUFFER_MAX_VERTEXES * 8)

// this is the highest maximum we'll ever report
#define MAX_GPU_TEXTURE_SIZE 2048

#define SRC_BLEND_COUNT   9
#define DST_BLEND_COUNT   8
#define PIPELINE_COUNT    (SRC_BLEND_COUNT * DST_BLEND_COUNT + 1) + 2
#define FRAME_COUNT       2
#define SAMPLER_COUNT     TW_COUNT * 3
#define DEPTH_STATE_COUNT (MTLCompareFunctionAlways + 1) * 2


#define MAX_DRAWS_PER_FRAME 1000

#define MTL_DEBUG 1

enum VertexBufferId
{
	VB_POSITION,
	VB_COLOR,
	VB_TEXCOORD,
	VB_TEXCOORD2,
	VB_NORMAL,
	VB_COUNT
};

enum TextureMode
{
	TM_BILINEAR,
	TM_ANISOTROPIC,
	TM_NEAREST,
	TM_COUNT
};

enum PipelineId
{
	PID_GENERIC,
	PID_SOFT_SPRITE,
	PID_DYNAMIC_LIGHT,
	PID_POST_PROCESS,
	PID_SCREENSHOT,
	PID_CLEAR,
	PID_COUNT
};

enum AlphaTest
{
	AT_ALWAYS,
	AT_GREATER_THAN_0,
	AT_LESS_THAN_HALF,
	AT_GREATER_OR_EQUAL_TO_HALF
};

struct Buffer
{
	// ft_descriptor_type descriptor_type;
	id<MTLBuffer> buffer;
	uint32_t capacity;
	uint32_t item_size;
	byte* mapped_data;
	const char* name;
	size_t read_offset;
	size_t write_offset;
};

struct RenderArea
{
	int32_t x;
	int32_t y;
	int32_t w;
	int32_t h;
	int32_t th;
	float znear;
	float zfar;
};

struct GenericVSData
{
	float model_view_matrix[16];
	float projection_matrix[16];
	float clip_plane[4];
};

struct DLVSData
{
	float model_view_matrix[16];
	float projection_matrix[16];
	float clip_plane[4];
	float os_light_pos[4];
	float os_eye_pos[4];
};

struct DLPushConstant
{
	float light_radius;
	float opaque;
	float intensity;
	float greyscale;
	float light_color[3];
	uint32_t ubo_index;
};

struct Shader
{
	id<MTLLibrary> library;
	id<MTLFunction> vertex;
	id<MTLFunction> fragment;
};

struct Frame
{
	id<MTLCommandQueue> queue;
	id<MTLCommandBuffer> cmd;
	id<MTLRenderCommandEncoder> encoder;
	id<MTLTexture> color_image;
	id<MTLTexture> depth_image;

	Buffer vertex_buffers[VB_COUNT];
	Buffer index_buffer;

	Buffer generic_ubo;
	Buffer dl_ubo;
};

struct Pipeline
{
	uint32_t pipeline_index;
	uint32_t depth_state_index;
	uint32_t cull_mode;
	uint32_t polygon_offset;
};

struct MetalStatic
{
	CAMetalLayer* layer;
	id<MTLDevice> device;
	uint32_t pipeline_count;
	id<MTLRenderPipelineState> pipelines[PIPELINE_COUNT];
	id<MTLSamplerState> samplers[SAMPLER_COUNT];
	id<MTLDepthStencilState> depth_states[DEPTH_STATE_COUNT];
};

struct Metal
{
	struct Frame frames[FRAME_COUNT];
	uint32_t current_frame_index;
	id<CAMetalDrawable> drawable;
	uint32_t texture_count;
	id<MTLTexture> textures[MAX_DRAWIMAGES];
	AlphaTest alpha_test;
	float frame_seed;
	RenderArea render_area;
	GenericVSData generic_vs_data;
	DLVSData dl_vs_data;
	DLPushConstant dl_pc;
	struct Pipeline current_pipeline;
};

static MetalStatic mtls;
static Metal mtl;

static inline MTLPixelFormat to_mtl_format(textureFormat_t f)
{
	switch (f)
	{
	case TF_RGBA8:
	default: return MTLPixelFormatRGBA8Unorm;
	}
}

static inline id<MTLTexture> create_image(MTLPixelFormat format)
{
	@autoreleasepool
	{
		MTLTextureDescriptor* info = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:glConfig.vidWidth height:glConfig.vidHeight mipmapped:NO];
		info.sampleCount = 1;
		info.storageMode = MTLStorageModePrivate;
		info.textureType = MTLTextureType2D;
		info.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
		id<MTLTexture> image = [mtls.device newTextureWithDescriptor:info];
		info = nil;
		return image;
	}
}

static inline id<MTLTexture> create_image2(image_t* _image, int mip_count, int w, int h)
{
	@autoreleasepool
	{
		MTLTextureDescriptor* info = [MTLTextureDescriptor new];
		info.arrayLength = 1;
		info.storageMode = MTLStorageModeShared;
		info.usage = MTLTextureUsageShaderRead;
		info.pixelFormat = to_mtl_format(_image->format);
		info.width = w;
		info.height = h;
		info.depth = 1;
		info.mipmapLevelCount = mip_count;
		info.sampleCount = 1;
		info.textureType = MTLTextureType2D;

		id<MTLTexture> image = [mtls.device newTextureWithDescriptor:info];
		info = nil;
		return image;
	}
}

static inline void update_generic_ubo()
{
	@autoreleasepool
	{
		Frame* f = &mtl.frames[mtl.current_frame_index];
		assert(f->generic_ubo.write_offset + sizeof(GenericVSData) <= MAX_DRAWS_PER_FRAME * sizeof(GenericVSData));
		memcpy(f->generic_ubo.mapped_data + f->generic_ubo.write_offset, &mtl.generic_vs_data, sizeof(GenericVSData));
		f->generic_ubo.read_offset = f->generic_ubo.write_offset;
		f->generic_ubo.write_offset += sizeof(GenericVSData);
	}
}

static inline void upload_geometry(VertexBufferId id, const void* data)
{
	@autoreleasepool
	{
		struct Frame* f = &mtl.frames[mtl.current_frame_index];
		Buffer* buffer = &f->vertex_buffers[id];
		memcpy(buffer->mapped_data + buffer->write_offset, data, tess.numVertexes * buffer->item_size);
		[f->encoder setVertexBufferOffset:buffer->write_offset atIndex:id];
		buffer->read_offset = buffer->write_offset;
		buffer->write_offset += tess.numVertexes * buffer->item_size;
	}
}

static inline void upload_indices(const void* data, uint32_t index_count)
{
	@autoreleasepool
	{
		struct Frame* f = &mtl.frames[mtl.current_frame_index];
		Buffer* buffer = &f->index_buffer;
		memcpy(buffer->mapped_data + buffer->write_offset * buffer->item_size, data, tess.numIndexes * buffer->item_size);
		buffer->read_offset = buffer->write_offset * buffer->item_size;
		buffer->write_offset += index_count;
	}
}

static inline void draw_elements(int index_count)
{
	@autoreleasepool
	{
		struct Frame* f = &mtl.frames[mtl.current_frame_index];

		[f->encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:index_count indexType:MTLIndexTypeUInt32 indexBuffer:f->index_buffer.buffer indexBufferOffset:f->index_buffer.read_offset];
		backEnd.pc3D[RB_DRAW_CALLS]++;
	}
}

static inline void set_viewport_and_scissor(int x, int y, int w, int h, int th)
{
	@autoreleasepool
	{
		struct Frame* f = &mtl.frames[mtl.current_frame_index];

		MTLViewport viewport;
		viewport.originX = x;
		viewport.originY = th - y - h;
		viewport.width = w;
		viewport.height = h;
		viewport.znear = mtl.render_area.znear;
		viewport.zfar = mtl.render_area.zfar;

		MTLScissorRect scissor;
		scissor.x = x;
		scissor.y = th - y - h;
		scissor.width = x + w;
		scissor.height = th - y;

		[f->encoder setViewport:viewport];
		[f->encoder setScissorRect:scissor];
	}
}

static inline AlphaTest get_alpha_test(unsigned int bits)
{
	switch (bits & GLS_ATEST_BITS)
	{
	case GLS_ATEST_GT_0: return AT_GREATER_THAN_0;
	case GLS_ATEST_LT_80: return AT_LESS_THAN_HALF;
	case GLS_ATEST_GE_80: return AT_GREATER_OR_EQUAL_TO_HALF;
	default: return AT_ALWAYS;
	}
}

static inline void bind_image(uint32_t idx, const image_t* image)
{
	@autoreleasepool
	{
		struct Frame* f = &mtl.frames[mtl.current_frame_index];

		TextureMode mode = TM_ANISOTROPIC;
		if (Q_stricmp(r_textureMode->string, "GL_NEAREST") == 0 &&
		    !backEnd.projection2D &&
		    (image->flags & (IMG_LMATLAS | IMG_EXTLMATLAS | IMG_NOPICMIP)) == 0)
		{
			mode = TM_NEAREST;
		}
		else if ((image->flags & IMG_NOAF) != 0)
		{
			mode = TM_BILINEAR;
		}

		[f->encoder setFragmentSamplerState:mtls.samplers[mode + image->wrapClampMode * TM_COUNT] atIndex:idx];
		[f->encoder setFragmentTexture:mtl.textures[image->texnum] atIndex:idx];
	}
}

static void gal_update_texture(image_t* image, int mip, int x, int y, int w, int h, const void* data);

static void update_animated_image(image_t* image, int w, int h, const byte* data, qbool dirty)
{
	@autoreleasepool
	{
		if (w != image->width || h != image->height)
		{
			image->width = w;
			image->height = h;
			mtl.textures[image->texnum] = create_image2(image, 1, w, h);
			gal_update_texture(image, 0, 0, 0, w, h, data);
		}
		else if (dirty)
		{
			gal_update_texture(image, 0, 0, 0, w, h, data);
		}
	}
}

static inline const image_t* get_bundle_image(const textureBundle_t* bundle)
{
	return R_UpdateAndGetBundleImage(bundle, &update_animated_image);
}

static inline void init_gl_config(void)
{
	Q_strncpyz(glConfig.vendor_string, "Apple", sizeof(glConfig.vendor_string));
	Q_strncpyz(glConfig.renderer_string, "Metal", sizeof(glConfig.renderer_string));
	Q_strncpyz(glConfig.version_string, "2", sizeof(glConfig.version_string));
	Q_strncpyz(glConfig.extensions_string, "", sizeof(glConfig.extensions_string));
	glConfig.unused_maxTextureSize = MAX_GPU_TEXTURE_SIZE;
	glConfig.unused_maxActiveTextures = 0;
	glConfig.unused_driverType = 0;
	glConfig.unused_hardwareType = 0;
	glConfig.unused_deviceSupportsGamma = qtrue;
	glConfig.unused_textureCompression = 0;
	glConfig.unused_textureEnvAddAvailable = qtrue;
	glConfig.unused_displayFrequency = 0;
	glConfig.unused_isFullscreen = !!r_fullscreen->integer;
	glConfig.unused_stereoEnabled = qfalse;
	glConfig.unused_smpActive = qfalse;

	glInfo.maxTextureSize = MAX_GPU_TEXTURE_SIZE;
	glInfo.maxAnisotropy = 16.0f;
	glInfo.depthFadeSupport = qfalse;
	glInfo.mipGenSupport = qfalse;
	glInfo.alphaToCoverageSupport = qfalse;

	glConfig.unused_maxTextureSize = MAX_GPU_TEXTURE_SIZE;
	glInfo.maxTextureSize = MAX_GPU_TEXTURE_SIZE;
	glInfo.depthFadeSupport = r_depthFade->integer == 1;
}

static inline MTLVertexDescriptor* get_vertex_descriptor(PipelineId id)
{
	MTLVertexDescriptor* info = [MTLVertexDescriptor new];
	switch (id)
	{
	case PID_GENERIC:
	{
		info.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
		info.layouts[0].stride = sizeof(tess.xyz[0]);
		info.layouts[1].stepFunction = MTLVertexStepFunctionPerVertex;
		info.layouts[1].stride = sizeof(tess.svars[0].colors[0]);
		info.layouts[2].stepFunction = MTLVertexStepFunctionPerVertex;
		info.layouts[2].stride = sizeof(tess.svars[0].texcoords[0]);
		info.layouts[3].stepFunction = MTLVertexStepFunctionPerVertex;
		info.layouts[3].stride = sizeof(tess.svars[0].texcoords[0]);

		info.attributes[0].format = MTLVertexFormatFloat4;
		info.attributes[0].offset = 0;
		info.attributes[0].bufferIndex = 0;
		info.attributes[1].format = MTLVertexFormatUChar4Normalized;
		info.attributes[1].offset = 0;
		info.attributes[1].bufferIndex = 1;
		info.attributes[2].format = MTLVertexFormatFloat2;
		info.attributes[2].offset = 0;
		info.attributes[2].bufferIndex = 2;
		info.attributes[3].format = MTLVertexFormatFloat2;
		info.attributes[3].offset = 0;
		info.attributes[3].bufferIndex = 3;

		break;
	}
	case PID_DYNAMIC_LIGHT:
	{
		info.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
		info.layouts[0].stride = sizeof(tess.xyz[0]);
		info.layouts[1].stepFunction = MTLVertexStepFunctionPerVertex;
		info.layouts[1].stride = sizeof(tess.svars[0].colors[0]);
		info.layouts[2].stepFunction = MTLVertexStepFunctionPerVertex;
		info.layouts[2].stride = sizeof(tess.svars[0].texcoords[0]);
		info.layouts[3].stepFunction = MTLVertexStepFunctionPerVertex;
		info.layouts[3].stride = sizeof(tess.svars[0].texcoords[0]);
		info.layouts[4].stepFunction = MTLVertexStepFunctionPerVertex;
		info.layouts[4].stride = sizeof(tess.normal[0]);

		info.attributes[0].format = MTLVertexFormatFloat4;
		info.attributes[0].offset = 0;
		info.attributes[0].bufferIndex = 0;
		info.attributes[1].format = MTLVertexFormatUChar4Normalized;
		info.attributes[1].offset = 0;
		info.attributes[1].bufferIndex = 1;
		info.attributes[2].format = MTLVertexFormatFloat2;
		info.attributes[2].offset = 0;
		info.attributes[2].bufferIndex = 2;
		info.attributes[3].format = MTLVertexFormatFloat2;
		info.attributes[3].offset = 0;
		info.attributes[3].bufferIndex = 3;
		info.attributes[4].format = MTLVertexFormatFloat4;
		info.attributes[4].offset = 0;
		info.attributes[4].bufferIndex = 4;
		break;
	}
	default:
	{
		break;
	}
	}
	return info;
}

static inline MTLBlendFactor to_mtl_blend_factor(uint32_t blend_bits)
{
	switch (blend_bits)
	{
	case GLS_SRCBLEND_ZERO:
	case GLS_DSTBLEND_ZERO:
		return MTLBlendFactorZero;
	case GLS_SRCBLEND_ONE:
	case GLS_DSTBLEND_ONE:
		return MTLBlendFactorOne;
	case GLS_SRCBLEND_DST_COLOR:
		return MTLBlendFactorDestinationColor;
	case GLS_DSTBLEND_SRC_COLOR:
		return MTLBlendFactorSourceColor;
	case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
		return MTLBlendFactorOneMinusDestinationColor;
	case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
		return MTLBlendFactorOneMinusSourceColor;
	case GLS_SRCBLEND_SRC_ALPHA:
	case GLS_DSTBLEND_SRC_ALPHA:
		return MTLBlendFactorSourceAlpha;
	case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:
	case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:
		return MTLBlendFactorOneMinusSourceAlpha;
	case GLS_SRCBLEND_DST_ALPHA:
	case GLS_DSTBLEND_DST_ALPHA:
		return MTLBlendFactorDestinationAlpha;
	case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
	case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
		return MTLBlendFactorOneMinusDestinationAlpha;
	case GLS_SRCBLEND_ALPHA_SATURATE:
		return MTLBlendFactorSourceAlphaSaturated;
	}
	assert(false);
	return -1;
}

static inline void create_pipeline(Shader* shader, PipelineId id, uint32_t state_bits)
{
	@autoreleasepool
	{
		NSError* error = nil;

		MTLRenderPipelineDescriptor* info = [[MTLRenderPipelineDescriptor alloc] init];
		info.vertexFunction = shader->vertex;
		info.fragmentFunction = shader->fragment;
		info.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle;
		info.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;

		info.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

		if (id == PID_POST_PROCESS)
		{
			info.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm; // FIXME
			info.depthAttachmentPixelFormat = MTLPixelFormatInvalid;
		}

		MTLRenderPipelineColorAttachmentDescriptor* att = info.colorAttachments[0];
		att.blendingEnabled = NO;

		if (state_bits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS))
		{
			MTLBlendFactor src_factor = to_mtl_blend_factor(state_bits & GLS_SRCBLEND_BITS);
			MTLBlendFactor dst_factor = to_mtl_blend_factor(state_bits & GLS_DSTBLEND_BITS);
			att.blendingEnabled = YES;
			att.sourceRGBBlendFactor = src_factor;
			att.sourceAlphaBlendFactor = src_factor;
			att.destinationRGBBlendFactor = dst_factor;
			att.destinationAlphaBlendFactor = dst_factor;
			att.rgbBlendOperation = MTLBlendOperationAdd;
			att.alphaBlendOperation = MTLBlendOperationAdd;
			att.writeMask = MTLColorWriteMaskAll;
		}

		info.sampleCount = 1;
		info.vertexDescriptor = get_vertex_descriptor(id);

		mtls.pipelines[mtls.pipeline_count++] = [mtls.device
		    newRenderPipelineStateWithDescriptor:info
		                                   error:&error];
	}
}

static inline void create_shader(NSString* path, NSString* ext, struct Shader* shader)
{
	@autoreleasepool
	{
		NSError* library_error = NULL;
		NSString* library_file = [[NSBundle mainBundle] pathForResource:path ofType:ext];
		shader->library = [mtls.device newLibraryWithFile:library_file error:&library_error];
		if (!shader->library)
		{
			NSLog(@"Library error: %@", library_error.localizedDescription);
		}
		shader->vertex = [shader->library newFunctionWithName:@"v_main"];
		shader->fragment = [shader->library newFunctionWithName:@"f_main"];
	}
}

static inline uint32_t get_src_blend_idx(uint32_t blend_bit)
{
	switch (blend_bit)
	{
	case GLS_SRCBLEND_ZERO:
		return 0;
	case GLS_SRCBLEND_ONE:
		return 1;
	case GLS_SRCBLEND_DST_COLOR:
		return 2;
	case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
		return 3;
	case GLS_SRCBLEND_SRC_ALPHA: return 4;
	case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA: return 5;
	case GLS_SRCBLEND_DST_ALPHA: return 6;
	case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA: return 7;
	case GLS_SRCBLEND_ALPHA_SATURATE: return 8;
	}
	return 0;
}

static inline uint32_t get_dst_blend_idx(uint32_t blend_bit)
{
	switch (blend_bit)
	{
	case GLS_DSTBLEND_ZERO:
		return 0;
	case GLS_DSTBLEND_ONE:
		return 1;
	case GLS_DSTBLEND_SRC_COLOR:
		return 2;
	case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
		return 3;
	case GLS_DSTBLEND_SRC_ALPHA: return 4;
	case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA: return 5;
	case GLS_DSTBLEND_DST_ALPHA: return 6;
	case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA: return 7;
	}
	return 0;
}

static inline void create_pipelines(void)
{
	@autoreleasepool
	{
		Shader shader;
		create_shader(@"generic", @"metallib", &shader);

		uint32_t src_blend_modes[SRC_BLEND_COUNT] = {
			GLS_SRCBLEND_ZERO,
			GLS_SRCBLEND_ONE,
			GLS_SRCBLEND_DST_COLOR,
			GLS_SRCBLEND_ONE_MINUS_DST_COLOR,
			GLS_SRCBLEND_SRC_ALPHA,
			GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA,
			GLS_SRCBLEND_DST_ALPHA,
			GLS_SRCBLEND_ONE_MINUS_DST_ALPHA,
			GLS_SRCBLEND_ALPHA_SATURATE,
		};

		uint32_t dst_blend_modes[DST_BLEND_COUNT] = {
			GLS_DSTBLEND_ZERO,
			GLS_DSTBLEND_ONE,
			GLS_DSTBLEND_SRC_COLOR,
			GLS_DSTBLEND_ONE_MINUS_SRC_COLOR,
			GLS_DSTBLEND_SRC_ALPHA,
			GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA,
			GLS_DSTBLEND_DST_ALPHA,
			GLS_DSTBLEND_ONE_MINUS_DST_ALPHA,
		};

		create_pipeline(&shader, PID_GENERIC, 0);
		for (uint32_t src = 0; src < SRC_BLEND_COUNT; ++src)
		{
			for (uint32_t dst = 0; dst < DST_BLEND_COUNT; ++dst)
			{
				create_pipeline(&shader, PID_GENERIC, src_blend_modes[src] | dst_blend_modes[dst]);
			}
		}

		shader.vertex = nil;
		shader.fragment = nil;

		create_shader(@"dl", @"metallib", &shader);
		create_pipeline(&shader, PID_DYNAMIC_LIGHT, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
		shader.vertex = nil;
		shader.fragment = nil;

		create_shader(@"post", @"metallib", &shader);
		create_pipeline(&shader, PID_POST_PROCESS, 0);
		shader.vertex = nil;
		shader.fragment = nil;
	}
}

static inline uint32_t get_pipeline_index(PipelineId id, uint32_t state_bits)
{
	switch (id)
	{
	case PID_GENERIC:
	{
		return (get_dst_blend_idx(state_bits & GLS_DSTBLEND_BITS) + get_src_blend_idx(state_bits & GLS_SRCBLEND_BITS) * DST_BLEND_COUNT) + ((state_bits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) != 0);
	}
	case PID_DYNAMIC_LIGHT:
	{
		assert((state_bits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) == (GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE));
		return SRC_BLEND_COUNT * DST_BLEND_COUNT + 1;
	}
	case PID_POST_PROCESS:
	{
		return SRC_BLEND_COUNT * DST_BLEND_COUNT + 2;
	}
	default:
		break;
	}

	assert(false);
	return -1;
}

static inline void create_depth_states(void)
{
	@autoreleasepool
	{
#define CMP_FUNC_COUNT MTLCompareFunctionAlways + 1
		for (uint32_t function = 0; function < CMP_FUNC_COUNT; ++function)
		{
			for (uint32_t write = 0; write < 2; write++)
			{
				MTLDepthStencilDescriptor* info = [MTLDepthStencilDescriptor new];
				info.depthCompareFunction = MTLCompareFunctionLessEqual;
				info.depthWriteEnabled = uint32_t(write);
				mtls.depth_states[write + function * 2] = [mtls.device newDepthStencilStateWithDescriptor:info];
			}
		}
	}
}

static inline void init_frame(struct Frame* f)
{
	@autoreleasepool
	{
		f->queue = [mtls.device newCommandQueue];

		f->color_image = create_image(MTLPixelFormatRGBA8Unorm);
		f->depth_image = create_image(MTLPixelFormatDepth32Float);

		struct Buffer* vb = f->vertex_buffers;
		vb[VB_POSITION].item_size = sizeof(tess.xyz[0]);
		vb[VB_NORMAL].item_size = sizeof(tess.normal[0]);
		vb[VB_TEXCOORD].item_size = sizeof(tess.svars[0].texcoords[0]);
		vb[VB_TEXCOORD2].item_size = sizeof(tess.svars[0].texcoords[0]);
		vb[VB_COLOR].item_size = sizeof(tess.svars[0].colors[0]);

		for (uint32_t i = 0; i < ARRAY_LEN(f->vertex_buffers); ++i)
		{
			vb[i].capacity = LARGEBUFFER_MAX_VERTEXES;
			vb[i].buffer = [mtls.device newBufferWithLength:vb[i].capacity * vb[i].item_size
			                                        options:MTLResourceCPUCacheModeDefaultCache];
			vb[i].mapped_data = (byte*)([vb[i].buffer contents]);
			assert(vb[i].mapped_data != NULL);
		}

		struct Buffer* ib = &f->index_buffer;
		ib->item_size = sizeof(tess.indexes[0]);
		ib->capacity = LARGEBUFFER_MAX_INDEXES;
		ib->buffer = [mtls.device newBufferWithLength:ib->capacity * ib->item_size
		                                      options:MTLResourceCPUCacheModeDefaultCache];
		ib->mapped_data = (byte*)([ib->buffer contents]);
		assert(ib->mapped_data != NULL);

		struct Buffer* ubo = &f->generic_ubo;
		ubo->item_size = sizeof(GenericVSData);
		ubo->capacity = MAX_DRAWS_PER_FRAME;
		ubo->buffer = [mtls.device newBufferWithLength:ubo->capacity * ubo->item_size
		                                       options:MTLResourceCPUCacheModeDefaultCache];
		ubo->mapped_data = (byte*)([ubo->buffer contents]);
		assert(ubo->mapped_data != NULL);

		struct Buffer* dl_ubo = &f->dl_ubo;
		dl_ubo->item_size = sizeof(DLVSData);
		dl_ubo->capacity = MAX_DRAWS_PER_FRAME;
		dl_ubo->buffer = [mtls.device newBufferWithLength:dl_ubo->capacity * dl_ubo->item_size
		                                          options:MTLResourceCPUCacheModeDefaultCache];
		dl_ubo->mapped_data = (byte*)([dl_ubo->buffer contents]);
		assert(dl_ubo->mapped_data != NULL);
	}
}

static inline void init_metal(void)
{
	@autoreleasepool
	{
		if (mtls.device == nil)
		{
			mtls.device = MTLCreateSystemDefaultDevice();
			mtls.layer = ((__bridge CAMetalLayer*)mtl_imp.layer);
			mtls.layer.device = mtls.device;

			create_pipelines();
			create_depth_states();

			for (int texture_mode = 0; texture_mode < TM_COUNT; ++texture_mode)
			{
				for (int wrap_mode = 0; wrap_mode < TW_COUNT; ++wrap_mode)
				{
					const int index = texture_mode + wrap_mode * TM_COUNT;
					// TODO:
					const int max_anisotropy = 16;

					MTLSamplerDescriptor* info = [MTLSamplerDescriptor new];
					info.minFilter = MTLSamplerMinMagFilterNearest;
					info.magFilter = MTLSamplerMinMagFilterLinear;
					info.mipFilter = MTLSamplerMipFilterLinear;

					if (texture_mode == TM_NEAREST)
					{
						info.minFilter = MTLSamplerMinMagFilterNearest;
						info.magFilter = MTLSamplerMinMagFilterNearest;
						info.mipFilter = MTLSamplerMipFilterNearest;
					}
					else if (texture_mode == TM_BILINEAR)
					{
						info.minFilter = MTLSamplerMinMagFilterLinear;
						info.magFilter = MTLSamplerMinMagFilterLinear;
						info.mipFilter = MTLSamplerMipFilterLinear;
					}

					MTLSamplerAddressMode mode = MTLSamplerAddressModeRepeat;
					if (wrap_mode == TW_CLAMP_TO_EDGE)
					{
						mode = MTLSamplerAddressModeClampToEdge;
					}
					info.sAddressMode = mode;
					info.tAddressMode = mode;
					info.rAddressMode = mode;
					info.maxAnisotropy = max_anisotropy;
					info.lodMinClamp = 0;
					info.lodMaxClamp = 16;

					mtls.samplers[index] = [mtls.device newSamplerStateWithDescriptor:info];
					info = nil;
				}
			}
		}

		mtls.layer.maximumDrawableCount = FRAME_COUNT;

		memset(&mtl, 0, sizeof(mtl));

		for (uint32_t i = 0; i < FRAME_COUNT; ++i)
		{
			struct Frame* frame = &mtl.frames[i];
			init_frame(frame);
		}
	}
}

static qbool gal_init(void)
{
	Sys_V_Init(GAL_METAL);
	init_gl_config();
	init_metal();
	return qtrue;
}

static void gal_shutdown(qbool full_shutdown)
{
	@autoreleasepool
	{
		for (uint32_t i = 0; i < mtl.texture_count; ++i)
		{
			mtl.textures[i] = nil;
		}

		tr.numImages = 0;
		memset(tr.images, 0, sizeof(tr.images));

		for (uint32_t i = 0; i < FRAME_COUNT; ++i)
		{
			struct Frame* f = &mtl.frames[i];

			f->dl_ubo.buffer = nil;
			f->generic_ubo.buffer = nil;
			f->index_buffer.buffer = nil;

			for (uint32_t ii = 0; ii < ARRAY_LEN(f->vertex_buffers); ++ii)
			{
				Buffer* vb = f->vertex_buffers;
				vb[ii].buffer = nil;
			}

			f->color_image = nil;
			f->depth_image = nil;

			f->queue = nil;
		}

		memset(&mtl, 0, sizeof(mtl));

		if (!full_shutdown)
		{
			return;
		}

		for (uint32_t i = 0; i < PIPELINE_COUNT; ++i)
		{
			mtls.pipelines[i] = nil;
		}

		for (uint32_t i = 0; i < SAMPLER_COUNT; ++i)
		{
			mtls.samplers[i] = nil;
		}

		for (uint32_t i = 0; i < DEPTH_STATE_COUNT; ++i)
		{
			mtls.depth_states[i] = nil;
		}

		mtls.device = nil;
		mtls.layer = nil;
		memset(&mtls, 0, sizeof(mtls));
	}
}

static void gal_begin_frame(void)
{
	@autoreleasepool
	{
		struct Frame* f = &mtl.frames[mtl.current_frame_index];

		f->cmd = [f->queue commandBuffer];

		for (uint32_t i = 0; i < VB_COUNT; ++i)
		{
			f->vertex_buffers[i].read_offset = 0;
			f->vertex_buffers[i].write_offset = 0;
		}
		f->index_buffer.write_offset = 0;
		f->index_buffer.read_offset = 0;
		f->generic_ubo.write_offset = 0;
		f->generic_ubo.read_offset = 0;
		f->dl_ubo.write_offset = 0;
		f->dl_ubo.read_offset = 0;

		mtl.alpha_test = AT_ALWAYS;

		mtl.frame_seed = (float)rand() / (float)RAND_MAX;

		mtl.render_area.znear = 0;
		mtl.render_area.zfar = 1;

		mtl.current_pipeline.pipeline_index = -1;
		mtl.current_pipeline.depth_state_index = -1;
		mtl.current_pipeline.cull_mode = -1;
		mtl.current_pipeline.polygon_offset = -1;

		if (r_swapInterval->modified)
		{
			mtls.layer.displaySyncEnabled = abs(r_swapInterval->integer) > 0;
		}
	}
}

static inline void set_pipeline(struct Frame* f, PipelineId id, uint32_t state_bits, cullType_t cull_type, qbool polygon_offset)
{
	@autoreleasepool
	{
		struct Pipeline* p = &mtl.current_pipeline;
		uint32_t pipeline_index = get_pipeline_index(id, state_bits);
		if (p->pipeline_index != pipeline_index)
		{
			[f->encoder setRenderPipelineState:mtls.pipelines[pipeline_index]];
		}

		uint32_t write = (state_bits & GLS_DEPTHMASK_TRUE) != 0;
		uint32_t test = MTLCompareFunctionAlways;
		if (state_bits & GLS_DEPTHFUNC_EQUAL)
		{
			test = MTLCompareFunctionEqual;
		}
		uint32_t depth_state_index = write + test * 2;
		if (p->depth_state_index != depth_state_index)
		{
			[f->encoder setDepthStencilState:mtls.depth_states[write + test * 2]];
		}

		if (p->polygon_offset != uint32_t(polygon_offset))
		{
			[f->encoder setDepthBias:float(polygon_offset) slopeScale:polygon_offset ? -1.0f : 0.0f clamp:1.0f];
		}
		MTLCullMode cull_mode = MTLCullModeNone;
		switch (cull_type)
		{
		case CT_FRONT_SIDED:
			cull_mode = MTLCullModeFront;
			break;
		case CT_BACK_SIDED:
			cull_mode = MTLCullModeBack;
			break;
		case CT_TWO_SIDED:
			cull_mode = MTLCullModeNone;
			break;
		default:
			assert(false);
			break;
		}
		if (p->cull_mode != cull_mode)
		{
			[f->encoder setCullMode:cull_mode];
		}
	}
}

static inline void draw_generic(void)
{
	@autoreleasepool
	{
		struct Frame* f = &mtl.frames[mtl.current_frame_index];
		struct
		{
			uint32_t instance_id;
			uint32_t alpha_test;
			uint32_t tex_env;
			float seed;
			float greyscale;
			float inv_gamma;
			float inv_brightness;
			float noise_scale;
			float alpha_boost;
		} pc = {
			f->generic_ubo.read_offset / sizeof(GenericVSData),
			0,
			0,
			mtl.frame_seed,
			tess.greyscale,
			1.0f / r_gamma->value,
			1.0f / r_brightness->value,
			backEnd.projection2D ? 0.0f : r_ditherStrength->value,
			r_alphaToCoverageMipBoost->value
		};

		upload_geometry(VB_POSITION, tess.xyz);
		upload_indices(tess.indexes, tess.numIndexes);

		for (int i = 0; i < tess.shader->numStages; ++i)
		{
			const shaderStage_t* const stage = tess.xstages[i];

			set_pipeline(f, PID_GENERIC, stage->stateBits, tess.shader->cullType, tess.shader->polygonOffset);

			set_viewport_and_scissor(mtl.render_area.x, mtl.render_area.y, mtl.render_area.w, mtl.render_area.h, mtl.render_area.th);
			pc.alpha_test = get_alpha_test(stage->stateBits);

			bind_image(0, get_bundle_image(&stage->bundle));

			if (stage->mtStages == 1)
			{
				const shaderStage_t* stage2 = tess.xstages[i + 1];
				pc.tex_env = stage2->mtEnv;
			}
			else
			{
				pc.tex_env = TE_DISABLED;
			}

			[f->encoder setVertexBytes:&pc length:sizeof(pc) atIndex:5];
			[f->encoder setFragmentBytes:&pc length:sizeof(pc) atIndex:5];

			upload_geometry(VB_TEXCOORD, tess.svars[i].texcoordsptr);
			upload_geometry(VB_COLOR, tess.svars[i].colors);

			if (stage->mtStages == 0)
			{
				[f->encoder setVertexBufferOffset:f->vertex_buffers[VB_TEXCOORD2].write_offset atIndex:VB_TEXCOORD2];
				bind_image(1, tr.whiteImage);
			}
			else
			{
				const shaderStage_t* const stage2 = tess.xstages[i + 1];
				bind_image(1, get_bundle_image(&stage2->bundle));
				upload_geometry(VB_TEXCOORD2, tess.svars[i + 1].texcoordsptr);
				++i;
			}

			draw_elements(tess.numIndexes);
		}
	}
}

static inline void draw_dynamic_light(void)
{
	@autoreleasepool
	{
		struct Frame* f = &mtl.frames[mtl.current_frame_index];
		set_pipeline(f, PID_DYNAMIC_LIGHT, backEnd.dlStateBits, tess.shader->cullType, tess.shader->polygonOffset);

		const int stage_index = tess.shader->lightingStages[ST_DIFFUSE];
		const shaderStage_t* stage = tess.xstages[stage_index];

		upload_geometry(VB_POSITION, tess.xyz);
		upload_geometry(VB_NORMAL, tess.normal);
		upload_geometry(VB_TEXCOORD, tess.svars[stage_index].texcoordsptr);
		upload_indices(tess.dlIndexes, tess.dlNumIndexes);

		mtl.dl_pc.opaque = backEnd.dlOpaque ? 1.0f : 0.0f;
		mtl.dl_pc.intensity = backEnd.dlIntensity;
		mtl.dl_pc.greyscale = tess.greyscale;
		mtl.dl_pc.ubo_index = f->dl_ubo.write_offset / sizeof(mtl.dl_vs_data);

		assert(f->dl_ubo.write_offset + sizeof(mtl.dl_vs_data) <= MAX_DRAWS_PER_FRAME * sizeof(mtl.dl_vs_data));
		memcpy(f->dl_ubo.mapped_data + f->dl_ubo.write_offset, &mtl.dl_vs_data, sizeof(mtl.dl_vs_data));
		f->dl_ubo.write_offset += sizeof(DLVSData);

		set_viewport_and_scissor(mtl.render_area.x, mtl.render_area.y, mtl.render_area.w, mtl.render_area.h, mtl.render_area.th);

		[f->encoder setVertexBytes:&mtl.dl_pc length:sizeof(mtl.dl_pc) atIndex:5];
		[f->encoder setFragmentBytes:&mtl.dl_pc length:sizeof(mtl.dl_pc) atIndex:5];

		bind_image(0, get_bundle_image(&stage->bundle));

		draw_elements(tess.dlNumIndexes);
	}
}

static inline void draw_depth_fade(void)
{
}

static void gal_draw(drawType_t type)
{
	switch (type)
	{
	case DT_GENERIC:
	{
		draw_generic();
		break;
	}
	case DT_DYNAMIC_LIGHT:
	{
		draw_dynamic_light();
		break;
	}
	case DT_SOFT_SPRITE:
	{
		draw_depth_fade();
		break;
	}
	}
}

static inline void execute_postprocess(struct Frame* f)
{
	@autoreleasepool
	{
		struct
		{
			float scale_x;
			float scale_y;
			float gamma;
			float brightness;
			float greyscale;
			float dummyps0;
		} pc = {
			1,
			1,
			1.0f / r_gamma->value,
			r_brightness->value,
			r_greyscale->value,
			0,
		};

		MTLRenderPassDescriptor* pass_info = [MTLRenderPassDescriptor new];
		pass_info.colorAttachments[0].texture = mtl.drawable.texture;
		pass_info.colorAttachments[0].loadAction = MTLLoadActionDontCare;
		pass_info.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
		pass_info.colorAttachments[0].storeAction = MTLStoreActionStore;

		id<MTLRenderCommandEncoder> e = [f->cmd renderCommandEncoderWithDescriptor:pass_info];
		pass_info = nil;

		[e setRenderPipelineState:mtls.pipelines[get_pipeline_index(PID_POST_PROCESS, 0)]];
		[e setFragmentSamplerState:mtls.samplers[0] atIndex:0];
		[e setFragmentTexture:f->color_image atIndex:0];
		[e setVertexBytes:&pc length:sizeof(pc) atIndex:0];
		[e setFragmentBytes:&pc length:sizeof(pc) atIndex:0];
		[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
		[e endEncoding];
		e = nil;
		backEnd.pc3D[RB_DRAW_CALLS]++;
	}
}

static void gal_end_frame(void)
{
	@autoreleasepool
	{
		struct Frame* f = &mtl.frames[mtl.current_frame_index];

		[f->encoder endEncoding];
		f->encoder = nil;

		mtl.drawable = [mtls.layer nextDrawable];

		execute_postprocess(f);

		[f->cmd presentDrawable:mtl.drawable];
		[f->cmd commit];
		mtl.drawable = nil;
		f->cmd = nil;
	}
}

static void gal_begin_2d()
{
	@autoreleasepool
	{
		R_MakeIdentityMatrix(mtl.generic_vs_data.model_view_matrix);
		R_MakeOrthoProjectionMatrix(mtl.generic_vs_data.projection_matrix, glConfig.vidWidth, glConfig.vidHeight);
		update_generic_ubo();

		mtl.render_area.x = 0;
		mtl.render_area.y = 0;
		mtl.render_area.w = glConfig.vidWidth;
		mtl.render_area.h = glConfig.vidHeight;
		mtl.render_area.th = glConfig.vidHeight;

		struct Frame* f = &mtl.frames[mtl.current_frame_index];

		if (f->encoder == nil)
		{
			MTLRenderPassDescriptor* pass_info = [MTLRenderPassDescriptor new];
			pass_info.colorAttachments[0].texture = f->color_image;
			pass_info.colorAttachments[0].loadAction = MTLLoadActionClear;
			pass_info.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
			pass_info.colorAttachments[0].storeAction = MTLStoreActionStore;
			pass_info.depthAttachment.texture = f->depth_image;
			pass_info.depthAttachment.clearDepth = 1.0f;
			pass_info.depthAttachment.loadAction = MTLLoadActionClear;
			pass_info.depthAttachment.storeAction = MTLStoreActionStore;

			f->encoder = [f->cmd renderCommandEncoderWithDescriptor:pass_info];
			pass_info = nil;

			[f->encoder setFrontFacingWinding:MTLWindingCounterClockwise];
			for (uint32_t i = 0; i < VB_COUNT; ++i)
			{
				[f->encoder setVertexBuffer:f->vertex_buffers[i].buffer offset:0 atIndex:i];
			}
			[f->encoder setVertexBuffer:f->generic_ubo.buffer offset:0 atIndex:6];
			[f->encoder setVertexBuffer:f->dl_ubo.buffer offset:0 atIndex:7];
		}
	}
}

static void gal_begin_3d(void)
{
	@autoreleasepool
	{
		struct Frame* f = &mtl.frames[mtl.current_frame_index];

		R_MakeIdentityMatrix(mtl.generic_vs_data.model_view_matrix);
		memcpy(mtl.generic_vs_data.projection_matrix, backEnd.viewParms.projectionMatrix, sizeof(mtl.generic_vs_data.projection_matrix));
		memcpy(mtl.dl_vs_data.projection_matrix, backEnd.viewParms.projectionMatrix, sizeof(mtl.dl_vs_data.projection_matrix));
		update_generic_ubo();

		mtl.render_area.x = backEnd.viewParms.viewportX;
		mtl.render_area.y = backEnd.viewParms.viewportY;
		mtl.render_area.w = backEnd.viewParms.viewportWidth;
		mtl.render_area.h = backEnd.viewParms.viewportHeight;
		mtl.render_area.th = glConfig.vidHeight;

		MTLRenderPassDescriptor* pass_info = [MTLRenderPassDescriptor new];
		pass_info.colorAttachments[0].texture = f->color_image;
		pass_info.colorAttachments[0].loadAction = MTLLoadActionLoad;
		pass_info.colorAttachments[0].storeAction = MTLStoreActionStore;
		pass_info.depthAttachment.texture = f->depth_image;
		pass_info.depthAttachment.clearDepth = 1.0f;
		pass_info.depthAttachment.loadAction = MTLLoadActionClear;
		pass_info.depthAttachment.storeAction = MTLStoreActionDontCare;

		if (f->encoder != nil)
		{
			[f->encoder endEncoding];
			f->encoder = nil;
		}
		f->encoder = [f->cmd renderCommandEncoderWithDescriptor:pass_info];
		pass_info = nil;

		[f->encoder setFrontFacingWinding:MTLWindingCounterClockwise];
		for (uint32_t i = 0; i < VB_COUNT; ++i)
		{
			[f->encoder setVertexBuffer:f->vertex_buffers[i].buffer offset:0 atIndex:i];
		}
		[f->encoder setVertexBuffer:f->generic_ubo.buffer offset:0 atIndex:6];
		[f->encoder setVertexBuffer:f->dl_ubo.buffer offset:0 atIndex:7];
	}
}

static void gal_begin_sky_and_clouds(double depth)
{
	mtl.render_area.znear = depth;
	mtl.render_area.zfar = depth;
}

static void gal_end_sky_and_clouds(void)
{
	mtl.render_area.znear = 0;
	mtl.render_area.zfar = 1;
}

static void gal_create_texture(image_t* image, int mip_count, int w, int h)
{
	if (mtl.texture_count >= ARRAY_LEN(mtl.textures))
		ri.Error(ERR_FATAL, "Too many textures allocated for the Metal back-end");

	mtl.textures[mtl.texture_count] = create_image2(image, mip_count, w, h);
	image->texnum = mtl.texture_count++;
}

static void gal_update_texture(image_t* image, int mip, int x, int y, int w, int h, const void* data)
{
	MTLRegion region = MTLRegionMake2D(x, y, w, h);
	[mtl.textures[image->texnum] replaceRegion:region mipmapLevel:mip withBytes:data bytesPerRow:w * 4];
}

static void gal_update_scratch(image_t* image, int w, int h, const void* data, qbool dirty)
{
	@autoreleasepool
	{
		if (image->texnum <= 0 || image->texnum > ARRAY_LEN(mtl.textures))
		{
			return;
		}

		if (w != image->width || h != image->height)
		{
			image->width = w;
			image->height = h;
			mtl.textures[image->texnum] = create_image2(image, 1, w, h);
			gal_update_texture(image, 0, 0, 0, w, h, data);
		}
		else if (dirty)
		{
			gal_update_texture(image, 0, 0, 0, w, h, data);
		}
	}
}

static void gal_create_texture_ex(image_t* image, int mip_count, int mip_offset, int w, int h, const void* mip0)
{
}

static void gal_begin_dynamic_light()
{
	const dlight_t* const dl = tess.light;
	mtl.dl_pc.light_color[0] = dl->color[0];
	mtl.dl_pc.light_color[1] = dl->color[1];
	mtl.dl_pc.light_color[2] = dl->color[2];
	mtl.dl_pc.light_radius = 1.0f / Square(dl->radius);

	mtl.dl_vs_data.os_eye_pos[0] = backEnd.orient.viewOrigin[0];
	mtl.dl_vs_data.os_eye_pos[1] = backEnd.orient.viewOrigin[1];
	mtl.dl_vs_data.os_eye_pos[2] = backEnd.orient.viewOrigin[2];
	mtl.dl_vs_data.os_eye_pos[3] = 1.0f;
	mtl.dl_vs_data.os_light_pos[0] = dl->transformed[0];
	mtl.dl_vs_data.os_light_pos[1] = dl->transformed[1];
	mtl.dl_vs_data.os_light_pos[2] = dl->transformed[2];
	mtl.dl_vs_data.os_light_pos[3] = 1.0f;
}

static void gal_read_pixels(int x, int y, int w, int h, int alignment, colorSpace_t color_space, void* out)
{
}

static void gal_set_model_view_matrix(const float* matrix)
{
	memcpy(mtl.generic_vs_data.model_view_matrix, matrix, sizeof(mtl.generic_vs_data.model_view_matrix));
	memcpy(mtl.dl_vs_data.model_view_matrix, matrix, sizeof(mtl.dl_vs_data.model_view_matrix));
	update_generic_ubo();
}

static void gal_set_depth_range(double znear, double zfar)
{
	mtl.render_area.znear = znear;
	mtl.render_area.zfar = zfar;
}

static void gal_print_info()
{
}

qbool GAL_GetMetal(graphicsAPILayer_t* rb)
{
	rb->Init = &gal_init;
	rb->ShutDown = &gal_shutdown;
	rb->BeginSkyAndClouds = &gal_begin_sky_and_clouds;
	rb->EndSkyAndClouds = &gal_end_sky_and_clouds;
	rb->ReadPixels = &gal_read_pixels;
	rb->BeginFrame = &gal_begin_frame;
	rb->EndFrame = &gal_end_frame;
	rb->CreateTexture = &gal_create_texture;
	rb->UpdateTexture = &gal_update_texture;
	rb->UpdateScratch = &gal_update_scratch;
	rb->CreateTextureEx = &gal_create_texture_ex;
	rb->Draw = &gal_draw;
	rb->Begin2D = &gal_begin_2d;
	rb->Begin3D = &gal_begin_3d;
	rb->SetModelViewMatrix = &gal_set_model_view_matrix;
	rb->SetDepthRange = &gal_set_depth_range;
	rb->BeginDynamicLight = &gal_begin_dynamic_light;
	rb->PrintInfo = &gal_print_info;

	return qtrue;
}
