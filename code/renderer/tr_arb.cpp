
#include "tr_local.h"


#if (VP_GLOBAL_MAX > 96)
#error VP_GLOBAL_MAX > MAX_PROGRAM_ENV_PARAMETERS_ARB
#endif


///////////////////////////////////////////////////////////////


struct GLSL_Program {
	GLuint vs;
	GLuint fs;
};


static GLSL_Program progCurrent;

static void GL_Program( const GLSL_Program& prog )
{
	assert( prog.vs && prog.fs );

	if (!progCurrent.vs)
		qglEnable( GL_VERTEX_PROGRAM_ARB );

	if (progCurrent.vs != prog.vs) {
		progCurrent.vs = prog.vs;
		qglBindProgram( GL_VERTEX_PROGRAM_ARB, prog.vs );
	}

	if (!progCurrent.fs)
		qglEnable( GL_FRAGMENT_PROGRAM_ARB );

	if (progCurrent.fs != prog.fs) {
		progCurrent.fs = prog.fs;
		qglBindProgram( GL_FRAGMENT_PROGRAM_ARB, prog.fs );
	}
}

void GL_Program()
{
	if (progCurrent.vs) {
		qglDisable( GL_VERTEX_PROGRAM_ARB );
		progCurrent.vs = 0;
	}

	if (progCurrent.fs) {
		qglDisable( GL_FRAGMENT_PROGRAM_ARB );
		progCurrent.fs = 0;
	}
}


static GLSL_Program progDynamic;


///////////////////////////////////////////////////////////////


static void ARB_Lighting_Setup()
{
	const shaderStage_t* pStage = tess.xstages[tess.shader->lightingStages[ST_DIFFUSE]];
	GL_SelectTexture(0);
	R_BindAnimatedImage( &pStage->bundle );
}


static void ARB_Lighting()
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

	ARB_Lighting_Setup();

	qglDrawElements( GL_TRIANGLES, numIndexes, GL_INDEX_TYPE, hitIndexes );
}


///////////////////////////////////////////////////////////////


void ARB_SetupLight()
{
	GL_Program( progDynamic );

	const dlight_t* dl = tess.light;
	vec3_t lightRGB;
	if (!glConfig.deviceSupportsGamma)
		VectorScale( dl->color, 2 * pow( r_intensity->value, r_gamma->value ), lightRGB );
	else
		VectorCopy( dl->color, lightRGB );

	qglProgramLocalParameter4f( GL_VERTEX_PROGRAM_ARB, 0, dl->transformed[0], dl->transformed[1], dl->transformed[2], 0 );
	qglProgramLocalParameter4f( GL_FRAGMENT_PROGRAM_ARB, 0, lightRGB[0], lightRGB[1], lightRGB[2], 1.0 );
	qglProgramLocalParameter4f( GL_FRAGMENT_PROGRAM_ARB, 1, 1.0 / Square(dl->radius), 0, 0, 0 );

	qglProgramEnvParameter4f( GL_VERTEX_PROGRAM_ARB, VP_GLOBAL_EYEPOS,
		backEnd.or.viewOrigin[0], backEnd.or.viewOrigin[1], backEnd.or.viewOrigin[2], 0 );
}


static void ARB_LightingPass()
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

	ARB_Lighting();

	qglUnlockArraysEXT();

	qglDisableClientState( GL_NORMAL_ARRAY );
}


///////////////////////////////////////////////////////////////


static void ARB_MultitextureStage( int stage )
{
	// everything we care about from the first stage is already done
	const shaderStage_t* pStage = tess.xstages[++stage];

	GL_SelectTexture( 1 );
	qglEnable( GL_TEXTURE_2D );
	GL_TexEnv( pStage->mtEnv );
	R_BindAnimatedImage( &pStage->bundle );

	stageVars_t svarsMT;
	R_ComputeTexCoords( pStage, svarsMT );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglTexCoordPointer( 2, GL_FLOAT, 0, svarsMT.texcoords );

	qglDrawElements( GL_TRIANGLES, tess.numIndexes, GL_INDEX_TYPE, tess.indexes );

	qglDisable( GL_TEXTURE_2D );
	GL_SelectTexture( 0 );
}


void ARB_StageIterator()
{
	if (tess.pass == shaderCommands_t::TP_LIGHT) {
		ARB_LightingPass();
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

		if (pStage->mtStages) {
			// we can't really cope with massive collapses, so
			assert( pStage->mtStages == 1 );
			ARB_MultitextureStage( stage );
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


static qbool ARB_LoadProgram(GLSL_Program& prog, const char* vp, const char* fp)
{
	qglGenPrograms(1, &prog.vs);
	qglBindProgram(GL_VERTEX_PROGRAM_ARB, prog.vs);
	qglProgramString(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, strlen(vp), vp);

	if (qglGetError() != GL_NO_ERROR)
		ri.Error(ERR_FATAL, "VP Compile Error: %s", qglGetString(GL_PROGRAM_ERROR_STRING_ARB));

	qglGenPrograms(1, &prog.fs);
	qglBindProgram(GL_FRAGMENT_PROGRAM_ARB, prog.fs);
	qglProgramString(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, strlen(fp), fp);

	if (qglGetError() != GL_NO_ERROR)
		ri.Error(ERR_FATAL, "FP Compile Error: %s", qglGetString(GL_PROGRAM_ERROR_STRING_ARB));

	return qtrue;
}


// welding these into the code to avoid having a pk3 dependency in the engine

static const char* legacyVP =
	"!!ARBvp1.0"
	"OPTION ARB_position_invariant;"
	"PARAM posEye = program.env[0];"
	"PARAM posLight = program.local[0];"
	"OUTPUT lv = result.texcoord[4];"
	"OUTPUT ev = result.texcoord[5];"
	"OUTPUT n = result.texcoord[6];"
	"MOV result.texcoord[0], vertex.texcoord;"
	"MOV n, vertex.normal;"
	"SUB ev, posEye, vertex.position;"
	"SUB lv, posLight, vertex.position;"
	"END";

static const char* legacyFP =
	"!!ARBfp1.0"
	"OPTION ARB_precision_hint_fastest;"
	"PARAM lightRGB = program.local[0];"
	"PARAM lightRange2recip = program.local[1];"
	"TEMP base; TEX base, fragment.texcoord[0], texture[0], 2D;"
	"ATTRIB dnLV = fragment.texcoord[4];"
	"ATTRIB dnEV = fragment.texcoord[5];"
	"ATTRIB n = fragment.texcoord[6];"
	"TEMP tmp, lv;"
	"DP3 tmp, dnLV, dnLV;"
	"RSQ lv.w, tmp.w;"
	"MUL lv.xyz, dnLV, lv.w;"
	"TEMP light;"
	"MUL tmp.x, tmp.w, lightRange2recip;"
	"SUB tmp.x, 1.0, tmp.x;"
	"MUL light.rgb, lightRGB, tmp.x;"
	"PARAM specRGB = 0.25;"
	"PARAM specEXP = 16.0;"
	"TEMP ev;"
	"DP3 ev, dnEV, dnEV;"
	"RSQ ev.w, ev.w;"
	"MUL ev.xyz, dnEV, ev.w;"
	"ADD tmp, lv, ev;"
	"DP3 tmp.w, tmp, tmp;"
	"RSQ tmp.w, tmp.w;"
	"MUL tmp.xyz, tmp, tmp.w;"
	"DP3_SAT tmp.w, n, tmp;"
	"POW tmp.w, tmp.w, specEXP.w;"
	"TEMP spec;"
	"MUL spec.rgb, specRGB, tmp.w;"
	"TEMP bump;"
	"DP3_SAT bump.w, n, lv;"
	"MAD base, base, bump.w, spec;"
	"MUL result.color.rgb, base, light;"
	"END";


qbool QGL_InitARB()
{
	ARB_LoadProgram( progDynamic, legacyVP, legacyFP );

	GL_Program();

	return qtrue;
}

