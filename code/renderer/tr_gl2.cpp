#include "tr_local.h"


struct GLSL_Program {
	GLuint p;  // linked program
	GLuint vs; // vertex shader
	GLuint fs; // fragment shader
};


static GLuint progCurrent;

static void GL_Program( const GLSL_Program& prog )
{
	assert( prog.p );

	if ( prog.p != progCurrent ) {
		qglUseProgram( prog.p );
		progCurrent = prog.p;
	}
}

void GL_Program()
{
	if ( progCurrent != 0 ) {
		qglUseProgram(0);
		progCurrent = 0;
	}
}


static GLSL_Program dynLightProg;

struct GLSL_DynLightProgramAttribs {
	// vertex shader:
	GLint osEyePos;		// 4f, object-space
	GLint osLightPos;	// 4f, object-space

	// pixel shader:
	GLint texture;			// 2D texture
	GLint lightColorRadius;	// 4f, w = 1 / (r^2)
};

static GLSL_DynLightProgramAttribs dynLightProgAttribs;


///////////////////////////////////////////////////////////////


static void GL2_DynLights_Setup()
{
	const shaderStage_t* pStage = tess.xstages[tess.shader->lightingStages[ST_DIFFUSE]];
	GL_SelectTexture( 0 );
	R_BindAnimatedImage( &pStage->bundle );
}


static void GL2_DynLights_Lighting()
{
	backEnd.pc[RB_LIT_VERTICES_LATECULLTEST] += tess.numVertexes;

	int i;
	byte clipBits[SHADER_MAX_VERTEXES];
	const dlight_t* dl = tess.light;

	for ( i = 0; i < tess.numVertexes; ++i ) {
		vec3_t dist;
		VectorSubtract( dl->transformed, tess.xyz[i], dist );

		if ( DotProduct( dist, tess.normal[i] ) <= 0.0 ) {
			clipBits[i] = (byte)-1;
			continue;
		}

		int clip = 0;
		if ( dist[0] > dl->radius ) {
			clip |= 1;
		} else if ( dist[0] < -dl->radius ) {
			clip |= 2;
		}
		if ( dist[1] > dl->radius ) {
			clip |= 4;
		} else if ( dist[1] < -dl->radius ) {
			clip |= 8;
		}
		if ( dist[2] > dl->radius ) {
			clip |= 16;
		} else if ( dist[2] < -dl->radius ) {
			clip |= 32;
		}

		clipBits[i] = clip;
	}

	// build a list of triangles that need light
	int numIndexes = 0;
	unsigned hitIndexes[SHADER_MAX_INDEXES];
	for ( i = 0; i < tess.numIndexes; i += 3 ) {
		int a = tess.indexes[i];
		int b = tess.indexes[i+1];
		int c = tess.indexes[i+2];
		if ( !(clipBits[a] & clipBits[b] & clipBits[c]) ) {
			hitIndexes[numIndexes] = a;
			hitIndexes[numIndexes+1] = b;
			hitIndexes[numIndexes+2] = c;
			numIndexes += 3;
		}
	}

	backEnd.pc[RB_LIT_INDICES_LATECULL_IN] += numIndexes;
	backEnd.pc[RB_LIT_INDICES_LATECULL_OUT] += tess.numIndexes - numIndexes;

	if ( !numIndexes )
		return;

	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );

	GL2_DynLights_Setup();

	qglDrawElements( GL_TRIANGLES, numIndexes, GL_INDEX_TYPE, hitIndexes );
}


///////////////////////////////////////////////////////////////


void GL2_DynLights_SetupLight()
{
	GL_Program( dynLightProg );

	const dlight_t* dl = tess.light;
	vec3_t lightColor;
	VectorCopy( dl->color, lightColor );

	qglUniform4f( dynLightProgAttribs.osLightPos, dl->transformed[0], dl->transformed[1], dl->transformed[2], 0.0f );
	qglUniform4f( dynLightProgAttribs.osEyePos, backEnd.orient.viewOrigin[0], backEnd.orient.viewOrigin[1], backEnd.orient.viewOrigin[2], 0.0f );
	qglUniform4f( dynLightProgAttribs.lightColorRadius, lightColor[0], lightColor[1], lightColor[2], 1.0f / Square(dl->radius) );
	qglUniform1i( dynLightProgAttribs.texture, 0 ); // we use texture unit 0
}


static void GL2_DynLights_LightingPass()
{
	if (tess.shader->lightingStages[ST_DIFFUSE] == -1)
		return;

	RB_DeformTessGeometry();

	GL_Cull( tess.shader->cullType );

	const shaderStage_t* pStage = tess.xstages[ tess.shader->lightingStages[ST_DIFFUSE] ];
	R_ComputeTexCoords( pStage, tess.svars );

	// since this is guaranteed to be a single pass, fill and lock all the arrays

	qglDisableClientState( GL_COLOR_ARRAY );

	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoords );

	qglEnableClientState( GL_NORMAL_ARRAY );
	qglNormalPointer( GL_FLOAT, 16, tess.normal );

	qglVertexPointer( 3, GL_FLOAT, 16, tess.xyz );
	qglLockArraysEXT( 0, tess.numVertexes );

	GL2_DynLights_Lighting();

	qglUnlockArraysEXT();

	qglDisableClientState( GL_NORMAL_ARRAY );
}


///////////////////////////////////////////////////////////////


// returns qtrue if needs to break early
static qbool GL2_DynLights_MultitextureStage( int stage )
{
	static stageVars_t svarsMT; // this is a huge struct

	const shaderStage_t* pPrevStage = tess.xstages[stage++];
	const shaderStage_t* pStage = tess.xstages[stage];
	const qbool lightmapOnly = r_lightmap->integer && (pStage->type == ST_LIGHTMAP || pPrevStage->type == ST_LIGHTMAP);

	if ( lightmapOnly ) {
		if ( pStage->type == ST_LIGHTMAP ) {
			R_BindAnimatedImage( &pStage->bundle );
			R_ComputeTexCoords( pStage, svarsMT );
			qglTexCoordPointer( 2, GL_FLOAT, 0, svarsMT.texcoords );
		}
		qglDrawElements( GL_TRIANGLES, tess.numIndexes, GL_INDEX_TYPE, tess.indexes );
		return qtrue;
	}

	if ( r_fullbright->integer ) {
		if ( pStage->type == ST_LIGHTMAP ) {
			Com_Memset( tess.svars.colors, tr.identityLightByte, tess.numVertexes * 4 );
			qglDrawElements( GL_TRIANGLES, tess.numIndexes, GL_INDEX_TYPE, tess.indexes );
			return qfalse;
		} else if ( pPrevStage->type == ST_LIGHTMAP ) {
			Com_Memset( tess.svars.colors, tr.identityLightByte, tess.numVertexes * 4 );
			R_BindAnimatedImage( &pStage->bundle );
			R_ComputeTexCoords( pStage, svarsMT );
			qglTexCoordPointer( 2, GL_FLOAT, 0, svarsMT.texcoords );
			qglDrawElements( GL_TRIANGLES, tess.numIndexes, GL_INDEX_TYPE, tess.indexes );
			return qfalse;
		}
	}

	GL_SelectTexture( 1 );
	qglEnable( GL_TEXTURE_2D );
	GL_TexEnv( pStage->mtEnv );
	R_BindAnimatedImage( &pStage->bundle );

	R_ComputeTexCoords( pStage, svarsMT );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglTexCoordPointer( 2, GL_FLOAT, 0, svarsMT.texcoords );

	qglDrawElements( GL_TRIANGLES, tess.numIndexes, GL_INDEX_TYPE, tess.indexes );

	qglDisable( GL_TEXTURE_2D );
	GL_SelectTexture( 0 );

	return qfalse;
}


void GL2_DynLights_StageIterator()
{
	if (tess.pass == shaderCommands_t::TP_LIGHT) {
		GL2_DynLights_LightingPass();
		return;
	}

	GL_Program();

	RB_DeformTessGeometry();

	GL_Cull( tess.shader->cullType );

	if ( tess.shader->polygonOffset )
		qglEnable( GL_POLYGON_OFFSET_FILL );

	// geometry is per-shader and can be compiled
	// color and tc are per-stage, and can't

	qglDisableClientState( GL_COLOR_ARRAY );
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );

	qglVertexPointer( 3, GL_FLOAT, 16, tess.xyz );	// padded for SIMD
	qglLockArraysEXT( 0, tess.numVertexes );

	qglEnableClientState( GL_COLOR_ARRAY );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.svars.colors );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoords );

	for ( int stage = 0; stage < tess.shader->numStages; ++stage ) {
		const shaderStage_t* pStage = tess.xstages[stage];
		R_ComputeColors( pStage, tess.svars );
		R_ComputeTexCoords( pStage, tess.svars );

		R_BindAnimatedImage( &pStage->bundle );
		GL_State( pStage->stateBits );

		// !!! FUCKING ati drivers incorrectly need this
		// they're locking+pulling the color array even tho it was explicitly NOT locked
		// so color changes are ignored unless we "update" the color pointer again
		qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.svars.colors );
		qglTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoords );

		if ( pStage->mtStages ) {
			// we can't really cope with massive collapses, so
			assert( pStage->mtStages == 1 );
			if ( GL2_DynLights_MultitextureStage( stage ) )
				break;
			stage += pStage->mtStages;
			continue;
		}

		qglDrawElements( GL_TRIANGLES, tess.numIndexes, GL_INDEX_TYPE, tess.indexes );
	}

	if ( tess.fogNum && tess.shader->fogPass )
		RB_FogPass();

	qglUnlockArraysEXT();

	if ( tess.shader->polygonOffset )
		qglDisable( GL_POLYGON_OFFSET_FILL );
}


///////////////////////////////////////////////////////////////


static qbool GL2_CreateShader( GLuint* shaderPtr, GLenum shaderType, const char* shaderSource )
{
	GLuint shader = qglCreateShader( shaderType );
	qglShaderSource( shader, 1, &shaderSource, NULL );
	qglCompileShader( shader );

	GLint result = GL_FALSE;
	qglGetShaderiv( shader, GL_COMPILE_STATUS, &result );
	if ( result == GL_TRUE ) {
		*shaderPtr = shader;
		return qtrue;
	}

	GLint logLength = 0;
	qglGetShaderiv( shader, GL_INFO_LOG_LENGTH, &logLength );

	static char log[4096]; // I've seen logs over 3 KB in size.
	qglGetShaderInfoLog( shader, sizeof(log), NULL, log );
	Com_Printf( "ERROR: %s shader: %s", shaderType == GL_VERTEX_SHADER ? "vertex" : "fragment", log );

	return qfalse;
}


static qbool GL2_CreateProgram( GLSL_Program& prog, const char* vs, const char* fs )
{
	if ( !GL2_CreateShader( &prog.vs, GL_VERTEX_SHADER, vs ) )
		return qfalse;

	if ( !GL2_CreateShader( &prog.fs, GL_FRAGMENT_SHADER, fs ) )
		return qfalse;

	prog.p = qglCreateProgram();
	qglAttachShader( prog.p, prog.vs );
	qglAttachShader( prog.p, prog.fs );
	qglLinkProgram( prog.p );

	return qtrue;
}


// We don't use "gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;"
// because most everything is rendered using the fixed function pipeline and
// ftransform makes sure we get matching results (and thus avoid Z-fighting).

static const char* dynLightVS =
"uniform vec4 osLightPos;\n"
"uniform vec4 osEyePos;\n"
"varying vec4 L;\n"		// object-space light vector
"varying vec4 V;\n"		// object-space view vector
"varying vec3 nN;\n"	// normalized object-space normal vector
"\n"
"void main()\n"
"{\n"
"	gl_Position = ftransform();\n"
"	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
"	L = osLightPos - gl_Vertex;\n"
"	V = osEyePos - gl_Vertex;\n"
"	nN = gl_Normal;\n"
"}\n"
"";

static const char* dynLightFS =
"uniform sampler2D texture;\n"
"uniform vec4 lightColorRadius;"	// w = 1 / (r^2)
"varying vec4 L;\n"		// object-space light vector
"varying vec4 V;\n"		// object-space view vector
"varying vec3 nN;\n"	// normalized object-space normal vector
"\n"
"void main()\n"
"{\n"
"	vec4 base = texture2D(texture, gl_TexCoord[0].xy);\n"
"	vec3 nL = normalize(L.xyz);\n"	// normalized light vector
"	vec3 nV = normalize(V.xyz);\n"	// normalized view vector
	// light intensity
"	float intensFactor = dot(L.xyz, L.xyz) * lightColorRadius.w;"
"	vec3 intens = lightColorRadius.rgb * (1.0 - intensFactor);\n"
	// specular reflection term (N.H)
"	float specFactor = clamp(dot(nN, normalize(nL + nV)), 0.0, 1.0);\n"
"	float spec = pow(specFactor, 16.0) * 0.25;\n"
	// Lambertian diffuse reflection term (N.L)
"	float diffuse = clamp(dot(nN, nL), 0.0, 1.0);\n"
"	gl_FragColor = (base * vec4(diffuse) + vec4(spec)) * vec4(intens, 1.0);\n"
"}\n"
"";


struct FrameBuffer {
	GLuint fbo;
	GLuint color;			// texture if MS, buffer if SS
	GLuint depthStencil;	// texture if MS, buffer if SS
	qbool multiSampled;
	qbool hasDepthStencil;
};

static FrameBuffer frameBufferMain;
static FrameBuffer frameBuffersPostProcess[2];
static unsigned int frameBufferReadIndex = 0; // read this for the latest color/depth data
static qbool frameBufferMultiSampling = qfalse;


#define CASE( x )		case x: return #x
#define GL( call )		call; GL2_CheckError( #call, __FUNCTION__, __FILE__, __LINE__ )


static const char* GL2_GetErrorString( GLenum ec )
{
	switch ( ec )
	{
		CASE( GL_NO_ERROR );
		CASE( GL_INVALID_ENUM );
		CASE( GL_INVALID_VALUE );
		CASE( GL_INVALID_OPERATION );
		CASE( GL_INVALID_FRAMEBUFFER_OPERATION );
		CASE( GL_OUT_OF_MEMORY );
		CASE( GL_STACK_UNDERFLOW );
		CASE( GL_STACK_OVERFLOW );
		default: return "?";
	}
}


static void GL2_CheckError( const char* call, const char* function, const char* file, int line )
{
	const GLenum ec = qglGetError();
	if ( ec == GL_NO_ERROR )
		return;

	const char* fileName = file;
	while ( *file )
	{
		if ( *file == '/' || *file == '\\' )
			fileName = file + 1;

		++file;
	}

	Com_Printf( "%s failed\n", call );
	Com_Printf( "%s:%d in %s\n", fileName, line, function );
	Com_Printf( "Error code: 0x%X (%d)\n", (unsigned int)ec, (int)ec );
	Com_Printf( "Error message: %s\n", GL2_GetErrorString(ec) );
}


static const char* GL2_GetFBOStatusString( GLenum status )
{
	switch ( status )
	{
		CASE( GL_FRAMEBUFFER_COMPLETE );
		CASE( GL_FRAMEBUFFER_UNDEFINED );
		CASE( GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT );
		CASE( GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT );
		CASE( GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER );
		CASE( GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER );
		CASE( GL_FRAMEBUFFER_UNSUPPORTED );
		CASE( GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE );
		CASE( GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS );
		default: return "?";
	}
}


#undef CASE


static qbool GL2_FBO_CreateSS( FrameBuffer& fb, qbool depthStencil )
{
	while ( qglGetError() != GL_NO_ERROR ) {} // clear the error queue

	if ( depthStencil )
	{
		GL(qglGenTextures( 1, &fb.depthStencil ));
		GL(qglBindTexture( GL_TEXTURE_2D, fb.depthStencil ));
		GL(qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST ));
		GL(qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST ));
		GL(qglTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, glConfig.vidWidth, glConfig.vidHeight, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL ));
	}

	GL(qglGenTextures( 1, &fb.color ));
	GL(qglBindTexture( GL_TEXTURE_2D, fb.color ));
	GL(qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR ));
	GL(qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR ));
	GL(qglTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, glConfig.vidWidth, glConfig.vidHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL ));

	GL(qglGenFramebuffers( 1, &fb.fbo ));
	GL(qglBindFramebuffer( GL_FRAMEBUFFER, fb.fbo ));
	GL(qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fb.color, 0 ));
	if ( depthStencil )
		GL(qglFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, fb.depthStencil, 0 ));

	const GLenum fboStatus = qglCheckFramebufferStatus( GL_FRAMEBUFFER );
	if ( fboStatus != GL_FRAMEBUFFER_COMPLETE )
	{
		Com_Printf( "Failed to create FBO (status 0x%X, error 0x%X)\n", (unsigned int)fboStatus, (unsigned int)qglGetError() );
		Com_Printf( "FBO status string: %s\n", GL2_GetFBOStatusString(fboStatus) );
		return qfalse;
	}

	qglBindFramebuffer( GL_FRAMEBUFFER, 0 );
	fb.multiSampled = qfalse;
	fb.hasDepthStencil = depthStencil;

	return qtrue;
}


static qbool GL2_FBO_CreateMS( FrameBuffer& fb )
{
	while ( qglGetError() != GL_NO_ERROR ) {} // clear the error queue

	GL(qglGenFramebuffers( 1, &fb.fbo ));
	GL(qglBindFramebuffer( GL_FRAMEBUFFER, fb.fbo ));

	GL(qglGenRenderbuffers( 1, &fb.color ));
	GL(qglBindRenderbuffer( GL_RENDERBUFFER, fb.color ));
	GL(qglRenderbufferStorageMultisample( GL_RENDERBUFFER, 4, GL_RGBA8, glConfig.vidWidth, glConfig.vidHeight ));
	GL(qglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, fb.color ));

	GL(qglGenRenderbuffers( 1, &fb.depthStencil ));
	GL(qglBindRenderbuffer( GL_RENDERBUFFER, fb.depthStencil ));
	GL(qglRenderbufferStorageMultisample( GL_RENDERBUFFER, 4, GL_DEPTH24_STENCIL8, glConfig.vidWidth, glConfig.vidHeight ));

	GL(qglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, fb.color ));
	GL(qglFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fb.depthStencil ));

	const GLenum fboStatus = qglCheckFramebufferStatus( GL_FRAMEBUFFER );
	if ( fboStatus != GL_FRAMEBUFFER_COMPLETE )
	{
		Com_Printf( "Failed to create FBO (status 0x%X, error 0x%X)\n", (unsigned int)fboStatus, (unsigned int)qglGetError() );
		Com_Printf( "FBO status string: %s\n", GL2_GetFBOStatusString(fboStatus) );
		return qfalse;
	}

	qglBindFramebuffer( GL_FRAMEBUFFER, 0 );
	fb.multiSampled = qtrue;
	fb.hasDepthStencil = qtrue;

	return qtrue;
}


static qbool GL2_FBO_Init()
{
	const int msaa = r_msaa->integer;
	const qbool validOption = msaa >= 2 && msaa <= 16;
	const qbool enable = validOption && qglRenderbufferStorageMultisample != NULL;
	frameBufferMultiSampling = enable;
	if ( validOption && !enable )
		Com_Printf( "Warning: MSAA requested but disabled because glRenderbufferStorageMultisample wasn't found\n" );

	if ( !enable )
		return	GL2_FBO_CreateSS( frameBuffersPostProcess[0], qtrue ) &&
				GL2_FBO_CreateSS( frameBuffersPostProcess[1], qtrue );

	return	GL2_FBO_CreateMS( frameBufferMain ) &&
			GL2_FBO_CreateSS( frameBuffersPostProcess[0], qfalse ) &&
			GL2_FBO_CreateSS( frameBuffersPostProcess[1], qfalse );
}


static void GL2_FBO_Bind( const FrameBuffer& fb )
{
	qglBindFramebuffer( GL_FRAMEBUFFER, fb.fbo );
	qglReadBuffer( GL_COLOR_ATTACHMENT0 );
	qglDrawBuffer( GL_COLOR_ATTACHMENT0 );
}


static void GL2_FBO_Bind()
{
	GL2_FBO_Bind( frameBuffersPostProcess[frameBufferReadIndex] );
}


static void GL2_FBO_Swap()
{
	frameBufferReadIndex ^= 1;
}


static void GL2_FBO_BlitSSToBackBuffer()
{
	const FrameBuffer& fbo = frameBuffersPostProcess[frameBufferReadIndex];
	qglBindFramebuffer( GL_READ_FRAMEBUFFER, fbo.fbo );
	qglBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );
	qglReadBuffer( GL_COLOR_ATTACHMENT0 );
	qglDrawBuffer( GL_BACK );

	const int w = glConfig.vidWidth;
	const int h = glConfig.vidHeight;
	qglBlitFramebuffer( 0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR );
}


static void GL2_FBO_BlitMSToSS()
{
	const FrameBuffer& r = frameBufferMain;
	const FrameBuffer& d = frameBuffersPostProcess[frameBufferReadIndex];
	qglBindFramebuffer( GL_READ_FRAMEBUFFER, r.fbo );
	qglBindFramebuffer( GL_DRAW_FRAMEBUFFER, d.fbo );
	qglReadBuffer( GL_COLOR_ATTACHMENT0 );
	qglDrawBuffer( GL_COLOR_ATTACHMENT0 );

	const int w = glConfig.vidWidth;
	const int h = glConfig.vidHeight;
	qglBlitFramebuffer( 0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR );
}


static void GL2_FullScreenQuad()
{
	const float w = glConfig.vidWidth;
	const float h = glConfig.vidHeight;
	qglBegin( GL_QUADS );
		qglTexCoord2f( 0.0f, 0.0f );
		qglVertex2f( 0.0f, h );
		qglTexCoord2f( 0.0f, 1.0f );
		qglVertex2f( 0.0f, 0.0f );
		qglTexCoord2f( 1.0f, 1.0f );
		qglVertex2f( w, 0.0f );
		qglTexCoord2f( 1.0f, 0.0f );
		qglVertex2f( w, h );
	qglEnd();
}


static GLSL_Program gammaProg;

struct GLSL_GammaProgramAttribs
{
	int texture;
	int gammaOverbright;
};

static GLSL_GammaProgramAttribs gammaProgAttribs;


static void GL2_PostProcessGamma()
{
	const int obBits = r_overBrightBits->integer;
	const float obScale = (float)( 1 << obBits );
	const float gamma = 1.0f / r_gamma->value;

	if ( gamma == 1.0f && obBits == 0 )
		return;

	GL2_FBO_Swap();
	GL2_FBO_Bind();

	GL_Program( gammaProg );
	qglUniform1i( gammaProgAttribs.texture, 0 ); // we use texture unit 0
	qglUniform4f( gammaProgAttribs.gammaOverbright, gamma, gamma, gamma, obScale );
	GL_SelectTexture( 0 );
	qglBindTexture( GL_TEXTURE_2D, frameBuffersPostProcess[frameBufferReadIndex ^ 1].color );
	GL2_FullScreenQuad();
}


static const char* gammaVS =
"void main()\n"
"{\n"
"	gl_Position = ftransform();\n"
"	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
"}\n"
"";

static const char* gammaFS =
"uniform sampler2D texture;\n"
"uniform vec4 gammaOverbright;\n"
"\n"
"void main()\n"
"{\n"
"	vec3 base = texture2D(texture, gl_TexCoord[0].xy).rgb;\n"
"	gl_FragColor = vec4(pow(base, gammaOverbright.xyz) * gammaOverbright.w, 1.0);\n"
"}\n"
"";


static GLSL_Program greyscaleProg;

struct GLSL_GreyscaleProgramAttribs
{
	int texture;
	int greyscale;
};

static GLSL_GreyscaleProgramAttribs greyscaleProgAttribs;
static qbool greyscaleProgramValid = qfalse;


static void GL2_PostProcessGreyscale()
{
	if ( !greyscaleProgramValid )
		return;

	const float greyscale = Com_Clamp( 0.0f, 1.0f, r_greyscale->value );
	if ( greyscale == 0.0f )
		return;

	GL2_FBO_Swap();
	GL2_FBO_Bind();

	GL_Program( greyscaleProg );
	qglUniform1i( greyscaleProgAttribs.texture, 0 ); // we use texture unit 0
	qglUniform1f( greyscaleProgAttribs.greyscale, greyscale );
	GL_SelectTexture( 0 );
	qglBindTexture( GL_TEXTURE_2D, frameBuffersPostProcess[frameBufferReadIndex ^ 1].color );
	GL2_FullScreenQuad();
}


static const char* greyscaleVS =
"void main()\n"
"{\n"
"	gl_Position = ftransform();\n"
"	gl_TexCoord[0] = gl_MultiTexCoord0;\n"
"}\n"
"";

static const char* greyscaleFS =
"uniform sampler2D texture;\n"
"uniform float greyscale;\n"
"\n"
"void main()\n"
"{\n"
"	vec3 base = texture2D(texture, gl_TexCoord[0].xy).rgb;\n"
"	vec3 grey = vec3(0.299 * base.r + 0.587 * base.g + 0.114 * base.b);\n"
"	gl_FragColor = vec4(mix(base, grey, greyscale), 1.0);\n"
"}\n"
"";


qbool QGL_InitGL2()
{
	if ( !GL2_FBO_Init() ) {
		Com_Printf( "ERROR: failed to create framebuffer objects\n" );
		return qfalse;
	}

	if ( !GL2_CreateProgram( dynLightProg, dynLightVS, dynLightFS ) ) {
		Com_Printf( "ERROR: failed to compile dynamic light shaders\n" );
		return qfalse;
	}
	dynLightProgAttribs.osEyePos = qglGetUniformLocation( dynLightProg.p, "osEyePos" );
	dynLightProgAttribs.osLightPos = qglGetUniformLocation( dynLightProg.p, "osLightPos" );
	dynLightProgAttribs.texture = qglGetUniformLocation( dynLightProg.p, "texture" );
	dynLightProgAttribs.lightColorRadius = qglGetUniformLocation( dynLightProg.p, "lightColorRadius" );

	if ( !GL2_CreateProgram( gammaProg, gammaVS, gammaFS ) ) {
		Com_Printf( "ERROR: failed to compile gamma correction shaders\n" );
		return qfalse;
	}
	gammaProgAttribs.texture = qglGetUniformLocation( gammaProg.p, "texture" );
	gammaProgAttribs.gammaOverbright = qglGetUniformLocation( gammaProg.p, "gammaOverbright" );

	greyscaleProgramValid = GL2_CreateProgram( greyscaleProg, greyscaleVS, greyscaleFS );
	if ( greyscaleProgramValid ) {
		greyscaleProgAttribs.texture = qglGetUniformLocation( greyscaleProg.p, "texture" );
		greyscaleProgAttribs.greyscale = qglGetUniformLocation( greyscaleProg.p, "greyscale" );
	} else {
		Com_Printf( "ERROR: failed to compile greyscale shaders\n" );
	}

	return qtrue;
}


void GL2_BeginFrame()
{
	if ( frameBufferMultiSampling )
		GL2_FBO_Bind( frameBufferMain );
	else
		GL2_FBO_Bind();

	GL_Program();
}


void GL2_EndFrame()
{
	if ( frameBufferMultiSampling )
		GL2_FBO_BlitMSToSS();

	GL2_PostProcessGamma();
	GL2_PostProcessGreyscale();

	// needed for later calls to GL_Bind because
	// the above functions use qglBindTexture directly
	glState.texID[glState.currenttmu] = 0;

	GL2_FBO_BlitSSToBackBuffer();
}

