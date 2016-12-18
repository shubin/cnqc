#ifndef __qglext_h_
#define __qglext_h_

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <GL/gl.h>
#include <GL/glext.h>

#include "../qcommon/q_shared.h"

extern cvar_t *r_arb_vertex_buffer_object;

// ARB_vertex_buffer_object
extern PFNGLBINDBUFFERARBPROC qglBindBufferARB;
extern PFNGLDELETEBUFFERSARBPROC qglDeleteBuffersARB;
extern PFNGLGENBUFFERSARBPROC qglGenBuffersARB;
extern PFNGLISBUFFERARBPROC qglIsBufferARB;
extern PFNGLBUFFERDATAARBPROC qglBufferDataARB;
extern PFNGLBUFFERSUBDATAARBPROC qglBufferSubDataARB;
extern PFNGLGETBUFFERSUBDATAARBPROC qglGetBufferSubDataARB;
extern PFNGLMAPBUFFERARBPROC qglMapBufferARB;
extern PFNGLUNMAPBUFFERARBPROC qglUnmapBufferARB;
extern PFNGLGETBUFFERPARAMETERIVARBPROC qglGetBufferParameterivARB;
extern PFNGLGETBUFFERPOINTERVARBPROC qglGetBufferPointervARB;

// ----------------------------------------------------------------
// VBO related stuff
extern GLuint idVBOData;
extern GLuint idVBOIndexes;

extern GLsizeiptrARB szVBOData_XYZ_Offset;
extern GLsizeiptrARB szVBOData_XYZ_Size;
extern GLsizeiptrARB szVBOData_TexCoords0_Offset;
extern GLsizeiptrARB szVBOData_TexCoords0_Size;
extern GLsizeiptrARB szVBOData_TexCoords1_Offset;
extern GLsizeiptrARB szVBOData_TexCoords1_Size;
extern GLsizeiptrARB szVBOData_VertColors_Offset;
extern GLsizeiptrARB szVBOData_VertColors_Size;
extern GLsizeiptrARB szVBOData_TotalSize;

extern GLsizeiptrARB szVBOIndexes_TotalSize;
// ----------------------------------------------------------------
// ----------------------------------------------------------------

qbool QGL_InitExtensions();

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // __qglext_h_
