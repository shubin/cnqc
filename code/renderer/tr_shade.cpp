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
// tr_shade.c

#include "tr_local.h"

/*
  THIS ENTIRE FILE IS BACK END

  This file deals with applying shaders to surface data in the tess struct.
*/


static void ID_INLINE R_DrawElements( int numIndexes, const glIndex_t* indexes )
{
	qglDrawElements( GL_TRIANGLES, numIndexes, GL_INDEX_TYPE, indexes );
}


// draw triangle outlines for debugging

static void DrawTris( const shaderCommands_t* input )
{
	GL_Bind( tr.whiteImage );
	qglColor3f( 1, 1, 1 );

	GL_State( GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE );
	qglDepthRange( 0, 0 );

	qglDisableClientState( GL_COLOR_ARRAY );
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );

	qglVertexPointer( 3, GL_FLOAT, 16, input->xyz );	// padded for SIMD

	qglLockArraysEXT( 0, input->numVertexes );

	R_DrawElements( input->numIndexes, input->indexes );

	qglUnlockArraysEXT();

	qglDepthRange( 0, 1 );
}


// draw vertex normals for debugging

static void DrawNormals( const shaderCommands_t* input )
{
	GL_Bind( tr.whiteImage );
	GL_State( GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE );
	qglDepthRange( 0, 0 );

	qglColor3f( 0, 0, 1 );
	qglBegin( GL_LINES );
	for (int i = 0; i < input->numVertexes; ++i) {
		vec3_t temp;
		qglVertex3fv( input->xyz[i] );
		VectorMA( input->xyz[i], 2, input->normal[i], temp );
		qglVertex3fv( temp );
	}
	qglEnd();

	qglColor3f( 1, 0, 0 );
	qglBegin( GL_LINES );
	for (int i = 0; i < input->numVertexes; ++i) {
		vec3_t temp;
		qglVertex3fv( input->xyz[i] );
		VectorMA( input->xyz[i], 2, input->tangent[i], temp );
		qglVertex3fv( temp );
	}
	qglEnd();

	qglColor3f( 0, 1, 0 );
	qglBegin( GL_LINES );
	for (int i = 0; i < input->numVertexes; ++i) {
		vec3_t temp, bitan;
		qglVertex3fv( input->xyz[i] );
		CrossProduct( input->normal[i], input->tangent[i], bitan );
		VectorScale( bitan, input->tangent[i][3], bitan );
		VectorMA( input->xyz[i], 2, bitan, temp );
		qglVertex3fv( temp );
	}
	qglEnd();

	qglDepthRange( 0, 1 );
}


///////////////////////////////////////////////////////////////


void R_BindAnimatedImage( const textureBundle_t* bundle )
{
	if ( bundle->isVideoMap ) {
		ri.CIN_RunCinematic(bundle->videoMapHandle);
		ri.CIN_UploadCinematic(bundle->videoMapHandle);
		return;
	}

	if ( bundle->numImageAnimations <= 1 ) {
		GL_Bind( bundle->image[0] );
		return;
	}

	// it is necessary to do this messy calc to make sure animations line up
	// exactly with waveforms of the same frequency
	double v = tess.shaderTime * bundle->imageAnimationSpeed * FUNCTABLE_SIZE;
	__int64 index = v; //myftol( tess.shaderTime * bundle->imageAnimationSpeed * FUNCTABLE_SIZE );
	index >>= FUNCTABLE_SHIFT;

	if ( index < 0 ) // may happen with shader time offsets
		index = 0;

	index %= bundle->numImageAnimations;

	GL_Bind( bundle->image[ index ] );
}


/*
=============================================================

SURFACE SHADERS

=============================================================
*/

shaderCommands_t tess;


// we must set some things up before beginning any tesselation
// because a surface may be forced to perform a RB_End due to overflow

void RB_BeginSurface( const shader_t* shader, int fogNum )
{
	tess.numIndexes = 0;
	tess.numVertexes = 0;
	tess.shader = shader;
	tess.fogNum = fogNum;
	tess.xstages = (const shaderStage_t**)shader->stages;
	tess.siFunc = shader->siFunc;

	tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
	if (tess.shader->clampTime && tess.shaderTime >= tess.shader->clampTime) {
		tess.shaderTime = tess.shader->clampTime;
	}
}


void RB_EndSurface()
{
	shaderCommands_t* input = &tess;

	if (!input->numIndexes)
		return;

	if (input->indexes[SHADER_MAX_INDEXES-1] != 0) {
		ri.Error( ERR_DROP, "RB_EndSurface() - SHADER_MAX_INDEXES hit" );
	}
	if (input->xyz[SHADER_MAX_VERTEXES-1][0] != 0) {
		ri.Error( ERR_DROP, "RB_EndSurface() - SHADER_MAX_VERTEXES hit" );
	}

	// for debugging of sort order issues, stop rendering after a given sort value
	if ( r_debugSort->integer && r_debugSort->integer < tess.shader->sort ) {
		return;
	}

	// update performance counters
	if (tess.pass == shaderCommands_t::TP_LIGHT) {
		backEnd.pc[RB_LIT_BATCHES]++;
		backEnd.pc[RB_LIT_VERTICES] += tess.numVertexes;
		backEnd.pc[RB_LIT_INDICES] += tess.numIndexes;
	} else {
		backEnd.pc[RB_BATCHES]++;
		backEnd.pc[RB_VERTICES] += tess.numVertexes;
		backEnd.pc[RB_INDICES] += tess.numIndexes;
	}

	// call off to shader specific tess end function
	tess.siFunc();

	// draw debugging stuff
	if (!backEnd.projection2D && (tess.pass == shaderCommands_t::TP_BASE)) {
		if (r_showtris->integer) DrawTris( input );
		if (r_shownormals->integer) DrawNormals( input );
	}

	// clear shader so we can tell we don't have any unclosed surfaces
	tess.numIndexes = 0;
}


///////////////////////////////////////////////////////////////


// blend a fog texture on top of everything else

void RB_FogPass()
{
	qglEnableClientState( GL_COLOR_ARRAY );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, tess.svars.colors );

	qglEnableClientState( GL_TEXTURE_COORD_ARRAY);
	qglTexCoordPointer( 2, GL_FLOAT, 0, tess.svars.texcoords[0] );

	const fog_t* fog = tr.world->fogs + tess.fogNum;

	for (int i = 0; i < tess.numVertexes; ++i ) {
		*(int*)&tess.svars.colors[i] = fog->colorInt;
	}

	RB_CalcFogTexCoords( ( float * ) tess.svars.texcoords[0] );

	GL_Bind( tr.fogImage );

	if ( tess.shader->fogPass == FP_EQUAL ) {
		GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL );
	} else {
		GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	}

	R_DrawElements( tess.numIndexes, tess.indexes );
}


void R_ComputeColors( const shaderStage_t* pStage, stageVars_t& svars )
{
	int i;

	//
	// rgbGen
	//
	switch ( pStage->rgbGen )
	{
	case CGEN_IDENTITY:
		Com_Memset( svars.colors, 0xff, tess.numVertexes * 4 );
		break;
	default:
	case CGEN_IDENTITY_LIGHTING:
		Com_Memset( svars.colors, tr.identityLightByte, tess.numVertexes * 4 );
		break;
	case CGEN_LIGHTING_DIFFUSE:
		RB_CalcDiffuseColor( ( unsigned char * ) svars.colors );
		break;
	case CGEN_VERTEX:
		if ( tr.identityLight == 1 )
		{
			Com_Memcpy( svars.colors, tess.vertexColors, tess.numVertexes * sizeof( tess.vertexColors[0] ) );
		}
		else
		{
			int k;
			for ( i = 0; i < tess.numVertexes; i++ )
			{
				for(k = 0; k < 3; k++)
					svars.colors[i][k] = tess.vertexColors[i][k] * tr.identityLight;

				svars.colors[i][3] = tess.vertexColors[i][3];
			}
		}
		break;
	case CGEN_CONST:
		for ( i = 0; i < tess.numVertexes; i++ ) {
			*(int *)svars.colors[i] = *(int *)pStage->constantColor;
		}
		break;
	case CGEN_EXACT_VERTEX:
		if(r_fullbright->integer < 0) {
			for (i = 0; i < tess.numVertexes; i++) {
				int k;
				
				vec3_t vColor = {(float)tess.vertexColors[i][0] * tr.identityLight / 255.0f, (float)tess.vertexColors[i][1] * tr.identityLight / 255.0f, (float)tess.vertexColors[i][2] * tr.identityLight / 255.0f};
				float greyColor = (vColor[0] + vColor[1] + vColor[2]) / 3.0f;

				for(k = 0; k < 3; k++) {
					if(r_maplightColorMode->integer > 0) {
						vColor[k] = vColor[k] * r_maplightSaturation->value + greyColor * (1.0f - r_maplightSaturation->value);
					}
					else {
						vColor[k] = greyColor;
					}
					vColor[k] *= r_maplightBrightness->value;
					vColor[k] *= vMaplightColorFilter[k];
					vColor[k] = vColor[k] < 0.0f ? 0.0f : vColor[k];
					vColor[k] = vColor[k] > 1.0f ? 1.0f : vColor[k];
					svars.colors[i][k] = (byte)(vColor[k] * 255.0f);
				}

				svars.colors[i][3] = tess.vertexColors[i][3];
			}
		}
		else {
			Com_Memcpy( svars.colors, tess.vertexColors, tess.numVertexes * sizeof( tess.vertexColors[0] ) );
		}
		break;
	case CGEN_ONE_MINUS_VERTEX:
		if ( tr.identityLight == 1 )
		{
			for ( i = 0; i < tess.numVertexes; i++ )
			{
				svars.colors[i][0] = 255 - tess.vertexColors[i][0];
				svars.colors[i][1] = 255 - tess.vertexColors[i][1];
				svars.colors[i][2] = 255 - tess.vertexColors[i][2];
			}
		}
		else
		{
			for ( i = 0; i < tess.numVertexes; i++ )
			{
				svars.colors[i][0] = ( 255 - tess.vertexColors[i][0] ) * tr.identityLight;
				svars.colors[i][1] = ( 255 - tess.vertexColors[i][1] ) * tr.identityLight;
				svars.colors[i][2] = ( 255 - tess.vertexColors[i][2] ) * tr.identityLight;
			}
		}
		break;
	case CGEN_FOG:
		{
			const fog_t* fog = tr.world->fogs + tess.fogNum;
			for ( i = 0; i < tess.numVertexes; i++ ) {
				*(int*)&svars.colors[i] = fog->colorInt;
			}
		}
		break;
	case CGEN_WAVEFORM:
		RB_CalcWaveColor( &pStage->rgbWave, ( unsigned char * ) svars.colors );
		break;
	case CGEN_ENTITY:
		RB_CalcColorFromEntity( ( unsigned char * ) svars.colors );
		break;
	case CGEN_ONE_MINUS_ENTITY:
		RB_CalcColorFromOneMinusEntity( ( unsigned char * ) svars.colors );
		break;
	}

	//
	// alphaGen
	//
	switch ( pStage->alphaGen )
	{
	case AGEN_SKIP:
		break;
	case AGEN_IDENTITY:
		if ( pStage->rgbGen != CGEN_IDENTITY ) {
			if ( ( pStage->rgbGen == CGEN_VERTEX && tr.identityLight != 1 ) ||
				 pStage->rgbGen != CGEN_VERTEX ) {
				for ( i = 0; i < tess.numVertexes; i++ ) {
					svars.colors[i][3] = 0xff;
				}
			}
		}
		break;
	case AGEN_CONST:
		if ( pStage->rgbGen != CGEN_CONST ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				svars.colors[i][3] = pStage->constantColor[3];
			}
		}
		break;
	case AGEN_WAVEFORM:
		RB_CalcWaveAlpha( &pStage->alphaWave, ( unsigned char * ) svars.colors );
		break;
	case AGEN_LIGHTING_SPECULAR:
		RB_CalcSpecularAlpha( ( unsigned char * ) svars.colors );
		break;
	case AGEN_ENTITY:
		RB_CalcAlphaFromEntity( ( unsigned char * ) svars.colors );
		break;
	case AGEN_ONE_MINUS_ENTITY:
		RB_CalcAlphaFromOneMinusEntity( ( unsigned char * ) svars.colors );
		break;
	case AGEN_VERTEX:
		if ( pStage->rgbGen != CGEN_VERTEX ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				svars.colors[i][3] = tess.vertexColors[i][3];
			}
		}
		break;
	case AGEN_ONE_MINUS_VERTEX:
		for ( i = 0; i < tess.numVertexes; i++ )
		{
			svars.colors[i][3] = 255 - tess.vertexColors[i][3];
		}
		break;
	case AGEN_PORTAL:
		{
			for ( i = 0; i < tess.numVertexes; i++ )
			{
				vec3_t v;
				VectorSubtract( tess.xyz[i], backEnd.viewParms.or.origin, v );
				float len = VectorLength( v ) / tess.shader->portalRange;
				svars.colors[i][3] = (byte)Com_Clamp( 0, 255, len * 255 );
			}
		}
		break;
	}

	//
	// fog adjustment for colors to fade out as fog increases
	//
	if ( tess.fogNum )
	{
		switch ( pStage->adjustColorsForFog )
		{
		case ACFF_MODULATE_RGB:
			RB_CalcModulateColorsByFog( ( unsigned char * ) svars.colors );
			break;
		case ACFF_MODULATE_ALPHA:
			RB_CalcModulateAlphasByFog( ( unsigned char * ) svars.colors );
			break;
		case ACFF_MODULATE_RGBA:
			RB_CalcModulateRGBAsByFog( ( unsigned char * ) svars.colors );
			break;
		case ACFF_NONE:
			break;
		}
	}
}


void R_ComputeTexCoords( const shaderStage_t* pStage, stageVars_t& svars )
{
	int i;

	// generate the base texture coordinates

	switch ( pStage->tcGen )
	{
	case TCGEN_IDENTITY:
		Com_Memset( svars.texcoords, 0, sizeof( float ) * 2 * tess.numVertexes );
		break;

	case TCGEN_TEXTURE:
		for ( i = 0 ; i < tess.numVertexes ; i++ ) {
			svars.texcoords[i][0] = tess.texCoords[i][0][0];
			svars.texcoords[i][1] = tess.texCoords[i][0][1];
		}
		break;

	case TCGEN_LIGHTMAP:
		for ( i = 0 ; i < tess.numVertexes ; i++ ) {
			svars.texcoords[i][0] = tess.texCoords[i][1][0];
			svars.texcoords[i][1] = tess.texCoords[i][1][1];
		}
		break;

	case TCGEN_VECTOR:
		for ( i = 0 ; i < tess.numVertexes ; i++ ) {
			svars.texcoords[i][0] = DotProduct( tess.xyz[i], pStage->tcGenVectors[0] );
			svars.texcoords[i][1] = DotProduct( tess.xyz[i], pStage->tcGenVectors[1] );
		}
		break;

	case TCGEN_FOG:
		RB_CalcFogTexCoords( ( float * ) svars.texcoords );
		break;

	case TCGEN_ENVIRONMENT_MAPPED:
		RB_CalcEnvironmentTexCoords( ( float * ) svars.texcoords );
		break;

	case TCGEN_BAD:
		return;
	}

	// then alter for any tcmods

	for ( i = 0; i < pStage->numTexMods; ++i ) {
		switch ( pStage->texMods[i].type )
		{
		case TMOD_NONE:
			i = TR_MAX_TEXMODS;		// break out of for loop
			break;

		case TMOD_TURBULENT:
			RB_CalcTurbulentTexCoords( &pStage->texMods[i].wave, (float*)svars.texcoords );
			break;

		case TMOD_ENTITY_TRANSLATE:
			RB_CalcScrollTexCoords( backEnd.currentEntity->e.shaderTexCoord, (float*)svars.texcoords );
			break;

		case TMOD_SCROLL:
			RB_CalcScrollTexCoords( pStage->texMods[i].scroll, (float*)svars.texcoords );
				break;

		case TMOD_SCALE:
			RB_CalcScaleTexCoords( pStage->texMods[i].scale, (float*)svars.texcoords );
			break;

		case TMOD_STRETCH:
			RB_CalcStretchTexCoords( &pStage->texMods[i].wave, (float*)svars.texcoords );
			break;

		case TMOD_TRANSFORM:
			RB_CalcTransformTexCoords( &pStage->texMods[i], (float*)svars.texcoords );
			break;

		case TMOD_ROTATE:
			RB_CalcRotateTexCoords( pStage->texMods[i].rotateSpeed, (float*)svars.texcoords );
			break;

		default:
			ri.Error( ERR_DROP, "ERROR: unknown texmod '%d' in shader '%s'\n", pStage->texMods[i].type, tess.shader->name );
			break;
		}
	}
}


///////////////////////////////////////////////////////////////


static void RB_IterateStagesGeneric( const shaderCommands_t* input )
{
	for (int stage = 0; stage < MAX_SHADER_STAGES; ++stage)
	{
		const shaderStage_t* pStage = tess.xstages[stage];
		if ( !pStage )
			break;

		R_ComputeColors( pStage, tess.svars );
		R_ComputeTexCoords( pStage, tess.svars );

		if ( r_lightmap->integer && (pStage->type == ST_LIGHTMAP) )
			GL_TexEnv( GL_REPLACE );

		R_BindAnimatedImage( &pStage->bundle );
		GL_State( pStage->stateBits );
		R_DrawElements( input->numIndexes, input->indexes );

		// allow skipping out to show just lightmaps during development
		if ( r_lightmap->integer && (pStage->type == ST_LIGHTMAP) ) {
			GL_TexEnv( GL_MODULATE );
			break;
		}
	}
}


void RB_StageIteratorGeneric()
{
	shaderCommands_t* input = &tess;

	if (tess.pass == shaderCommands_t::TP_LIGHT)
		return;

	GL_Program();

	RB_DeformTessGeometry();

	GL_Cull( input->shader->cullType );

	if ( input->shader->polygonOffset )
	{
		qglEnable( GL_POLYGON_OFFSET_FILL );
		//qglPolygonOffset( r_offsetFactor->value, r_offsetUnits->value );
	}

	// geometry is per-shader and can be compiled
	// color and tc are per-stage, and can't

	qglDisableClientState( GL_COLOR_ARRAY );
	qglDisableClientState( GL_TEXTURE_COORD_ARRAY );

	qglVertexPointer( 3, GL_FLOAT, 16, input->xyz );	// padded for SIMD
	qglLockArraysEXT( 0, input->numVertexes );

	qglEnableClientState( GL_COLOR_ARRAY );
	qglColorPointer( 4, GL_UNSIGNED_BYTE, 0, input->svars.colors );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	qglTexCoordPointer( 2, GL_FLOAT, 0, input->svars.texcoords[0] );

	// call shader function
	RB_IterateStagesGeneric( input );

	if ( tess.fogNum && tess.shader->fogPass ) {
		RB_FogPass();
	}

	qglUnlockArraysEXT();

	if ( input->shader->polygonOffset )
	{
		qglDisable( GL_POLYGON_OFFSET_FILL );
	}
}

