#include "../qcommon/q_shared.h"
#include "../renderer/tr_local.h"

#if defined(USE_SDL_VIDEO)
    #include <SDL.h>
    #include <SDL_opengl.h>
#else
    #ifndef _WIN32    
        #include <dlfcn.h>
    #endif    
#endif

#include "qgl_ext.h"

cvar_t *r_arb_vertex_buffer_object;

// ARB_vertex_buffer_object
PFNGLBINDBUFFERARBPROC qglBindBufferARB;
PFNGLDELETEBUFFERSARBPROC qglDeleteBuffersARB;
PFNGLGENBUFFERSARBPROC qglGenBuffersARB;
PFNGLISBUFFERARBPROC qglIsBufferARB;
PFNGLBUFFERDATAARBPROC qglBufferDataARB;
PFNGLBUFFERSUBDATAARBPROC qglBufferSubDataARB;
PFNGLGETBUFFERSUBDATAARBPROC qglGetBufferSubDataARB;
PFNGLMAPBUFFERARBPROC qglMapBufferARB;
PFNGLUNMAPBUFFERARBPROC qglUnmapBufferARB;
PFNGLGETBUFFERPARAMETERIVARBPROC qglGetBufferParameterivARB;
PFNGLGETBUFFERPOINTERVARBPROC qglGetBufferPointervARB;

// ----------------------------------------------------------------
// VBO related stuff
GLuint idVBOData = 0;
GLuint idVBOIndexes = 0;

GLsizeiptrARB szVBOData_XYZ_Offset = 0;
GLsizeiptrARB szVBOData_XYZ_Size = 0;
GLsizeiptrARB szVBOData_TexCoords0_Offset = 0;
GLsizeiptrARB szVBOData_TexCoords0_Size = 0;
GLsizeiptrARB szVBOData_TexCoords1_Offset = 0;
GLsizeiptrARB szVBOData_TexCoords1_Size = 0;
GLsizeiptrARB szVBOData_VertColors_Offset = 0;
GLsizeiptrARB szVBOData_VertColors_Size = 0;
GLsizeiptrARB szVBOData_TotalSize = 0;

GLsizeiptrARB szVBOIndexes_TotalSize = 0;
// ----------------------------------------------------------------
// ----------------------------------------------------------------

#if USE_SDL_VIDEO
#define GPA( a ) SDL_GL_GetProcAddress( a )
qbool GLimp_sdl_init_video(void);
#else
#define GPA( a ) dlsym( glw_state.OpenGLLib, a )
#endif

#ifdef _WIN32
	#define QGL_GetFunction(apicall, fn) q##fn = ( apicall )qwglGetProcAddress( #fn## ); \
	if (!q##fn) Com_Error( ERR_FATAL, "QGL_InitExtensions: "#fn" not found" );
#else
	#define QGL_GetFunction(apicall, fn) q##fn = ( apicall ) GPA( #fn ); \
	if (!q##fn) Com_Error( ERR_FATAL, "QGL_InitExtensions: "#fn" not found" );
#endif

qbool QGL_InitExtensions() {
	r_arb_vertex_buffer_object = ri.Cvar_Get("r_arb_vertex_buffer_object", "0", CVAR_ARCHIVE | CVAR_LATCH);
	
	ri.Printf(PRINT_ALL, "--------------------- QGL_InitExtensions ---------------------\n");
	
	if (strstr(glConfig.extensions_string, "GL_ARB_vertex_buffer_object")) {
		ri.Printf(PRINT_ALL, "+ QGL_InitExtensions: using ARB_vertex_buffer_object\n");
		QGL_GetFunction(PFNGLBINDBUFFERARBPROC, glBindBufferARB);
		QGL_GetFunction(PFNGLDELETEBUFFERSARBPROC, glDeleteBuffersARB);
		QGL_GetFunction(PFNGLGENBUFFERSARBPROC, glGenBuffersARB);
		QGL_GetFunction(PFNGLISBUFFERARBPROC, glIsBufferARB);
		QGL_GetFunction(PFNGLBUFFERDATAARBPROC, glBufferDataARB);
		QGL_GetFunction(PFNGLBUFFERSUBDATAARBPROC, glBufferSubDataARB);
		QGL_GetFunction(PFNGLGETBUFFERSUBDATAARBPROC, glGetBufferSubDataARB);
		QGL_GetFunction(PFNGLMAPBUFFERARBPROC, glMapBufferARB);
		QGL_GetFunction(PFNGLUNMAPBUFFERARBPROC, glUnmapBufferARB);
		QGL_GetFunction(PFNGLGETBUFFERPARAMETERIVARBPROC, glGetBufferParameterivARB);
		QGL_GetFunction(PFNGLGETBUFFERPOINTERVARBPROC, glGetBufferPointervARB);
	}
	else {
		ri.Printf(PRINT_ALL, "- QGL_InitExtensions: ARB_vertex_buffer_object not found\n");
	}
	
	ri.Printf(PRINT_ALL, "--------------------------------------------------------------\n");
	
	return qtrue;
}
