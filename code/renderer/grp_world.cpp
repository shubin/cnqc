/*
===========================================================================
Copyright (C) 2022-2023 Gian 'myT' Schellenbaum

This file is part of Challenge Quake 3 (CNQ3).

Challenge Quake 3 is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Challenge Quake 3 is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Challenge Quake 3. If not, see <https://www.gnu.org/licenses/>.
===========================================================================
*/
// Gameplay Rendering Pipeline - world/3D rendering


#include "grp_local.h"
#include "../imgui/imgui.h"


#pragma pack(push, 1)

struct VertexBase
{
	vec3_t position;
	vec3_t normal;
};

struct VertexStage
{
	vec2_t texCoords;
	uint32_t color;
};

struct LargestVertex
{
	VertexBase base;
	VertexStage stages[MAX_SHADER_STAGES];
};

struct ZPPVertexRC
{
	float mvp[16];
};

struct DynamicVertexRC
{
	float mvp[16];
	float clipPlane[4];
	uint32_t bufferByteOffset;
};

struct DynamicPixelRC
{
	uint32_t textureIndex;
	uint32_t samplerIndex;
};

#pragma pack(pop)

static const char* zpp_vs = R"grml(
cbuffer RootConstants
{
	float4x4 mvp;
};

float4 main(float4 position : POSITION) : SV_Position
{
	return mul(mvp, float4(position.xyz, 1.0));
}
)grml";

static const char* zpp_ps = R"grml(
void main()
{
}
)grml";

static const char* dyn_vs = R"grml(
cbuffer RootConstants
{
	float4x4 mvp;
	float4 clipPlane;
	uint bufferByteOffset;
};

float4 main() : SV_Position
{
	//return mul(mvp, float4(position.xyz, 1.0));
	return float4(0, 0, 0, 0);
}
)grml";

static const char* dyn_ps = R"grml(
cbuffer RootConstants
{
	uint textureIndex;
	uint samplerIndex;
};

Texture2D textures2D[2048] : register(t0);
SamplerState samplers[2] : register(s0);

float4 main() : SV_TARGET
{
	//return textures2D[textureIndex].Sample(samplers[samplerIndex], input.texCoords) * input.color;
	return float4(0.0, 0.5, 0.0, 1.0);
}
)grml";


void World::Init()
{
	{
		TextureDesc desc("depth buffer", glConfig.vidWidth, glConfig.vidHeight);
		desc.shortLifeTime = true;
		desc.initialState = ResourceStates::DepthWriteBit;
		desc.allowedState = ResourceStates::DepthAccessBits | ResourceStates::PixelShaderAccessBit;
		desc.format = TextureFormat::Depth32_Float;
		desc.SetClearDepthStencil(0.0f, 0);
		depthTexture = CreateTexture(desc);
		depthTextureIndex = grp.RegisterTexture(depthTexture);
	}

	if(!grp.firstInit)
	{
		return;
	}

	//
	// depth pre-pass
	//
	{
		RootSignatureDesc desc("Z pre-pass");
		desc.usingVertexBuffers = true;
		desc.constants[ShaderStage::Vertex].byteCount = sizeof(ZPPVertexRC);
		desc.constants[ShaderStage::Pixel].byteCount = 0;
		desc.samplerVisibility = ShaderStages::None;
		desc.genericVisibility = ShaderStages::None;
		zppRootSignature = CreateRootSignature(desc);
	}
	{
		zppDescriptorTable = CreateDescriptorTable(DescriptorTableDesc("Z pre-pass", zppRootSignature));
	}
	{
		GraphicsPipelineDesc desc("Z pre-pass", zppRootSignature);
		desc.vertexShader = CompileVertexShader(zpp_vs);
		desc.pixelShader = CompilePixelShader(zpp_ps);
		desc.vertexLayout.AddAttribute(0, ShaderSemantic::Position, DataType::Float32, 4, 0);
		desc.depthStencil.depthComparison = ComparisonFunction::GreaterEqual;
		desc.depthStencil.depthStencilFormat = TextureFormat::Depth32_Float;
		desc.depthStencil.enableDepthTest = true;
		desc.depthStencil.enableDepthWrites = true;
		desc.rasterizer.cullMode = CullMode::Back;
		zppPipeline = CreateGraphicsPipeline(desc);
	}
	prePassGeo.maxVertexCount = 1 << 20;
	prePassGeo.maxIndexCount = 8 * prePassGeo.maxVertexCount;
	{
		BufferDesc desc("depth pre-pass index", sizeof(Index) * prePassGeo.maxIndexCount, ResourceStates::IndexBufferBit);
		prePassGeo.indexBuffer = CreateBuffer(desc);
	}
	{
		BufferDesc desc("depth pre-pass vertex", 16 * prePassGeo.maxVertexCount, ResourceStates::VertexBufferBit);
		prePassGeo.vertexBuffer = CreateBuffer(desc);
	}

	//
	// dynamic (streamed) geometry
	//
	{
		RootSignatureDesc desc("dynamic");
		desc.usingVertexBuffers = false;
		desc.constants[ShaderStage::Vertex].byteCount = sizeof(DynamicVertexRC);
		desc.constants[ShaderStage::Pixel].byteCount = sizeof(DynamicPixelRC);
		desc.samplerVisibility = ShaderStages::PixelBit;
		desc.genericVisibility = ShaderStages::VertexBit | ShaderStages::PixelBit;
		dynRootSignature = CreateRootSignature(desc);
	}
	{
		dynDescriptorTable = CreateDescriptorTable(DescriptorTableDesc("dynamic", dynRootSignature));
	}
	{
		GraphicsPipelineDesc desc("dynamic", dynRootSignature);
		desc.vertexShader = CompileVertexShader(dyn_vs);
		desc.pixelShader = CompilePixelShader(dyn_ps);
		desc.depthStencil.depthComparison = ComparisonFunction::GreaterEqual;
		desc.depthStencil.depthStencilFormat = TextureFormat::Depth32_Float;
		desc.depthStencil.enableDepthTest = true;
		desc.depthStencil.enableDepthWrites = true;
		desc.rasterizer.cullMode = CullMode::Back;
		dynPipeline = CreateGraphicsPipeline(desc);
	}
	dynamicGeo.maxVertexCount = SHADER_MAX_VERTEXES;
	dynamicGeo.maxIndexCount = SHADER_MAX_INDEXES;
	{
		BufferDesc desc("dynamic index", sizeof(Index) * dynamicGeo.maxIndexCount, ResourceStates::IndexBufferBit);
		desc.memoryUsage = MemoryUsage::Upload;
		dynamicGeo.indexBuffer = CreateBuffer(desc);
	}
	{
		BufferDesc desc("dynamic vertex", sizeof(LargestVertex) * dynamicGeo.maxVertexCount, ResourceStates::VertexBufferBit);
		desc.memoryUsage = MemoryUsage::Upload;
		dynamicGeo.vertexBuffer = CreateBuffer(desc);
	}
}

void World::BeginFrame()
{
}

void World::Begin()
{
	if(grp.renderMode == RenderMode::World)
	{
		return;
	}

	// copy backEnd.viewParms.projectionMatrix
	CmdSetViewportAndScissor(backEnd.viewParms);

	// @TODO: this should be moved out of the RP since none of the decision-making is RP-specific
#if 0
	bool shouldClearColor = qfalse;
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	if(backEnd.rdflags & RDF_HYPERSPACE) // @TODO: who sets that up again?
	{
		const float c = RB_HyperspaceColor();
		clearColor[0] = c;
		clearColor[1] = c;
		clearColor[2] = c;
		shouldClearColor = qtrue;
	}
	else if(r_fastsky->integer && !(backEnd.rdflags & RDF_NOWORLDMODEL))
	{
		shouldClearColor = qtrue;
	}
	if(shouldClearColor)
	{
		ClearColor(clearColor);
	}
#endif

	if(backEnd.viewParms.isPortal)
	{
		float plane[4];
		plane[0] = backEnd.viewParms.portalPlane.normal[0];
		plane[1] = backEnd.viewParms.portalPlane.normal[1];
		plane[2] = backEnd.viewParms.portalPlane.normal[2];
		plane[3] = backEnd.viewParms.portalPlane.dist;

		float plane2[4];
		plane2[0] = DotProduct(backEnd.viewParms.orient.axis[0], plane);
		plane2[1] = DotProduct(backEnd.viewParms.orient.axis[1], plane);
		plane2[2] = DotProduct(backEnd.viewParms.orient.axis[2], plane);
		plane2[3] = DotProduct(plane, backEnd.viewParms.orient.origin) - plane[3];

		float* o = plane;
		const float* m = s_flipMatrix;
		const float* v = plane2;
		o[0] = m[0] * v[0] + m[4] * v[1] + m[8] * v[2] + m[12] * v[3];
		o[1] = m[1] * v[0] + m[5] * v[1] + m[9] * v[2] + m[13] * v[3];
		o[2] = m[2] * v[0] + m[6] * v[1] + m[10] * v[2] + m[14] * v[3];
		o[3] = m[3] * v[0] + m[7] * v[1] + m[11] * v[2] + m[15] * v[3];

		memcpy(clipPlane, plane, sizeof(clipPlane));
	}
	else
	{
		memset(clipPlane, 0, sizeof(clipPlane));
	}

	grp.renderMode = RenderMode::World;
}

void World::DrawPrePass()
{
	if(tr.world == NULL ||
		prePassGeo.indexCount <= 0 ||
		prePassGeo.vertexCount <= 0)
	{
		return;
	}

	TextureBarrier tb(depthTexture, ResourceStates::DepthWriteBit);
	CmdBarrier(1, &tb);

	CmdBindRootSignature(zppRootSignature);
	CmdBindPipeline(zppPipeline);
	CmdBindDescriptorTable(zppRootSignature, zppDescriptorTable);

	// @TODO: evaluate later whether binding the color target here is OK?
	CmdBindRenderTargets(0, NULL, &depthTexture);
	CmdClearDepthTarget(depthTexture, 0.0f);

	float mvp[16];
	R_MultMatrix(backEnd.viewParms.world.modelMatrix, backEnd.viewParms.projectionMatrix, mvp);
	CmdSetRootConstants(zppRootSignature, ShaderStage::Vertex, mvp);
	CmdBindPipeline(zppPipeline);
	CmdBindIndexBuffer(prePassGeo.indexBuffer, indexType, 0);
	const uint32_t vertexStride = 4 * sizeof(float);
	CmdBindVertexBuffers(1, &prePassGeo.vertexBuffer, &vertexStride, NULL);
	CmdDrawIndexed(prePassGeo.indexCount, 0, 0);
}

void World::DrawBatch()
{
	if(tess.numVertexes <= 0 ||
		tess.numIndexes <= 0)
	{
		return;
	}

	// backEnd.orient.modelMatrix
	// backEnd.viewParms.projectionMatrix

	tess.numVertexes = 0;
	tess.numIndexes = 0;
}

void World::DrawGUI()
{
	TextureBarrier tb(depthTexture, ResourceStates::DepthReadBit);
	CmdBarrier(1, &tb);
	if(ImGui::Begin("depth", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Image((ImTextureID)depthTextureIndex, ImVec2(640, 360));
		//ImGui::Image(30, ImVec2(640, 360));
	}
	ImGui::End();
}

void World::ProcessWorld(world_t& world)
{
	float* vtx = (float*)BeginBufferUpload(prePassGeo.vertexBuffer);
	Index* idx = (Index*)BeginBufferUpload(prePassGeo.indexBuffer);

	int firstVertex = 0;
	int firstIndex = 0;
	for(int s = 0; s < world.numsurfaces; ++s)
	{
		msurface_t* const surf = &world.surfaces[s];
		surf->numVertexes = 0;
		surf->numIndexes = 0;
		surf->firstVertex = 0;
		surf->firstIndex = 0;

		// @TODO: make sure it's static as well!
		if(surf->shader->numStages == 0)
		{
			continue;
		}

		rb_surfaceTable[*surf->data](surf->data);
		const int surfVertexCount = tess.numVertexes;
		const int surfIndexCount = tess.numIndexes;
		if(surfVertexCount <= 0 || surfIndexCount <= 0)
		{
			continue;
		}
		if(firstVertex + surfVertexCount >= prePassGeo.maxVertexCount ||
			firstIndex + surfIndexCount >= prePassGeo.maxIndexCount)
		{
			break;
		}

		/*
		// @TODO: compute all attributes for static geo
		for(int i = 0; i < surf->shader->numStages; ++i)
		{
			const shaderStage_t* const stage = surf->shader->stages[i];
			R_ComputeColors(stage, tess.svars[i], 0, tess.numVertexes);
			R_ComputeTexCoords(stage, tess.svars[i], 0, tess.numVertexes, qfalse);
		}
		*/

		for(int v = 0; v < tess.numVertexes; ++v)
		{
			*vtx++ = tess.xyz[v][0];
			*vtx++ = tess.xyz[v][1];
			*vtx++ = tess.xyz[v][2];
			*vtx++ = 1.0f;
		}

		for(int i = 0; i < tess.numIndexes; ++i)
		{
			*idx++ = tess.indexes[i] + firstVertex;
		}

		surf->numVertexes = tess.numVertexes;
		surf->numIndexes = tess.numIndexes;
		surf->firstVertex = firstVertex;
		surf->firstIndex = firstIndex;
		firstVertex += surfVertexCount;
		firstIndex += surfIndexCount;
		tess.numVertexes = 0;
		tess.numIndexes = 0;
	}

	EndBufferUpload(prePassGeo.vertexBuffer);
	EndBufferUpload(prePassGeo.indexBuffer);

	prePassGeo.vertexCount = firstVertex;
	prePassGeo.indexCount = firstIndex;
	prePassGeo.firstVertex = 0;
	prePassGeo.firstIndex = 0;
}

void World::DrawSceneView(const drawSceneViewCommand_t& cmd)
{
	backEnd.refdef = cmd.refdef;
	backEnd.viewParms = cmd.viewParms;

	Begin();
	DrawPrePass();

	const HTexture swapChain = GetSwapChainTexture();
	CmdBindRenderTargets(1, &swapChain, &depthTexture);

	const drawSurf_t* drawSurfs = cmd.drawSurfs;
	const int opaqueCount = cmd.numDrawSurfs - cmd.numTranspSurfs;
	//const int transpCount = cmd.numTranspSurfs;
	const double originalTime = backEnd.refdef.floatTime;

	const shader_t* shader = NULL;
	unsigned int sort = (unsigned int)-1;
	int oldEntityNum = -1;
	backEnd.currentEntity = &tr.worldEntity;
	qbool oldDepthRange = qfalse;
	qbool depthRange = qfalse;

	int i;
	const drawSurf_t* drawSurf;
	for(i = 0, drawSurf = drawSurfs; i < opaqueCount; ++i, ++drawSurf)
	{
		int fogNum;
		int entityNum;
		R_DecomposeSort(drawSurf->sort, &entityNum, &shader, &fogNum);
		tess.shader = shader; // @TODO: should be the one of the current batch...

		sort = drawSurf->sort;

		//
		// change the modelview matrix if needed
		//
		if(entityNum != oldEntityNum)
		{
			depthRange = qfalse;

			if(entityNum != ENTITYNUM_WORLD)
			{
				backEnd.currentEntity = &backEnd.refdef.entities[entityNum];
				if(backEnd.currentEntity->intShaderTime)
					backEnd.refdef.floatTime = originalTime - (double)backEnd.currentEntity->e.shaderTime.iShaderTime / 1000.0;
				else
					backEnd.refdef.floatTime = originalTime - backEnd.currentEntity->e.shaderTime.fShaderTime;
				// we have to reset the shaderTime as well otherwise image animations start
				// from the wrong frame
				tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;

				// set up the transformation matrix
				R_RotateForEntity(backEnd.currentEntity, &backEnd.viewParms, &backEnd.orient);

				if(backEnd.currentEntity->e.renderfx & RF_DEPTHHACK)
				{
					// hack the depth range to prevent view model from poking into walls
					depthRange = qtrue;
				}
			}
			else
			{
				backEnd.currentEntity = &tr.worldEntity;
				backEnd.refdef.floatTime = originalTime;
				backEnd.orient = backEnd.viewParms.world;
				// we have to reset the shaderTime as well otherwise image animations on
				// the world (like water) continue with the wrong frame
				tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
			}

			//gal.SetModelViewMatrix(backEnd.orient.modelMatrix);

			//
			// change depthrange if needed
			//
			if(oldDepthRange != depthRange)
			{
				if(depthRange)
				{
					//gal.SetDepthRange(0, 0.3);
				}
				else
				{
					//gal.SetDepthRange(0, 1);
				}
				oldDepthRange = depthRange;
			}

			oldEntityNum = entityNum;
		}

		//const int firstVertex = tess.numVertexes;
		//const int firstIndex = tess.numIndexes;
		const int firstVertex = 0;
		const int firstIndex = 0;
		rb_surfaceTable[*drawSurf->surface](drawSurf->surface);
		const int numVertexes = tess.numVertexes - firstVertex;
		const int numIndexes = tess.numIndexes - firstIndex;
		RB_DeformTessGeometry(firstVertex, numVertexes, firstIndex, numIndexes);
		for(int s = 0; s < shader->numStages; ++s)
		{
			R_ComputeColors(shader->stages[s], tess.svars[s], firstVertex, numVertexes);
			R_ComputeTexCoords(shader->stages[s], tess.svars[s], firstVertex, numVertexes, qfalse);
		}

		// upload batch data

		DrawBatch();
	}

	backEnd.refdef.floatTime = originalTime;

	DrawBatch();

	// go back to the world model-view matrix
	//gal.SetModelViewMatrix(backEnd.viewParms.world.modelMatrix);
	if(depthRange)
	{
		//gal.SetDepthRange(0, 1);
	}
}
