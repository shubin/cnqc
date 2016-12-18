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

glconfig_t	glConfig;
glinfo_t	glInfo;

glstate_t	glState;

static void GfxInfo_f( void );

cvar_t	*r_verbose;

cvar_t	*r_displayRefresh;

cvar_t	*r_detailTextures;

#ifdef USE_R_SMP
cvar_t	*r_smp;
cvar_t	*r_showSmp;
cvar_t	*r_skipBackEnd;
#endif

cvar_t	*r_intensity;
cvar_t	*r_gamma;
cvar_t	*r_ignorehwgamma;

cvar_t	*r_measureOverdraw;

cvar_t	*r_inGameVideo;
cvar_t	*r_fastsky;
cvar_t	*r_dynamiclight;

cvar_t	*r_lodbias;
cvar_t	*r_lodscale;

cvar_t	*r_norefresh;
cvar_t	*r_drawentities;
cvar_t	*r_drawworld;
cvar_t	*r_speeds;
cvar_t	*r_fullbright;
cvar_t	*r_novis;
cvar_t	*r_nocull;
cvar_t	*r_nocurves;

cvar_t	*r_ext_compressed_textures;
cvar_t	*r_ext_max_anisotropy;
cvar_t	*r_ext_multisample;

cvar_t	*r_ignoreGLErrors;

cvar_t	*r_stencilbits;
cvar_t	*r_depthbits;
cvar_t	*r_colorbits;
cvar_t	*r_stereo;
cvar_t	*r_texturebits;

cvar_t	*r_drawBuffer;
cvar_t	*r_lightmap;
cvar_t	*r_vertexLight;
cvar_t	*r_uiFullScreen;
cvar_t	*r_mode;
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

cvar_t	*r_overBrightBits;
cvar_t	*r_mapOverBrightBits;

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

// 1.32e
cvar_t  *r_clamptoedge;
int     gl_clamp_mode = 0;

static void AssertCvarRange( cvar_t *cv, float minVal, float maxVal, qbool shouldBeIntegral )
{
	if ( shouldBeIntegral )
	{
		if ( ( int ) cv->value != cv->integer )
		{
			ri.Printf( PRINT_WARNING, "WARNING: cvar '%s' must be integral (%f)\n", cv->name, cv->value );
			ri.Cvar_Set( cv->name, va( "%d", cv->integer ) );
		}
	}

	if ( cv->value < minVal )
	{
		ri.Printf( PRINT_WARNING, "WARNING: cvar '%s' out of range (%f < %f)\n", cv->name, cv->value, minVal );
		ri.Cvar_Set( cv->name, va( "%f", minVal ) );
	}
	else if ( cv->value > maxVal )
	{
		ri.Printf( PRINT_WARNING, "WARNING: cvar '%s' out of range (%f > %f)\n", cv->name, cv->value, maxVal );
		ri.Cvar_Set( cv->name, va( "%f", maxVal ) );
	}
}


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


/*
** InitOpenGL
**
** This function is responsible for initializing a valid OpenGL subsystem.  This
** is done by calling GLimp_Init (which gives us a working OGL subsystem) then
** setting variables, checking GL constants, and reporting the gfx system config
** to the user.
*/
static void InitOpenGL()
{
	//
	// initialize OS specific portions of the renderer
	//
	// GLimp_Init directly or indirectly references the following cvars:
	//		- r_fullscreen
	//		- r_mode
	//		- r_(color|depth|stencil)bits
	//		- r_ignorehwgamma
	//		- r_gamma
	//

	if ( glConfig.vidWidth == 0 )
	{
		GLimp_Init();

		qglGetIntegerv( GL_MAX_TEXTURE_SIZE, &glInfo.maxTextureSize );

		qglGetIntegerv( GL_MAX_ELEMENTS_INDICES, &glInfo.maxDrawElementsI );
		qglGetIntegerv( GL_MAX_ELEMENTS_VERTICES, &glInfo.maxDrawElementsV );
	}

	// init command buffers and SMP
	R_InitCommandBuffers();

	GfxInfo_f();

	GL_SetDefaultState();
	
	if (strstr(glConfig.extensions_string, "GL_ARB_vertex_buffer_object")) {
		qglGenBuffersARB(1, &idVBOData);
		qglGenBuffersARB(1, &idVBOIndexes);
	}
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

qbool R_GetModeInfo( int* width, int* height, float* aspect )
{
    if (r_mode->string && *r_mode->string)
    {
        if (r_mode->integer)
        {
            int w(-1), h(-1), hz(-1);
            if (2 <= sscanf( r_mode->string, "%ix%i@%i", &w, &h, &hz))
            {
                r_width->integer = w >= 320 && w <= 2560 ? w : r_width->integer;
                r_height->integer = h >= 240 && h <= 1600 ? h : r_height->integer;
                r_displayRefresh->integer = hz >= 0 && hz <= 200 ? hz : r_displayRefresh->integer;
                r_customaspect->value = (float)r_width->integer / r_height->integer;
            }
            
            *width = r_width->integer;
            *height = r_height->integer;
            *aspect = r_customaspect->value;
            return qtrue;
        }
    }
    
    return qfalse;
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

	if ( /*( tr.overbrightBits > 0 ) &&*/ glConfig.deviceSupportsGamma )
		R_GammaCorrect( pRGB, c );

	ri.FS_WriteFile( fileName, p, sizeof(TargaHeader) + c );
}


static void RB_TakeScreenshotJPG( int x, int y, int width, int height, const char* fileName )
{
	RI_AutoPtr p( width * height * 4 );
	qglReadPixels( x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, p );

	if ( /*( tr.overbrightBits > 0 ) &&*/ glConfig.deviceSupportsGamma )
		R_GammaCorrect( p, width * height * 4 );

	RI_AutoPtr out( width * height * 4 );
	int n = SaveJPGToBuffer( out, 95, width, height, p );
	ri.FS_WriteFile( fileName, out, n );
}


const void* RB_TakeScreenshotCmd( const screenshotCommand_t* cmd )
{
	switch (cmd->type) {
		case screenshotCommand_t::SS_JPG:
			RB_TakeScreenshotJPG( cmd->x, cmd->y, cmd->width, cmd->height, cmd->fileName );
			break;
		case screenshotCommand_t::SS_TGA:
			RB_TakeScreenshotTGA( cmd->x, cmd->y, cmd->width, cmd->height, cmd->fileName );
			break;
	}
	return (const void*)(cmd + 1);
}


// screenshot filename is YYYY_MM_DD-HH_MM_SS-TTT
// so you can find the damn things and you never run out of them for movies  :)

static void R_TakeScreenshot( const char* ext, screenshotCommand_t::ss_type type )
{
	static char s[MAX_OSPATH]; // bad things may happen if we somehow manage to take 2 ss in 1 frame

	screenshotCommand_t* cmd = (screenshotCommand_t*)R_GetCommandBuffer( sizeof(*cmd) );
	if ( !cmd )
		return;

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
}


static void R_ScreenShotTGA_f(void)
{
	R_TakeScreenshot( "tga", screenshotCommand_t::SS_TGA );
}


static void R_ScreenShotJPG_f(void)
{
	R_TakeScreenshot( "jpg", screenshotCommand_t::SS_JPG );
}


//============================================================================


const void *RB_TakeVideoFrameCmd( const void *data )
{
	int frameSize;
	const videoFrameCommand_t* cmd = (const videoFrameCommand_t*)data;

	qglReadPixels( 0, 0, cmd->width, cmd->height, GL_RGBA,
			GL_UNSIGNED_BYTE, cmd->captureBuffer );

	// gamma correct
	if( /*( tr.overbrightBits > 0 ) &&*/ glConfig.deviceSupportsGamma )
		R_GammaCorrect( cmd->captureBuffer, cmd->width * cmd->height * 4 );

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
	const char* enablestrings[] = { "disabled", "enabled" };

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

	if ( glConfig.deviceSupportsGamma )
		ri.Printf( PRINT_DEVELOPER, "GAMMA: hardware w/ %d overbright bits\n", tr.overbrightBits );
	else
		ri.Printf( PRINT_DEVELOPER, "GAMMA: software w/ %d overbright bits\n", tr.overbrightBits );

	ri.Printf( PRINT_DEVELOPER, "texturemode: %s\n", r_textureMode->string );
	ri.Printf( PRINT_DEVELOPER, "picmip: %d\n", r_picmip->integer );
	ri.Printf( PRINT_DEVELOPER, "texture bits: %d\n", r_texturebits->integer );
	ri.Printf( PRINT_DEVELOPER, "compressed textures: %s\n", enablestrings[r_ext_compressed_textures->integer] );
	ri.Printf( PRINT_DEVELOPER, "ambient pass: %s\n", r_vertexLight->integer ? "vertex" : "lightmap" );
	if ( r_finish->integer ) {
		ri.Printf( PRINT_DEVELOPER, "Forcing glFinish\n" );
	}

	ri.Printf( PRINT_DEVELOPER, "CPU: %s\n", sys_cpustring->string );
#ifdef USE_R_SMP
	if ( glInfo.smpActive ) {
		ri.Printf( PRINT_ALL, "Using dual processor acceleration\n" );
	}
#endif	
}


static void R_WindowMode_f() 
{
	const char *str = ri.Cmd_Argv(1);

	if     ( !Q_stricmpn( str, "restart",        7 ) )  GLimp_WindowMode( WMODE_RESTART );
	else if( !Q_stricmpn( str, "minimized",      3 ) )  GLimp_WindowMode( WMODE_SET_MINIMIZED );
	else if( !Q_stricmpn( str, "windowed",       3 ) )  GLimp_WindowMode( WMODE_SET_WINDOWED );
	else if( !Q_stricmpn( str, "fullscreen",     4 ) )  GLimp_WindowMode( WMODE_SET_FULLSCREEN );
	else if( !Q_stricmpn( str, "swapFullscreen", 8 ) )  GLimp_WindowMode( WMODE_SWAP_FULLSCREEN );
	else if( !Q_stricmpn( str, "swapMinimized",  7 ) )  GLimp_WindowMode( WMODE_SWAP_MINIMIZED );
	else
	{
		str = ri.Cmd_Argv(0);
		ri.Printf( PRINT_ALL,
			"How to use:\n"
			"\\%s restart\n"
			"\\%s minimized\n"
			"\\%s windowed\n"
			"\\%s fullscreen\n"
			"\\%s swapFullscreen\n"
			"\\%s swapMinimized\n"
			, str, str, str, str, str, str
			);
	}

	Cvar_Get( "in_keyboardShortcuts", "", 0 )->modified = qtrue;
}

///////////////////////////////////////////////////////////////


static struct r_ConsoleCmd {
	const char* cmd;
	xcommand_t fn;
} const r_ConsoleCmds[] = {
	{ "gfxinfo", GfxInfo_f },
	{ "imagelist", R_ImageList_f },
	{ "shaderlist", R_ShaderList_f },
	{ "skinlist", R_SkinList_f },
	{ "modellist", R_Modellist_f },
	{ "screenshot", R_ScreenShotTGA_f },
	{ "screenshotJPEG", R_ScreenShotJPG_f },
	{ "windowMode", R_WindowMode_f },
	{ }
};


static void R_Register()
{
	//
	// latched and archived variables
	//
	r_ext_compressed_textures = ri.Cvar_Get( "r_ext_compressed_textures", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_ext_max_anisotropy = ri.Cvar_Get( "r_ext_max_anisotropy", "16", CVAR_ARCHIVE | CVAR_LATCH );
	r_ext_multisample = ri.Cvar_Get( "r_ext_multisample", "0", CVAR_ARCHIVE | CVAR_LATCH );
	
	///////////////////////////////////////////////////////////////
	r_maplightBrightness = ri.Cvar_Get("r_maplightBrightness", "1", CVAR_ARCHIVE | CVAR_LATCH);
	AssertCvarRange(r_maplightBrightness, 0.0f, 8.0f, qfalse);
	r_maplightSaturation = ri.Cvar_Get("r_maplightSaturation", "1", CVAR_ARCHIVE | CVAR_LATCH);
	AssertCvarRange(r_maplightSaturation, 0.0f, 4.0f, qfalse);
	r_maplightColorMode = ri.Cvar_Get("r_maplightColorMode", "1", CVAR_ARCHIVE | CVAR_LATCH);
	AssertCvarRange(r_maplightColorMode, 0, 1, qtrue);
	r_maplightColor = ri.Cvar_Get("r_maplightColor", "ffffff", CVAR_ARCHIVE | CVAR_LATCH);

	r_picmip = ri.Cvar_Get ("r_picmip", "0", CVAR_ARCHIVE | CVAR_LATCH );
	AssertCvarRange( r_picmip, 0, 16, qtrue );
	r_roundImagesDown = ri.Cvar_Get ("r_roundImagesDown", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_colorMipLevels = ri.Cvar_Get ("r_colorMipLevels", "0", CVAR_LATCH );
	r_detailTextures = ri.Cvar_Get( "r_detailtextures", "1", CVAR_ARCHIVE | CVAR_LATCH );
	r_texturebits = ri.Cvar_Get( "r_texturebits", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_colorbits = ri.Cvar_Get( "r_colorbits", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_stereo = ri.Cvar_Get( "r_stereo", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_stencilbits = ri.Cvar_Get( "r_stencilbits", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_depthbits = ri.Cvar_Get( "r_depthbits", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_overBrightBits = ri.Cvar_Get( "r_overBrightBits", "1", CVAR_ARCHIVE | CVAR_LATCH );
	r_ignorehwgamma = ri.Cvar_Get( "r_ignorehwgamma", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_mode = ri.Cvar_Get( "r_mode", "0", CVAR_ARCHIVE | CVAR_LATCH );
#if USE_SDL_VIDEO
	r_fullscreen = ri.Cvar_Get( "r_fullscreen", "1", CVAR_ARCHIVE );
#else
    r_fullscreen = ri.Cvar_Get( "r_fullscreen", "1", CVAR_ARCHIVE | CVAR_LATCH );
#endif	
	r_width = ri.Cvar_Get( "r_width", "800", CVAR_ARCHIVE | CVAR_LATCH );
	r_height = ri.Cvar_Get( "r_height", "600", CVAR_ARCHIVE | CVAR_LATCH );
	r_customaspect = ri.Cvar_Get( "r_customaspect", "1.333", CVAR_ARCHIVE | CVAR_LATCH );
	r_vertexLight = ri.Cvar_Get( "r_vertexLight", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_fullbright = ri.Cvar_Get( "r_fullbright", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_subdivisions = ri.Cvar_Get( "r_subdivisions", "4", CVAR_ARCHIVE | CVAR_LATCH );
#ifdef USE_R_SMP
	r_smp = ri.Cvar_Get( "r_smp", "0", CVAR_ARCHIVE | CVAR_LATCH );
    r_showSmp = ri.Cvar_Get ("r_showSmp", "0", CVAR_CHEAT);
    r_skipBackEnd = ri.Cvar_Get ("r_skipBackEnd", "0", CVAR_CHEAT);
#endif	

	//
	// temporary latched variables that can only change over a restart
	//
	r_displayRefresh = ri.Cvar_Get( "r_displayRefresh", "0", CVAR_LATCH );
	AssertCvarRange( r_displayRefresh, 0, 200, qtrue );
	r_mapOverBrightBits = ri.Cvar_Get ("r_mapOverBrightBits", "2", CVAR_LATCH );
	r_intensity = ri.Cvar_Get ("r_intensity", "1", CVAR_ARCHIVE | CVAR_LATCH );
	r_singleShader = ri.Cvar_Get ("r_singleShader", "0", CVAR_CHEAT | CVAR_LATCH );

	//
	// archived variables that can change at any time
	//
	r_lodCurveError = ri.Cvar_Get( "r_lodCurveError", "250", CVAR_CHEAT );
	r_lodbias = ri.Cvar_Get( "r_lodbias", "0", CVAR_ARCHIVE );
	r_flares = ri.Cvar_Get ("r_flares", "0", CVAR_ARCHIVE );
	r_ignoreGLErrors = ri.Cvar_Get( "r_ignoreGLErrors", "1", CVAR_ARCHIVE );
	r_fastsky = ri.Cvar_Get( "r_fastsky", "0", CVAR_ARCHIVE );
	r_noportals = ri.Cvar_Get( "r_noportals", "0", CVAR_ARCHIVE );
	r_inGameVideo = ri.Cvar_Get( "r_inGameVideo", "1", CVAR_ARCHIVE );
	r_dynamiclight = ri.Cvar_Get( "r_dynamiclight", "1", CVAR_ARCHIVE );
	r_finish = ri.Cvar_Get( "r_finish", "0", CVAR_ARCHIVE );
	r_textureMode = ri.Cvar_Get( "r_textureMode", "GL_LINEAR_MIPMAP_NEAREST", CVAR_ARCHIVE );
#if USE_SDL_VIDEO	
	r_swapInterval = ri.Cvar_Get( "r_swapInterval", "0", CVAR_ARCHIVE | CVAR_LATCH );
#else
    r_swapInterval = ri.Cvar_Get( "r_swapInterval", "0", CVAR_ARCHIVE );
#endif	
	r_gamma = ri.Cvar_Get( "r_gamma", "1", CVAR_ARCHIVE );

	r_ambientScale = ri.Cvar_Get( "r_ambientScale", "0.6", CVAR_CHEAT );
	r_directedScale = ri.Cvar_Get( "r_directedScale", "1", CVAR_CHEAT );

	//
	// temporary variables that can change at any time
	//
	r_uiFullScreen = ri.Cvar_Get( "r_uifullscreen", "0", CVAR_TEMP );

	r_showImages = ri.Cvar_Get( "r_showImages", "0", CVAR_TEMP );

	r_debugLight = ri.Cvar_Get( "r_debuglight", "0", CVAR_TEMP );
	r_debugSort = ri.Cvar_Get( "r_debugSort", "0", CVAR_CHEAT );

	r_nocurves = ri.Cvar_Get ("r_nocurves", "0", CVAR_CHEAT );
	r_drawworld = ri.Cvar_Get ("r_drawworld", "1", CVAR_CHEAT );
	r_lightmap = ri.Cvar_Get ("r_lightmap", "0", 0 );
	r_portalOnly = ri.Cvar_Get ("r_portalOnly", "0", CVAR_CHEAT );

	r_flareSize = ri.Cvar_Get ("r_flareSize", "40", CVAR_CHEAT);
	r_flareFade = ri.Cvar_Get ("r_flareFade", "7", CVAR_CHEAT);
	r_flareCoeff = ri.Cvar_Get ("r_flareCoeff", "150", CVAR_CHEAT);


	r_measureOverdraw = ri.Cvar_Get( "r_measureOverdraw", "0", CVAR_CHEAT );
	r_lodscale = ri.Cvar_Get( "r_lodscale", "5", CVAR_CHEAT );
	r_norefresh = ri.Cvar_Get ("r_norefresh", "0", CVAR_CHEAT);
	r_drawentities = ri.Cvar_Get ("r_drawentities", "1", CVAR_CHEAT );
	r_nocull = ri.Cvar_Get ("r_nocull", "0", CVAR_CHEAT);
	r_novis = ri.Cvar_Get ("r_novis", "0", CVAR_CHEAT);
	r_speeds = ri.Cvar_Get ("r_speeds", "0", CVAR_CHEAT);
	r_verbose = ri.Cvar_Get( "r_verbose", "0", CVAR_CHEAT );
	r_debugSurface = ri.Cvar_Get ("r_debugSurface", "0", CVAR_CHEAT);
	r_nobind = ri.Cvar_Get ("r_nobind", "0", CVAR_CHEAT);
	r_showtris = ri.Cvar_Get ("r_showtris", "0", CVAR_CHEAT);
	r_showsky = ri.Cvar_Get ("r_showsky", "0", CVAR_CHEAT);
	r_shownormals = ri.Cvar_Get ("r_shownormals", "0", CVAR_CHEAT);
	r_clear = ri.Cvar_Get ("r_clear", "0", CVAR_CHEAT);
	r_drawBuffer = ri.Cvar_Get( "r_drawBuffer", "GL_BACK", CVAR_CHEAT );
	r_lockpvs = ri.Cvar_Get ("r_lockpvs", "0", CVAR_CHEAT);

	r_maxpolys = ri.Cvar_Get( "r_maxpolys", va("%d", DEFAULT_MAX_POLYS), 0 );
	r_maxpolyverts = ri.Cvar_Get( "r_maxpolyverts", va("%d", DEFAULT_MAX_POLYVERTS), 0 );

    r_clamptoedge =  ri.Cvar_Get( "r_clamptoedge", "0", CVAR_ARCHIVE | CVAR_LATCH );
   
	for (int i = 0; r_ConsoleCmds[i].cmd; ++i) {
		ri.Cmd_AddCommand( r_ConsoleCmds[i].cmd, r_ConsoleCmds[i].fn );
	}
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

	if ((int)tess.xyz & 15)
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

    if ( r_clamptoedge && r_clamptoedge->integer > 0 )
        gl_clamp_mode = GL_CLAMP_TO_EDGE;
    else
        gl_clamp_mode = GL_CLAMP;
    
	byte* ptr = (byte*)ri.Hunk_Alloc( sizeof( *backEndData[0] ) + sizeof(srfPoly_t) * max_polys + sizeof(polyVert_t) * max_polyverts, h_low );
	backEndData[0] = (backEndData_t*)ptr;
	backEndData[0]->polys = (srfPoly_t *) (ptr + sizeof( *backEndData[0] ));
	backEndData[0]->polyVerts = (polyVert_t *) (ptr + sizeof( *backEndData[0] ) + sizeof(srfPoly_t) * max_polys);

	backEndData[1] = NULL;
#ifdef USE_R_SMP
	if (r_smp->integer) {
		ptr = (byte*)ri.Hunk_Alloc( sizeof( *backEndData[1] ) + sizeof(srfPoly_t) * max_polys + sizeof(polyVert_t) * max_polyverts, h_low );
		backEndData[1] = (backEndData_t*)ptr;
		backEndData[1]->polys = (srfPoly_t *) (ptr + sizeof( *backEndData[1] ));
		backEndData[1]->polyVerts = (polyVert_t *) (ptr + sizeof( *backEndData[1] ) + sizeof(srfPoly_t) * max_polys);
	}
#endif
	

	R_ToggleSmpFrame();

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

	for (int i = 0; r_ConsoleCmds[i].cmd; ++i) {
		ri.Cmd_RemoveCommand( r_ConsoleCmds[i].cmd );
	}

	if ( tr.registered ) {
		R_SyncRenderThread();
		R_ShutdownCommandBuffers();
		R_DeleteTextures();
	}

	R_DoneFreeType();
	
	if (strstr(glConfig.extensions_string, "GL_ARB_vertex_buffer_object")) {
		if(qglIsBufferARB(idVBOData)) 
			qglDeleteBuffersARB(1, &idVBOData);
		
		if(qglIsBufferARB(idVBOIndexes)) 
			qglDeleteBuffersARB(1, &idVBOIndexes);
	}

	// shut down platform specific OpenGL stuff
	if ( destroyWindow ) {
		GLimp_Shutdown();
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

	// drakkar
	re.WindowFocus = GLimp_WindowFocus;

	return &re;
}
