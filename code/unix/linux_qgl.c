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
/*
** LINUX_QGL.C
**
** This file implements the operating system binding of GL to QGL function
** pointers.  When doing a port of Quake2 you must implement the following
** two functions:
**
** QGL_Init() - loads libraries, assigns function pointers, etc.
** QGL_Shutdown() - unloads libraries, NULLs function pointers
*/

#include "../qcommon/q_shared.h"
#include <unistd.h>
#include <sys/types.h>

#include <float.h>
#include "unix_glw.h"

// bk001129 - from cvs1.17 (mkv)
#if defined(__FX__)
#include <GL/fxmesa.h>
#endif

#if defined(USE_SDL_VIDEO)
#include <SDL.h>
#include <SDL_opengl.h>
#else
#include <dlfcn.h>
#endif

#include "../renderer/tr_local.h"

// bk001129 - from cvs1.17 (mkv)
#if defined(__FX__)
//FX Mesa Functions
fxMesaContext (*qfxMesaCreateContext)(GLuint win, GrScreenResolution_t, GrScreenRefresh_t, const GLint attribList[]);
fxMesaContext (*qfxMesaCreateBestContext)(GLuint win, GLint width, GLint height, const GLint attribList[]);
void (*qfxMesaDestroyContext)(fxMesaContext ctx);
void (*qfxMesaMakeCurrent)(fxMesaContext ctx);
fxMesaContext (*qfxMesaGetCurrentContext)(void);
void (*qfxMesaSwapBuffers)(void);
#endif

//GLX Functions
#if !defined(USE_SDL_VIDEO)
XVisualInfo * (*qglXChooseVisual)( Display *dpy, int screen, int *attribList );
GLXContext (*qglXCreateContext)( Display *dpy, XVisualInfo *vis, GLXContext shareList, Bool direct );
void (*qglXDestroyContext)( Display *dpy, GLXContext ctx );
Bool (*qglXMakeCurrent)( Display *dpy, GLXDrawable drawable, GLXContext ctx);
void (*qglXCopyContext)( Display *dpy, GLXContext src, GLXContext dst, GLuint mask );
void (*qglXSwapBuffers)( Display *dpy, GLXDrawable drawable );
#endif

void ( APIENTRY * qglSwapIntervalEXT)( int interval );			// added to setup SDL swap interval support
void ( APIENTRY * qglAccum )(GLenum op, GLfloat value);
void ( APIENTRY * qglAlphaFunc )(GLenum func, GLclampf ref);
GLboolean ( APIENTRY * qglAreTexturesResident )(GLsizei n, const GLuint *textures, GLboolean *residences);
void ( APIENTRY * qglArrayElement )(GLint i);
void ( APIENTRY * qglBegin )(GLenum mode);
void ( APIENTRY * qglBindTexture )(GLenum target, GLuint texture);
void ( APIENTRY * qglBitmap )(GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig, GLfloat xmove, GLfloat ymove, const GLubyte *bitmap);
void ( APIENTRY * qglBlendFunc )(GLenum sfactor, GLenum dfactor);
void ( APIENTRY * qglCallList )(GLuint list);
void ( APIENTRY * qglCallLists )(GLsizei n, GLenum type, const GLvoid *lists);
void ( APIENTRY * qglClear )(GLbitfield mask);
void ( APIENTRY * qglClearAccum )(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void ( APIENTRY * qglClearColor )(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
void ( APIENTRY * qglClearDepth )(GLclampd depth);
void ( APIENTRY * qglClearIndex )(GLfloat c);
void ( APIENTRY * qglClearStencil )(GLint s);
void ( APIENTRY * qglClipPlane )(GLenum plane, const GLdouble *equation);
void ( APIENTRY * qglColor3b )(GLbyte red, GLbyte green, GLbyte blue);
void ( APIENTRY * qglColor3bv )(const GLbyte *v);
void ( APIENTRY * qglColor3d )(GLdouble red, GLdouble green, GLdouble blue);
void ( APIENTRY * qglColor3dv )(const GLdouble *v);
void ( APIENTRY * qglColor3f )(GLfloat red, GLfloat green, GLfloat blue);
void ( APIENTRY * qglColor3fv )(const GLfloat *v);
void ( APIENTRY * qglColor3i )(GLint red, GLint green, GLint blue);
void ( APIENTRY * qglColor3iv )(const GLint *v);
void ( APIENTRY * qglColor3s )(GLshort red, GLshort green, GLshort blue);
void ( APIENTRY * qglColor3sv )(const GLshort *v);
void ( APIENTRY * qglColor3ub )(GLubyte red, GLubyte green, GLubyte blue);
void ( APIENTRY * qglColor3ubv )(const GLubyte *v);
void ( APIENTRY * qglColor3ui )(GLuint red, GLuint green, GLuint blue);
void ( APIENTRY * qglColor3uiv )(const GLuint *v);
void ( APIENTRY * qglColor3us )(GLushort red, GLushort green, GLushort blue);
void ( APIENTRY * qglColor3usv )(const GLushort *v);
void ( APIENTRY * qglColor4b )(GLbyte red, GLbyte green, GLbyte blue, GLbyte alpha);
void ( APIENTRY * qglColor4bv )(const GLbyte *v);
void ( APIENTRY * qglColor4d )(GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha);
void ( APIENTRY * qglColor4dv )(const GLdouble *v);
void ( APIENTRY * qglColor4f )(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void ( APIENTRY * qglColor4fv )(const GLfloat *v);
void ( APIENTRY * qglColor4i )(GLint red, GLint green, GLint blue, GLint alpha);
void ( APIENTRY * qglColor4iv )(const GLint *v);
void ( APIENTRY * qglColor4s )(GLshort red, GLshort green, GLshort blue, GLshort alpha);
void ( APIENTRY * qglColor4sv )(const GLshort *v);
void ( APIENTRY * qglColor4ub )(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha);
void ( APIENTRY * qglColor4ubv )(const GLubyte *v);
void ( APIENTRY * qglColor4ui )(GLuint red, GLuint green, GLuint blue, GLuint alpha);
void ( APIENTRY * qglColor4uiv )(const GLuint *v);
void ( APIENTRY * qglColor4us )(GLushort red, GLushort green, GLushort blue, GLushort alpha);
void ( APIENTRY * qglColor4usv )(const GLushort *v);
void ( APIENTRY * qglColorMask )(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
void ( APIENTRY * qglColorMaterial )(GLenum face, GLenum mode);
void ( APIENTRY * qglColorPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void ( APIENTRY * qglCopyPixels )(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type);
void ( APIENTRY * qglCopyTexImage1D )(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLint border);
void ( APIENTRY * qglCopyTexImage2D )(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
void ( APIENTRY * qglCopyTexSubImage1D )(GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width);
void ( APIENTRY * qglCopyTexSubImage2D )(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
void ( APIENTRY * qglCullFace )(GLenum mode);
void ( APIENTRY * qglDeleteLists )(GLuint list, GLsizei range);
void ( APIENTRY * qglDeleteTextures )(GLsizei n, const GLuint *textures);
void ( APIENTRY * qglDepthFunc )(GLenum func);
void ( APIENTRY * qglDepthMask )(GLboolean flag);
void ( APIENTRY * qglDepthRange )(GLclampd zNear, GLclampd zFar);
void ( APIENTRY * qglDisable )(GLenum cap);
void ( APIENTRY * qglDisableClientState )(GLenum array);
void ( APIENTRY * qglDrawArrays )(GLenum mode, GLint first, GLsizei count);
void ( APIENTRY * qglDrawBuffer )(GLenum mode);
void ( APIENTRY * qglDrawElements )(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
void ( APIENTRY * qglDrawPixels )(GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
void ( APIENTRY * qglEdgeFlag )(GLboolean flag);
void ( APIENTRY * qglEdgeFlagPointer )(GLsizei stride, const GLvoid *pointer);
void ( APIENTRY * qglEdgeFlagv )(const GLboolean *flag);
void ( APIENTRY * qglEnable )(GLenum cap);
void ( APIENTRY * qglEnableClientState )(GLenum array);
void ( APIENTRY * qglEnd )(void);
void ( APIENTRY * qglEndList )(void);
void ( APIENTRY * qglEvalCoord1d )(GLdouble u);
void ( APIENTRY * qglEvalCoord1dv )(const GLdouble *u);
void ( APIENTRY * qglEvalCoord1f )(GLfloat u);
void ( APIENTRY * qglEvalCoord1fv )(const GLfloat *u);
void ( APIENTRY * qglEvalCoord2d )(GLdouble u, GLdouble v);
void ( APIENTRY * qglEvalCoord2dv )(const GLdouble *u);
void ( APIENTRY * qglEvalCoord2f )(GLfloat u, GLfloat v);
void ( APIENTRY * qglEvalCoord2fv )(const GLfloat *u);
void ( APIENTRY * qglEvalMesh1 )(GLenum mode, GLint i1, GLint i2);
void ( APIENTRY * qglEvalMesh2 )(GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2);
void ( APIENTRY * qglEvalPoint1 )(GLint i);
void ( APIENTRY * qglEvalPoint2 )(GLint i, GLint j);
void ( APIENTRY * qglFeedbackBuffer )(GLsizei size, GLenum type, GLfloat *buffer);
void ( APIENTRY * qglFinish )(void);
void ( APIENTRY * qglFlush )(void);
void ( APIENTRY * qglFogf )(GLenum pname, GLfloat param);
void ( APIENTRY * qglFogfv )(GLenum pname, const GLfloat *params);
void ( APIENTRY * qglFogi )(GLenum pname, GLint param);
void ( APIENTRY * qglFogiv )(GLenum pname, const GLint *params);
void ( APIENTRY * qglFrontFace )(GLenum mode);
void ( APIENTRY * qglFrustum )(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
GLuint ( APIENTRY * qglGenLists )(GLsizei range);
void ( APIENTRY * qglGenTextures )(GLsizei n, GLuint *textures);
void ( APIENTRY * qglGetBooleanv )(GLenum pname, GLboolean *params);
void ( APIENTRY * qglGetClipPlane )(GLenum plane, GLdouble *equation);
void ( APIENTRY * qglGetDoublev )(GLenum pname, GLdouble *params);
GLenum ( APIENTRY * qglGetError )(void);
void ( APIENTRY * qglGetFloatv )(GLenum pname, GLfloat *params);
void ( APIENTRY * qglGetIntegerv )(GLenum pname, GLint *params);
void ( APIENTRY * qglGetLightfv )(GLenum light, GLenum pname, GLfloat *params);
void ( APIENTRY * qglGetLightiv )(GLenum light, GLenum pname, GLint *params);
void ( APIENTRY * qglGetMapdv )(GLenum target, GLenum query, GLdouble *v);
void ( APIENTRY * qglGetMapfv )(GLenum target, GLenum query, GLfloat *v);
void ( APIENTRY * qglGetMapiv )(GLenum target, GLenum query, GLint *v);
void ( APIENTRY * qglGetMaterialfv )(GLenum face, GLenum pname, GLfloat *params);
void ( APIENTRY * qglGetMaterialiv )(GLenum face, GLenum pname, GLint *params);
void ( APIENTRY * qglGetPixelMapfv )(GLenum map, GLfloat *values);
void ( APIENTRY * qglGetPixelMapuiv )(GLenum map, GLuint *values);
void ( APIENTRY * qglGetPixelMapusv )(GLenum map, GLushort *values);
void ( APIENTRY * qglGetPointerv )(GLenum pname, GLvoid* *params);
void ( APIENTRY * qglGetPolygonStipple )(GLubyte *mask);
const GLubyte * ( APIENTRY * qglGetString )(GLenum name);
void ( APIENTRY * qglGetTexEnvfv )(GLenum target, GLenum pname, GLfloat *params);
void ( APIENTRY * qglGetTexEnviv )(GLenum target, GLenum pname, GLint *params);
void ( APIENTRY * qglGetTexGendv )(GLenum coord, GLenum pname, GLdouble *params);
void ( APIENTRY * qglGetTexGenfv )(GLenum coord, GLenum pname, GLfloat *params);
void ( APIENTRY * qglGetTexGeniv )(GLenum coord, GLenum pname, GLint *params);
void ( APIENTRY * qglGetTexImage )(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels);
void ( APIENTRY * qglGetTexLevelParameterfv )(GLenum target, GLint level, GLenum pname, GLfloat *params);
void ( APIENTRY * qglGetTexLevelParameteriv )(GLenum target, GLint level, GLenum pname, GLint *params);
void ( APIENTRY * qglGetTexParameterfv )(GLenum target, GLenum pname, GLfloat *params);
void ( APIENTRY * qglGetTexParameteriv )(GLenum target, GLenum pname, GLint *params);
void ( APIENTRY * qglHint )(GLenum target, GLenum mode);
void ( APIENTRY * qglIndexMask )(GLuint mask);
void ( APIENTRY * qglIndexPointer )(GLenum type, GLsizei stride, const GLvoid *pointer);
void ( APIENTRY * qglIndexd )(GLdouble c);
void ( APIENTRY * qglIndexdv )(const GLdouble *c);
void ( APIENTRY * qglIndexf )(GLfloat c);
void ( APIENTRY * qglIndexfv )(const GLfloat *c);
void ( APIENTRY * qglIndexi )(GLint c);
void ( APIENTRY * qglIndexiv )(const GLint *c);
void ( APIENTRY * qglIndexs )(GLshort c);
void ( APIENTRY * qglIndexsv )(const GLshort *c);
void ( APIENTRY * qglIndexub )(GLubyte c);
void ( APIENTRY * qglIndexubv )(const GLubyte *c);
void ( APIENTRY * qglInitNames )(void);
void ( APIENTRY * qglInterleavedArrays )(GLenum format, GLsizei stride, const GLvoid *pointer);
GLboolean ( APIENTRY * qglIsEnabled )(GLenum cap);
GLboolean ( APIENTRY * qglIsList )(GLuint list);
GLboolean ( APIENTRY * qglIsTexture )(GLuint texture);
void ( APIENTRY * qglLightModelf )(GLenum pname, GLfloat param);
void ( APIENTRY * qglLightModelfv )(GLenum pname, const GLfloat *params);
void ( APIENTRY * qglLightModeli )(GLenum pname, GLint param);
void ( APIENTRY * qglLightModeliv )(GLenum pname, const GLint *params);
void ( APIENTRY * qglLightf )(GLenum light, GLenum pname, GLfloat param);
void ( APIENTRY * qglLightfv )(GLenum light, GLenum pname, const GLfloat *params);
void ( APIENTRY * qglLighti )(GLenum light, GLenum pname, GLint param);
void ( APIENTRY * qglLightiv )(GLenum light, GLenum pname, const GLint *params);
void ( APIENTRY * qglLineStipple )(GLint factor, GLushort pattern);
void ( APIENTRY * qglLineWidth )(GLfloat width);
void ( APIENTRY * qglListBase )(GLuint base);
void ( APIENTRY * qglLoadIdentity )(void);
void ( APIENTRY * qglLoadMatrixd )(const GLdouble *m);
void ( APIENTRY * qglLoadMatrixf )(const GLfloat *m);
void ( APIENTRY * qglLoadName )(GLuint name);
void ( APIENTRY * qglLogicOp )(GLenum opcode);
void ( APIENTRY * qglMap1d )(GLenum target, GLdouble u1, GLdouble u2, GLint stride, GLint order, const GLdouble *points);
void ( APIENTRY * qglMap1f )(GLenum target, GLfloat u1, GLfloat u2, GLint stride, GLint order, const GLfloat *points);
void ( APIENTRY * qglMap2d )(GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder, GLdouble v1, GLdouble v2, GLint vstride, GLint vorder, const GLdouble *points);
void ( APIENTRY * qglMap2f )(GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder, GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, const GLfloat *points);
void ( APIENTRY * qglMapGrid1d )(GLint un, GLdouble u1, GLdouble u2);
void ( APIENTRY * qglMapGrid1f )(GLint un, GLfloat u1, GLfloat u2);
void ( APIENTRY * qglMapGrid2d )(GLint un, GLdouble u1, GLdouble u2, GLint vn, GLdouble v1, GLdouble v2);
void ( APIENTRY * qglMapGrid2f )(GLint un, GLfloat u1, GLfloat u2, GLint vn, GLfloat v1, GLfloat v2);
void ( APIENTRY * qglMaterialf )(GLenum face, GLenum pname, GLfloat param);
void ( APIENTRY * qglMaterialfv )(GLenum face, GLenum pname, const GLfloat *params);
void ( APIENTRY * qglMateriali )(GLenum face, GLenum pname, GLint param);
void ( APIENTRY * qglMaterialiv )(GLenum face, GLenum pname, const GLint *params);
void ( APIENTRY * qglMatrixMode )(GLenum mode);
void ( APIENTRY * qglMultMatrixd )(const GLdouble *m);
void ( APIENTRY * qglMultMatrixf )(const GLfloat *m);
void ( APIENTRY * qglNewList )(GLuint list, GLenum mode);
void ( APIENTRY * qglNormal3b )(GLbyte nx, GLbyte ny, GLbyte nz);
void ( APIENTRY * qglNormal3bv )(const GLbyte *v);
void ( APIENTRY * qglNormal3d )(GLdouble nx, GLdouble ny, GLdouble nz);
void ( APIENTRY * qglNormal3dv )(const GLdouble *v);
void ( APIENTRY * qglNormal3f )(GLfloat nx, GLfloat ny, GLfloat nz);
void ( APIENTRY * qglNormal3fv )(const GLfloat *v);
void ( APIENTRY * qglNormal3i )(GLint nx, GLint ny, GLint nz);
void ( APIENTRY * qglNormal3iv )(const GLint *v);
void ( APIENTRY * qglNormal3s )(GLshort nx, GLshort ny, GLshort nz);
void ( APIENTRY * qglNormal3sv )(const GLshort *v);
void ( APIENTRY * qglNormalPointer )(GLenum type, GLsizei stride, const GLvoid *pointer);
void ( APIENTRY * qglOrtho )(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
void ( APIENTRY * qglPassThrough )(GLfloat token);
void ( APIENTRY * qglPixelMapfv )(GLenum map, GLsizei mapsize, const GLfloat *values);
void ( APIENTRY * qglPixelMapuiv )(GLenum map, GLsizei mapsize, const GLuint *values);
void ( APIENTRY * qglPixelMapusv )(GLenum map, GLsizei mapsize, const GLushort *values);
void ( APIENTRY * qglPixelStoref )(GLenum pname, GLfloat param);
void ( APIENTRY * qglPixelStorei )(GLenum pname, GLint param);
void ( APIENTRY * qglPixelTransferf )(GLenum pname, GLfloat param);
void ( APIENTRY * qglPixelTransferi )(GLenum pname, GLint param);
void ( APIENTRY * qglPixelZoom )(GLfloat xfactor, GLfloat yfactor);
void ( APIENTRY * qglPointSize )(GLfloat size);
void ( APIENTRY * qglPolygonMode )(GLenum face, GLenum mode);
void ( APIENTRY * qglPolygonOffset )(GLfloat factor, GLfloat units);
void ( APIENTRY * qglPolygonStipple )(const GLubyte *mask);
void ( APIENTRY * qglPopAttrib )(void);
void ( APIENTRY * qglPopClientAttrib )(void);
void ( APIENTRY * qglPopMatrix )(void);
void ( APIENTRY * qglPopName )(void);
void ( APIENTRY * qglPrioritizeTextures )(GLsizei n, const GLuint *textures, const GLclampf *priorities);
void ( APIENTRY * qglPushAttrib )(GLbitfield mask);
void ( APIENTRY * qglPushClientAttrib )(GLbitfield mask);
void ( APIENTRY * qglPushMatrix )(void);
void ( APIENTRY * qglPushName )(GLuint name);
void ( APIENTRY * qglRasterPos2d )(GLdouble x, GLdouble y);
void ( APIENTRY * qglRasterPos2dv )(const GLdouble *v);
void ( APIENTRY * qglRasterPos2f )(GLfloat x, GLfloat y);
void ( APIENTRY * qglRasterPos2fv )(const GLfloat *v);
void ( APIENTRY * qglRasterPos2i )(GLint x, GLint y);
void ( APIENTRY * qglRasterPos2iv )(const GLint *v);
void ( APIENTRY * qglRasterPos2s )(GLshort x, GLshort y);
void ( APIENTRY * qglRasterPos2sv )(const GLshort *v);
void ( APIENTRY * qglRasterPos3d )(GLdouble x, GLdouble y, GLdouble z);
void ( APIENTRY * qglRasterPos3dv )(const GLdouble *v);
void ( APIENTRY * qglRasterPos3f )(GLfloat x, GLfloat y, GLfloat z);
void ( APIENTRY * qglRasterPos3fv )(const GLfloat *v);
void ( APIENTRY * qglRasterPos3i )(GLint x, GLint y, GLint z);
void ( APIENTRY * qglRasterPos3iv )(const GLint *v);
void ( APIENTRY * qglRasterPos3s )(GLshort x, GLshort y, GLshort z);
void ( APIENTRY * qglRasterPos3sv )(const GLshort *v);
void ( APIENTRY * qglRasterPos4d )(GLdouble x, GLdouble y, GLdouble z, GLdouble w);
void ( APIENTRY * qglRasterPos4dv )(const GLdouble *v);
void ( APIENTRY * qglRasterPos4f )(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void ( APIENTRY * qglRasterPos4fv )(const GLfloat *v);
void ( APIENTRY * qglRasterPos4i )(GLint x, GLint y, GLint z, GLint w);
void ( APIENTRY * qglRasterPos4iv )(const GLint *v);
void ( APIENTRY * qglRasterPos4s )(GLshort x, GLshort y, GLshort z, GLshort w);
void ( APIENTRY * qglRasterPos4sv )(const GLshort *v);
void ( APIENTRY * qglReadBuffer )(GLenum mode);
void ( APIENTRY * qglReadPixels )(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);
void ( APIENTRY * qglRectd )(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2);
void ( APIENTRY * qglRectdv )(const GLdouble *v1, const GLdouble *v2);
void ( APIENTRY * qglRectf )(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2);
void ( APIENTRY * qglRectfv )(const GLfloat *v1, const GLfloat *v2);
void ( APIENTRY * qglRecti )(GLint x1, GLint y1, GLint x2, GLint y2);
void ( APIENTRY * qglRectiv )(const GLint *v1, const GLint *v2);
void ( APIENTRY * qglRects )(GLshort x1, GLshort y1, GLshort x2, GLshort y2);
void ( APIENTRY * qglRectsv )(const GLshort *v1, const GLshort *v2);
GLint ( APIENTRY * qglRenderMode )(GLenum mode);
void ( APIENTRY * qglRotated )(GLdouble angle, GLdouble x, GLdouble y, GLdouble z);
void ( APIENTRY * qglRotatef )(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void ( APIENTRY * qglScaled )(GLdouble x, GLdouble y, GLdouble z);
void ( APIENTRY * qglScalef )(GLfloat x, GLfloat y, GLfloat z);
void ( APIENTRY * qglScissor )(GLint x, GLint y, GLsizei width, GLsizei height);
void ( APIENTRY * qglSelectBuffer )(GLsizei size, GLuint *buffer);
void ( APIENTRY * qglShadeModel )(GLenum mode);
void ( APIENTRY * qglStencilFunc )(GLenum func, GLint ref, GLuint mask);
void ( APIENTRY * qglStencilMask )(GLuint mask);
void ( APIENTRY * qglStencilOp )(GLenum fail, GLenum zfail, GLenum zpass);
void ( APIENTRY * qglTexCoord1d )(GLdouble s);
void ( APIENTRY * qglTexCoord1dv )(const GLdouble *v);
void ( APIENTRY * qglTexCoord1f )(GLfloat s);
void ( APIENTRY * qglTexCoord1fv )(const GLfloat *v);
void ( APIENTRY * qglTexCoord1i )(GLint s);
void ( APIENTRY * qglTexCoord1iv )(const GLint *v);
void ( APIENTRY * qglTexCoord1s )(GLshort s);
void ( APIENTRY * qglTexCoord1sv )(const GLshort *v);
void ( APIENTRY * qglTexCoord2d )(GLdouble s, GLdouble t);
void ( APIENTRY * qglTexCoord2dv )(const GLdouble *v);
void ( APIENTRY * qglTexCoord2f )(GLfloat s, GLfloat t);
void ( APIENTRY * qglTexCoord2fv )(const GLfloat *v);
void ( APIENTRY * qglTexCoord2i )(GLint s, GLint t);
void ( APIENTRY * qglTexCoord2iv )(const GLint *v);
void ( APIENTRY * qglTexCoord2s )(GLshort s, GLshort t);
void ( APIENTRY * qglTexCoord2sv )(const GLshort *v);
void ( APIENTRY * qglTexCoord3d )(GLdouble s, GLdouble t, GLdouble r);
void ( APIENTRY * qglTexCoord3dv )(const GLdouble *v);
void ( APIENTRY * qglTexCoord3f )(GLfloat s, GLfloat t, GLfloat r);
void ( APIENTRY * qglTexCoord3fv )(const GLfloat *v);
void ( APIENTRY * qglTexCoord3i )(GLint s, GLint t, GLint r);
void ( APIENTRY * qglTexCoord3iv )(const GLint *v);
void ( APIENTRY * qglTexCoord3s )(GLshort s, GLshort t, GLshort r);
void ( APIENTRY * qglTexCoord3sv )(const GLshort *v);
void ( APIENTRY * qglTexCoord4d )(GLdouble s, GLdouble t, GLdouble r, GLdouble q);
void ( APIENTRY * qglTexCoord4dv )(const GLdouble *v);
void ( APIENTRY * qglTexCoord4f )(GLfloat s, GLfloat t, GLfloat r, GLfloat q);
void ( APIENTRY * qglTexCoord4fv )(const GLfloat *v);
void ( APIENTRY * qglTexCoord4i )(GLint s, GLint t, GLint r, GLint q);
void ( APIENTRY * qglTexCoord4iv )(const GLint *v);
void ( APIENTRY * qglTexCoord4s )(GLshort s, GLshort t, GLshort r, GLshort q);
void ( APIENTRY * qglTexCoord4sv )(const GLshort *v);
void ( APIENTRY * qglTexCoordPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void ( APIENTRY * qglTexEnvf )(GLenum target, GLenum pname, GLfloat param);
void ( APIENTRY * qglTexEnvfv )(GLenum target, GLenum pname, const GLfloat *params);
void ( APIENTRY * qglTexEnvi )(GLenum target, GLenum pname, GLint param);
void ( APIENTRY * qglTexEnviv )(GLenum target, GLenum pname, const GLint *params);
void ( APIENTRY * qglTexGend )(GLenum coord, GLenum pname, GLdouble param);
void ( APIENTRY * qglTexGendv )(GLenum coord, GLenum pname, const GLdouble *params);
void ( APIENTRY * qglTexGenf )(GLenum coord, GLenum pname, GLfloat param);
void ( APIENTRY * qglTexGenfv )(GLenum coord, GLenum pname, const GLfloat *params);
void ( APIENTRY * qglTexGeni )(GLenum coord, GLenum pname, GLint param);
void ( APIENTRY * qglTexGeniv )(GLenum coord, GLenum pname, const GLint *params);
void ( APIENTRY * qglTexImage1D )(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void ( APIENTRY * qglTexImage2D )(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void ( APIENTRY * qglTexParameterf )(GLenum target, GLenum pname, GLfloat param);
void ( APIENTRY * qglTexParameterfv )(GLenum target, GLenum pname, const GLfloat *params);
void ( APIENTRY * qglTexParameteri )(GLenum target, GLenum pname, GLint param);
void ( APIENTRY * qglTexParameteriv )(GLenum target, GLenum pname, const GLint *params);
void ( APIENTRY * qglTexSubImage1D )(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels);
void ( APIENTRY * qglTexSubImage2D )(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
void ( APIENTRY * qglTranslated )(GLdouble x, GLdouble y, GLdouble z);
void ( APIENTRY * qglTranslatef )(GLfloat x, GLfloat y, GLfloat z);
void ( APIENTRY * qglVertex2d )(GLdouble x, GLdouble y);
void ( APIENTRY * qglVertex2dv )(const GLdouble *v);
void ( APIENTRY * qglVertex2f )(GLfloat x, GLfloat y);
void ( APIENTRY * qglVertex2fv )(const GLfloat *v);
void ( APIENTRY * qglVertex2i )(GLint x, GLint y);
void ( APIENTRY * qglVertex2iv )(const GLint *v);
void ( APIENTRY * qglVertex2s )(GLshort x, GLshort y);
void ( APIENTRY * qglVertex2sv )(const GLshort *v);
void ( APIENTRY * qglVertex3d )(GLdouble x, GLdouble y, GLdouble z);
void ( APIENTRY * qglVertex3dv )(const GLdouble *v);
void ( APIENTRY * qglVertex3f )(GLfloat x, GLfloat y, GLfloat z);
void ( APIENTRY * qglVertex3fv )(const GLfloat *v);
void ( APIENTRY * qglVertex3i )(GLint x, GLint y, GLint z);
void ( APIENTRY * qglVertex3iv )(const GLint *v);
void ( APIENTRY * qglVertex3s )(GLshort x, GLshort y, GLshort z);
void ( APIENTRY * qglVertex3sv )(const GLshort *v);
void ( APIENTRY * qglVertex4d )(GLdouble x, GLdouble y, GLdouble z, GLdouble w);
void ( APIENTRY * qglVertex4dv )(const GLdouble *v);
void ( APIENTRY * qglVertex4f )(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void ( APIENTRY * qglVertex4fv )(const GLfloat *v);
void ( APIENTRY * qglVertex4i )(GLint x, GLint y, GLint z, GLint w);
void ( APIENTRY * qglVertex4iv )(const GLint *v);
void ( APIENTRY * qglVertex4s )(GLshort x, GLshort y, GLshort z, GLshort w);
void ( APIENTRY * qglVertex4sv )(const GLshort *v);
void ( APIENTRY * qglVertexPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
void ( APIENTRY * qglViewport )(GLint x, GLint y, GLsizei width, GLsizei height);

void ( APIENTRY * qglMultiTexCoord2fARB )( GLenum texture, GLfloat s, GLfloat t );
void ( APIENTRY * qglActiveTextureARB )( GLenum texture );
void ( APIENTRY * qglClientActiveTextureARB )( GLenum texture );

PFNGLLOCKARRAYSEXTPROC qglLockArraysEXT;
PFNGLUNLOCKARRAYSEXTPROC qglUnlockArraysEXT;

void ( APIENTRY * qglPointParameterfEXT)( GLenum param, GLfloat value );
void ( APIENTRY * qglPointParameterfvEXT)( GLenum param, const GLfloat *value );
void ( APIENTRY * qglColorTableEXT)( int, int, int, int, int, const void * );
void ( APIENTRY * qgl3DfxSetPaletteEXT)( GLuint * );
void ( APIENTRY * qglSelectTextureSGIS)( GLenum );
void ( APIENTRY * qglMTexCoord2fSGIS)( GLenum, GLfloat, GLfloat );

static void ( APIENTRY * dllAccum )(GLenum op, GLfloat value);
static void ( APIENTRY * dllAlphaFunc )(GLenum func, GLclampf ref);
GLboolean ( APIENTRY * dllAreTexturesResident )(GLsizei n, const GLuint *textures, GLboolean *residences);
static void ( APIENTRY * dllArrayElement )(GLint i);
static void ( APIENTRY * dllBegin )(GLenum mode);
static void ( APIENTRY * dllBindTexture )(GLenum target, GLuint texture);
static void ( APIENTRY * dllBitmap )(GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig, GLfloat xmove, GLfloat ymove, const GLubyte *bitmap);
static void ( APIENTRY * dllBlendFunc )(GLenum sfactor, GLenum dfactor);
static void ( APIENTRY * dllCallList )(GLuint list);
static void ( APIENTRY * dllCallLists )(GLsizei n, GLenum type, const GLvoid *lists);
static void ( APIENTRY * dllClear )(GLbitfield mask);
static void ( APIENTRY * dllClearAccum )(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
static void ( APIENTRY * dllClearColor )(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
static void ( APIENTRY * dllClearDepth )(GLclampd depth);
static void ( APIENTRY * dllClearIndex )(GLfloat c);
static void ( APIENTRY * dllClearStencil )(GLint s);
static void ( APIENTRY * dllClipPlane )(GLenum plane, const GLdouble *equation);
static void ( APIENTRY * dllColor3b )(GLbyte red, GLbyte green, GLbyte blue);
static void ( APIENTRY * dllColor3bv )(const GLbyte *v);
static void ( APIENTRY * dllColor3d )(GLdouble red, GLdouble green, GLdouble blue);
static void ( APIENTRY * dllColor3dv )(const GLdouble *v);
static void ( APIENTRY * dllColor3f )(GLfloat red, GLfloat green, GLfloat blue);
static void ( APIENTRY * dllColor3fv )(const GLfloat *v);
static void ( APIENTRY * dllColor3i )(GLint red, GLint green, GLint blue);
static void ( APIENTRY * dllColor3iv )(const GLint *v);
static void ( APIENTRY * dllColor3s )(GLshort red, GLshort green, GLshort blue);
static void ( APIENTRY * dllColor3sv )(const GLshort *v);
static void ( APIENTRY * dllColor3ub )(GLubyte red, GLubyte green, GLubyte blue);
static void ( APIENTRY * dllColor3ubv )(const GLubyte *v);
static void ( APIENTRY * dllColor3ui )(GLuint red, GLuint green, GLuint blue);
static void ( APIENTRY * dllColor3uiv )(const GLuint *v);
static void ( APIENTRY * dllColor3us )(GLushort red, GLushort green, GLushort blue);
static void ( APIENTRY * dllColor3usv )(const GLushort *v);
static void ( APIENTRY * dllColor4b )(GLbyte red, GLbyte green, GLbyte blue, GLbyte alpha);
static void ( APIENTRY * dllColor4bv )(const GLbyte *v);
static void ( APIENTRY * dllColor4d )(GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha);
static void ( APIENTRY * dllColor4dv )(const GLdouble *v);
static void ( APIENTRY * dllColor4f )(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
static void ( APIENTRY * dllColor4fv )(const GLfloat *v);
static void ( APIENTRY * dllColor4i )(GLint red, GLint green, GLint blue, GLint alpha);
static void ( APIENTRY * dllColor4iv )(const GLint *v);
static void ( APIENTRY * dllColor4s )(GLshort red, GLshort green, GLshort blue, GLshort alpha);
static void ( APIENTRY * dllColor4sv )(const GLshort *v);
static void ( APIENTRY * dllColor4ub )(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha);
static void ( APIENTRY * dllColor4ubv )(const GLubyte *v);
static void ( APIENTRY * dllColor4ui )(GLuint red, GLuint green, GLuint blue, GLuint alpha);
static void ( APIENTRY * dllColor4uiv )(const GLuint *v);
static void ( APIENTRY * dllColor4us )(GLushort red, GLushort green, GLushort blue, GLushort alpha);
static void ( APIENTRY * dllColor4usv )(const GLushort *v);
static void ( APIENTRY * dllColorMask )(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
static void ( APIENTRY * dllColorMaterial )(GLenum face, GLenum mode);
static void ( APIENTRY * dllColorPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
static void ( APIENTRY * dllCopyPixels )(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type);
static void ( APIENTRY * dllCopyTexImage1D )(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLint border);
static void ( APIENTRY * dllCopyTexImage2D )(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
static void ( APIENTRY * dllCopyTexSubImage1D )(GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width);
static void ( APIENTRY * dllCopyTexSubImage2D )(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
static void ( APIENTRY * dllCullFace )(GLenum mode);
static void ( APIENTRY * dllDeleteLists )(GLuint list, GLsizei range);
static void ( APIENTRY * dllDeleteTextures )(GLsizei n, const GLuint *textures);
static void ( APIENTRY * dllDepthFunc )(GLenum func);
static void ( APIENTRY * dllDepthMask )(GLboolean flag);
static void ( APIENTRY * dllDepthRange )(GLclampd zNear, GLclampd zFar);
static void ( APIENTRY * dllDisable )(GLenum cap);
static void ( APIENTRY * dllDisableClientState )(GLenum array);
static void ( APIENTRY * dllDrawArrays )(GLenum mode, GLint first, GLsizei count);
static void ( APIENTRY * dllDrawBuffer )(GLenum mode);
static void ( APIENTRY * dllDrawElements )(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
static void ( APIENTRY * dllDrawPixels )(GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
static void ( APIENTRY * dllEdgeFlag )(GLboolean flag);
static void ( APIENTRY * dllEdgeFlagPointer )(GLsizei stride, const GLvoid *pointer);
static void ( APIENTRY * dllEdgeFlagv )(const GLboolean *flag);
static void ( APIENTRY * dllEnable )(GLenum cap);
static void ( APIENTRY * dllEnableClientState )(GLenum array);
static void ( APIENTRY * dllEnd )(void);
static void ( APIENTRY * dllEndList )(void);
static void ( APIENTRY * dllEvalCoord1d )(GLdouble u);
static void ( APIENTRY * dllEvalCoord1dv )(const GLdouble *u);
static void ( APIENTRY * dllEvalCoord1f )(GLfloat u);
static void ( APIENTRY * dllEvalCoord1fv )(const GLfloat *u);
static void ( APIENTRY * dllEvalCoord2d )(GLdouble u, GLdouble v);
static void ( APIENTRY * dllEvalCoord2dv )(const GLdouble *u);
static void ( APIENTRY * dllEvalCoord2f )(GLfloat u, GLfloat v);
static void ( APIENTRY * dllEvalCoord2fv )(const GLfloat *u);
static void ( APIENTRY * dllEvalMesh1 )(GLenum mode, GLint i1, GLint i2);
static void ( APIENTRY * dllEvalMesh2 )(GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2);
static void ( APIENTRY * dllEvalPoint1 )(GLint i);
static void ( APIENTRY * dllEvalPoint2 )(GLint i, GLint j);
static void ( APIENTRY * dllFeedbackBuffer )(GLsizei size, GLenum type, GLfloat *buffer);
static void ( APIENTRY * dllFinish )(void);
static void ( APIENTRY * dllFlush )(void);
static void ( APIENTRY * dllFogf )(GLenum pname, GLfloat param);
static void ( APIENTRY * dllFogfv )(GLenum pname, const GLfloat *params);
static void ( APIENTRY * dllFogi )(GLenum pname, GLint param);
static void ( APIENTRY * dllFogiv )(GLenum pname, const GLint *params);
static void ( APIENTRY * dllFrontFace )(GLenum mode);
static void ( APIENTRY * dllFrustum )(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
GLuint ( APIENTRY * dllGenLists )(GLsizei range);
static void ( APIENTRY * dllGenTextures )(GLsizei n, GLuint *textures);
static void ( APIENTRY * dllGetBooleanv )(GLenum pname, GLboolean *params);
static void ( APIENTRY * dllGetClipPlane )(GLenum plane, GLdouble *equation);
static void ( APIENTRY * dllGetDoublev )(GLenum pname, GLdouble *params);
GLenum ( APIENTRY * dllGetError )(void);
static void ( APIENTRY * dllGetFloatv )(GLenum pname, GLfloat *params);
static void ( APIENTRY * dllGetIntegerv )(GLenum pname, GLint *params);
static void ( APIENTRY * dllGetLightfv )(GLenum light, GLenum pname, GLfloat *params);
static void ( APIENTRY * dllGetLightiv )(GLenum light, GLenum pname, GLint *params);
static void ( APIENTRY * dllGetMapdv )(GLenum target, GLenum query, GLdouble *v);
static void ( APIENTRY * dllGetMapfv )(GLenum target, GLenum query, GLfloat *v);
static void ( APIENTRY * dllGetMapiv )(GLenum target, GLenum query, GLint *v);
static void ( APIENTRY * dllGetMaterialfv )(GLenum face, GLenum pname, GLfloat *params);
static void ( APIENTRY * dllGetMaterialiv )(GLenum face, GLenum pname, GLint *params);
static void ( APIENTRY * dllGetPixelMapfv )(GLenum map, GLfloat *values);
static void ( APIENTRY * dllGetPixelMapuiv )(GLenum map, GLuint *values);
static void ( APIENTRY * dllGetPixelMapusv )(GLenum map, GLushort *values);
static void ( APIENTRY * dllGetPointerv )(GLenum pname, GLvoid* *params);
static void ( APIENTRY * dllGetPolygonStipple )(GLubyte *mask);
const GLubyte * ( APIENTRY * dllGetString )(GLenum name);
static void ( APIENTRY * dllGetTexEnvfv )(GLenum target, GLenum pname, GLfloat *params);
static void ( APIENTRY * dllGetTexEnviv )(GLenum target, GLenum pname, GLint *params);
static void ( APIENTRY * dllGetTexGendv )(GLenum coord, GLenum pname, GLdouble *params);
static void ( APIENTRY * dllGetTexGenfv )(GLenum coord, GLenum pname, GLfloat *params);
static void ( APIENTRY * dllGetTexGeniv )(GLenum coord, GLenum pname, GLint *params);
static void ( APIENTRY * dllGetTexImage )(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels);
static void ( APIENTRY * dllGetTexLevelParameterfv )(GLenum target, GLint level, GLenum pname, GLfloat *params);
static void ( APIENTRY * dllGetTexLevelParameteriv )(GLenum target, GLint level, GLenum pname, GLint *params);
static void ( APIENTRY * dllGetTexParameterfv )(GLenum target, GLenum pname, GLfloat *params);
static void ( APIENTRY * dllGetTexParameteriv )(GLenum target, GLenum pname, GLint *params);
static void ( APIENTRY * dllHint )(GLenum target, GLenum mode);
static void ( APIENTRY * dllIndexMask )(GLuint mask);
static void ( APIENTRY * dllIndexPointer )(GLenum type, GLsizei stride, const GLvoid *pointer);
static void ( APIENTRY * dllIndexd )(GLdouble c);
static void ( APIENTRY * dllIndexdv )(const GLdouble *c);
static void ( APIENTRY * dllIndexf )(GLfloat c);
static void ( APIENTRY * dllIndexfv )(const GLfloat *c);
static void ( APIENTRY * dllIndexi )(GLint c);
static void ( APIENTRY * dllIndexiv )(const GLint *c);
static void ( APIENTRY * dllIndexs )(GLshort c);
static void ( APIENTRY * dllIndexsv )(const GLshort *c);
static void ( APIENTRY * dllIndexub )(GLubyte c);
static void ( APIENTRY * dllIndexubv )(const GLubyte *c);
static void ( APIENTRY * dllInitNames )(void);
static void ( APIENTRY * dllInterleavedArrays )(GLenum format, GLsizei stride, const GLvoid *pointer);
GLboolean ( APIENTRY * dllIsEnabled )(GLenum cap);
GLboolean ( APIENTRY * dllIsList )(GLuint list);
GLboolean ( APIENTRY * dllIsTexture )(GLuint texture);
static void ( APIENTRY * dllLightModelf )(GLenum pname, GLfloat param);
static void ( APIENTRY * dllLightModelfv )(GLenum pname, const GLfloat *params);
static void ( APIENTRY * dllLightModeli )(GLenum pname, GLint param);
static void ( APIENTRY * dllLightModeliv )(GLenum pname, const GLint *params);
static void ( APIENTRY * dllLightf )(GLenum light, GLenum pname, GLfloat param);
static void ( APIENTRY * dllLightfv )(GLenum light, GLenum pname, const GLfloat *params);
static void ( APIENTRY * dllLighti )(GLenum light, GLenum pname, GLint param);
static void ( APIENTRY * dllLightiv )(GLenum light, GLenum pname, const GLint *params);
static void ( APIENTRY * dllLineStipple )(GLint factor, GLushort pattern);
static void ( APIENTRY * dllLineWidth )(GLfloat width);
static void ( APIENTRY * dllListBase )(GLuint base);
static void ( APIENTRY * dllLoadIdentity )(void);
static void ( APIENTRY * dllLoadMatrixd )(const GLdouble *m);
static void ( APIENTRY * dllLoadMatrixf )(const GLfloat *m);
static void ( APIENTRY * dllLoadName )(GLuint name);
static void ( APIENTRY * dllLogicOp )(GLenum opcode);
static void ( APIENTRY * dllMap1d )(GLenum target, GLdouble u1, GLdouble u2, GLint stride, GLint order, const GLdouble *points);
static void ( APIENTRY * dllMap1f )(GLenum target, GLfloat u1, GLfloat u2, GLint stride, GLint order, const GLfloat *points);
static void ( APIENTRY * dllMap2d )(GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder, GLdouble v1, GLdouble v2, GLint vstride, GLint vorder, const GLdouble *points);
static void ( APIENTRY * dllMap2f )(GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder, GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, const GLfloat *points);
static void ( APIENTRY * dllMapGrid1d )(GLint un, GLdouble u1, GLdouble u2);
static void ( APIENTRY * dllMapGrid1f )(GLint un, GLfloat u1, GLfloat u2);
static void ( APIENTRY * dllMapGrid2d )(GLint un, GLdouble u1, GLdouble u2, GLint vn, GLdouble v1, GLdouble v2);
static void ( APIENTRY * dllMapGrid2f )(GLint un, GLfloat u1, GLfloat u2, GLint vn, GLfloat v1, GLfloat v2);
static void ( APIENTRY * dllMaterialf )(GLenum face, GLenum pname, GLfloat param);
static void ( APIENTRY * dllMaterialfv )(GLenum face, GLenum pname, const GLfloat *params);
static void ( APIENTRY * dllMateriali )(GLenum face, GLenum pname, GLint param);
static void ( APIENTRY * dllMaterialiv )(GLenum face, GLenum pname, const GLint *params);
static void ( APIENTRY * dllMatrixMode )(GLenum mode);
static void ( APIENTRY * dllMultMatrixd )(const GLdouble *m);
static void ( APIENTRY * dllMultMatrixf )(const GLfloat *m);
static void ( APIENTRY * dllNewList )(GLuint list, GLenum mode);
static void ( APIENTRY * dllNormal3b )(GLbyte nx, GLbyte ny, GLbyte nz);
static void ( APIENTRY * dllNormal3bv )(const GLbyte *v);
static void ( APIENTRY * dllNormal3d )(GLdouble nx, GLdouble ny, GLdouble nz);
static void ( APIENTRY * dllNormal3dv )(const GLdouble *v);
static void ( APIENTRY * dllNormal3f )(GLfloat nx, GLfloat ny, GLfloat nz);
static void ( APIENTRY * dllNormal3fv )(const GLfloat *v);
static void ( APIENTRY * dllNormal3i )(GLint nx, GLint ny, GLint nz);
static void ( APIENTRY * dllNormal3iv )(const GLint *v);
static void ( APIENTRY * dllNormal3s )(GLshort nx, GLshort ny, GLshort nz);
static void ( APIENTRY * dllNormal3sv )(const GLshort *v);
static void ( APIENTRY * dllNormalPointer )(GLenum type, GLsizei stride, const GLvoid *pointer);
static void ( APIENTRY * dllOrtho )(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
static void ( APIENTRY * dllPassThrough )(GLfloat token);
static void ( APIENTRY * dllPixelMapfv )(GLenum map, GLsizei mapsize, const GLfloat *values);
static void ( APIENTRY * dllPixelMapuiv )(GLenum map, GLsizei mapsize, const GLuint *values);
static void ( APIENTRY * dllPixelMapusv )(GLenum map, GLsizei mapsize, const GLushort *values);
static void ( APIENTRY * dllPixelStoref )(GLenum pname, GLfloat param);
static void ( APIENTRY * dllPixelStorei )(GLenum pname, GLint param);
static void ( APIENTRY * dllPixelTransferf )(GLenum pname, GLfloat param);
static void ( APIENTRY * dllPixelTransferi )(GLenum pname, GLint param);
static void ( APIENTRY * dllPixelZoom )(GLfloat xfactor, GLfloat yfactor);
static void ( APIENTRY * dllPointSize )(GLfloat size);
static void ( APIENTRY * dllPolygonMode )(GLenum face, GLenum mode);
static void ( APIENTRY * dllPolygonOffset )(GLfloat factor, GLfloat units);
static void ( APIENTRY * dllPolygonStipple )(const GLubyte *mask);
static void ( APIENTRY * dllPopAttrib )(void);
static void ( APIENTRY * dllPopClientAttrib )(void);
static void ( APIENTRY * dllPopMatrix )(void);
static void ( APIENTRY * dllPopName )(void);
static void ( APIENTRY * dllPrioritizeTextures )(GLsizei n, const GLuint *textures, const GLclampf *priorities);
static void ( APIENTRY * dllPushAttrib )(GLbitfield mask);
static void ( APIENTRY * dllPushClientAttrib )(GLbitfield mask);
static void ( APIENTRY * dllPushMatrix )(void);
static void ( APIENTRY * dllPushName )(GLuint name);
static void ( APIENTRY * dllRasterPos2d )(GLdouble x, GLdouble y);
static void ( APIENTRY * dllRasterPos2dv )(const GLdouble *v);
static void ( APIENTRY * dllRasterPos2f )(GLfloat x, GLfloat y);
static void ( APIENTRY * dllRasterPos2fv )(const GLfloat *v);
static void ( APIENTRY * dllRasterPos2i )(GLint x, GLint y);
static void ( APIENTRY * dllRasterPos2iv )(const GLint *v);
static void ( APIENTRY * dllRasterPos2s )(GLshort x, GLshort y);
static void ( APIENTRY * dllRasterPos2sv )(const GLshort *v);
static void ( APIENTRY * dllRasterPos3d )(GLdouble x, GLdouble y, GLdouble z);
static void ( APIENTRY * dllRasterPos3dv )(const GLdouble *v);
static void ( APIENTRY * dllRasterPos3f )(GLfloat x, GLfloat y, GLfloat z);
static void ( APIENTRY * dllRasterPos3fv )(const GLfloat *v);
static void ( APIENTRY * dllRasterPos3i )(GLint x, GLint y, GLint z);
static void ( APIENTRY * dllRasterPos3iv )(const GLint *v);
static void ( APIENTRY * dllRasterPos3s )(GLshort x, GLshort y, GLshort z);
static void ( APIENTRY * dllRasterPos3sv )(const GLshort *v);
static void ( APIENTRY * dllRasterPos4d )(GLdouble x, GLdouble y, GLdouble z, GLdouble w);
static void ( APIENTRY * dllRasterPos4dv )(const GLdouble *v);
static void ( APIENTRY * dllRasterPos4f )(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
static void ( APIENTRY * dllRasterPos4fv )(const GLfloat *v);
static void ( APIENTRY * dllRasterPos4i )(GLint x, GLint y, GLint z, GLint w);
static void ( APIENTRY * dllRasterPos4iv )(const GLint *v);
static void ( APIENTRY * dllRasterPos4s )(GLshort x, GLshort y, GLshort z, GLshort w);
static void ( APIENTRY * dllRasterPos4sv )(const GLshort *v);
static void ( APIENTRY * dllReadBuffer )(GLenum mode);
static void ( APIENTRY * dllReadPixels )(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);
static void ( APIENTRY * dllRectd )(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2);
static void ( APIENTRY * dllRectdv )(const GLdouble *v1, const GLdouble *v2);
static void ( APIENTRY * dllRectf )(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2);
static void ( APIENTRY * dllRectfv )(const GLfloat *v1, const GLfloat *v2);
static void ( APIENTRY * dllRecti )(GLint x1, GLint y1, GLint x2, GLint y2);
static void ( APIENTRY * dllRectiv )(const GLint *v1, const GLint *v2);
static void ( APIENTRY * dllRects )(GLshort x1, GLshort y1, GLshort x2, GLshort y2);
static void ( APIENTRY * dllRectsv )(const GLshort *v1, const GLshort *v2);
GLint ( APIENTRY * dllRenderMode )(GLenum mode);
static void ( APIENTRY * dllRotated )(GLdouble angle, GLdouble x, GLdouble y, GLdouble z);
static void ( APIENTRY * dllRotatef )(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
static void ( APIENTRY * dllScaled )(GLdouble x, GLdouble y, GLdouble z);
static void ( APIENTRY * dllScalef )(GLfloat x, GLfloat y, GLfloat z);
static void ( APIENTRY * dllScissor )(GLint x, GLint y, GLsizei width, GLsizei height);
static void ( APIENTRY * dllSelectBuffer )(GLsizei size, GLuint *buffer);
static void ( APIENTRY * dllShadeModel )(GLenum mode);
static void ( APIENTRY * dllStencilFunc )(GLenum func, GLint ref, GLuint mask);
static void ( APIENTRY * dllStencilMask )(GLuint mask);
static void ( APIENTRY * dllStencilOp )(GLenum fail, GLenum zfail, GLenum zpass);
static void ( APIENTRY * dllTexCoord1d )(GLdouble s);
static void ( APIENTRY * dllTexCoord1dv )(const GLdouble *v);
static void ( APIENTRY * dllTexCoord1f )(GLfloat s);
static void ( APIENTRY * dllTexCoord1fv )(const GLfloat *v);
static void ( APIENTRY * dllTexCoord1i )(GLint s);
static void ( APIENTRY * dllTexCoord1iv )(const GLint *v);
static void ( APIENTRY * dllTexCoord1s )(GLshort s);
static void ( APIENTRY * dllTexCoord1sv )(const GLshort *v);
static void ( APIENTRY * dllTexCoord2d )(GLdouble s, GLdouble t);
static void ( APIENTRY * dllTexCoord2dv )(const GLdouble *v);
static void ( APIENTRY * dllTexCoord2f )(GLfloat s, GLfloat t);
static void ( APIENTRY * dllTexCoord2fv )(const GLfloat *v);
static void ( APIENTRY * dllTexCoord2i )(GLint s, GLint t);
static void ( APIENTRY * dllTexCoord2iv )(const GLint *v);
static void ( APIENTRY * dllTexCoord2s )(GLshort s, GLshort t);
static void ( APIENTRY * dllTexCoord2sv )(const GLshort *v);
static void ( APIENTRY * dllTexCoord3d )(GLdouble s, GLdouble t, GLdouble r);
static void ( APIENTRY * dllTexCoord3dv )(const GLdouble *v);
static void ( APIENTRY * dllTexCoord3f )(GLfloat s, GLfloat t, GLfloat r);
static void ( APIENTRY * dllTexCoord3fv )(const GLfloat *v);
static void ( APIENTRY * dllTexCoord3i )(GLint s, GLint t, GLint r);
static void ( APIENTRY * dllTexCoord3iv )(const GLint *v);
static void ( APIENTRY * dllTexCoord3s )(GLshort s, GLshort t, GLshort r);
static void ( APIENTRY * dllTexCoord3sv )(const GLshort *v);
static void ( APIENTRY * dllTexCoord4d )(GLdouble s, GLdouble t, GLdouble r, GLdouble q);
static void ( APIENTRY * dllTexCoord4dv )(const GLdouble *v);
static void ( APIENTRY * dllTexCoord4f )(GLfloat s, GLfloat t, GLfloat r, GLfloat q);
static void ( APIENTRY * dllTexCoord4fv )(const GLfloat *v);
static void ( APIENTRY * dllTexCoord4i )(GLint s, GLint t, GLint r, GLint q);
static void ( APIENTRY * dllTexCoord4iv )(const GLint *v);
static void ( APIENTRY * dllTexCoord4s )(GLshort s, GLshort t, GLshort r, GLshort q);
static void ( APIENTRY * dllTexCoord4sv )(const GLshort *v);
static void ( APIENTRY * dllTexCoordPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
static void ( APIENTRY * dllTexEnvf )(GLenum target, GLenum pname, GLfloat param);
static void ( APIENTRY * dllTexEnvfv )(GLenum target, GLenum pname, const GLfloat *params);
static void ( APIENTRY * dllTexEnvi )(GLenum target, GLenum pname, GLint param);
static void ( APIENTRY * dllTexEnviv )(GLenum target, GLenum pname, const GLint *params);
static void ( APIENTRY * dllTexGend )(GLenum coord, GLenum pname, GLdouble param);
static void ( APIENTRY * dllTexGendv )(GLenum coord, GLenum pname, const GLdouble *params);
static void ( APIENTRY * dllTexGenf )(GLenum coord, GLenum pname, GLfloat param);
static void ( APIENTRY * dllTexGenfv )(GLenum coord, GLenum pname, const GLfloat *params);
static void ( APIENTRY * dllTexGeni )(GLenum coord, GLenum pname, GLint param);
static void ( APIENTRY * dllTexGeniv )(GLenum coord, GLenum pname, const GLint *params);
static void ( APIENTRY * dllTexImage1D )(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
static void ( APIENTRY * dllTexImage2D )(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
static void ( APIENTRY * dllTexParameterf )(GLenum target, GLenum pname, GLfloat param);
static void ( APIENTRY * dllTexParameterfv )(GLenum target, GLenum pname, const GLfloat *params);
static void ( APIENTRY * dllTexParameteri )(GLenum target, GLenum pname, GLint param);
static void ( APIENTRY * dllTexParameteriv )(GLenum target, GLenum pname, const GLint *params);
static void ( APIENTRY * dllTexSubImage1D )(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels);
static void ( APIENTRY * dllTexSubImage2D )(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
static void ( APIENTRY * dllTranslated )(GLdouble x, GLdouble y, GLdouble z);
static void ( APIENTRY * dllTranslatef )(GLfloat x, GLfloat y, GLfloat z);
static void ( APIENTRY * dllVertex2d )(GLdouble x, GLdouble y);
static void ( APIENTRY * dllVertex2dv )(const GLdouble *v);
static void ( APIENTRY * dllVertex2f )(GLfloat x, GLfloat y);
static void ( APIENTRY * dllVertex2fv )(const GLfloat *v);
static void ( APIENTRY * dllVertex2i )(GLint x, GLint y);
static void ( APIENTRY * dllVertex2iv )(const GLint *v);
static void ( APIENTRY * dllVertex2s )(GLshort x, GLshort y);
static void ( APIENTRY * dllVertex2sv )(const GLshort *v);
static void ( APIENTRY * dllVertex3d )(GLdouble x, GLdouble y, GLdouble z);
static void ( APIENTRY * dllVertex3dv )(const GLdouble *v);
static void ( APIENTRY * dllVertex3f )(GLfloat x, GLfloat y, GLfloat z);
static void ( APIENTRY * dllVertex3fv )(const GLfloat *v);
static void ( APIENTRY * dllVertex3i )(GLint x, GLint y, GLint z);
static void ( APIENTRY * dllVertex3iv )(const GLint *v);
static void ( APIENTRY * dllVertex3s )(GLshort x, GLshort y, GLshort z);
static void ( APIENTRY * dllVertex3sv )(const GLshort *v);
static void ( APIENTRY * dllVertex4d )(GLdouble x, GLdouble y, GLdouble z, GLdouble w);
static void ( APIENTRY * dllVertex4dv )(const GLdouble *v);
static void ( APIENTRY * dllVertex4f )(GLfloat x, GLfloat y, GLfloat z, GLfloat w);
static void ( APIENTRY * dllVertex4fv )(const GLfloat *v);
static void ( APIENTRY * dllVertex4i )(GLint x, GLint y, GLint z, GLint w);
static void ( APIENTRY * dllVertex4iv )(const GLint *v);
static void ( APIENTRY * dllVertex4s )(GLshort x, GLshort y, GLshort z, GLshort w);
static void ( APIENTRY * dllVertex4sv )(const GLshort *v);
static void ( APIENTRY * dllVertexPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
static void ( APIENTRY * dllViewport )(GLint x, GLint y, GLsizei width, GLsizei height);


/*
** QGL_Shutdown
**
** Unloads the specified DLL then nulls out all the proc pointers.
*/
void QGL_Shutdown( void )
{
	if ( glw_state.OpenGLLib )
	{
		// 25/09/05 Tim Angus <tim@ngus.net>
		// Certain combinations of hardware and software, specifically
		// Linux/SMP/Nvidia/agpgart (OK, OK. MY combination of hardware and
		// software), seem to cause a catastrophic (hard reboot required) crash
		// when libGL is dynamically unloaded. I'm unsure of the precise cause,
		// suffice to say I don't see anything in the Q3 code that could cause it.
		// I suspect it's an Nvidia driver bug, but without the source or means to
		// debug I obviously can't prove (or disprove) this. Interestingly (though
		// perhaps not suprisingly), Enemy Territory and Doom 3 both exhibit the
		// same problem.
		//
		// After many, many reboots and prodding here and there, it seems that a
		// placing a short delay before libGL is unloaded works around the problem.
		// This delay is changable via the r_GLlibCoolDownMsec cvar (nice name
		// huh?), and it defaults to 0. For me, 500 seems to work.
		//if( r_GLlibCoolDownMsec->integer )
		usleep( 500 * 1000 );

		#if USE_SDL_VIDEO
		SDL_QuitSubSystem(SDL_INIT_VIDEO);
		#else
		dlclose ( glw_state.OpenGLLib );
		#endif
		glw_state.OpenGLLib = NULL;
	}

	qglAccum                     = NULL;
	qglAlphaFunc                 = NULL;
	qglAreTexturesResident       = NULL;
	qglArrayElement              = NULL;
	qglBegin                     = NULL;
	qglBindTexture               = NULL;
	qglBitmap                    = NULL;
	qglBlendFunc                 = NULL;
	qglCallList                  = NULL;
	qglCallLists                 = NULL;
	qglClear                     = NULL;
	qglClearAccum                = NULL;
	qglClearColor                = NULL;
	qglClearDepth                = NULL;
	qglClearIndex                = NULL;
	qglClearStencil              = NULL;
	qglClipPlane                 = NULL;
	qglColor3b                   = NULL;
	qglColor3bv                  = NULL;
	qglColor3d                   = NULL;
	qglColor3dv                  = NULL;
	qglColor3f                   = NULL;
	qglColor3fv                  = NULL;
	qglColor3i                   = NULL;
	qglColor3iv                  = NULL;
	qglColor3s                   = NULL;
	qglColor3sv                  = NULL;
	qglColor3ub                  = NULL;
	qglColor3ubv                 = NULL;
	qglColor3ui                  = NULL;
	qglColor3uiv                 = NULL;
	qglColor3us                  = NULL;
	qglColor3usv                 = NULL;
	qglColor4b                   = NULL;
	qglColor4bv                  = NULL;
	qglColor4d                   = NULL;
	qglColor4dv                  = NULL;
	qglColor4f                   = NULL;
	qglColor4fv                  = NULL;
	qglColor4i                   = NULL;
	qglColor4iv                  = NULL;
	qglColor4s                   = NULL;
	qglColor4sv                  = NULL;
	qglColor4ub                  = NULL;
	qglColor4ubv                 = NULL;
	qglColor4ui                  = NULL;
	qglColor4uiv                 = NULL;
	qglColor4us                  = NULL;
	qglColor4usv                 = NULL;
	qglColorMask                 = NULL;
	qglColorMaterial             = NULL;
	qglColorPointer              = NULL;
	qglCopyPixels                = NULL;
	qglCopyTexImage1D            = NULL;
	qglCopyTexImage2D            = NULL;
	qglCopyTexSubImage1D         = NULL;
	qglCopyTexSubImage2D         = NULL;
	qglCullFace                  = NULL;
	qglDeleteLists               = NULL;
	qglDeleteTextures            = NULL;
	qglDepthFunc                 = NULL;
	qglDepthMask                 = NULL;
	qglDepthRange                = NULL;
	qglDisable                   = NULL;
	qglDisableClientState        = NULL;
	qglDrawArrays                = NULL;
	qglDrawBuffer                = NULL;
	qglDrawElements              = NULL;
	qglDrawPixels                = NULL;
	qglEdgeFlag                  = NULL;
	qglEdgeFlagPointer           = NULL;
	qglEdgeFlagv                 = NULL;
	qglEnable                    = NULL;
	qglEnableClientState         = NULL;
	qglEnd                       = NULL;
	qglEndList                   = NULL;
	qglEvalCoord1d               = NULL;
	qglEvalCoord1dv              = NULL;
	qglEvalCoord1f               = NULL;
	qglEvalCoord1fv              = NULL;
	qglEvalCoord2d               = NULL;
	qglEvalCoord2dv              = NULL;
	qglEvalCoord2f               = NULL;
	qglEvalCoord2fv              = NULL;
	qglEvalMesh1                 = NULL;
	qglEvalMesh2                 = NULL;
	qglEvalPoint1                = NULL;
	qglEvalPoint2                = NULL;
	qglFeedbackBuffer            = NULL;
	qglFinish                    = NULL;
	qglFlush                     = NULL;
	qglFogf                      = NULL;
	qglFogfv                     = NULL;
	qglFogi                      = NULL;
	qglFogiv                     = NULL;
	qglFrontFace                 = NULL;
	qglFrustum                   = NULL;
	qglGenLists                  = NULL;
	qglGenTextures               = NULL;
	qglGetBooleanv               = NULL;
	qglGetClipPlane              = NULL;
	qglGetDoublev                = NULL;
	qglGetError                  = NULL;
	qglGetFloatv                 = NULL;
	qglGetIntegerv               = NULL;
	qglGetLightfv                = NULL;
	qglGetLightiv                = NULL;
	qglGetMapdv                  = NULL;
	qglGetMapfv                  = NULL;
	qglGetMapiv                  = NULL;
	qglGetMaterialfv             = NULL;
	qglGetMaterialiv             = NULL;
	qglGetPixelMapfv             = NULL;
	qglGetPixelMapuiv            = NULL;
	qglGetPixelMapusv            = NULL;
	qglGetPointerv               = NULL;
	qglGetPolygonStipple         = NULL;
	qglGetString                 = NULL;
	qglGetTexEnvfv               = NULL;
	qglGetTexEnviv               = NULL;
	qglGetTexGendv               = NULL;
	qglGetTexGenfv               = NULL;
	qglGetTexGeniv               = NULL;
	qglGetTexImage               = NULL;
	qglGetTexLevelParameterfv    = NULL;
	qglGetTexLevelParameteriv    = NULL;
	qglGetTexParameterfv         = NULL;
	qglGetTexParameteriv         = NULL;
	qglHint                      = NULL;
	qglIndexMask                 = NULL;
	qglIndexPointer              = NULL;
	qglIndexd                    = NULL;
	qglIndexdv                   = NULL;
	qglIndexf                    = NULL;
	qglIndexfv                   = NULL;
	qglIndexi                    = NULL;
	qglIndexiv                   = NULL;
	qglIndexs                    = NULL;
	qglIndexsv                   = NULL;
	qglIndexub                   = NULL;
	qglIndexubv                  = NULL;
	qglInitNames                 = NULL;
	qglInterleavedArrays         = NULL;
	qglIsEnabled                 = NULL;
	qglIsList                    = NULL;
	qglIsTexture                 = NULL;
	qglLightModelf               = NULL;
	qglLightModelfv              = NULL;
	qglLightModeli               = NULL;
	qglLightModeliv              = NULL;
	qglLightf                    = NULL;
	qglLightfv                   = NULL;
	qglLighti                    = NULL;
	qglLightiv                   = NULL;
	qglLineStipple               = NULL;
	qglLineWidth                 = NULL;
	qglListBase                  = NULL;
	qglLoadIdentity              = NULL;
	qglLoadMatrixd               = NULL;
	qglLoadMatrixf               = NULL;
	qglLoadName                  = NULL;
	qglLogicOp                   = NULL;
	qglMap1d                     = NULL;
	qglMap1f                     = NULL;
	qglMap2d                     = NULL;
	qglMap2f                     = NULL;
	qglMapGrid1d                 = NULL;
	qglMapGrid1f                 = NULL;
	qglMapGrid2d                 = NULL;
	qglMapGrid2f                 = NULL;
	qglMaterialf                 = NULL;
	qglMaterialfv                = NULL;
	qglMateriali                 = NULL;
	qglMaterialiv                = NULL;
	qglMatrixMode                = NULL;
	qglMultMatrixd               = NULL;
	qglMultMatrixf               = NULL;
	qglNewList                   = NULL;
	qglNormal3b                  = NULL;
	qglNormal3bv                 = NULL;
	qglNormal3d                  = NULL;
	qglNormal3dv                 = NULL;
	qglNormal3f                  = NULL;
	qglNormal3fv                 = NULL;
	qglNormal3i                  = NULL;
	qglNormal3iv                 = NULL;
	qglNormal3s                  = NULL;
	qglNormal3sv                 = NULL;
	qglNormalPointer             = NULL;
	qglOrtho                     = NULL;
	qglPassThrough               = NULL;
	qglPixelMapfv                = NULL;
	qglPixelMapuiv               = NULL;
	qglPixelMapusv               = NULL;
	qglPixelStoref               = NULL;
	qglPixelStorei               = NULL;
	qglPixelTransferf            = NULL;
	qglPixelTransferi            = NULL;
	qglPixelZoom                 = NULL;
	qglPointSize                 = NULL;
	qglPolygonMode               = NULL;
	qglPolygonOffset             = NULL;
	qglPolygonStipple            = NULL;
	qglPopAttrib                 = NULL;
	qglPopClientAttrib           = NULL;
	qglPopMatrix                 = NULL;
	qglPopName                   = NULL;
	qglPrioritizeTextures        = NULL;
	qglPushAttrib                = NULL;
	qglPushClientAttrib          = NULL;
	qglPushMatrix                = NULL;
	qglPushName                  = NULL;
	qglRasterPos2d               = NULL;
	qglRasterPos2dv              = NULL;
	qglRasterPos2f               = NULL;
	qglRasterPos2fv              = NULL;
	qglRasterPos2i               = NULL;
	qglRasterPos2iv              = NULL;
	qglRasterPos2s               = NULL;
	qglRasterPos2sv              = NULL;
	qglRasterPos3d               = NULL;
	qglRasterPos3dv              = NULL;
	qglRasterPos3f               = NULL;
	qglRasterPos3fv              = NULL;
	qglRasterPos3i               = NULL;
	qglRasterPos3iv              = NULL;
	qglRasterPos3s               = NULL;
	qglRasterPos3sv              = NULL;
	qglRasterPos4d               = NULL;
	qglRasterPos4dv              = NULL;
	qglRasterPos4f               = NULL;
	qglRasterPos4fv              = NULL;
	qglRasterPos4i               = NULL;
	qglRasterPos4iv              = NULL;
	qglRasterPos4s               = NULL;
	qglRasterPos4sv              = NULL;
	qglReadBuffer                = NULL;
	qglReadPixels                = NULL;
	qglRectd                     = NULL;
	qglRectdv                    = NULL;
	qglRectf                     = NULL;
	qglRectfv                    = NULL;
	qglRecti                     = NULL;
	qglRectiv                    = NULL;
	qglRects                     = NULL;
	qglRectsv                    = NULL;
	qglRenderMode                = NULL;
	qglRotated                   = NULL;
	qglRotatef                   = NULL;
	qglScaled                    = NULL;
	qglScalef                    = NULL;
	qglScissor                   = NULL;
	qglSelectBuffer              = NULL;
	qglShadeModel                = NULL;
	qglStencilFunc               = NULL;
	qglStencilMask               = NULL;
	qglStencilOp                 = NULL;
	qglTexCoord1d                = NULL;
	qglTexCoord1dv               = NULL;
	qglTexCoord1f                = NULL;
	qglTexCoord1fv               = NULL;
	qglTexCoord1i                = NULL;
	qglTexCoord1iv               = NULL;
	qglTexCoord1s                = NULL;
	qglTexCoord1sv               = NULL;
	qglTexCoord2d                = NULL;
	qglTexCoord2dv               = NULL;
	qglTexCoord2f                = NULL;
	qglTexCoord2fv               = NULL;
	qglTexCoord2i                = NULL;
	qglTexCoord2iv               = NULL;
	qglTexCoord2s                = NULL;
	qglTexCoord2sv               = NULL;
	qglTexCoord3d                = NULL;
	qglTexCoord3dv               = NULL;
	qglTexCoord3f                = NULL;
	qglTexCoord3fv               = NULL;
	qglTexCoord3i                = NULL;
	qglTexCoord3iv               = NULL;
	qglTexCoord3s                = NULL;
	qglTexCoord3sv               = NULL;
	qglTexCoord4d                = NULL;
	qglTexCoord4dv               = NULL;
	qglTexCoord4f                = NULL;
	qglTexCoord4fv               = NULL;
	qglTexCoord4i                = NULL;
	qglTexCoord4iv               = NULL;
	qglTexCoord4s                = NULL;
	qglTexCoord4sv               = NULL;
	qglTexCoordPointer           = NULL;
	qglTexEnvf                   = NULL;
	qglTexEnvfv                  = NULL;
	qglTexEnvi                   = NULL;
	qglTexEnviv                  = NULL;
	qglTexGend                   = NULL;
	qglTexGendv                  = NULL;
	qglTexGenf                   = NULL;
	qglTexGenfv                  = NULL;
	qglTexGeni                   = NULL;
	qglTexGeniv                  = NULL;
	qglTexImage1D                = NULL;
	qglTexImage2D                = NULL;
	qglTexParameterf             = NULL;
	qglTexParameterfv            = NULL;
	qglTexParameteri             = NULL;
	qglTexParameteriv            = NULL;
	qglTexSubImage1D             = NULL;
	qglTexSubImage2D             = NULL;
	qglTranslated                = NULL;
	qglTranslatef                = NULL;
	qglVertex2d                  = NULL;
	qglVertex2dv                 = NULL;
	qglVertex2f                  = NULL;
	qglVertex2fv                 = NULL;
	qglVertex2i                  = NULL;
	qglVertex2iv                 = NULL;
	qglVertex2s                  = NULL;
	qglVertex2sv                 = NULL;
	qglVertex3d                  = NULL;
	qglVertex3dv                 = NULL;
	qglVertex3f                  = NULL;
	qglVertex3fv                 = NULL;
	qglVertex3i                  = NULL;
	qglVertex3iv                 = NULL;
	qglVertex3s                  = NULL;
	qglVertex3sv                 = NULL;
	qglVertex4d                  = NULL;
	qglVertex4dv                 = NULL;
	qglVertex4f                  = NULL;
	qglVertex4fv                 = NULL;
	qglVertex4i                  = NULL;
	qglVertex4iv                 = NULL;
	qglVertex4s                  = NULL;
	qglVertex4sv                 = NULL;
	qglVertexPointer             = NULL;
	qglViewport                  = NULL;

// bk001129 - from cvs1.17 (mkv)
#if defined(__FX__)
	qfxMesaCreateContext         = NULL;
	qfxMesaCreateBestContext     = NULL;
	qfxMesaDestroyContext        = NULL;
	qfxMesaMakeCurrent           = NULL;
	qfxMesaGetCurrentContext     = NULL;
	qfxMesaSwapBuffers           = NULL;
#endif

#if !defined(USE_SDL_VIDEO)
	qglXChooseVisual             = NULL;
	qglXCreateContext            = NULL;
	qglXDestroyContext           = NULL;
	qglXMakeCurrent              = NULL;
	qglXCopyContext              = NULL;
	qglXSwapBuffers              = NULL;
#endif
} // QGL_Shutdown


/*
** GPA
** 
** This'll setup a wrapper around calling GetProcAddress for all our
** GL to QGL bindings, hopefully making them less cumbersome to setup
**
*/

#if USE_SDL_VIDEO
#define GPA( a ) SDL_GL_GetProcAddress( a )
qboolean GLimp_sdl_init_video(void);
#else
#define GPA( a ) dlsym( glw_state.OpenGLLib, a )
#endif


/*
** QGL_Init
**
** This is responsible for binding our qgl function pointers to 
** the appropriate GL stuff.  In Windows this means doing a 
** LoadLibrary and a bunch of calls to GetProcAddress.  On other
** operating systems we need to do the right thing, whatever that
** might be.
** 
*/

qboolean QGL_Init( const char *dllname )
{
	if (glw_state.OpenGLLib == 0)
	{
		#if USE_SDL_VIDEO
		if (GLimp_sdl_init_video() == qfalse)
			return qfalse;
		glw_state.OpenGLLib = (void*)(long)((SDL_GL_LoadLibrary(dllname) == -1) ? 0 : 1);
		#else
		glw_state.OpenGLLib = dlopen( dllname, RTLD_LAZY|RTLD_GLOBAL );
		#endif
	}

/* @brut: i don't know who did this, but it's blatantly Shit And Wrong
	if (glw_state.OpenGLLib == 0)
	{
		char	fn[1024];
		// FILE *fp; // bk001204 - unused

		// if we are not setuid, try current directory
		if (dllname != NULL) {
			getcwd(fn, sizeof(fn));
			Q_strcat(fn, sizeof(fn), "/");
			Q_strcat(fn, sizeof(fn), dllname);

			#if USE_SDL_VIDEO
			glw_state.OpenGLLib = (void*)(long)((SDL_GL_LoadLibrary(fn) == -1) ? 0 : 1);
			#else
			glw_state.OpenGLLib = dlopen( fn, RTLD_LAZY );
			#endif
			if ( glw_state.OpenGLLib == 0 ) {
				ri.Printf(PRINT_ALL, "QGL_Init: Can't load %s from /etc/ld.so.conf or current dir: %s\n", dllname, do_dlerror());
				return qfalse;
			}
		} else {
			ri.Printf(PRINT_ALL, "QGL_Init: Can't load %s from /etc/ld.so.conf: %s\n", dllname, do_dlerror());
			return qfalse;
		}
	}
*/

	qglAccum                     = dllAccum				=(void (*)(GLenum, GLfloat))GPA( "glAccum" );
	qglAlphaFunc                 = dllAlphaFunc			=(void (*)(GLenum, GLclampf))GPA( "glAlphaFunc" );
	qglAreTexturesResident       = dllAreTexturesResident		=(GLboolean (*)(GLsizei, const GLuint*, GLboolean*))GPA( "glAreTexturesResident" );
	qglArrayElement              = dllArrayElement			=(void (*)(GLint))GPA( "glArrayElement" );
	qglBegin                     = dllBegin				=(void (*)(GLenum))GPA( "glBegin" );
	qglBindTexture               = dllBindTexture			=(void (*)(GLenum, GLuint))GPA( "glBindTexture" );
	qglBitmap                    = dllBitmap			=(void (*)(GLsizei, GLsizei, GLfloat, GLfloat, GLfloat, GLfloat, const GLubyte*))GPA( "glBitmap" );
	qglBlendFunc                 = dllBlendFunc			=(void (*)(GLenum, GLenum))GPA( "glBlendFunc" );
	qglCallList                  = dllCallList			=(void (*)(GLuint))GPA( "glCallList" );
	qglCallLists                 = dllCallLists			=(void (*)(GLsizei, GLenum, const GLvoid*))GPA( "glCallLists" );
	qglClear                     = dllClear				=(void (*)(GLbitfield))GPA( "glClear" );
	qglClearAccum                = dllClearAccum			=(void (*)(GLfloat, GLfloat, GLfloat, GLfloat))GPA( "glClearAccum" );
	qglClearColor                = dllClearColor			=(void (*)(GLclampf, GLclampf, GLclampf, GLclampf))GPA( "glClearColor" );
	qglClearDepth                = dllClearDepth			=(void (*)(GLclampd))GPA( "glClearDepth" );
	qglClearIndex                = dllClearIndex			=(void (*)(GLfloat))GPA( "glClearIndex" );
	qglClearStencil              = dllClearStencil			=(void (*)(GLint))GPA( "glClearStencil" );
	qglClipPlane                 = dllClipPlane			=(void (*)(GLenum, const GLdouble*))GPA( "glClipPlane" );
	qglColor3b                   = dllColor3b			=(void (*)(GLbyte, GLbyte, GLbyte))GPA( "glColor3b" );
	qglColor3bv                  = dllColor3bv			=(void (*)(const GLbyte*))GPA( "glColor3bv" );
	qglColor3d                   = dllColor3d			=(void (*)(GLdouble, GLdouble, GLdouble))GPA( "glColor3d" );
	qglColor3dv                  = dllColor3dv			=(void (*)(const GLdouble*))GPA( "glColor3dv" );
	qglColor3f                   = dllColor3f			=(void (*)(GLfloat, GLfloat, GLfloat))GPA( "glColor3f" );
	qglColor3fv                  = dllColor3fv			=(void (*)(const GLfloat*))GPA( "glColor3fv" );
	qglColor3i                   = dllColor3i			=(void (*)(GLint, GLint, GLint))GPA( "glColor3i" );
	qglColor3iv                  = dllColor3iv			=(void (*)(const GLint*))GPA( "glColor3iv" );
	qglColor3s                   = dllColor3s			=(void (*)(GLshort, GLshort, GLshort))GPA( "glColor3s" );
	qglColor3sv                  = dllColor3sv			=(void (*)(const GLshort*))GPA( "glColor3sv" );
	qglColor3ub                  = dllColor3ub			=(void (*)(GLubyte, GLubyte, GLubyte))GPA( "glColor3ub" );
	qglColor3ubv                 = dllColor3ubv			=(void (*)(const GLubyte*))GPA( "glColor3ubv" );
	qglColor3ui                  = dllColor3ui			=(void (*)(GLuint, GLuint, GLuint))GPA( "glColor3ui" );
	qglColor3uiv                 = dllColor3uiv			=(void (*)(const GLuint*))GPA( "glColor3uiv" );
	qglColor3us                  = dllColor3us			=(void (*)(GLushort, GLushort, GLushort))GPA( "glColor3us" );
	qglColor3usv                 = dllColor3usv			=(void (*)(const GLushort*))GPA( "glColor3usv" );
	qglColor4b                   = dllColor4b			=(void (*)(GLbyte, GLbyte, GLbyte, GLbyte))GPA( "glColor4b" );
	qglColor4bv                  = dllColor4bv			=(void (*)(const GLbyte*))GPA( "glColor4bv" );
	qglColor4d                   = dllColor4d			=(void (*)(GLdouble, GLdouble, GLdouble, GLdouble))GPA( "glColor4d" );
	qglColor4dv                  = dllColor4dv			=(void (*)(const GLdouble*))GPA( "glColor4dv" );
	qglColor4f                   = dllColor4f			=(void (*)(GLfloat, GLfloat, GLfloat, GLfloat))GPA( "glColor4f" );
	qglColor4fv                  = dllColor4fv			=(void (*)(const GLfloat*))GPA( "glColor4fv" );
	qglColor4i                   = dllColor4i			=(void (*)(GLint, GLint, GLint, GLint))GPA( "glColor4i" );
	qglColor4iv                  = dllColor4iv			=(void (*)(const GLint*))GPA( "glColor4iv" );
	qglColor4s                   = dllColor4s			=(void (*)(GLshort, GLshort, GLshort, GLshort))GPA( "glColor4s" );
	qglColor4sv                  = dllColor4sv			=(void (*)(const GLshort*))GPA( "glColor4sv" );
	qglColor4ub                  = dllColor4ub			=(void (*)(GLubyte, GLubyte, GLubyte, GLubyte))GPA( "glColor4ub" );
	qglColor4ubv                 = dllColor4ubv			=(void (*)(const GLubyte*))GPA( "glColor4ubv" );
	qglColor4ui                  = dllColor4ui			=(void (*)(GLuint, GLuint, GLuint, GLuint))GPA( "glColor4ui" );
	qglColor4uiv                 = dllColor4uiv			=(void (*)(const GLuint*))GPA( "glColor4uiv" );
	qglColor4us                  = dllColor4us			=(void (*)(GLushort, GLushort, GLushort, GLushort))GPA( "glColor4us" );
	qglColor4usv                 = dllColor4usv			=(void (*)(const GLushort*))GPA( "glColor4usv" );
	qglColorMask                 = dllColorMask			=(void (*)(GLboolean, GLboolean, GLboolean, GLboolean))GPA( "glColorMask" );
	qglColorMaterial             = dllColorMaterial			=(void (*)(GLenum, GLenum))GPA( "glColorMaterial" );
	qglColorPointer              = dllColorPointer			=(void (*)(GLint, GLenum, GLsizei, const GLvoid*))GPA( "glColorPointer" );
	qglCopyPixels                = dllCopyPixels			=(void (*)(GLint, GLint, GLsizei, GLsizei, GLenum))GPA( "glCopyPixels" );
	qglCopyTexImage1D            = dllCopyTexImage1D		=(void (*)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLint))GPA( "glCopyTexImage1D" );
	qglCopyTexImage2D            = dllCopyTexImage2D		=(void (*)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint))GPA( "glCopyTexImage2D" );
	qglCopyTexSubImage1D         = dllCopyTexSubImage1D		=(void (*)(GLenum, GLint, GLint, GLint, GLint, GLsizei))GPA( "glCopyTexSubImage1D" );
	qglCopyTexSubImage2D         = dllCopyTexSubImage2D		=(void (*)(GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei))GPA( "glCopyTexSubImage2D" );
	qglCullFace                  = dllCullFace			=(void (*)(GLenum))GPA( "glCullFace" );
	qglDeleteLists               = dllDeleteLists			=(void (*)(GLuint, GLsizei))GPA( "glDeleteLists" );
	qglDeleteTextures            = dllDeleteTextures		=(void (*)(GLsizei, const GLuint*))GPA( "glDeleteTextures" );
	qglDepthFunc                 = dllDepthFunc			=(void (*)(GLenum))GPA( "glDepthFunc" );
	qglDepthMask                 = dllDepthMask			=(void (*)(GLboolean))GPA( "glDepthMask" );
	qglDepthRange                = dllDepthRange			=(void (*)(GLclampd, GLclampd))GPA( "glDepthRange" );
	qglDisable                   = dllDisable			=(void (*)(GLenum))GPA( "glDisable" );
	qglDisableClientState        = dllDisableClientState		=(void (*)(GLenum))GPA( "glDisableClientState" );
	qglDrawArrays                = dllDrawArrays			=(void (*)(GLenum, GLint, GLsizei))GPA( "glDrawArrays" );
	qglDrawBuffer                = dllDrawBuffer			=(void (*)(GLenum))GPA( "glDrawBuffer" );
	qglDrawElements              = dllDrawElements			=(void (*)(GLenum, GLsizei, GLenum, const GLvoid*))GPA( "glDrawElements" );
	qglDrawPixels                = dllDrawPixels			=(void (*)(GLsizei, GLsizei, GLenum, GLenum, const GLvoid*))GPA( "glDrawPixels" );
	qglEdgeFlag                  = dllEdgeFlag			=(void (*)(GLboolean))GPA( "glEdgeFlag" );
	qglEdgeFlagPointer           = dllEdgeFlagPointer		=(void (*)(GLsizei, const GLvoid*))GPA( "glEdgeFlagPointer" );
	qglEdgeFlagv                 = dllEdgeFlagv			=(void (*)(const GLboolean*))GPA( "glEdgeFlagv" );
	qglEnable                    = dllEnable			=(void (*)(GLenum))GPA( "glEnable" );
	qglEnableClientState         = dllEnableClientState		=(void (*)(GLenum))GPA( "glEnableClientState" );
	qglEnd                       = dllEnd				=(void (*)())GPA( "glEnd" );
	qglEndList                   = dllEndList			=(void (*)())GPA( "glEndList" );
	qglEvalCoord1d		     = dllEvalCoord1d			=(void (*)(GLdouble))GPA( "glEvalCoord1d" );
	qglEvalCoord1dv              = dllEvalCoord1dv			=(void (*)(const GLdouble*))GPA( "glEvalCoord1dv" );
	qglEvalCoord1f               = dllEvalCoord1f			=(void (*)(GLfloat))GPA( "glEvalCoord1f" );
	qglEvalCoord1fv              = dllEvalCoord1fv			=(void (*)(const GLfloat*))GPA( "glEvalCoord1fv" );
	qglEvalCoord2d               = dllEvalCoord2d			=(void (*)(GLdouble, GLdouble))GPA( "glEvalCoord2d" );
	qglEvalCoord2dv              = dllEvalCoord2dv			=(void (*)(const GLdouble*))GPA( "glEvalCoord2dv" );
	qglEvalCoord2f               = dllEvalCoord2f			=(void (*)(GLfloat, GLfloat))GPA( "glEvalCoord2f" );
	qglEvalCoord2fv              = dllEvalCoord2fv			=(void (*)(const GLfloat*))GPA( "glEvalCoord2fv" );
	qglEvalMesh1                 = dllEvalMesh1			=(void (*)(GLenum, GLint, GLint))GPA( "glEvalMesh1" );
	qglEvalMesh2                 = dllEvalMesh2			=(void (*)(GLenum, GLint, GLint, GLint, GLint))GPA( "glEvalMesh2" );
	qglEvalPoint1                = dllEvalPoint1			=(void (*)(GLint))GPA( "glEvalPoint1" );
	qglEvalPoint2                = dllEvalPoint2			=(void (*)(GLint, GLint))GPA( "glEvalPoint2" );
	qglFeedbackBuffer            = dllFeedbackBuffer		=(void (*)(GLsizei, GLenum, GLfloat*))GPA( "glFeedbackBuffer" );
	qglFinish                    = dllFinish			=(void (*)())GPA( "glFinish" );
	qglFlush                     = dllFlush				=(void (*)())GPA( "glFlush" );
	qglFogf                      = dllFogf				=(void (*)(GLenum, GLfloat))GPA( "glFogf" );
	qglFogfv                     = dllFogfv				=(void (*)(GLenum, const GLfloat*))GPA( "glFogfv" );
	qglFogi                      = dllFogi				=(void (*)(GLenum, GLint))GPA( "glFogi" );
	qglFogiv                     = dllFogiv				=(void (*)(GLenum, const GLint*))GPA( "glFogiv" );
	qglFrontFace                 = dllFrontFace			=(void (*)(GLenum))GPA( "glFrontFace" );
	qglFrustum                   = dllFrustum			=(void (*)(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble))GPA( "glFrustum" );
	qglGenLists                  = dllGenLists			=(GLuint (*)(GLsizei))GPA( "glGenLists" );
	qglGenTextures               = dllGenTextures			=(void (*)(GLsizei, GLuint*))GPA( "glGenTextures" );
	qglGetBooleanv               = dllGetBooleanv			=(void (*)(GLenum, GLboolean*))GPA( "glGetBooleanv" );
	qglGetClipPlane              = dllGetClipPlane			=(void (*)(GLenum, GLdouble*))GPA( "glGetClipPlane" );
	qglGetDoublev                = dllGetDoublev			=(void (*)(GLenum, GLdouble*))GPA( "glGetDoublev" );
	qglGetError                  = dllGetError			=(GLenum (*)())GPA( "glGetError" );
	qglGetFloatv                 = dllGetFloatv			=(void (*)(GLenum, GLfloat*))GPA( "glGetFloatv" );
	qglGetIntegerv               = dllGetIntegerv			=(void (*)(GLenum, GLint*))GPA( "glGetIntegerv" );
	qglGetLightfv                = dllGetLightfv			=(void (*)(GLenum, GLenum, GLfloat*))GPA( "glGetLightfv" );
	qglGetLightiv                = dllGetLightiv			=(void (*)(GLenum, GLenum, GLint*))GPA( "glGetLightiv" );
	qglGetMapdv                  = dllGetMapdv			=(void (*)(GLenum, GLenum, GLdouble*))GPA( "glGetMapdv" );
	qglGetMapfv                  = dllGetMapfv			=(void (*)(GLenum, GLenum, GLfloat*))GPA( "glGetMapfv" );
	qglGetMapiv                  = dllGetMapiv			=(void (*)(GLenum, GLenum, GLint*))GPA( "glGetMapiv" );
	qglGetMaterialfv             = dllGetMaterialfv			=(void (*)(GLenum, GLenum, GLfloat*))GPA( "glGetMaterialfv" );
	qglGetMaterialiv             = dllGetMaterialiv			=(void (*)(GLenum, GLenum, GLint*))GPA( "glGetMaterialiv" );
	qglGetPixelMapfv             = dllGetPixelMapfv			=(void (*)(GLenum, GLfloat*))GPA( "glGetPixelMapfv" );
	qglGetPixelMapuiv            = dllGetPixelMapuiv		=(void (*)(GLenum, GLuint*))GPA( "glGetPixelMapuiv" );
	qglGetPixelMapusv            = dllGetPixelMapusv		=(void (*)(GLenum, GLushort*))GPA( "glGetPixelMapusv" );
	qglGetPointerv               = dllGetPointerv			=(void (*)(GLenum, GLvoid**))GPA( "glGetPointerv" );
	qglGetPolygonStipple         = dllGetPolygonStipple		=(void (*)(GLubyte*))GPA( "glGetPolygonStipple" );
	qglGetString                 = dllGetString			=(const GLubyte* (*)(GLenum))GPA( "glGetString" );
	qglGetTexEnvfv               = dllGetTexEnvfv			=(void (*)(GLenum, GLenum, GLfloat*))GPA( "glGetTexEnvfv" );
	qglGetTexEnviv               = dllGetTexEnviv			=(void (*)(GLenum, GLenum, GLint*))GPA( "glGetTexEnviv" );
	qglGetTexGendv               = dllGetTexGendv			=(void (*)(GLenum, GLenum, GLdouble*))GPA( "glGetTexGendv" );
	qglGetTexGenfv               = dllGetTexGenfv			=(void (*)(GLenum, GLenum, GLfloat*))GPA( "glGetTexGenfv" );
	qglGetTexGeniv               = dllGetTexGeniv			=(void (*)(GLenum, GLenum, GLint*))GPA( "glGetTexGeniv" );
	qglGetTexImage               = dllGetTexImage			=(void (*)(GLenum, GLint, GLenum, GLenum, GLvoid*))GPA( "glGetTexImage" );
	qglGetTexLevelParameterfv    = dllGetTexLevelParameterfv	=(void (*)(GLenum target, GLint level, GLenum pname, GLfloat *params))GPA( "glGetTexLevelParameterfv" );
	qglGetTexLevelParameteriv    = dllGetTexLevelParameteriv	=(void (*)(GLenum target, GLint level, GLenum pname, GLint *params))GPA( "glGetTexLevelParameteriv" );
	qglGetTexParameterfv         = dllGetTexParameterfv		=(void (*)(GLenum, GLenum, GLfloat*))GPA( "glGetTexParameterfv" );
	qglGetTexParameteriv         = dllGetTexParameteriv		=(void (*)(GLenum, GLenum, GLint*))GPA( "glGetTexParameteriv" );
	qglHint                      = dllHint				=(void (*)(GLenum, GLenum))GPA( "glHint" );
	qglIndexMask                 = dllIndexMask			=(void (*)(GLuint))GPA( "glIndexMask" );
	qglIndexPointer              = dllIndexPointer			=(void (*)(GLenum, GLsizei, const GLvoid*))GPA( "glIndexPointer" );
	qglIndexd                    = dllIndexd			=(void (*)(GLdouble))GPA( "glIndexd" );
	qglIndexdv                   = dllIndexdv			=(void (*)(const GLdouble*))GPA( "glIndexdv" );
	qglIndexf                    = dllIndexf			=(void (*)(GLfloat))GPA( "glIndexf" );
	qglIndexfv                   = dllIndexfv			=(void (*)(const GLfloat*))GPA( "glIndexfv" );
	qglIndexi                    = dllIndexi			=(void (*)(GLint))GPA( "glIndexi" );
	qglIndexiv                   = dllIndexiv			=(void (*)(const GLint*))GPA( "glIndexiv" );
	qglIndexs                    = dllIndexs			=(void (*)(GLshort))GPA( "glIndexs" );
	qglIndexsv                   = dllIndexsv			=(void (*)(const GLshort*))GPA( "glIndexsv" );
	qglIndexub                   = dllIndexub			=(void (*)(GLubyte))GPA( "glIndexub" );
	qglIndexubv                  = dllIndexubv			=(void (*)(const GLubyte*))GPA( "glIndexubv" );
	qglInitNames                 = dllInitNames			=(void (*)())GPA( "glInitNames" );
	qglInterleavedArrays         = dllInterleavedArrays		=(void (*)(GLenum, GLsizei, const GLvoid*))GPA( "glInterleavedArrays" );
	qglIsEnabled                 = dllIsEnabled			=(GLboolean (*)(GLenum))GPA( "glIsEnabled" );
	qglIsList                    = dllIsList			=(GLboolean (*)(GLuint))GPA( "glIsList" );
	qglIsTexture                 = dllIsTexture			=(GLboolean (*)(GLuint))GPA( "glIsTexture" );
	qglLightModelf               = dllLightModelf			=(void (*)(GLenum, GLfloat))GPA( "glLightModelf" );
	qglLightModelfv              = dllLightModelfv			=(void (*)(GLenum, const GLfloat*))GPA( "glLightModelfv" );
	qglLightModeli               = dllLightModeli			=(void (*)(GLenum, GLint))GPA( "glLightModeli" );
	qglLightModeliv              = dllLightModeliv			=(void (*)(GLenum, const GLint*))GPA( "glLightModeliv" );
	qglLightf                    = dllLightf			=(void (*)(GLenum, GLenum, GLfloat))GPA( "glLightf" );
	qglLightfv                   = dllLightfv			=(void (*)(GLenum, GLenum, const GLfloat*))GPA( "glLightfv" );
	qglLighti                    = dllLighti			=(void (*)(GLenum, GLenum, GLint))GPA( "glLighti" );
	qglLightiv                   = dllLightiv			=(void (*)(GLenum, GLenum, const GLint*))GPA( "glLightiv" );
	qglLineStipple               = dllLineStipple			=(void (*)(GLint, GLushort))GPA( "glLineStipple" );
	qglLineWidth                 = dllLineWidth			=(void (*)(GLfloat))GPA( "glLineWidth" );
	qglListBase                  = dllListBase			=(void (*)(GLuint))GPA( "glListBase" );
	qglLoadIdentity              = dllLoadIdentity			=(void (*)())GPA( "glLoadIdentity" );
	qglLoadMatrixd               = dllLoadMatrixd			=(void (*)(const GLdouble*))GPA( "glLoadMatrixd" );
	qglLoadMatrixf               = dllLoadMatrixf			=(void (*)(const GLfloat*))GPA( "glLoadMatrixf" );
	qglLoadName                  = dllLoadName			=(void (*)(GLuint))GPA( "glLoadName" );
	qglLogicOp                   = dllLogicOp			=(void (*)(GLenum))GPA( "glLogicOp" );
	qglMap1d                     = dllMap1d				=(void (*)(GLenum, GLdouble, GLdouble, GLint, GLint, const GLdouble*))GPA( "glMap1d" );
	qglMap1f                     = dllMap1f				=(void (*)(GLenum, GLfloat, GLfloat, GLint, GLint, const GLfloat*))GPA( "glMap1f" );
	qglMap2d                     = dllMap2d				=(void (*)(GLenum, GLdouble, GLdouble, GLint, GLint, GLdouble, GLdouble, GLint, GLint, const GLdouble*))GPA( "glMap2d" );
	qglMap2f                     = dllMap2f				=(void (*)(GLenum, GLfloat, GLfloat, GLint, GLint, GLfloat, GLfloat, GLint, GLint, const GLfloat*))GPA( "glMap2f" );
	qglMapGrid1d                 = dllMapGrid1d			=(void (*)(GLint, GLdouble, GLdouble))GPA( "glMapGrid1d" );
	qglMapGrid1f                 = dllMapGrid1f			=(void (*)(GLint, GLfloat, GLfloat))GPA( "glMapGrid1f" );
	qglMapGrid2d                 = dllMapGrid2d			=(void (*)(GLint, GLdouble, GLdouble, GLint, GLdouble, GLdouble))GPA( "glMapGrid2d" );
	qglMapGrid2f                 = dllMapGrid2f			=(void (*)(GLint, GLfloat, GLfloat, GLint, GLfloat, GLfloat))GPA( "glMapGrid2f" );
	qglMaterialf                 = dllMaterialf			=(void (*)(GLenum, GLenum, GLfloat))GPA( "glMaterialf" );
	qglMaterialfv                = dllMaterialfv			=(void (*)(GLenum, GLenum, const GLfloat*))GPA( "glMaterialfv" );
	qglMateriali                 = dllMateriali			=(void (*)(GLenum, GLenum, GLint))GPA( "glMateriali" );
	qglMaterialiv                = dllMaterialiv			=(void (*)(GLenum, GLenum, const GLint*))GPA( "glMaterialiv" );
	qglMatrixMode                = dllMatrixMode			=(void (*)(GLenum))GPA( "glMatrixMode" );
	qglMultMatrixd               = dllMultMatrixd			=(void (*)(const GLdouble*))GPA( "glMultMatrixd" );
	qglMultMatrixf               = dllMultMatrixf			=(void (*)(const GLfloat*))GPA( "glMultMatrixf" );
	qglNewList                   = dllNewList			=(void (*)(GLuint, GLenum))GPA( "glNewList" );
	qglNormal3b                  = dllNormal3b			=(void (*)(GLbyte, GLbyte, GLbyte))GPA( "glNormal3b" );
	qglNormal3bv                 = dllNormal3bv			=(void (*)(const GLbyte*))GPA( "glNormal3bv" );
	qglNormal3d                  = dllNormal3d			=(void (*)(GLdouble, GLdouble, GLdouble))GPA( "glNormal3d" );
	qglNormal3dv                 = dllNormal3dv			=(void (*)(const GLdouble*))GPA( "glNormal3dv" );
	qglNormal3f                  = dllNormal3f			=(void (*)(GLfloat, GLfloat, GLfloat))GPA( "glNormal3f" );
	qglNormal3fv                 = dllNormal3fv			=(void (*)(const GLfloat*))GPA( "glNormal3fv" );
	qglNormal3i                  = dllNormal3i			=(void (*)(GLint, GLint, GLint))GPA( "glNormal3i" );
	qglNormal3iv                 = dllNormal3iv			=(void (*)(const GLint*))GPA( "glNormal3iv" );
	qglNormal3s                  = dllNormal3s			=(void (*)(GLshort, GLshort, GLshort))GPA( "glNormal3s" );
	qglNormal3sv                 = dllNormal3sv			=(void (*)(const GLshort*))GPA( "glNormal3sv" );
	qglNormalPointer             = dllNormalPointer			=(void (*)(GLenum, GLsizei, const GLvoid*))GPA( "glNormalPointer" );
	qglOrtho                     = dllOrtho				=(void (*)(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble))GPA( "glOrtho" );
	qglPassThrough               = dllPassThrough			=(void (*)(GLfloat))GPA( "glPassThrough" );
	qglPixelMapfv                = dllPixelMapfv			=(void (*)(GLenum, GLsizei, const GLfloat*))GPA( "glPixelMapfv" );
	qglPixelMapuiv               = dllPixelMapuiv			=(void (*)(GLenum, GLsizei, const GLuint*))GPA( "glPixelMapuiv" );
	qglPixelMapusv               = dllPixelMapusv			=(void (*)(GLenum, GLsizei, const GLushort*))GPA( "glPixelMapusv" );
	qglPixelStoref               = dllPixelStoref			=(void (*)(GLenum, GLfloat))GPA( "glPixelStoref" );
	qglPixelStorei               = dllPixelStorei			=(void (*)(GLenum, GLint))GPA( "glPixelStorei" );
	qglPixelTransferf            = dllPixelTransferf		=(void (*)(GLenum, GLfloat))GPA( "glPixelTransferf" );
	qglPixelTransferi            = dllPixelTransferi		=(void (*)(GLenum, GLint))GPA( "glPixelTransferi" );
	qglPixelZoom                 = dllPixelZoom			=(void (*)(GLfloat, GLfloat))GPA( "glPixelZoom" );
	qglPointSize                 = dllPointSize			=(void (*)(GLfloat))GPA( "glPointSize" );
	qglPolygonMode               = dllPolygonMode			=(void (*)(GLenum, GLenum))GPA( "glPolygonMode" );
	qglPolygonOffset             = dllPolygonOffset			=(void (*)(GLfloat, GLfloat))GPA( "glPolygonOffset" );
	qglPolygonStipple            = dllPolygonStipple		=(void (*)(const GLubyte*))GPA( "glPolygonStipple" );
	qglPopAttrib                 = dllPopAttrib			=(void (*)())GPA( "glPopAttrib" );
	qglPopClientAttrib           = dllPopClientAttrib		=(void (*)())GPA( "glPopClientAttrib" );
	qglPopMatrix                 = dllPopMatrix			=(void (*)())GPA( "glPopMatrix" );
	qglPopName                   = dllPopName			=(void (*)())GPA( "glPopName" );
	qglPrioritizeTextures        = dllPrioritizeTextures		=(void (*)(GLsizei, const GLuint*, const GLclampf*))GPA( "glPrioritizeTextures" );
	qglPushAttrib                = dllPushAttrib			=(void (*)(GLbitfield))GPA( "glPushAttrib" );
	qglPushClientAttrib          = dllPushClientAttrib		=(void (*)(GLbitfield))GPA( "glPushClientAttrib" );
	qglPushMatrix                = dllPushMatrix			=(void (*)())GPA( "glPushMatrix" );
	qglPushName                  = dllPushName			=(void (*)(GLuint))GPA( "glPushName" );
	qglRasterPos2d               = dllRasterPos2d			=(void (*)(GLdouble, GLdouble))GPA( "glRasterPos2d" );
	qglRasterPos2dv              = dllRasterPos2dv			=(void (*)(const GLdouble*))GPA( "glRasterPos2dv" );
	qglRasterPos2f               = dllRasterPos2f			=(void (*)(GLfloat, GLfloat))GPA( "glRasterPos2f" );
	qglRasterPos2fv              = dllRasterPos2fv			=(void (*)(const GLfloat*))GPA( "glRasterPos2fv" );
	qglRasterPos2i               = dllRasterPos2i			=(void (*)(GLint, GLint))GPA( "glRasterPos2i" );
	qglRasterPos2iv              = dllRasterPos2iv			=(void (*)(const GLint*))GPA( "glRasterPos2iv" );
	qglRasterPos2s               = dllRasterPos2s			=(void (*)(GLshort, GLshort))GPA( "glRasterPos2s" );
	qglRasterPos2sv              = dllRasterPos2sv			=(void (*)(const GLshort*))GPA( "glRasterPos2sv" );
	qglRasterPos3d               = dllRasterPos3d			=(void (*)(GLdouble, GLdouble, GLdouble))GPA( "glRasterPos3d" );
	qglRasterPos3dv              = dllRasterPos3dv			=(void (*)(const GLdouble*))GPA( "glRasterPos3dv" );
	qglRasterPos3f               = dllRasterPos3f			=(void (*)(GLfloat, GLfloat, GLfloat))GPA( "glRasterPos3f" );
	qglRasterPos3fv              = dllRasterPos3fv			=(void (*)(const GLfloat*))GPA( "glRasterPos3fv" );
	qglRasterPos3i               = dllRasterPos3i			=(void (*)(GLint, GLint, GLint))GPA( "glRasterPos3i" );
	qglRasterPos3iv              = dllRasterPos3iv			=(void (*)(const GLint*))GPA( "glRasterPos3iv" );
	qglRasterPos3s               = dllRasterPos3s			=(void (*)(GLshort, GLshort, GLshort))GPA( "glRasterPos3s" );
	qglRasterPos3sv              = dllRasterPos3sv			=(void (*)(const GLshort*))GPA( "glRasterPos3sv" );
	qglRasterPos4d               = dllRasterPos4d			=(void (*)(GLdouble, GLdouble, GLdouble, GLdouble))GPA( "glRasterPos4d" );
	qglRasterPos4dv              = dllRasterPos4dv			=(void (*)(const GLdouble*))GPA( "glRasterPos4dv" );
	qglRasterPos4f               = dllRasterPos4f			=(void (*)(GLfloat, GLfloat, GLfloat, GLfloat))GPA( "glRasterPos4f" );
	qglRasterPos4fv              = dllRasterPos4fv			=(void (*)(const GLfloat*))GPA( "glRasterPos4fv" );
	qglRasterPos4i               = dllRasterPos4i			=(void (*)(GLint, GLint, GLint, GLint))GPA( "glRasterPos4i" );
	qglRasterPos4iv              = dllRasterPos4iv			=(void (*)(const GLint*))GPA( "glRasterPos4iv" );
	qglRasterPos4s               = dllRasterPos4s			=(void (*)(GLshort, GLshort, GLshort, GLshort))GPA( "glRasterPos4s" );
	qglRasterPos4sv              = dllRasterPos4sv			=(void (*)(const GLshort*))GPA( "glRasterPos4sv" );
	qglReadBuffer                = dllReadBuffer			=(void (*)(GLenum))GPA( "glReadBuffer" );
	qglReadPixels                = dllReadPixels			=(void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid*))GPA( "glReadPixels" );
	qglRectd                     = dllRectd				=(void (*)(GLdouble, GLdouble, GLdouble, GLdouble))GPA( "glRectd" );
	qglRectdv                    = dllRectdv			=(void (*)(const GLdouble*, const GLdouble*))GPA( "glRectdv" );
	qglRectf                     = dllRectf				=(void (*)(GLfloat, GLfloat, GLfloat, GLfloat))GPA( "glRectf" );
	qglRectfv                    = dllRectfv			=(void (*)(const GLfloat*, const GLfloat*))GPA( "glRectfv" );
	qglRecti                     = dllRecti				=(void (*)(GLint, GLint, GLint, GLint))GPA( "glRecti" );
	qglRectiv                    = dllRectiv			=(void (*)(const GLint*, const GLint*))GPA( "glRectiv" );
	qglRects                     = dllRects				=(void (*)(GLshort, GLshort, GLshort, GLshort))GPA( "glRects" );
	qglRectsv                    = dllRectsv			=(void (*)(const GLshort*, const GLshort*))GPA( "glRectsv" );
	qglRenderMode                = dllRenderMode			=(GLint (*)(GLenum))GPA( "glRenderMode" );
	qglRotated                   = dllRotated			=(void (*)(GLdouble, GLdouble, GLdouble, GLdouble))GPA( "glRotated" );
	qglRotatef                   = dllRotatef			=(void (*)(GLfloat, GLfloat, GLfloat, GLfloat))GPA( "glRotatef" );
	qglScaled                    = dllScaled			=(void (*)(GLdouble, GLdouble, GLdouble))GPA( "glScaled" );
	qglScalef                    = dllScalef			=(void (*)(GLfloat, GLfloat, GLfloat))GPA( "glScalef" );
	qglScissor                   = dllScissor			=(void (*)(GLint, GLint, GLsizei, GLsizei))GPA( "glScissor" );
	qglSelectBuffer              = dllSelectBuffer			=(void (*)(GLsizei, GLuint*))GPA( "glSelectBuffer" );
	qglShadeModel                = dllShadeModel			=(void (*)(GLenum))GPA( "glShadeModel" );
	qglStencilFunc               = dllStencilFunc			=(void (*)(GLenum, GLint, GLuint))GPA( "glStencilFunc" );
	qglStencilMask               = dllStencilMask			=(void (*)(GLuint))GPA( "glStencilMask" );
	qglStencilOp                 = dllStencilOp			=(void (*)(GLenum, GLenum, GLenum))GPA( "glStencilOp" );
	qglTexCoord1d                = dllTexCoord1d			=(void (*)(GLdouble))GPA( "glTexCoord1d" );
	qglTexCoord1dv               = dllTexCoord1dv			=(void (*)(const GLdouble*))GPA( "glTexCoord1dv" );
	qglTexCoord1f                = dllTexCoord1f			=(void (*)(GLfloat))GPA( "glTexCoord1f" );
	qglTexCoord1fv               = dllTexCoord1fv			=(void (*)(const GLfloat*))GPA( "glTexCoord1fv" );
	qglTexCoord1i                = dllTexCoord1i			=(void (*)(GLint))GPA( "glTexCoord1i" );
	qglTexCoord1iv               = dllTexCoord1iv			=(void (*)(const GLint*))GPA( "glTexCoord1iv" );
	qglTexCoord1s                = dllTexCoord1s			=(void (*)(GLshort))GPA( "glTexCoord1s" );
	qglTexCoord1sv               = dllTexCoord1sv			=(void (*)(const GLshort*))GPA( "glTexCoord1sv" );
	qglTexCoord2d                = dllTexCoord2d			=(void (*)(GLdouble, GLdouble))GPA( "glTexCoord2d" );
	qglTexCoord2dv               = dllTexCoord2dv			=(void (*)(const GLdouble*))GPA( "glTexCoord2dv" );
	qglTexCoord2f                = dllTexCoord2f			=(void (*)(GLfloat, GLfloat))GPA( "glTexCoord2f" );
	qglTexCoord2fv               = dllTexCoord2fv			=(void (*)(const GLfloat*))GPA( "glTexCoord2fv" );
	qglTexCoord2i                = dllTexCoord2i			=(void (*)(GLint, GLint))GPA( "glTexCoord2i" );
	qglTexCoord2iv               = dllTexCoord2iv			=(void (*)(const GLint*))GPA( "glTexCoord2iv" );
	qglTexCoord2s                = dllTexCoord2s			=(void (*)(GLshort, GLshort))GPA( "glTexCoord2s" );
	qglTexCoord2sv               = dllTexCoord2sv			=(void (*)(const GLshort*))GPA( "glTexCoord2sv" );
	qglTexCoord3d                = dllTexCoord3d			=(void (*)(GLdouble, GLdouble, GLdouble))GPA( "glTexCoord3d" );
	qglTexCoord3dv               = dllTexCoord3dv			=(void (*)(const GLdouble*))GPA( "glTexCoord3dv" );
	qglTexCoord3f                = dllTexCoord3f			=(void (*)(GLfloat, GLfloat, GLfloat))GPA( "glTexCoord3f" );
	qglTexCoord3fv               = dllTexCoord3fv			=(void (*)(const GLfloat*))GPA( "glTexCoord3fv" );
	qglTexCoord3i                = dllTexCoord3i			=(void (*)(GLint, GLint, GLint))GPA( "glTexCoord3i" );
	qglTexCoord3iv               = dllTexCoord3iv			=(void (*)(const GLint*))GPA( "glTexCoord3iv" );
	qglTexCoord3s                = dllTexCoord3s			=(void (*)(GLshort, GLshort, GLshort))GPA( "glTexCoord3s" );
	qglTexCoord3sv               = dllTexCoord3sv			=(void (*)(const GLshort*))GPA( "glTexCoord3sv" );
	qglTexCoord4d                = dllTexCoord4d			=(void (*)(GLdouble, GLdouble, GLdouble, GLdouble))GPA( "glTexCoord4d" );
	qglTexCoord4dv               = dllTexCoord4dv			=(void (*)(const GLdouble*))GPA( "glTexCoord4dv" );
	qglTexCoord4f                = dllTexCoord4f			=(void (*)(GLfloat, GLfloat, GLfloat, GLfloat))GPA( "glTexCoord4f" );
	qglTexCoord4fv               = dllTexCoord4fv			=(void (*)(const GLfloat*))GPA( "glTexCoord4fv" );
	qglTexCoord4i                = dllTexCoord4i			=(void (*)(GLint, GLint, GLint, GLint))GPA( "glTexCoord4i" );
	qglTexCoord4iv               = dllTexCoord4iv			=(void (*)(const GLint*))GPA( "glTexCoord4iv" );
	qglTexCoord4s                = dllTexCoord4s			=(void (*)(GLshort, GLshort, GLshort, GLshort))GPA( "glTexCoord4s" );
	qglTexCoord4sv               = dllTexCoord4sv			=(void (*)(const GLshort*))GPA( "glTexCoord4sv" );
	qglTexCoordPointer           = dllTexCoordPointer		=(void (*)(GLint, GLenum, GLsizei, const GLvoid*))GPA( "glTexCoordPointer" );
	qglTexEnvf                   = dllTexEnvf			=(void (*)(GLenum, GLenum, GLfloat))GPA( "glTexEnvf" );
	qglTexEnvfv                  = dllTexEnvfv			=(void (*)(GLenum, GLenum, const GLfloat*))GPA( "glTexEnvfv" );
	qglTexEnvi                   = dllTexEnvi			=(void (*)(GLenum, GLenum, GLint))GPA( "glTexEnvi" );
	qglTexEnviv                  = dllTexEnviv			=(void (*)(GLenum, GLenum, const GLint*))GPA( "glTexEnviv" );
	qglTexGend                   = dllTexGend			=(void (*)(GLenum, GLenum, GLdouble))GPA( "glTexGend" );
	qglTexGendv                  = dllTexGendv			=(void (*)(GLenum, GLenum, const GLdouble*))GPA( "glTexGendv" );
	qglTexGenf                   = dllTexGenf			=(void (*)(GLenum, GLenum, GLfloat))GPA( "glTexGenf" );
	qglTexGenfv                  = dllTexGenfv			=(void (*)(GLenum, GLenum, const GLfloat*))GPA( "glTexGenfv" );
	qglTexGeni                   = dllTexGeni			=(void (*)(GLenum, GLenum, GLint))GPA( "glTexGeni" );
	qglTexGeniv                  = dllTexGeniv			=(void (*)(GLenum, GLenum, const GLint*))GPA( "glTexGeniv" );
	qglTexImage1D                = dllTexImage1D			=(void (*)(GLenum, GLint, GLint, GLsizei, GLint, GLenum, GLenum, const GLvoid*))GPA( "glTexImage1D" );
	qglTexImage2D                = dllTexImage2D			=(void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid*))GPA( "glTexImage2D" );
	qglTexParameterf             = dllTexParameterf			=(void (*)(GLenum, GLenum, GLfloat))GPA( "glTexParameterf" );
	qglTexParameterfv            = dllTexParameterfv		=(void (*)(GLenum, GLenum, const GLfloat*))GPA( "glTexParameterfv" );
	qglTexParameteri             = dllTexParameteri			=(void (*)(GLenum, GLenum, GLint))GPA( "glTexParameteri" );
	qglTexParameteriv            = dllTexParameteriv		=(void (*)(GLenum, GLenum, const GLint*))GPA( "glTexParameteriv" );
	qglTexSubImage1D             = dllTexSubImage1D			=(void (*)(GLenum, GLint, GLint, GLsizei, GLenum, GLenum, const GLvoid*))GPA( "glTexSubImage1D" );
	qglTexSubImage2D             = dllTexSubImage2D			=(void (*)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid*))GPA( "glTexSubImage2D" );
	qglTranslated                = dllTranslated			=(void (*)(GLdouble, GLdouble, GLdouble))GPA( "glTranslated" );
	qglTranslatef                = dllTranslatef			=(void (*)(GLfloat, GLfloat, GLfloat))GPA( "glTranslatef" );
	qglVertex2d                  = dllVertex2d			=(void (*)(GLdouble, GLdouble))GPA( "glVertex2d" );
	qglVertex2dv                 = dllVertex2dv			=(void (*)(const GLdouble*))GPA( "glVertex2dv" );
	qglVertex2f                  = dllVertex2f			=(void (*)(GLfloat, GLfloat))GPA( "glVertex2f" );
	qglVertex2fv                 = dllVertex2fv			=(void (*)(const GLfloat*))GPA( "glVertex2fv" );
	qglVertex2i                  = dllVertex2i			=(void (*)(GLint, GLint))GPA( "glVertex2i" );
	qglVertex2iv                 = dllVertex2iv			=(void (*)(const GLint*))GPA( "glVertex2iv" );
	qglVertex2s                  = dllVertex2s			=(void (*)(GLshort, GLshort))GPA( "glVertex2s" );
	qglVertex2sv                 = dllVertex2sv			=(void (*)(const GLshort*))GPA( "glVertex2sv" );
	qglVertex3d                  = dllVertex3d			=(void (*)(GLdouble, GLdouble, GLdouble))GPA( "glVertex3d" );
	qglVertex3dv                 = dllVertex3dv			=(void (*)(const GLdouble*))GPA( "glVertex3dv" );
	qglVertex3f                  = dllVertex3f			=(void (*)(GLfloat, GLfloat, GLfloat))GPA( "glVertex3f" );
	qglVertex3fv                 = dllVertex3fv			=(void (*)(const GLfloat*))GPA( "glVertex3fv" );
	qglVertex3i                  = dllVertex3i			=(void (*)(GLint, GLint, GLint))GPA( "glVertex3i" );
	qglVertex3iv                 = dllVertex3iv			=(void (*)(const GLint*))GPA( "glVertex3iv" );
	qglVertex3s                  = dllVertex3s			=(void (*)(GLshort, GLshort, GLshort))GPA( "glVertex3s" );
	qglVertex3sv                 = dllVertex3sv			=(void (*)(const GLshort*))GPA( "glVertex3sv" );
	qglVertex4d                  = dllVertex4d			=(void (*)(GLdouble, GLdouble, GLdouble, GLdouble))GPA( "glVertex4d" );
	qglVertex4dv                 = dllVertex4dv			=(void (*)(const GLdouble*))GPA( "glVertex4dv" );
	qglVertex4f                  = dllVertex4f			=(void (*)(GLfloat, GLfloat, GLfloat, GLfloat))GPA( "glVertex4f" );
	qglVertex4fv                 = dllVertex4fv			=(void (*)(const GLfloat*))GPA( "glVertex4fv" );
	qglVertex4i                  = dllVertex4i			=(void (*)(GLint, GLint, GLint, GLint))GPA( "glVertex4i" );
	qglVertex4iv                 = dllVertex4iv			=(void (*)(const GLint*))GPA( "glVertex4iv" );
	qglVertex4s                  = dllVertex4s			=(void (*)(GLshort, GLshort, GLshort, GLshort))GPA( "glVertex4s" );
	qglVertex4sv                 = dllVertex4sv			=(void (*)(const GLshort*))GPA( "glVertex4sv" );
	qglVertexPointer             = dllVertexPointer			=(void (*)(GLint, GLenum, GLsizei, const GLvoid*))GPA( "glVertexPointer" );
	qglViewport                  = dllViewport			=(void (*)(GLint, GLint, GLsizei, GLsizei))GPA( "glViewport" );

// bk001129 - from cvs1.17 (mkv)
#if defined(__FX__)
	qfxMesaCreateContext         =  GPA("fxMesaCreateContext");
	qfxMesaCreateBestContext     =  GPA("fxMesaCreateBestContext");
	qfxMesaDestroyContext        =  GPA("fxMesaDestroyContext");
	qfxMesaMakeCurrent           =  GPA("fxMesaMakeCurrent");
	qfxMesaGetCurrentContext     =  GPA("fxMesaGetCurrentContext");
	qfxMesaSwapBuffers           =  GPA("fxMesaSwapBuffers");
#endif

#if !defined(USE_SDL_VIDEO)
	qglXChooseVisual             =  GPA("glXChooseVisual");
	qglXCreateContext            =  GPA("glXCreateContext");
	qglXDestroyContext           =  GPA("glXDestroyContext");
	qglXMakeCurrent              =  GPA("glXMakeCurrent");
	qglXCopyContext              =  GPA("glXCopyContext");
	qglXSwapBuffers              =  GPA("glXSwapBuffers");
#endif

	qglLockArraysEXT = 0;
	qglUnlockArraysEXT = 0;
	qglActiveTextureARB = 0;
	qglClientActiveTextureARB = 0;
	qglSwapIntervalEXT = 0;

	qglPointParameterfEXT = NULL;
	qglPointParameterfvEXT = NULL;
	qglColorTableEXT = NULL;
	qgl3DfxSetPaletteEXT = NULL;
	qglSelectTextureSGIS = NULL;
	qglMTexCoord2fSGIS = NULL;
	qglMultiTexCoord2fARB = NULL;

	return qtrue;
}

// QGL_ARB stuff
#define QGL_ARB(fn, apicall) q##fn = ( apicall ) GPA( #fn"ARB" ); \
        if (!q##fn) Com_Error( ERR_FATAL, "QGL_ARB: "#fn"ARB not found" );

PFNGLGENPROGRAMSARBPROC qglGenPrograms;
PFNGLBINDPROGRAMARBPROC qglBindProgram;
PFNGLPROGRAMSTRINGARBPROC qglProgramString;
PFNGLDELETEPROGRAMSARBPROC qglDeletePrograms;

PFNGLDISABLEVERTEXATTRIBARRAYARBPROC qglDisableVertexAttribArray;
PFNGLENABLEVERTEXATTRIBARRAYARBPROC qglEnableVertexAttribArray;
PFNGLVERTEXATTRIBPOINTERARBPROC qglVertexAttribPointer;

PFNGLPROGRAMENVPARAMETER4FARBPROC qglProgramEnvParameter4f;
PFNGLPROGRAMLOCALPARAMETER4FARBPROC qglProgramLocalParameter4f;

// Snag ARB process handles
qbool GLW_InitARB()
{
	if (atof((const char*)qglGetString(GL_VERSION)) < 1.4)
		return qfalse;

	QGL_ARB( glGenPrograms, PFNGLGENPROGRAMSARBPROC );
	QGL_ARB( glBindProgram, PFNGLBINDPROGRAMARBPROC );
	QGL_ARB( glProgramString, PFNGLPROGRAMSTRINGARBPROC );
	QGL_ARB( glDeletePrograms, PFNGLDELETEPROGRAMSARBPROC );

	QGL_ARB( glDisableVertexAttribArray, PFNGLDISABLEVERTEXATTRIBARRAYARBPROC );
	QGL_ARB( glEnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYARBPROC );
	QGL_ARB( glVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERARBPROC );

	QGL_ARB( glProgramEnvParameter4f, PFNGLPROGRAMENVPARAMETER4FARBPROC ); 
	QGL_ARB( glProgramLocalParameter4f, PFNGLPROGRAMLOCALPARAMETER4FARBPROC );

	return qtrue;
}

// END linux_qgl.c
