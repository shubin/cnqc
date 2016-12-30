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
** QGL_WIN.C
**
** This file implements the operating system binding of GL to QGL function
** pointers.  When doing a port of Quake3 you must implement the following
** two functions:
**
** QGL_Init() - loads libraries, assigns function pointers, etc.
** QGL_Shutdown() - unloads libraries, NULLs function pointers
*/

#include "../qcommon/q_shared.h"

#define WIN32_LEAN_AND_MEAN
#include "../win32/windows.h"
#include <GL/gl.h>
#include "glext.h"

#include "glw_win.h"

int ( WINAPI * qwglSwapIntervalEXT)( int interval );

int   ( WINAPI * qwglChoosePixelFormat )(HDC, CONST PIXELFORMATDESCRIPTOR *);
int   ( WINAPI * qwglChoosePixelFormatARB )(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
int   ( WINAPI * qwglDescribePixelFormat) (HDC, int, UINT, LPPIXELFORMATDESCRIPTOR);
int   ( WINAPI * qwglGetPixelFormat)(HDC);
BOOL  ( WINAPI * qwglSetPixelFormat)(HDC, int, CONST PIXELFORMATDESCRIPTOR *);
BOOL  ( WINAPI * qwglSwapBuffers)(HDC);

BOOL  ( WINAPI * qwglCopyContext)(HGLRC, HGLRC, UINT);
HGLRC ( WINAPI * qwglCreateContext)(HDC);
HGLRC ( WINAPI * qwglCreateLayerContext)(HDC, int);
BOOL  ( WINAPI * qwglDeleteContext)(HGLRC);
HGLRC ( WINAPI * qwglGetCurrentContext)(VOID);
HDC   ( WINAPI * qwglGetCurrentDC)(VOID);
PROC  ( WINAPI * qwglGetProcAddress)(LPCSTR);
BOOL  ( WINAPI * qwglMakeCurrent)(HDC, HGLRC);
BOOL  ( WINAPI * qwglShareLists)(HGLRC, HGLRC);

BOOL ( WINAPI * qwglDescribeLayerPlane)(HDC, int, int, UINT,
                                            LPLAYERPLANEDESCRIPTOR);
int  ( WINAPI * qwglSetLayerPaletteEntries)(HDC, int, int, int,
                                                CONST COLORREF *);
int  ( WINAPI * qwglGetLayerPaletteEntries)(HDC, int, int, int,
                                                COLORREF *);
BOOL ( WINAPI * qwglRealizeLayerPalette)(HDC, int, BOOL);
BOOL ( WINAPI * qwglSwapLayerBuffers)(HDC, UINT);

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

void ( APIENTRY * qglActiveTextureARB )( GLenum texture );
void ( APIENTRY * qglClientActiveTextureARB )( GLenum texture );

PFNGLLOCKARRAYSEXTPROC qglLockArraysEXT;
PFNGLUNLOCKARRAYSEXTPROC qglUnlockArraysEXT;


/*
** QGL_Shutdown
**
** Unloads the specified DLL then nulls out all the proc pointers.  This
** is only called during a hard shutdown of the OGL subsystem (e.g. vid_restart).
*/
void QGL_Shutdown()
{
	//ri.Printf( PRINT_ALL, "...shutting down QGL\n" );

	if ( glw_state.hinstOpenGL )
	{
		//ri.Printf( PRINT_ALL, "...unloading OpenGL DLL\n" );
		FreeLibrary( glw_state.hinstOpenGL );
	}

	glw_state.hinstOpenGL = NULL;

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

	qwglCopyContext              = NULL;
	qwglCreateContext            = NULL;
	qwglCreateLayerContext       = NULL;
	qwglDeleteContext            = NULL;
	qwglDescribeLayerPlane       = NULL;
	qwglGetCurrentContext        = NULL;
	qwglGetCurrentDC             = NULL;
	qwglGetLayerPaletteEntries   = NULL;
	qwglGetProcAddress           = NULL;
	qwglMakeCurrent              = NULL;
	qwglRealizeLayerPalette      = NULL;
	qwglSetLayerPaletteEntries   = NULL;
	qwglShareLists               = NULL;
	qwglSwapLayerBuffers         = NULL;

	qwglChoosePixelFormat        = NULL;
	qwglChoosePixelFormatARB     = NULL;
	qwglDescribePixelFormat      = NULL;
	qwglGetPixelFormat           = NULL;
	qwglSetPixelFormat           = NULL;
	qwglSwapBuffers              = NULL;
}


#ifdef _MSC_VER
#pragma warning( disable : 4057 4113 ) // pretty much all GPA use
#pragma warning( disable : 4047 4133 ) // various WINGDIAPI/FARPROC mismatches
#endif

#define QGL_GPA(fn) q##fn = GetProcAddress( glw_state.hinstOpenGL, #fn )

/*
** QGL_Init
**
** This is responsible for binding our qgl function pointers to
** the appropriate GL stuff.  In Windows this means doing a
** LoadLibrary and a bunch of calls to GetProcAddress.  On other
** operating systems we need to do the right thing, whatever that
** might be.
*/
qbool QGL_Init( const char* dllname )
{
	if ( ( glw_state.hinstOpenGL = LoadLibrary( dllname ) ) == 0 )
		return qfalse;

	QGL_GPA( glAccum );
	QGL_GPA( glAlphaFunc );
	QGL_GPA( glAreTexturesResident );
	QGL_GPA( glArrayElement );
	QGL_GPA( glBegin );
	QGL_GPA( glBindTexture );
	QGL_GPA( glBitmap );
	QGL_GPA( glBlendFunc );
	QGL_GPA( glCallList );
	QGL_GPA( glCallLists );
	QGL_GPA( glClear );
	QGL_GPA( glClearAccum );
	QGL_GPA( glClearColor );
	QGL_GPA( glClearDepth );
	QGL_GPA( glClearIndex );
	QGL_GPA( glClearStencil );
	QGL_GPA( glClipPlane );
	QGL_GPA( glColor3b );
	QGL_GPA( glColor3bv );
	QGL_GPA( glColor3d );
	QGL_GPA( glColor3dv );
	QGL_GPA( glColor3f );
	QGL_GPA( glColor3fv );
	QGL_GPA( glColor3i );
	QGL_GPA( glColor3iv );
	QGL_GPA( glColor3s );
	QGL_GPA( glColor3sv );
	QGL_GPA( glColor3ub );
	QGL_GPA( glColor3ubv );
	QGL_GPA( glColor3ui );
	QGL_GPA( glColor3uiv );
	QGL_GPA( glColor3us );
	QGL_GPA( glColor3usv );
	QGL_GPA( glColor4b );
	QGL_GPA( glColor4bv );
	QGL_GPA( glColor4d );
	QGL_GPA( glColor4dv );
	QGL_GPA( glColor4f );
	QGL_GPA( glColor4fv );
	QGL_GPA( glColor4i );
	QGL_GPA( glColor4iv );
	QGL_GPA( glColor4s );
	QGL_GPA( glColor4sv );
	QGL_GPA( glColor4ub );
	QGL_GPA( glColor4ubv );
	QGL_GPA( glColor4ui );
	QGL_GPA( glColor4uiv );
	QGL_GPA( glColor4us );
	QGL_GPA( glColor4usv );
	QGL_GPA( glColorMask );
	QGL_GPA( glColorMaterial );
	QGL_GPA( glColorPointer );
	QGL_GPA( glCopyPixels );
	QGL_GPA( glCopyTexImage1D );
	QGL_GPA( glCopyTexImage2D );
	QGL_GPA( glCopyTexSubImage1D );
	QGL_GPA( glCopyTexSubImage2D );
	QGL_GPA( glCullFace );
	QGL_GPA( glDeleteLists );
	QGL_GPA( glDeleteTextures );
	QGL_GPA( glDepthFunc );
	QGL_GPA( glDepthMask );
	QGL_GPA( glDepthRange );
	QGL_GPA( glDisable );
	QGL_GPA( glDisableClientState );
	QGL_GPA( glDrawArrays );
	QGL_GPA( glDrawBuffer );
	QGL_GPA( glDrawElements );
	QGL_GPA( glDrawPixels );
	QGL_GPA( glEdgeFlag );
	QGL_GPA( glEdgeFlagPointer );
	QGL_GPA( glEdgeFlagv );
	QGL_GPA( glEnable );
	QGL_GPA( glEnableClientState );
	QGL_GPA( glEnd );
	QGL_GPA( glEndList );
	QGL_GPA( glEvalCoord1d );
	QGL_GPA( glEvalCoord1dv );
	QGL_GPA( glEvalCoord1f );
	QGL_GPA( glEvalCoord1fv );
	QGL_GPA( glEvalCoord2d );
	QGL_GPA( glEvalCoord2dv );
	QGL_GPA( glEvalCoord2f );
	QGL_GPA( glEvalCoord2fv );
	QGL_GPA( glEvalMesh1 );
	QGL_GPA( glEvalMesh2 );
	QGL_GPA( glEvalPoint1 );
	QGL_GPA( glEvalPoint2 );
	QGL_GPA( glFeedbackBuffer );
	QGL_GPA( glFinish );
	QGL_GPA( glFlush );
	QGL_GPA( glFogf );
	QGL_GPA( glFogfv );
	QGL_GPA( glFogi );
	QGL_GPA( glFogiv );
	QGL_GPA( glFrontFace );
	QGL_GPA( glFrustum );
	QGL_GPA( glGenLists );
	QGL_GPA( glGenTextures );
	QGL_GPA( glGetBooleanv );
	QGL_GPA( glGetClipPlane );
	QGL_GPA( glGetDoublev );
	QGL_GPA( glGetError );
	QGL_GPA( glGetFloatv );
	QGL_GPA( glGetIntegerv );
	QGL_GPA( glGetLightfv );
	QGL_GPA( glGetLightiv );
	QGL_GPA( glGetMapdv );
	QGL_GPA( glGetMapfv );
	QGL_GPA( glGetMapiv );
	QGL_GPA( glGetMaterialfv );
	QGL_GPA( glGetMaterialiv );
	QGL_GPA( glGetPixelMapfv );
	QGL_GPA( glGetPixelMapuiv );
	QGL_GPA( glGetPixelMapusv );
	QGL_GPA( glGetPointerv );
	QGL_GPA( glGetPolygonStipple );
	QGL_GPA( glGetString );
	QGL_GPA( glGetTexEnvfv );
	QGL_GPA( glGetTexEnviv );
	QGL_GPA( glGetTexGendv );
	QGL_GPA( glGetTexGenfv );
	QGL_GPA( glGetTexGeniv );
	QGL_GPA( glGetTexImage );
	QGL_GPA( glGetTexLevelParameterfv );
	QGL_GPA( glGetTexLevelParameteriv );
	QGL_GPA( glGetTexParameterfv );
	QGL_GPA( glGetTexParameteriv );
	QGL_GPA( glHint );
	QGL_GPA( glIndexMask );
	QGL_GPA( glIndexPointer );
	QGL_GPA( glIndexd );
	QGL_GPA( glIndexdv );
	QGL_GPA( glIndexf );
	QGL_GPA( glIndexfv );
	QGL_GPA( glIndexi );
	QGL_GPA( glIndexiv );
	QGL_GPA( glIndexs );
	QGL_GPA( glIndexsv );
	QGL_GPA( glIndexub );
	QGL_GPA( glIndexubv );
	QGL_GPA( glInitNames );
	QGL_GPA( glInterleavedArrays );
	QGL_GPA( glIsEnabled );
	QGL_GPA( glIsList );
	QGL_GPA( glIsTexture );
	QGL_GPA( glLightModelf );
	QGL_GPA( glLightModelfv );
	QGL_GPA( glLightModeli );
	QGL_GPA( glLightModeliv );
	QGL_GPA( glLightf );
	QGL_GPA( glLightfv );
	QGL_GPA( glLighti );
	QGL_GPA( glLightiv );
	QGL_GPA( glLineStipple );
	QGL_GPA( glLineWidth );
	QGL_GPA( glListBase );
	QGL_GPA( glLoadIdentity );
	QGL_GPA( glLoadMatrixd );
	QGL_GPA( glLoadMatrixf );
	QGL_GPA( glLoadName );
	QGL_GPA( glLogicOp );
	QGL_GPA( glMap1d );
	QGL_GPA( glMap1f );
	QGL_GPA( glMap2d );
	QGL_GPA( glMap2f );
	QGL_GPA( glMapGrid1d );
	QGL_GPA( glMapGrid1f );
	QGL_GPA( glMapGrid2d );
	QGL_GPA( glMapGrid2f );
	QGL_GPA( glMaterialf );
	QGL_GPA( glMaterialfv );
	QGL_GPA( glMateriali );
	QGL_GPA( glMaterialiv );
	QGL_GPA( glMatrixMode );
	QGL_GPA( glMultMatrixd );
	QGL_GPA( glMultMatrixf );
	QGL_GPA( glNewList );
	QGL_GPA( glNormal3b );
	QGL_GPA( glNormal3bv );
	QGL_GPA( glNormal3d );
	QGL_GPA( glNormal3dv );
	QGL_GPA( glNormal3f );
	QGL_GPA( glNormal3fv );
	QGL_GPA( glNormal3i );
	QGL_GPA( glNormal3iv );
	QGL_GPA( glNormal3s );
	QGL_GPA( glNormal3sv );
	QGL_GPA( glNormalPointer );
	QGL_GPA( glOrtho );
	QGL_GPA( glPassThrough );
	QGL_GPA( glPixelMapfv );
	QGL_GPA( glPixelMapuiv );
	QGL_GPA( glPixelMapusv );
	QGL_GPA( glPixelStoref );
	QGL_GPA( glPixelStorei );
	QGL_GPA( glPixelTransferf );
	QGL_GPA( glPixelTransferi );
	QGL_GPA( glPixelZoom );
	QGL_GPA( glPointSize );
	QGL_GPA( glPolygonMode );
	QGL_GPA( glPolygonOffset );
	QGL_GPA( glPolygonStipple );
	QGL_GPA( glPopAttrib );
	QGL_GPA( glPopClientAttrib );
	QGL_GPA( glPopMatrix );
	QGL_GPA( glPopName );
	QGL_GPA( glPrioritizeTextures );
	QGL_GPA( glPushAttrib );
	QGL_GPA( glPushClientAttrib );
	QGL_GPA( glPushMatrix );
	QGL_GPA( glPushName );
	QGL_GPA( glRasterPos2d );
	QGL_GPA( glRasterPos2dv );
	QGL_GPA( glRasterPos2f );
	QGL_GPA( glRasterPos2fv );
	QGL_GPA( glRasterPos2i );
	QGL_GPA( glRasterPos2iv );
	QGL_GPA( glRasterPos2s );
	QGL_GPA( glRasterPos2sv );
	QGL_GPA( glRasterPos3d );
	QGL_GPA( glRasterPos3dv );
	QGL_GPA( glRasterPos3f );
	QGL_GPA( glRasterPos3fv );
	QGL_GPA( glRasterPos3i );
	QGL_GPA( glRasterPos3iv );
	QGL_GPA( glRasterPos3s );
	QGL_GPA( glRasterPos3sv );
	QGL_GPA( glRasterPos4d );
	QGL_GPA( glRasterPos4dv );
	QGL_GPA( glRasterPos4f );
	QGL_GPA( glRasterPos4fv );
	QGL_GPA( glRasterPos4i );
	QGL_GPA( glRasterPos4iv );
	QGL_GPA( glRasterPos4s );
	QGL_GPA( glRasterPos4sv );
	QGL_GPA( glReadBuffer );
	QGL_GPA( glReadPixels );
	QGL_GPA( glRectd );
	QGL_GPA( glRectdv );
	QGL_GPA( glRectf );
	QGL_GPA( glRectfv );
	QGL_GPA( glRecti );
	QGL_GPA( glRectiv );
	QGL_GPA( glRects );
	QGL_GPA( glRectsv );
	QGL_GPA( glRenderMode );
	QGL_GPA( glRotated );
	QGL_GPA( glRotatef );
	QGL_GPA( glScaled );
	QGL_GPA( glScalef );
	QGL_GPA( glScissor );
	QGL_GPA( glSelectBuffer );
	QGL_GPA( glShadeModel );
	QGL_GPA( glStencilFunc );
	QGL_GPA( glStencilMask );
	QGL_GPA( glStencilOp );
	QGL_GPA( glTexCoord1d );
	QGL_GPA( glTexCoord1dv );
	QGL_GPA( glTexCoord1f );
	QGL_GPA( glTexCoord1fv );
	QGL_GPA( glTexCoord1i );
	QGL_GPA( glTexCoord1iv );
	QGL_GPA( glTexCoord1s );
	QGL_GPA( glTexCoord1sv );
	QGL_GPA( glTexCoord2d );
	QGL_GPA( glTexCoord2dv );
	QGL_GPA( glTexCoord2f );
	QGL_GPA( glTexCoord2fv );
	QGL_GPA( glTexCoord2i );
	QGL_GPA( glTexCoord2iv );
	QGL_GPA( glTexCoord2s );
	QGL_GPA( glTexCoord2sv );
	QGL_GPA( glTexCoord3d );
	QGL_GPA( glTexCoord3dv );
	QGL_GPA( glTexCoord3f );
	QGL_GPA( glTexCoord3fv );
	QGL_GPA( glTexCoord3i );
	QGL_GPA( glTexCoord3iv );
	QGL_GPA( glTexCoord3s );
	QGL_GPA( glTexCoord3sv );
	QGL_GPA( glTexCoord4d );
	QGL_GPA( glTexCoord4dv );
	QGL_GPA( glTexCoord4f );
	QGL_GPA( glTexCoord4fv );
	QGL_GPA( glTexCoord4i );
	QGL_GPA( glTexCoord4iv );
	QGL_GPA( glTexCoord4s );
	QGL_GPA( glTexCoord4sv );
	QGL_GPA( glTexCoordPointer );
	QGL_GPA( glTexEnvf );
	QGL_GPA( glTexEnvfv );
	QGL_GPA( glTexEnvi );
	QGL_GPA( glTexEnviv );
	QGL_GPA( glTexGend );
	QGL_GPA( glTexGendv );
	QGL_GPA( glTexGenf );
	QGL_GPA( glTexGenfv );
	QGL_GPA( glTexGeni );
	QGL_GPA( glTexGeniv );
	QGL_GPA( glTexImage1D );
	QGL_GPA( glTexImage2D );
	QGL_GPA( glTexParameterf );
	QGL_GPA( glTexParameterfv );
	QGL_GPA( glTexParameteri );
	QGL_GPA( glTexParameteriv );
	QGL_GPA( glTexSubImage1D );
	QGL_GPA( glTexSubImage2D );
	QGL_GPA( glTranslated );
	QGL_GPA( glTranslatef );
	QGL_GPA( glVertex2d );
	QGL_GPA( glVertex2dv );
	QGL_GPA( glVertex2f );
	QGL_GPA( glVertex2fv );
	QGL_GPA( glVertex2i );
	QGL_GPA( glVertex2iv );
	QGL_GPA( glVertex2s );
	QGL_GPA( glVertex2sv );
	QGL_GPA( glVertex3d );
	QGL_GPA( glVertex3dv );
	QGL_GPA( glVertex3f );
	QGL_GPA( glVertex3fv );
	QGL_GPA( glVertex3i );
	QGL_GPA( glVertex3iv );
	QGL_GPA( glVertex3s );
	QGL_GPA( glVertex3sv );
	QGL_GPA( glVertex4d );
	QGL_GPA( glVertex4dv );
	QGL_GPA( glVertex4f );
	QGL_GPA( glVertex4fv );
	QGL_GPA( glVertex4i );
	QGL_GPA( glVertex4iv );
	QGL_GPA( glVertex4s );
	QGL_GPA( glVertex4sv );
	QGL_GPA( glVertexPointer );
	QGL_GPA( glViewport );

	QGL_GPA( wglCopyContext );
	QGL_GPA( wglCreateContext );
	QGL_GPA( wglCreateLayerContext );
	QGL_GPA( wglDeleteContext );
	QGL_GPA( wglDescribeLayerPlane );
	QGL_GPA( wglGetCurrentContext );
	QGL_GPA( wglGetCurrentDC );
	QGL_GPA( wglGetLayerPaletteEntries );
	QGL_GPA( wglGetProcAddress );
	QGL_GPA( wglMakeCurrent );
	QGL_GPA( wglRealizeLayerPalette );
	QGL_GPA( wglSetLayerPaletteEntries );
	QGL_GPA( wglShareLists );
	QGL_GPA( wglSwapLayerBuffers );

	QGL_GPA( wglChoosePixelFormat );
	QGL_GPA( wglDescribePixelFormat );
	QGL_GPA( wglGetPixelFormat );
	QGL_GPA( wglSetPixelFormat );
	QGL_GPA( wglSwapBuffers );

	// required extensions
	qglLockArraysEXT = 0;
	qglUnlockArraysEXT = 0;
	qglActiveTextureARB = 0;
	qglClientActiveTextureARB = 0;

	// optional extensions
	qwglSwapIntervalEXT = 0;
	qwglChoosePixelFormatARB = 0;

	return qtrue;
};

#undef QGL_GPA

#ifdef _MSC_VER
#pragma warning( default : 4047 4133 )
#endif


///////////////////////////////////////////////////////////////

// the logfile system is obsolete - use GLIntercept

// note that all GL2/ARB functions have to be retrieved by wglGPA
// which means they can't be set up until AFTER there's a current context


// GL1.4 functions for shitty intel drivers, sigh

#define QGL_ARB(fn) q##fn = qwglGetProcAddress( #fn##"ARB" ); \
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


qbool GLW_InitARB()
{
	if (atof((const char*)qglGetString(GL_VERSION)) < 1.4)
		return qfalse;

	QGL_ARB( glGenPrograms );
	QGL_ARB( glBindProgram );
	QGL_ARB( glProgramString );
	QGL_ARB( glDeletePrograms );

	QGL_ARB( glDisableVertexAttribArray );
	QGL_ARB( glEnableVertexAttribArray );
	QGL_ARB( glVertexAttribPointer );

	QGL_ARB( glProgramEnvParameter4f );
	QGL_ARB( glProgramLocalParameter4f );

	return qtrue;
}


///////////////////////////////////////////////////////////////

/* GL2 functions

#define QGL_EXT(fn) q##fn = qwglGetProcAddress( #fn ); \
	if (!q##fn) Com_Error( ERR_FATAL, "QGL_EXT: "#fn" not found" );

PFNGLCREATESHADERPROC qglCreateShader;
PFNGLSHADERSOURCEPROC qglShaderSource;
PFNGLCOMPILESHADERPROC qglCompileShader;
PFNGLATTACHSHADERPROC qglAttachShader;
PFNGLDETACHSHADERPROC qglDetachShader;
PFNGLDELETESHADERPROC qglDeleteShader;

PFNGLCREATEPROGRAMPROC qglCreateProgram;
PFNGLLINKPROGRAMPROC qglLinkProgram;
PFNGLUSEPROGRAMPROC qglUseProgram;
PFNGLDELETEPROGRAMPROC qglDeleteProgram;

PFNGLBINDATTRIBLOCATIONPROC qglBindAttribLocation;
PFNGLDISABLEVERTEXATTRIBARRAYPROC qglDisableVertexAttribArray;
PFNGLENABLEVERTEXATTRIBARRAYPROC qglEnableVertexAttribArray;
PFNGLVERTEXATTRIBPOINTERPROC qglVertexAttribPointer;

PFNGLGETUNIFORMLOCATIONPROC qglGetUniformLocation;
PFNGLUNIFORM1IPROC qglUniform1i;
PFNGLUNIFORM1FPROC qglUniform1f;
PFNGLUNIFORM3FPROC qglUniform3f;
PFNGLUNIFORM4FPROC qglUniform4f;

PFNGLGETOBJECTPARAMETERIVARBPROC qglGetObjectParameteriv;
PFNGLGETINFOLOGARBPROC qglGetInfoLog;


qbool GLW_InitGL2()
{
	if (atof((const char*)qglGetString(GL_VERSION)) < 2.0)
		return qfalse;

	QGL_EXT( glCreateShader );
	QGL_EXT( glShaderSource );
	QGL_EXT( glCompileShader );
	QGL_EXT( glAttachShader );
	QGL_EXT( glDetachShader );
	QGL_EXT( glDeleteShader );

	QGL_EXT( glCreateProgram );
	QGL_EXT( glLinkProgram );
	QGL_EXT( glUseProgram );
	QGL_EXT( glDeleteProgram );

	QGL_EXT( glBindAttribLocation );
	QGL_EXT( glDisableVertexAttribArray );
	QGL_EXT( glEnableVertexAttribArray );
	QGL_EXT( glVertexAttribPointer );

	QGL_EXT( glGetUniformLocation );
	QGL_EXT( glUniform1i );
	QGL_EXT( glUniform1f );
	QGL_EXT( glUniform3f );
	QGL_EXT( glUniform4f );

	QGL_ARB( glGetObjectParameteriv );
	QGL_ARB( glGetInfoLog );

	return qtrue;
}

*/
