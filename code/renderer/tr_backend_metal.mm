#include "tr_local.h"

// @NOTE: MAX_VERTEXES and MAX_INDEXES are *per frame*
#define LARGEBUFFER_MAX_FRAMES      4
#define LARGEBUFFER_MAX_VERTEXES    131072
#define LARGEBUFFER_MAX_INDEXES     (LARGEBUFFER_MAX_VERTEXES * 8)

// this is the highest maximum we'll ever report
#define MAX_GPU_TEXTURE_SIZE        2048

struct Metal
{
};

static Metal mtl;

static qbool gal_init(void)
{
	return qtrue;
}

static void gal_shutdown(qbool full_shutdown)
{
}

static void gal_begin_frame(void)
{
}

static void gal_end_frame(void)
{
}

static void gal_draw(drawType_t type)
{
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
}

static void gal_update_texture(image_t* image, int mip, int x, int y, int w, int h, const void* data)
{
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

static void gal_begin_2d()
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
