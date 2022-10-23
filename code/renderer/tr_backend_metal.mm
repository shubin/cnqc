#include "tr_local.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AppKit/NSView.h>

#include "../macosx/macosx_public.h"

// @NOTE: MAX_VERTEXES and MAX_INDEXES are *per frame*
#define LARGEBUFFER_MAX_FRAMES   4
#define LARGEBUFFER_MAX_VERTEXES 131072
#define LARGEBUFFER_MAX_INDEXES  (LARGEBUFFER_MAX_VERTEXES * 8)

// this is the highest maximum we'll ever report
#define MAX_GPU_TEXTURE_SIZE 2048

#define FRAME_COUNT   2
#define SAMPLER_COUNT TW_COUNT * 3

#define MAX_DRAWS_PER_FRAME 500

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

struct Image
{
	id<MTLTexture> image;
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

struct Frame
{
	id<MTLCommandQueue> queue;
	id<MTLCommandBuffer> cmd;
	id<MTLCommandEncoder> encoder;
	Image color_image;
	Image depth_image;

	Buffer vertex_buffers[VB_COUNT];
	Buffer index_buffer;

	Buffer generic_ubo;
};

struct Metal
{
	CAMetalLayer* layer;
	id<MTLDevice> device;
	struct Frame frames[FRAME_COUNT];
	uint32_t current_frame_index;
	id<CAMetalDrawable> drawable;
	id<MTLSamplerState> samplers[SAMPLER_COUNT];
	uint32_t texture_count;
	Image textures[MAX_DRAWIMAGES];
	RenderArea render_area;
	GenericVSData generic_vs_data;
	PipelineId pipeline_id;
};

static Metal mtl;

static inline MTLPixelFormat to_mtl_format(textureFormat_t f)
{
	switch (f)
	{
		case TF_RGBA8:
		default: return MTLPixelFormatRGBA8Unorm;
	}
}

static inline void create_image(struct Image* image, MTLPixelFormat format)
{
	MTLTextureDescriptor* info = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:glConfig.vidWidth height:glConfig.vidHeight mipmapped:NO];
	info.sampleCount = 1;
	info.storageMode = MTLStorageModePrivate;
	info.textureType = MTLTextureType2D;
	info.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
	image->image = [mtl.device newTextureWithDescriptor:info];
	[info release];
}

static inline void create_image2(struct Image* texture, image_t* image, int mip_count, int w, int h)
{
	MTLTextureDescriptor* info = [MTLTextureDescriptor new];
	info.arrayLength = 1;
	info.usage = MTLTextureUsageShaderRead;
	info.pixelFormat = to_mtl_format(image->format);
	info.width = w;
	info.height = h;
	info.depth = 1;
	info.mipmapLevelCount = mip_count;
	info.sampleCount = 1;
	info.textureType = MTLTextureType2D;

	texture->image = [mtl.device newTextureWithDescriptor:info];
	[info release];
}

static inline void update_generic_ubo()
{
	Frame* f = &mtl.frames[mtl.current_frame_index];
	assert(f->generic_ubo.write_offset + sizeof(GenericVSData) <= MAX_DRAWS_PER_FRAME * sizeof(GenericVSData));
	memcpy(f->generic_ubo.mapped_data + f->generic_ubo.write_offset, &mtl.generic_vs_data, sizeof(GenericVSData));
	f->generic_ubo.read_offset = f->generic_ubo.write_offset;
	f->generic_ubo.write_offset += sizeof(GenericVSData);
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

static inline void init_frame(struct Frame* f)
{
	f->queue = [mtl.device newCommandQueue];

	create_image(&f->color_image, MTLPixelFormatRGBA8Unorm);
	create_image(&f->depth_image, MTLPixelFormatDepth32Float);

	struct Buffer* vb = f->vertex_buffers;
	vb[VB_POSITION].item_size = sizeof(tess.xyz[0]);
	vb[VB_NORMAL].item_size = sizeof(tess.normal[0]);
	vb[VB_TEXCOORD].item_size = sizeof(tess.svars[0].texcoords[0]);
	vb[VB_TEXCOORD2].item_size = sizeof(tess.svars[0].texcoords[0]);
	vb[VB_COLOR].item_size = sizeof(tess.svars[0].colors[0]);

	for (uint32_t i = 0; i < ARRAY_LEN(f->vertex_buffers); ++i)
	{
		vb[i].capacity = SHADER_MAX_VERTEXES;
		vb[i].buffer = [mtl.device newBufferWithLength:vb[i].capacity * vb[i].item_size
		                                       options:MTLResourceCPUCacheModeDefaultCache];
		vb[i].mapped_data = (byte*)([vb[i].buffer contents]);
		assert(vb[i].mapped_data != NULL);
	}

	struct Buffer* ib = &f->index_buffer;
	ib->item_size = sizeof(tess.indexes[0]);
	ib->capacity = SHADER_MAX_INDEXES;
	ib->buffer = [mtl.device newBufferWithLength:ib->capacity * ib->item_size
	                                     options:MTLResourceCPUCacheModeDefaultCache];
	ib->mapped_data = (byte*)([ib->buffer contents]);
	assert(ib->mapped_data != NULL);

	struct Buffer* ubo = &f->generic_ubo;
	ubo->item_size = sizeof(GenericVSData);
	ubo->capacity = MAX_DRAWS_PER_FRAME;
	ubo->buffer = [mtl.device newBufferWithLength:ib->capacity * ib->item_size
	                                      options:MTLResourceCPUCacheModeDefaultCache];
	ubo->mapped_data = (byte*)([ubo->buffer contents]);
	assert(ubo->mapped_data != NULL);
}

static inline void init_metal(void)
{
	memset(&mtl, 0, sizeof(mtl));

	mtl.device = MTLCreateSystemDefaultDevice();
	mtl.layer = ((CAMetalLayer*)mtl_imp.layer);
	mtl.layer.device = mtl.device;
	mtl.layer.maximumDrawableCount = FRAME_COUNT;

	for (uint32_t i = 0; i < FRAME_COUNT; ++i)
	{
		struct Frame* frame = &mtl.frames[i];
		init_frame(frame);
	}

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

			mtl.samplers[index] = [mtl.device newSamplerStateWithDescriptor:info];
			[info release];
		}
	}
}

static qbool gal_init(void)
{
	if (glConfig.vidWidth == 0)
	{
		// the order of these calls can not be changed
		Sys_V_Init(GAL_METAL);
		init_gl_config();
		init_metal();
		// apply the current V-Sync option after the first rendered frame
		r_swapInterval->modified = qtrue;
	}

	// SetDefaultState();

	return qtrue;
}

static void gal_shutdown(qbool full_shutdown)
{
	if (!full_shutdown)
		return;

	for (uint32_t i = 0; i < mtl.texture_count; ++i)
	{
		[mtl.textures[i].image release];
	}

	tr.numImages = 0;
	memset(tr.images, 0, sizeof(tr.images));

	for (uint32_t i = 0; i < ARRAY_LEN(mtl.samplers); ++i)
	{
		[mtl.samplers[i] release];
	}

	for (uint32_t i = 0; i < FRAME_COUNT; ++i)
	{
		struct Frame* f = &mtl.frames[i];

		[f->generic_ubo.buffer release];
		[f->index_buffer.buffer release];

		for (uint32_t ii = 0; ii < ARRAY_LEN(f->vertex_buffers); ++ii)
		{
			Buffer* vb = f->vertex_buffers;
			[vb[ii].buffer release];
		}

		[f->color_image.image release];
		[f->depth_image.image release];

		[f->queue release];
	}

	[mtl.device release];

	memset(&mtl, 0, sizeof(mtl));
}

static void gal_begin_frame(void)
{
	struct Frame* f = &mtl.frames[mtl.current_frame_index];
	if (f->cmd != nil)
	{
		[f->cmd release];
	}
	f->cmd = [f->queue commandBuffer];
}

static void gal_end_frame(void)
{
	struct Frame* f = &mtl.frames[mtl.current_frame_index];

	if (f->encoder != nil)
	{
		[f->encoder endEncoding];
		[f->encoder release];
		f->encoder = nil;
	}

	mtl.drawable = [mtl.layer nextDrawable];

	MTLRenderPassDescriptor* pass_info = [MTLRenderPassDescriptor new];
	pass_info.colorAttachments[0].clearColor = MTLClearColorMake(0.2, 0.3, 0.4, 1.0);
	pass_info.colorAttachments[0].texture = mtl.drawable.texture;
	if (MTL_DEBUG)
	{
		pass_info.colorAttachments[0].loadAction = MTLLoadActionClear;
	}
	else
	{
		pass_info.colorAttachments[0].loadAction = MTLLoadActionDontCare;
	}
	pass_info.colorAttachments[0].storeAction = MTLStoreActionStore;

	id<MTLRenderCommandEncoder> encoder = [f->cmd renderCommandEncoderWithDescriptor:pass_info];
	[encoder endEncoding];

	[f->cmd presentDrawable:mtl.drawable];
	[f->cmd commit];
}

static void draw_generic(void)
{
	// struct {
	// 	uint32_t instance_id;
	// 	uint32_t alpha_test;
	// 	uint32_t tex_env;
	// 	float seed;
	// 	float greyscale;
	// 	float inv_gamma;
	// 	float inv_brightness;
	// 	float noise_scale;
	// 	float alpha_boost;
	// } pc = {

	// };
}

static inline void draw_dynamic_light(void)
{
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

static void gal_begin_2d()
{
	R_MakeIdentityMatrix(mtl.generic_vs_data.model_view_matrix);
	R_MakeOrthoProjectionMatrix(mtl.generic_vs_data.projection_matrix, glConfig.vidWidth, glConfig.vidHeight);
	update_generic_ubo();

	mtl.render_area.x = 0;
	mtl.render_area.y = 0;
	mtl.render_area.w = glConfig.vidWidth;
	mtl.render_area.h = glConfig.vidHeight;
	mtl.render_area.th = glConfig.vidHeight;

	mtl.pipeline_id = PID_GENERIC;

	struct Frame* f = &mtl.frames[mtl.current_frame_index];
	if (f->encoder == nil)
	{
		MTLRenderPassDescriptor* pass_info = [MTLRenderPassDescriptor new];
		pass_info.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
		pass_info.colorAttachments[0].texture = f->color_image.image;
		pass_info.colorAttachments[0].loadAction = MTLLoadActionClear;
		pass_info.colorAttachments[0].storeAction = MTLStoreActionStore;

		pass_info.depthAttachment.loadAction = MTLLoadActionClear;
		pass_info.depthAttachment.storeAction = MTLStoreActionStore;
		pass_info.depthAttachment.texture = f->depth_image.image;
		pass_info.depthAttachment.clearDepth = 1.0;

		f->encoder = [f->cmd renderCommandEncoderWithDescriptor:pass_info];
		[pass_info release];
	}
}

static void gal_begin_3d(void)
{
}

static void gal_begin_sky_and_clouds(double depth)
{
}

static void gal_end_sky_and_clouds(void)
{
}

static void gal_create_texture(image_t* image, int mip_count, int w, int h)
{
	if (mtl.texture_count >= ARRAY_LEN(mtl.textures))
		ri.Error(ERR_FATAL, "Too many textures allocated for the Fluent back-end");

	create_image2(&mtl.textures[mtl.texture_count], image, mip_count, w, h);
	image->texnum = mtl.texture_count++;
}

static void gal_update_texture(image_t* image, int mip, int x, int y, int w, int h, const void* data)
{
	MTLRegion region = MTLRegionMake2D(x, y, w, h);
	// FIXME: bytesPerRow
	[mtl.textures[image->texnum].image replaceRegion:region mipmapLevel:mip withBytes:data bytesPerRow:w * 4];
}

static void gal_update_scratch(image_t* image, int w, int h, const void* data, qbool dirty)
{
}

static void gal_create_texture_ex(image_t* image, int mip_count, int mip_offset, int w, int h, const void* mip0)
{
}

static void gal_begin_dynamic_light()
{
}

static void gal_read_pixels(int x, int y, int w, int h, int alignment, colorSpace_t color_space, void* out)
{
}

static void gal_set_model_view_matrix(const float* matrix)
{
}

static void gal_set_depth_range(double z_near, double z_far)
{
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
