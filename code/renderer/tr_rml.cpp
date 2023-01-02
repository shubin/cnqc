#include "tr_local.h"

#define MAX_RML_VERTICES	10000
#define MAX_RML_INDICES		(6 * MAX_RML_VERTICES)

static rmlvertex_t		rmlVertices[MAX_RML_VERTICES];
static rmlvertex_t		*rmlVertexPointer = &rmlVertices[0];
static int				rmlIndices[MAX_RML_INDICES];
static int				*rmlIndexPointer = &rmlIndices[0];

void R_ClearRmlFrame()
{
	rmlVertexPointer = &rmlVertices[0];
	rmlIndexPointer = &rmlIndices[0];
}

// technically, all commands should probably check tr.registered
// but realistically, only begin+end frame really need to
#define R_CMD_RET(T, ID)   T* cmd = (T*)R_GetCommandBuffer( sizeof(T), qfalse ); if (!cmd) return; cmd->commandId = ID
#define R_CMD_NORET(T, ID) T* cmd = (T*)R_GetCommandBuffer( sizeof(T), qfalse ); if (cmd)  cmd->commandId = ID
#define R_CMD_END(T, ID)   T* cmd = (T*)R_GetCommandBuffer( sizeof(T), qtrue  );           cmd->commandId = ID

void RE_RenderGeometry( const rmlvertex_t* vertices, int num_vertices, const int* indices, int num_indices, qhandle_t hShader, const vec2_t translation )
{
	// check overflow
	if ( num_vertices > MAX_RML_VERTICES - ( rmlVertexPointer - &rmlVertices[0] ) ) {
		return;
	}

	if ( num_indices > MAX_RML_INDICES - ( rmlIndexPointer - &rmlIndices[0] ) ) {
		return;
	}

	// issue the render command
	R_CMD_RET( geometryCommand_t, RC_GEOMETRY );

	cmd->shader = hShader ? R_GetShaderByHandle( hShader ) : tr.defaultRmlShader;
	cmd->vertices = rmlVertexPointer;
	cmd->numVertices = num_vertices;
	cmd->indices = rmlIndexPointer;
	cmd->numIndices = num_indices;
	cmd->translation[0] = translation[0];
	cmd->translation[1] = translation[1];

	// copy vertex and index data to the frame-local buffer
	memcpy( rmlVertexPointer, vertices, sizeof( rmlvertex_t ) * num_vertices );
	rmlVertexPointer += num_vertices;

	memcpy( rmlIndexPointer, indices, sizeof( int ) * num_indices );
	rmlIndexPointer += num_indices;
}

void RE_SaveScissor( qboolean save )
{
	R_CMD_RET( scissorCommand_t, RC_SCISSOR );
	cmd->op = save ? SCISSOR_OP_SAVE : SCISSOR_OP_RESTORE;
}

void RE_EnableScissor( qboolean enable )
{
	R_CMD_RET(scissorCommand_t, RC_SCISSOR);
	cmd->op = enable ? SCISSOR_OP_ENABLE : SCISSOR_OP_DISABLE;
}

void RE_SetScissor( int x, int y, int width, int height )
{
	R_CMD_RET( scissorCommand_t, RC_SCISSOR );
	cmd->op = SCISSOR_OP_SET;
	cmd->x = x;
	cmd->y = y;
	cmd->w = width;
	cmd->h = height;
	if (cmd->x == 2127) {
		int bp = 1;
	}
}

qhandle_t RE_LoadTexture( const char* source, int* w, int* h )
{
	const image_t *image = R_FindImageFile( source, 0, TW_CLAMP_TO_EDGE );
	if ( image != NULL ) {
		*w = image->width;
		*h = image->height;
		qhandle_t hShader = RE_RegisterShaderFromImage( va( "rml:%s", source ), image );
		tr.shaders[hShader]->cullType = CT_TWO_SIDED;
		return hShader;
	}
	shader_t* shader = R_FindShader( source, 0, qfalse );
	if ( shader != NULL && shader->numStages > 0 ) {
		const image_t* image = shader->stages[0]->bundle.image[0];
		if ( image != NULL ) {
			//shader->cullType = CT_TWO_SIDED;
			*w = image->width;
			*h = image->height;
			return shader->index;
		}
	}
	return 0;
}

qhandle_t RE_UploadTexture( const byte* source, int w, int h )
{
	unsigned int crc;

	if ( source == NULL ) {
		return 0;
	}

	CRC32_Begin( &crc );
	CRC32_ProcessBlock( &crc, source, w * h * 4 );
	CRC32_End( &crc );

	const char *image_name = va( "img:%08X", crc );
	const image_t* image = NULL;//R_FindImageFile( image_name, 0, TW_CLAMP_TO_EDGE );
	if ( image == NULL ) {
		image = R_CreateImage( image_name, (byte*)source, w, h, TF_RGBA8, 0, TW_CLAMP_TO_EDGE );
	}
	qhandle_t hShader = RE_RegisterShaderFromImage( va( "tex:%08X", crc ), image );
	tr.shaders[hShader]->cullType = CT_TWO_SIDED;
	return hShader;
}

void RE_SetMatrix( const float *matrix ) {
	R_CMD_RET( matrixCommand_t, RC_MATRIX );
	if ( matrix == NULL ) {
		static float identity[16] = {
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1,
		};
		matrix = identity;
	}
	memcpy( cmd->matrix, matrix, sizeof( cmd->matrix ) );
}
