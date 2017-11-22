/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_init.c -- functions that are not called every frame

#include "tr_local.h"
#include "tr_help.h"

glconfig_t	glConfig;
glinfo_t	glInfo;

glstate_t	glState;

screenshotCommand_t	r_delayedScreenshot;
qbool				r_delayedScreenshotPending = qfalse;
int					r_delayedScreenshotFrame = 0;

static void GfxInfo_f( void );

cvar_t	*r_verbose;

cvar_t	*r_displayRefresh;

cvar_t	*r_detailTextures;

cvar_t	*r_intensity;
cvar_t	*r_gamma;
cvar_t	*r_greyscale;

cvar_t	*r_measureOverdraw;

cvar_t	*r_fastsky;
cvar_t	*r_dynamiclight;

cvar_t	*r_lodbias;
cvar_t	*r_lodscale;

cvar_t	*r_norefresh;
cvar_t	*r_drawentities;
cvar_t	*r_drawworld;
cvar_t	*r_speeds;
cvar_t	*r_fullbright;
cvar_t	*r_lightmap;
cvar_t	*r_novis;
cvar_t	*r_nocull;
cvar_t	*r_nocurves;

cvar_t	*r_ext_max_anisotropy;
cvar_t	*r_msaa;

cvar_t	*r_ignoreGLErrors;

cvar_t	*r_vertexLight;
cvar_t	*r_uiFullScreen;
cvar_t	*r_mode;
cvar_t	*r_blitMode;
cvar_t	*r_nobind;
cvar_t	*r_singleShader;
cvar_t	*r_roundImagesDown;
cvar_t	*r_colorMipLevels;
cvar_t	*r_picmip;
cvar_t	*r_showtris;
cvar_t	*r_showsky;
cvar_t	*r_shownormals;
cvar_t	*r_finish;
cvar_t	*r_clear;
cvar_t	*r_swapInterval;
cvar_t	*r_textureMode;
cvar_t	*r_lockpvs;
cvar_t	*r_noportals;
cvar_t	*r_portalOnly;

cvar_t	*r_subdivisions;
cvar_t	*r_lodCurveError;

cvar_t	*r_width;
cvar_t	*r_height;
cvar_t	*r_customaspect;

cvar_t	*r_brightness;
cvar_t	*r_mapBrightness;

cvar_t	*r_debugSurface;

cvar_t	*r_showImages;

cvar_t	*r_ambientScale;
cvar_t	*r_directedScale;
cvar_t	*r_debugLight;
cvar_t	*r_debugSort;

cvar_t	*r_flares;
cvar_t	*r_flareSize;
cvar_t	*r_flareFade;
cvar_t	*r_flareCoeff;

///////////////////////////////////////////////////////////////
cvar_t	*r_maplightBrightness;
cvar_t	*r_maplightSaturation;
cvar_t	*r_maplightColor;
cvar_t	*r_maplightColorMode;
vec3_t	vMaplightColorFilter;

// these limits apply to the sum of all scenes in a frame:
// the main view, all the 3D icons, and even the console etc
#define DEFAULT_MAX_POLYS		8192
#define DEFAULT_MAX_POLYVERTS	32768
static cvar_t* r_maxpolys;
static cvar_t* r_maxpolyverts;
int max_polys;
int max_polyverts;


static void GL_SetDefaultState()
{
	qglClearDepth( 1.0f );

	qglCullFace( GL_FRONT );

	qglColor4f( 1,1,1,1 );

	for ( int i = 0; i < MAX_TMUS; ++i ) {
		GL_SelectTexture( i );
		GL_TextureMode( r_textureMode->string );
		GL_TexEnv( GL_MODULATE );
		qglDisable( GL_TEXTURE_2D );
	}

	GL_SelectTexture( 0 );
	qglEnable( GL_TEXTURE_2D );

	qglShadeModel( GL_SMOOTH );
	qglDepthFunc( GL_LEQUAL );

	qglPolygonOffset( -1, -1 );

	// the vertex array is always enabled, but the color and texture
	// arrays are enabled and disabled around the compiled vertex array call
	qglEnableClientState( GL_VERTEX_ARRAY );

	//
	// make sure our GL state vector is set correctly
	//
	glState.glStateBits = GLS_DEPTHTEST_DISABLE | GLS_DEPTHMASK_TRUE;

	qglPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	qglDepthMask( GL_TRUE );
	qglDisable( GL_DEPTH_TEST );
	qglEnable( GL_SCISSOR_TEST );
	qglDisable( GL_CULL_FACE );
	qglDisable( GL_BLEND );
}


static void GL_InitGLConfig()
{
	Q_strncpyz( glConfig.vendor_string, (const char*)qglGetString( GL_VENDOR ), sizeof( glConfig.vendor_string ) );
	Q_strncpyz( glConfig.renderer_string, (const char*)qglGetString( GL_RENDERER ), sizeof( glConfig.renderer_string ) );
	Q_strncpyz( glConfig.version_string, (const char*)qglGetString( GL_VERSION ), sizeof( glConfig.version_string ) );
	Q_strncpyz( glConfig.extensions_string, (const char*)qglGetString( GL_EXTENSIONS ), sizeof( glConfig.extensions_string ) );
	qglGetIntegerv( GL_MAX_TEXTURE_SIZE, &glConfig.unused_maxTextureSize );
	glConfig.unused_maxActiveTextures = 0;
	glConfig.unused_driverType = 0;		// ICD
	glConfig.unused_hardwareType = 0;	// generic
	glConfig.unused_deviceSupportsGamma = qtrue;
	glConfig.unused_textureCompression = 0;	// no compression
	glConfig.unused_textureEnvAddAvailable = qtrue;
	glConfig.unused_displayFrequency = 0;
	glConfig.unused_isFullscreen = !!r_fullscreen->integer;
	glConfig.unused_stereoEnabled = qfalse;
	glConfig.unused_smpActive = qfalse;
}


static void GL_InitGLInfo()
{
	qglGetIntegerv( GL_MAX_TEXTURE_SIZE, &glInfo.maxTextureSize );
	qglGetIntegerv( GL_MAX_ELEMENTS_INDICES, &glInfo.maxDrawElementsI );
	qglGetIntegerv( GL_MAX_ELEMENTS_VERTICES, &glInfo.maxDrawElementsV );

	if ( strstr( glConfig.extensions_string, "GL_EXT_texture_filter_anisotropic" ) )
		qglGetIntegerv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &glInfo.maxAnisotropy );
	else
		glInfo.maxAnisotropy = 0;
}


static void GL_InitExtensions()
{
	const char* missingExtension = NULL;
	if ( !Sys_GL_LoadExtensions( &missingExtension ) )
		ri.Error( ERR_FATAL, "GL_InitExtensions() - failed to load %s\n", missingExtension ? missingExtension : "a required extension" );

	if ( !GL2_Init() )
		ri.Error( ERR_FATAL, "GL_InitExtensions() - failed to create GL2 objects\n" );
}


/*
** InitOpenGL
**
** This function is responsible for initializing a valid OpenGL subsystem.  This
** is done by calling Sys_GL_Init (which gives us a working OGL subsystem) then
** setting variables, checking GL constants, and reporting the gfx system config
** to the user.
*/
static void InitOpenGL()
{
	// Sys_GL_Init initializes OS-specific portions of the renderer
	// it directly or indirectly references the following cvars:
	// r_fullscreen, r_mode, r_width, r_height

	if ( glConfig.vidWidth == 0 )
	{
		// the order of these calls can not be changed
		Sys_GL_Init();
		GL_InitGLConfig();
		GL_InitGLInfo();
		GL_InitExtensions();

		// apply the current V-Sync option after the first rendered frame
		r_swapInterval->modified = qtrue;
	}

	GfxInfo_f();

	GL_SetDefaultState();
}


void GL_CheckErrors()
{
	int err = qglGetError();
	if ((err == GL_NO_ERROR) || r_ignoreGLErrors->integer)
		return;

	char s[64];
	switch( err ) {
		case GL_INVALID_ENUM:
			strcpy( s, "GL_INVALID_ENUM" );
			break;
		case GL_INVALID_VALUE:
			strcpy( s, "GL_INVALID_VALUE" );
			break;
		case GL_INVALID_OPERATION:
			strcpy( s, "GL_INVALID_OPERATION" );
			break;
		case GL_STACK_OVERFLOW:
			strcpy( s, "GL_STACK_OVERFLOW" );
			break;
		case GL_STACK_UNDERFLOW:
			strcpy( s, "GL_STACK_UNDERFLOW" );
			break;
		case GL_OUT_OF_MEMORY:
			strcpy( s, "GL_OUT_OF_MEMORY" );
			break;
		default:
			Com_sprintf( s, sizeof(s), "%i", err);
			break;
	}

	ri.Error( ERR_FATAL, "GL_CheckErrors: %s", s );
}


void R_ConfigureVideoMode( int desktopWidth, int desktopHeight )
{
	glInfo.winFullscreen = !!r_fullscreen->integer;
	glInfo.vidFullscreen = r_fullscreen->integer && r_mode->integer == VIDEOMODE_CHANGE;

	if (r_fullscreen->integer && r_mode->integer == VIDEOMODE_DESKTOPRES) {
		glConfig.vidWidth = desktopWidth;
		glConfig.vidHeight = desktopHeight;
		glConfig.windowAspect = (float)glConfig.vidWidth / (float)glConfig.vidHeight;
		glInfo.winWidth = desktopWidth;
		glInfo.winHeight = desktopHeight;
		return;
	}

	if (r_fullscreen->integer && r_mode->integer == VIDEOMODE_UPSCALE) {
		glConfig.vidWidth = r_width->integer;
		glConfig.vidHeight = r_height->integer;
		glConfig.windowAspect = (float)glConfig.vidWidth / (float)glConfig.vidHeight;
		glInfo.winWidth = desktopWidth;
		glInfo.winHeight = desktopHeight;
		return;
	}
		
	glConfig.vidWidth = r_width->integer;
	glConfig.vidHeight = r_height->integer;
	glConfig.windowAspect = (float)glConfig.vidWidth / (float)glConfig.vidHeight;
	glInfo.winWidth = r_width->integer;
	glInfo.winHeight = r_height->integer;
}


///////////////////////////////////////////////////////////////


static void RB_TakeScreenshotTGA( int x, int y, int width, int height, const char* fileName )
{
	int c = (width * height * 3);
	RI_AutoPtr p( sizeof(TargaHeader) + c );

	TargaHeader* tga = p.Get<TargaHeader>();
	Com_Memset( tga, 0, sizeof(TargaHeader) );
	tga->image_type = 2; // uncompressed BGR
	tga->width = LittleShort( width );
	tga->height = LittleShort( height );
	tga->pixel_size = 24;

	byte* pRGB = p + sizeof(TargaHeader);
	qglReadPixels( x, y, width, height, GL_RGB, GL_UNSIGNED_BYTE, pRGB );

	// swap RGB to BGR
	for (int i = 0; i < c; i += 3)
	{
		byte r = pRGB[i];
		pRGB[i] = pRGB[i+2];
		pRGB[i+2] = r;
	}

	ri.FS_WriteFile( fileName, p, sizeof(TargaHeader) + c );
}


static void RB_TakeScreenshotJPG( int x, int y, int width, int height, const char* fileName )
{
	RI_AutoPtr p( width * height * 4 );
	qglReadPixels( x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, p );

	RI_AutoPtr out( width * height * 4 );
	int n = SaveJPGToBuffer( out, 95, width, height, p );
	ri.FS_WriteFile( fileName, out, n );
}


const void* RB_TakeScreenshotCmd( const screenshotCommand_t* cmd )
{
	// NOTE: the current read buffer is the last FBO color attachment texture that was written to
	// therefore, qglReadPixels will get the latest data even with double/triple buffering enabled

	switch (cmd->type) {
		case screenshotCommand_t::SS_JPG:
			RB_TakeScreenshotJPG( cmd->x, cmd->y, cmd->width, cmd->height, cmd->fileName );
			break;
		case screenshotCommand_t::SS_TGA:
			RB_TakeScreenshotTGA( cmd->x, cmd->y, cmd->width, cmd->height, cmd->fileName );
			break;
	}

	if (cmd->conVis > 0.0f) {
		ri.SetConsoleVisibility( cmd->conVis );
		r_delayedScreenshotPending = qfalse;
		r_delayedScreenshotFrame = 0;
	}

	return (const void*)(cmd + 1);
}


// screenshot filename is YYYY_MM_DD-HH_MM_SS-TTT
// so you can find the damn things and you never run out of them for movies  :)

static void R_TakeScreenshot( const char* ext, screenshotCommand_t::ss_type type, qbool hideConsole )
{
	static char s[MAX_OSPATH]; // bad things may happen if we somehow manage to take 2 ss in 1 frame

	const float conVis = hideConsole ? ri.SetConsoleVisibility( 0.0f ) : 0.0f;
	screenshotCommand_t* cmd;
	if ( conVis > 0.0f ) {
		cmd = &r_delayedScreenshot;
		r_delayedScreenshotPending = qtrue;
		r_delayedScreenshotFrame = 0;
	} else {
		cmd = (screenshotCommand_t*)R_GetCommandBuffer( sizeof(screenshotCommand_t) );
		if ( !cmd )
			return;
	}

	if (ri.Cmd_Argc() == 2) {
		Com_sprintf( s, sizeof(s), "screenshots/%s.%s", ri.Cmd_Argv(1), ext );
	} else {
		qtime_t t;
		Com_RealTime( &t );
		int ms = min( 999, backEnd.refdef.time & 1023 );
		Com_sprintf( s, sizeof(s), "screenshots/%d_%02d_%02d-%02d_%02d_%02d-%03d.%s",
			1900+t.tm_year, 1+t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, ms, ext );
	}
	ri.Printf( PRINT_ALL, "Wrote %s\n", s );

	cmd->commandId = RC_SCREENSHOT;
	cmd->x = 0;
	cmd->y = 0;
	cmd->width = glConfig.vidWidth;
	cmd->height = glConfig.vidHeight;
	cmd->fileName = s;
	cmd->type = type;
	cmd->conVis = conVis;
}


static void R_ScreenShotTGA_f()
{
	R_TakeScreenshot( "tga", screenshotCommand_t::SS_TGA, qfalse );
}


static void R_ScreenShotJPG_f()
{
	R_TakeScreenshot( "jpg", screenshotCommand_t::SS_JPG, qfalse );
}


static void R_ScreenShotNoConTGA_f()
{
	R_TakeScreenshot( "tga", screenshotCommand_t::SS_TGA, qtrue );
}


static void R_ScreenShotNoConJPG_f()
{
	R_TakeScreenshot( "jpg", screenshotCommand_t::SS_JPG, qtrue );
}


//============================================================================


const void *RB_TakeVideoFrameCmd( const void *data )
{
	int frameSize;
	const videoFrameCommand_t* cmd = (const videoFrameCommand_t*)data;

	qglReadPixels( 0, 0, cmd->width, cmd->height, GL_RGBA,
			GL_UNSIGNED_BYTE, cmd->captureBuffer );

	if( cmd->motionJpeg )
	{
		frameSize = SaveJPGToBuffer( cmd->encodeBuffer, 95,
				cmd->width, cmd->height, cmd->captureBuffer );
	}
	else
	{
		frameSize = cmd->width * cmd->height * 4;

		// Vertically flip the image
		for(int i = 0; i < cmd->height; i++ )
		{
			Com_Memcpy( &cmd->encodeBuffer[ i * ( cmd->width * 4 ) ],
					&cmd->captureBuffer[ ( cmd->height - i - 1 ) * ( cmd->width * 4 ) ],
					cmd->width * 4 );
		}
	}

	ri.CL_WriteAVIVideoFrame( cmd->encodeBuffer, frameSize );

	return (const void *)(cmd + 1);
}


///////////////////////////////////////////////////////////////


void GfxInfo_f( void )
{
	cvar_t* sys_cpustring = ri.Cvar_Get( "sys_cpustring", "", 0 );

	ri.Printf( PRINT_DEVELOPER, "\nGL_VENDOR: %s\n", glConfig.vendor_string );
	ri.Printf( PRINT_DEVELOPER, "GL_RENDERER: %s\n", glConfig.renderer_string );
	ri.Printf( PRINT_DEVELOPER, "GL_VERSION: %s\n", glConfig.version_string );
	ri.Printf( PRINT_DEVELOPER, "GL_EXTENSIONS: %s\n", glConfig.extensions_string );
	ri.Printf( PRINT_ALL, "PIXELFORMAT: RGBA%d Z%d S%d\n", glConfig.colorBits, glConfig.depthBits, glConfig.stencilBits );
	ri.Printf( PRINT_ALL, "MODE: %dx%d ", glConfig.vidWidth, glConfig.vidHeight );
	if ( glInfo.displayFrequency )
		ri.Printf( PRINT_ALL, "%dHz\n", glInfo.displayFrequency );
	else
		ri.Printf( PRINT_ALL, "\n" );

	ri.Printf( PRINT_DEVELOPER, "texturemode: %s\n", r_textureMode->string );
	ri.Printf( PRINT_DEVELOPER, "picmip: %d\n", r_picmip->integer );
	ri.Printf( PRINT_DEVELOPER, "ambient pass: %s\n", r_vertexLight->integer ? "vertex" : "lightmap" );
	if ( r_finish->integer ) {
		ri.Printf( PRINT_DEVELOPER, "Forcing glFinish\n" );
	}

	ri.Printf( PRINT_DEVELOPER, "CPU: %s\n", sys_cpustring->string );
}


///////////////////////////////////////////////////////////////


static const cmdTableItem_t r_cmds[] =
{
	{ "gfxinfo", GfxInfo_f, NULL, "prints display mode info" },
	{ "imagelist", R_ImageList_f, NULL, "prints loaded images" },
	{ "shaderlist", R_ShaderList_f, NULL, "prints loaded shaders" },
	{ "skinlist", R_SkinList_f, NULL, "prints loaded skins" },
	{ "modellist", R_Modellist_f, NULL, "prints loaded models" },
	{ "screenshot", R_ScreenShotTGA_f, NULL, "takes a TARGA (.tga) screenshot" },
	{ "screenshotJPEG", R_ScreenShotJPG_f, NULL, "takes a JPEG (.jpg) screenshot" },
	{ "screenshotnc", R_ScreenShotNoConTGA_f, NULL, "takes a TARGA screenshot w/o the console" },
	{ "screenshotncJPEG", R_ScreenShotNoConJPG_f, NULL, "takes a JPEG screenshot w/o the console" }
};


static const cvarTableItem_t r_cvars[] =
{
	//
	// latched and archived variables
	//
	{ &r_ext_max_anisotropy, "r_ext_max_anisotropy", "16", CVAR_ARCHIVE | CVAR_LATCH, CVART_INTEGER, "0", "16", help_r_ext_max_anisotropy },
	{ &r_msaa, "r_msaa", "4", CVAR_ARCHIVE | CVAR_LATCH, CVART_INTEGER, "0", "16", "anti-aliasing sample count, 0=off" },
	{ &r_picmip, "r_picmip", "0", CVAR_ARCHIVE | CVAR_LATCH, CVART_INTEGER, "0", "16", help_r_picmip },
	{ &r_roundImagesDown, "r_roundImagesDown", "0", CVAR_ARCHIVE | CVAR_LATCH, CVART_BOOL, NULL, NULL, help_r_roundImagesDown },
	{ &r_colorMipLevels, "r_colorMipLevels", "0", CVAR_LATCH, CVART_BOOL, NULL, NULL, "colorizes textures based on their mip level" },
	{ &r_detailTextures, "r_detailtextures", "1", CVAR_ARCHIVE | CVAR_LATCH, CVART_BOOL, NULL, NULL, "enables detail textures shader stages" },
	{ &r_mode, "r_mode", "0", CVAR_ARCHIVE | CVAR_LATCH, CVART_INTEGER, "0", XSTRING(VIDEOMODE_MAX), help_r_mode },
	{ &r_blitMode, "r_blitMode", "0", CVAR_ARCHIVE, CVART_INTEGER, "0", XSTRING(BLITMODE_MAX), help_r_blitMode },
	{ &r_brightness, "r_brightness", "2", CVAR_ARCHIVE | CVAR_LATCH, CVART_FLOAT, "0.25", "32", "overall brightness" },
	// should be called r_lightmapBrightness
	{ &r_mapBrightness, "r_mapBrightness", "4", CVAR_ARCHIVE | CVAR_LATCH, CVART_FLOAT, "0.25", "32", help_r_mapBrightness },
	// should be called r_textureBrightness
	{ &r_intensity, "r_intensity", "1", CVAR_ARCHIVE | CVAR_LATCH, CVART_FLOAT, "1", NULL, "brightness of non-lightmap textures" },
	{ &r_fullscreen, "r_fullscreen", "1", CVAR_ARCHIVE | CVAR_LATCH, CVART_BOOL, NULL, NULL, "full-screen mode" },
	{ &r_width, "r_width", "1280", CVAR_ARCHIVE | CVAR_LATCH, CVART_INTEGER, "320", "65535", "custom window/render width" help_r_mode01 },
	{ &r_height, "r_height", "720", CVAR_ARCHIVE | CVAR_LATCH, CVART_INTEGER, "240", "65535", "custom window/render height" help_r_mode01 },
	{ &r_customaspect, "r_customaspect", "1", CVAR_ARCHIVE | CVAR_LATCH, CVART_INTEGER, "0.1", "10", "custom pixel aspect ratio" help_r_mode01 },
	{ &r_vertexLight, "r_vertexLight", "0", CVAR_ARCHIVE | CVAR_LATCH, CVART_BOOL, NULL, NULL, "disables lightmap texture blending" },
	// note that r_subdivisions > 64 will create rendering artefacts because you'll see the other side of a curved surface when against it
	{ &r_subdivisions, "r_subdivisions", "1", CVAR_ARCHIVE | CVAR_LATCH, CVART_FLOAT, "1", "64", help_r_subdivisions },

	//
	// latched variables that can only change over a restart
	//
	{ &r_displayRefresh, "r_displayRefresh", "0", CVAR_LATCH, CVART_INTEGER, "0", "480", "0 lets the driver decide" },
	{ &r_singleShader, "r_singleShader", "0", CVAR_CHEAT | CVAR_LATCH },

	//
	// archived variables that can change at any time
	//
	{ &r_lodbias, "r_lodbias", "0", CVAR_ARCHIVE, CVART_INTEGER, "0", "16", help_r_lodbias },
	{ &r_flares, "r_flares", "0", CVAR_ARCHIVE, CVART_BOOL, NULL, NULL, "enables light flares" },
	{ &r_ignoreGLErrors, "r_ignoreGLErrors", "1", CVAR_ARCHIVE, CVART_BOOL, NULL, NULL, "if 0, OpenGL errors are fatal" },
	{ &r_fastsky, "r_fastsky", "0", CVAR_ARCHIVE, CVART_BOOL, NULL, NULL, help_r_fastsky },
	{ &r_noportals, "r_noportals", "0", CVAR_ARCHIVE, CVART_BOOL, NULL, NULL, help_r_noportals },
	{ &r_dynamiclight, "r_dynamiclight", "1", CVAR_ARCHIVE, CVART_BOOL, NULL, NULL, "enables dynamic lights" },
	{ &r_finish, "r_finish", "0", CVAR_ARCHIVE, CVART_BOOL, NULL, NULL, "enables glFinish calls" },
	{ &r_textureMode, "r_textureMode", "GL_LINEAR_MIPMAP_LINEAR", CVAR_ARCHIVE, CVART_STRING, NULL, NULL, help_r_textureMode },
	{ &r_swapInterval, "r_swapInterval", "0", CVAR_ARCHIVE, CVART_INTEGER, "0", "8", help_r_swapInterval },
	{ &r_gamma, "r_gamma", "1", CVAR_ARCHIVE, CVART_FLOAT, "0.5", "3", help_r_gamma },
	{ &r_greyscale, "r_greyscale", "0", CVAR_ARCHIVE, CVART_FLOAT, "0", "1", "controls how monochrome the final image looks" },
	{ &r_lightmap, "r_lightmap", "0", CVAR_ARCHIVE, CVART_BOOL, NULL, NULL, help_r_lightmap },
	{ &r_fullbright, "r_fullbright", "0", CVAR_ARCHIVE, CVART_BOOL, NULL, NULL, help_r_fullbright },

	//
	// temporary variables that can change at any time
	//
	{ &r_lodCurveError, "r_lodCurveError", "250", CVAR_CHEAT },
	{ &r_ambientScale, "r_ambientScale", "0.6", CVAR_CHEAT },
	{ &r_directedScale, "r_directedScale", "1", CVAR_CHEAT },
	{ &r_uiFullScreen, "r_uifullscreen", "0", CVAR_TEMP },
	{ &r_showImages, "r_showImages", "0", CVAR_TEMP },
	{ &r_debugLight, "r_debuglight", "0", CVAR_TEMP },
	{ &r_debugSort, "r_debugSort", "0", CVAR_CHEAT },
	{ &r_nocurves, "r_nocurves", "0", CVAR_CHEAT },
	{ &r_drawworld, "r_drawworld", "1", CVAR_CHEAT },
	{ &r_portalOnly, "r_portalOnly", "0", CVAR_CHEAT },
	{ &r_flareSize, "r_flareSize", "40", CVAR_CHEAT },
	{ &r_flareFade, "r_flareFade", "7", CVAR_CHEAT },
	{ &r_flareCoeff, "r_flareCoeff", "150", CVAR_CHEAT },
	{ &r_measureOverdraw, "r_measureOverdraw", "0", CVAR_CHEAT },
	{ &r_lodscale, "r_lodscale", "5", CVAR_CHEAT },
	{ &r_norefresh, "r_norefresh", "0", CVAR_CHEAT },
	{ &r_drawentities, "r_drawentities", "1", CVAR_CHEAT },
	{ &r_nocull, "r_nocull", "0", CVAR_CHEAT },
	{ &r_novis, "r_novis", "0", CVAR_CHEAT },
	{ &r_speeds, "r_speeds", "0", CVAR_CHEAT },
	{ &r_verbose, "r_verbose", "0", CVAR_CHEAT },
	{ &r_debugSurface, "r_debugSurface", "0", CVAR_CHEAT },
	{ &r_nobind, "r_nobind", "0", CVAR_CHEAT },
	{ &r_showtris, "r_showtris", "0", CVAR_CHEAT },
	{ &r_showsky, "r_showsky", "0", CVAR_CHEAT },
	{ &r_shownormals, "r_shownormals", "0", CVAR_CHEAT },
	{ &r_clear, "r_clear", "0", CVAR_CHEAT },
	{ &r_lockpvs, "r_lockpvs", "0", CVAR_CHEAT },
	{ &r_maxpolys, "r_maxpolys", XSTRING(DEFAULT_MAX_POLYS), 0 },
	{ &r_maxpolyverts, "r_maxpolyverts", XSTRING(DEFAULT_MAX_POLYVERTS), 0 }
};


static void R_Register()
{
	ri.Cvar_RegisterTable( r_cvars, ARRAY_LEN(r_cvars) );
	ri.Cmd_RegisterTable( r_cmds, ARRAY_LEN(r_cmds) );
}


void R_Init()
{
	COMPILE_TIME_ASSERT( sizeof(glconfig_t) == 11332 );
	COMPILE_TIME_ASSERT( sizeof(TargaHeader) == 18 );

	QSUBSYSTEM_INIT_START( "Renderer" );

	// clear all our internal state
	Com_Memset( &tr, 0, sizeof( tr ) );
	Com_Memset( &backEnd, 0, sizeof( backEnd ) );
	Com_Memset( &tess, 0, sizeof( tess ) );

	if ((intptr_t)tess.xyz & 15)
		Com_Printf( "WARNING: tess.xyz not 16 byte aligned\n" );

	// init function tables
	//
	for (int i = 0; i < FUNCTABLE_SIZE; ++i)
	{
		tr.sinTable[i]		= sin( DEG2RAD( i * 360.0f / ( ( float ) ( FUNCTABLE_SIZE - 1 ) ) ) );
		tr.squareTable[i]	= ( i < FUNCTABLE_SIZE/2 ) ? 1.0f : -1.0f;
		tr.sawToothTable[i] = (float)i / FUNCTABLE_SIZE;
		tr.inverseSawToothTable[i] = 1.0f - tr.sawToothTable[i];

		if ( i < FUNCTABLE_SIZE / 4 )
			tr.triangleTable[i] = (float)i / (FUNCTABLE_SIZE / 4);
		else if ( i < FUNCTABLE_SIZE / 2 )
			tr.triangleTable[i] = 1.0f - tr.triangleTable[i - FUNCTABLE_SIZE / 4];
		else
			tr.triangleTable[i] = -tr.triangleTable[i - FUNCTABLE_SIZE / 2];
	}

	R_InitFogTable();

	R_NoiseInit();

	R_Register();

	max_polys = max( r_maxpolys->integer, DEFAULT_MAX_POLYS );
	max_polyverts = max( r_maxpolyverts->integer, DEFAULT_MAX_POLYVERTS );

	byte* ptr = (byte*)ri.Hunk_Alloc( sizeof(backEndData_t) + sizeof(srfPoly_t) * max_polys + sizeof(polyVert_t) * max_polyverts, h_low );
	backEndData = (backEndData_t*)ptr;
	backEndData->polys = (srfPoly_t*)(ptr + sizeof(backEndData_t));
	backEndData->polyVerts = (polyVert_t*)(ptr + sizeof(backEndData_t) + sizeof(srfPoly_t) * max_polys);

	R_ClearFrame();

	InitOpenGL();

	R_InitImages();

	R_InitShaders();

	R_InitSkins();

	R_ModelInit();

	R_InitFreeType();

	int err = qglGetError();
	if (err != GL_NO_ERROR)
		ri.Printf( PRINT_ALL, "glGetError() = 0x%x\n", err );

	QSUBSYSTEM_INIT_DONE( "Renderer" );
}


static void RE_Shutdown( qbool destroyWindow )
{
	ri.Printf( PRINT_DEVELOPER, "RE_Shutdown( %i )\n", destroyWindow );

	if ( tr.registered ) {
		ri.Cmd_UnregisterModule();
		R_SyncRenderThread();
		R_DeleteTextures();
	}

	R_DoneFreeType();
	
	// shut down platform specific OpenGL stuff
	if ( destroyWindow ) {
		Sys_GL_Shutdown();
		memset( &glConfig, 0, sizeof( glConfig ) );
		memset( &glState, 0, sizeof( glState ) );
	}

	tr.registered = qfalse;
}


static void RE_BeginRegistration( glconfig_t* glconfigOut )
{
	R_Init();

	*glconfigOut = glConfig;

	R_SyncRenderThread();

	tr.viewCluster = -1;		// force markleafs to regenerate
	R_ClearFlares();
	RE_ClearScene();

	tr.registered = qtrue;
}


// touch all images to make sure they are resident

static void RE_EndRegistration()
{
	R_SyncRenderThread();
	if (!Sys_LowPhysicalMemory()) {
		RB_ShowImages();
	}
}


static int RE_GetCameraMatrixTime()
{
	return re_cameraMatrixTime;
}


const refexport_t* GetRefAPI( const refimport_t* rimp )
{
	static refexport_t re;

	ri = *rimp;

	Com_Memset( &re, 0, sizeof( re ) );

	// the RE_ functions are Renderer Entry points

	re.Shutdown = RE_Shutdown;

	re.BeginRegistration = RE_BeginRegistration;
	re.RegisterModel = RE_RegisterModel;
	re.RegisterSkin = RE_RegisterSkin;
	re.RegisterShader = RE_RegisterShader;
	re.RegisterShaderNoMip = RE_RegisterShaderNoMip;
	re.LoadWorld = RE_LoadWorldMap;
	re.SetWorldVisData = RE_SetWorldVisData;
	re.EndRegistration = RE_EndRegistration;

	re.BeginFrame = RE_BeginFrame;
	re.EndFrame = RE_EndFrame;

	re.MarkFragments = R_MarkFragments;
	re.LerpTag = R_LerpTag;
	re.ModelBounds = R_ModelBounds;

	re.ClearScene = RE_ClearScene;
	re.AddRefEntityToScene = RE_AddRefEntityToScene;
	re.AddPolyToScene = RE_AddPolyToScene;
	re.LightForPoint = R_LightForPoint;
	re.AddLightToScene = RE_AddLightToScene;
	re.RenderScene = RE_RenderScene;

	re.SetColor = RE_SetColor;
	re.DrawStretchPic = RE_StretchPic;
	re.DrawStretchRaw = RE_StretchRaw;
	re.UploadCinematic = RE_UploadCinematic;

	re.RegisterFont = RE_RegisterFont;
	re.GetEntityToken = R_GetEntityToken;
	re.inPVS = R_inPVS;

	re.TakeVideoFrame = RE_TakeVideoFrame;

	re.GetCameraMatrixTime = RE_GetCameraMatrixTime;

	return &re;
}
