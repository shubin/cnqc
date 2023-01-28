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
namespace zpp
{
#include "hlsl/depth_pre_pass_vs.h"
#include "hlsl/depth_pre_pass_ps.h"
}
namespace fog
{
#include "hlsl/fog_vs.h"
}
namespace fog_inside
{
#include "hlsl/fog_inside_ps.h"
}
namespace fog_outside
{
#include "hlsl/fog_outside_ps.h"
}


#pragma pack(push, 4)

struct ZPPVertexRC
{
	float modelViewMatrix[16];
	float projectionMatrix[16];
};

struct FogVertexRC
{
	float modelViewMatrix[16];
	float projectionMatrix[16];
	float boxMin[4];
	float boxMax[4];
};

struct FogPixelRC
{
	float colorDepth[4];
};

#pragma pack(pop)


static bool drawPrePass = false;
static bool drawDynamic = true;
static bool drawTransparents = true;
static bool drawFog = true;


static bool HasStaticGeo(const drawSurf_t* drawSurf)
{
	return drawSurf->staticGeoChunk > 0 && drawSurf->staticGeoChunk < ARRAY_LEN(grp.world.statChunks);
}

static void UpdateEntityData(int entityNum, double originalTime)
{
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

		// @TODO: depth range
		/*if(backEnd.currentEntity->e.renderfx & RF_DEPTHHACK)
		{
		}*/
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
}

static void ExtractCameraPosition(vec3_t cameraPos, const float* modelView)
{
	float modelViewT[16];
	R_TransposeMatrix(modelView, modelViewT);

	// plane normals 
	vec3_t n1, n2, n3;
	VectorCopy(modelViewT + 0 * 4, n1);
	VectorCopy(modelViewT + 1 * 4, n2);
	VectorCopy(modelViewT + 2 * 4, n3);

	// plane distances
	const float d1 = modelViewT[0 * 4 + 3];
	const float d2 = modelViewT[1 * 4 + 3];
	const float d3 = modelViewT[2 * 4 + 3];

	// intersection of the 3 planes
	vec3_t n2n3, n3n1, n1n2;
	CrossProduct(n2, n3, n2n3);
	CrossProduct(n3, n1, n3n1);
	CrossProduct(n1, n2, n1n2);

	// top = (n2n3 * d1) + (n3n1 * d2) + (n1n2 * d3)
	vec3_t top;
	VectorMA(vec3_origin, d1, n2n3, top);
	VectorMA(top, d2, n3n1, top);
	VectorMA(top, d3, n1n2, top);
	const float denom = DotProduct(n1, n2n3);
	VectorScale(top, -1.0f / denom, cameraPos);
}

static void ExtractCameraAxisVectors(vec3_t axisX, vec3_t axisY, vec3_t axisZ, const float* modelView)
{
	axisX[0] = modelView[0];
	axisX[1] = modelView[4];
	axisX[2] = modelView[8];

	axisY[0] = modelView[1];
	axisY[1] = modelView[5];
	axisY[2] = modelView[9];

	axisZ[0] = modelView[ 2];
	axisZ[1] = modelView[ 6];
	axisZ[2] = modelView[10];
}


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

		if(!IsNullHandle(fogDescriptorTable))
		{
			DescriptorTableUpdate update;
			update.SetTextures(1, &depthTexture);
			UpdateDescriptorTable(fogDescriptorTable, update);
		}
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
		desc.vertexShader = ShaderByteCode(zpp::g_vs);
		desc.pixelShader = ShaderByteCode(zpp::g_ps);
		desc.vertexLayout.AddAttribute(0, ShaderSemantic::Position, DataType::Float32, 4, 0);
		desc.depthStencil.depthComparison = ComparisonFunction::GreaterEqual;
		desc.depthStencil.depthStencilFormat = TextureFormat::Depth32_Float;
		desc.depthStencil.enableDepthTest = true;
		desc.depthStencil.enableDepthWrites = true;
		desc.rasterizer.cullMode = CT_FRONT_SIDED; // need 1 PSO per cull mode...
		zppPipeline = CreateGraphicsPipeline(desc);
	}
	{
		const uint32_t maxVertexCount = 1 << 20;
		const uint32_t maxIndexCount = 8 * maxVertexCount;
		zppVertexBuffer.Init(maxVertexCount, 16);
		zppIndexBuffer.Init(maxIndexCount, sizeof(Index));
		{
			BufferDesc desc("depth pre-pass vertex", zppVertexBuffer.byteCount, ResourceStates::VertexBufferBit);
			zppVertexBuffer.buffer = CreateBuffer(desc);
		}
		{
			BufferDesc desc("depth pre-pass index", zppIndexBuffer.byteCount, ResourceStates::IndexBufferBit);
			zppIndexBuffer.buffer = CreateBuffer(desc);
		}
	}

	//
	// dynamic (streamed) geometry
	//
	for(uint32_t f = 0; f < FrameCount; ++f)
	{
		const int MaxDynamicVertexCount = 256 << 10;
		const int MaxDynamicIndexCount = MaxDynamicVertexCount * 8;
		GeometryBuffers& db = dynBuffers[f];
		db.vertexBuffers.Create(va("dynamic #%d", f + 1), MemoryUsage::Upload, MaxDynamicVertexCount);
		db.indexBuffer.Create(va("dynamic #%d", f + 1), MemoryUsage::Upload, MaxDynamicIndexCount);
	}

	//
	// static (GPU-resident) geometry
	//
	{
		const int MaxVertexCount = 256 << 10;
		const int MaxIndexCount = MaxVertexCount * 8;
		statBuffers.vertexBuffers.Create("static", MemoryUsage::GPU, MaxVertexCount);
		statBuffers.indexBuffer.Create("static", MemoryUsage::GPU, MaxIndexCount);
	}

	//
	// fog
	//
	{
		RootSignatureDesc desc("fog");
		desc.usingVertexBuffers = true;
		desc.AddRange(DescriptorType::Texture, 0, 1);
		desc.genericVisibility = ShaderStages::PixelBit;
		desc.constants[ShaderStage::Vertex].byteCount = sizeof(FogVertexRC);
		desc.constants[ShaderStage::Pixel].byteCount = sizeof(FogPixelRC);
		fogRootSignature = CreateRootSignature(desc);
	}
	{
		DescriptorTableDesc desc("fog", fogRootSignature);
		fogDescriptorTable = CreateDescriptorTable(desc);

		DescriptorTableUpdate update;
		update.SetTextures(1, &depthTexture);
		UpdateDescriptorTable(fogDescriptorTable, update);
	}
	{
		GraphicsPipelineDesc desc("fog outside", fogRootSignature);
		desc.vertexShader = ShaderByteCode(fog::g_vs);
		desc.pixelShader = ShaderByteCode(fog_outside::g_ps);
		desc.depthStencil.DisableDepth();
		desc.rasterizer.cullMode = CT_BACK_SIDED;
		desc.rasterizer.polygonOffset = false;
		desc.AddRenderTarget(GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA, TextureFormat::RGBA32_UNorm);
		desc.vertexLayout.AddAttribute(0, ShaderSemantic::Position, DataType::Float32, 3, 0);
		fogOutsidePipeline = CreateGraphicsPipeline(desc);
	}
	{
		GraphicsPipelineDesc desc("fog inside", fogRootSignature);
		desc.vertexShader = ShaderByteCode(fog::g_vs);
		desc.pixelShader = ShaderByteCode(fog_inside::g_ps);
		desc.depthStencil.DisableDepth();
		desc.rasterizer.cullMode = CT_FRONT_SIDED;
		desc.rasterizer.polygonOffset = false;
		desc.AddRenderTarget(GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA, TextureFormat::RGBA32_UNorm);
		desc.vertexLayout.AddAttribute(0, ShaderSemantic::Position, DataType::Float32, 3, 0);
		fogInsidePipeline = CreateGraphicsPipeline(desc);
	}
	{
		const uint32_t indices[] =
		{
			0, 1, 2, 2, 1, 3,
			4, 0, 6, 6, 0, 2,
			7, 5, 6, 6, 5, 4,
			3, 1, 7, 7, 1, 5,
			4, 5, 0, 0, 5, 1,
			3, 7, 2, 2, 7, 6
		};

		BufferDesc desc("box index", sizeof(indices), ResourceStates::IndexBufferBit);
		boxIndexBuffer = CreateBuffer(desc);

		uint8_t* mapped = BeginBufferUpload(boxIndexBuffer);
		memcpy(mapped, indices, sizeof(indices));
		EndBufferUpload(boxIndexBuffer);
	}
	{
		const float vertices[] =
		{
			0.0f, 1.0f, 0.0f,
			1.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f,
			1.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 1.0f,
			1.0f, 1.0f, 1.0f,
			0.0f, 0.0f, 1.0f,
			1.0f, 0.0f, 1.0f
		};

		BufferDesc desc("box vertex", sizeof(vertices), ResourceStates::VertexBufferBit);
		boxVertexBuffer = CreateBuffer(desc);

		uint8_t* mapped = BeginBufferUpload(boxVertexBuffer);
		memcpy(mapped, vertices, sizeof(vertices));
		EndBufferUpload(boxVertexBuffer);
	}
}

void World::BeginFrame()
{
	dynBuffers[GetFrameIndex()].Rewind();

	boundVertexBuffers = BufferFamily::Invalid;
	boundIndexBuffer = BufferFamily::Invalid;
	boundStaticVertexBuffersFirst = 0;
	boundStaticVertexBuffersCount = UINT32_MAX;
}

void World::Begin()
{
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

	if(grp.renderMode == RenderMode::World)
	{
		return;
	}

	CmdSetViewportAndScissor(backEnd.viewParms);

	TextureBarrier tb(depthTexture, ResourceStates::DepthWriteBit);
	CmdBarrier(1, &tb);

	grp.renderMode = RenderMode::World;
}

void World::DrawPrePass()
{
	if(!drawPrePass ||
		tr.world == NULL ||
		zppIndexBuffer.batchCount == 0 ||
		zppVertexBuffer.batchCount == 0)
	{
		return;
	}

	grp.BeginRenderPass("Depth Pre-Pass", 0.75f, 0.75f, 0.375f);

	// @TODO: evaluate later whether binding the color target here is OK?
	CmdBindRenderTargets(0, NULL, &depthTexture);

	CmdBindRootSignature(zppRootSignature);
	CmdBindPipeline(zppPipeline);
	CmdBindDescriptorTable(zppRootSignature, zppDescriptorTable);

	ZPPVertexRC vertexRC;
	memcpy(vertexRC.modelViewMatrix, backEnd.viewParms.world.modelMatrix, sizeof(vertexRC.modelViewMatrix));
	memcpy(vertexRC.projectionMatrix, backEnd.viewParms.projectionMatrix, sizeof(vertexRC.projectionMatrix));
	CmdSetRootConstants(zppRootSignature, ShaderStage::Vertex, &vertexRC);
	CmdBindPipeline(zppPipeline);
	CmdBindIndexBuffer(zppIndexBuffer.buffer, indexType, 0);
	const uint32_t vertexStride = 4 * sizeof(float);
	CmdBindVertexBuffers(1, &zppVertexBuffer.buffer, &vertexStride, NULL);
	CmdDrawIndexed(zppIndexBuffer.batchCount, 0, 0);
	boundVertexBuffers = BufferFamily::PrePass;
	boundIndexBuffer = BufferFamily::PrePass;

	grp.EndRenderPass();
}

void World::BeginBatch(const shader_t* shader, bool hasStaticGeo)
{
	tess.numVertexes = 0;
	tess.numIndexes = 0;
	tess.fogNum = 0;
	tess.depthFade = DFT_NONE;
	tess.deformsPreApplied = qfalse;
	tess.xstages = (const shaderStage_t**)shader->stages;
	tess.shader = shader;
	tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
	if(tess.shader->clampTime && tess.shaderTime >= tess.shader->clampTime)
	{
		tess.shaderTime = tess.shader->clampTime;
	}
	batchHasStaticGeo = hasStaticGeo;
}

void World::EndBatch(HPipeline& pso)
{
	if((!batchHasStaticGeo && tess.numVertexes <= 0) ||
		tess.numIndexes <= 0 ||
		tess.shader->numStages == 0 ||
		tess.shader->numPipelines <= 0)
	{
		// @TODO: make sure we never get tess.shader->numStages 0 here in the first place
		tess.numVertexes = 0;
		tess.numIndexes = 0;
		return;
	}

	GeometryBuffers& db = dynBuffers[GetFrameIndex()];
	if(!db.vertexBuffers.CanAdd(tess.numVertexes) ||
		!db.indexBuffer.CanAdd(tess.numIndexes))
	{
		Q_assert(!"Dynamic buffer too small!");
		return;
	}

	// darken the hell fog in the BS pit on cpm21 for debugging
#if 0
	if(Q_stristr(tess.shader->name, "hellfog"))
	{
		for(int v = 0; v < tess.numVertexes; ++v)
		{
			tess.svars[0].colors[v][0] = 127;
			tess.svars[0].colors[v][1] = 127;
			tess.svars[0].colors[v][2] = 127;
			tess.svars[0].colors[v][3] = 255;
		}
	}
#endif

	if(!batchHasStaticGeo)
	{
		db.vertexBuffers.Upload(0, tess.shader->numStages);
	}
	db.indexBuffer.Upload();

	BindIndexBuffer(false);

	for(int p = 0; p < tess.shader->numPipelines; ++p)
	{
		const pipeline_t& pipeline = tess.shader->pipelines[p];
		const int psoIndex = pipeline.pipeline;
		if(pso != grp.psos[psoIndex].pipeline)
		{
			pso = grp.psos[psoIndex].pipeline;
			CmdBindPipeline(pso);
		}

		DynamicVertexRC vertexRC = {};
		memcpy(vertexRC.modelViewMatrix, backEnd.orient.modelMatrix, sizeof(vertexRC.modelViewMatrix));
		memcpy(vertexRC.projectionMatrix, backEnd.viewParms.projectionMatrix, sizeof(vertexRC.projectionMatrix));
		memcpy(vertexRC.clipPlane, clipPlane, sizeof(vertexRC.clipPlane));
		CmdSetRootConstants(grp.opaqueRootSignature, ShaderStage::Vertex, &vertexRC);

		DynamicPixelRC pixelRC = {};
		for(int s = 0; s < pipeline.numStages; ++s)
		{
			const image_t* image = GetBundleImage(tess.shader->stages[pipeline.firstStage + s]->bundle);
			const uint32_t texIdx = image->textureIndex;
			const uint32_t sampIdx = GetSamplerIndex(image);
			Q_assert(texIdx > 0);
			pixelRC.stageIndices[s] = (sampIdx << 16) | (texIdx);
		}
		CmdSetRootConstants(grp.opaqueRootSignature, ShaderStage::Pixel, &pixelRC);

		BindVertexBuffers(batchHasStaticGeo, pipeline.firstStage, pipeline.numStages);
		
		CmdDrawIndexed(tess.numIndexes, db.indexBuffer.batchFirst, batchHasStaticGeo ? 0 : db.vertexBuffers.batchFirst);
	}

	if(!batchHasStaticGeo)
	{
		db.vertexBuffers.EndBatch(tess.numVertexes);
	}
	db.indexBuffer.EndBatch(tess.numIndexes);
	tess.numVertexes = 0;
	tess.numIndexes = 0;
}

void World::DrawGUI()
{
	if(tr.world == NULL)
	{
		return;
	}

#if 0
	TextureBarrier tb(depthTexture, ResourceStates::DepthReadBit);
	CmdBarrier(1, &tb);
	if(ImGui::Begin("depth", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Image((ImTextureID)depthTextureIndex, ImVec2(640, 360));
	}
	ImGui::End();
#endif

	if(ImGui::Begin("World"))
	{
		ImGui::Checkbox("Depth Pre-Pass", &drawPrePass);
		ImGui::Checkbox("Draw Dynamic", &drawDynamic);
		ImGui::Checkbox("Draw Transparents", &drawTransparents);
		ImGui::Checkbox("Draw Fog", &drawFog);
		ImGui::Text("PSO count: %d", (int)grp.psoCount);

		vec3_t axis[3];
		ExtractCameraAxisVectors(axis[0], axis[1], axis[2], backEnd.viewParms.world.modelMatrix);
		const char* axisNames[] = { "X", "Y", "Z", };
		for(int a = 0; a < 3; ++a)
		{
			ImGui::Text("%s: %s", axisNames[a], v3tos(axis[a]));
		}

		vec3_t cameraPos;
		ExtractCameraPosition(cameraPos, backEnd.viewParms.world.modelMatrix);
		ImGui::Text("Camera Position: %s", v3tos(cameraPos));

		for(int f = 1; f < tr.world->numfogs; ++f)
		{
			ImGui::Text("Fog: %s", v4tos(tr.world->fogs[f].surface));
		}
	}
	ImGui::End();
}

void World::ProcessWorld(world_t& world)
{
	{
		zppVertexBuffer.batchFirst = 0;
		zppIndexBuffer.batchFirst = 0;
		zppVertexBuffer.batchCount = 0;
		zppIndexBuffer.batchCount = 0;

		float* vtx = (float*)BeginBufferUpload(zppVertexBuffer.buffer);
		Index* idx = (Index*)BeginBufferUpload(zppIndexBuffer.buffer);

		int firstVertex = 0;
		int firstIndex = 0;
		for(int s = 0; s < world.numsurfaces; ++s)
		{
			msurface_t* const surf = &world.surfaces[s];
			if(surf->shader->numStages == 0 ||
				surf->shader->isDynamic)
			{
				continue;
			}

			tess.numVertexes = 0;
			tess.numIndexes = 0;
			tess.shader = surf->shader; // unsure if needed, but just in case
			R_TessellateSurface(surf->data);
			const int surfVertexCount = tess.numVertexes;
			const int surfIndexCount = tess.numIndexes;
			if(surfVertexCount <= 0 || surfIndexCount <= 0)
			{
				continue;
			}
			if(firstVertex + surfVertexCount >= zppVertexBuffer.totalCount ||
				firstIndex + surfIndexCount >= zppIndexBuffer.totalCount)
			{
				break;
			}

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

			firstVertex += surfVertexCount;
			firstIndex += surfIndexCount;
			tess.numVertexes = 0;
			tess.numIndexes = 0;
		}

		EndBufferUpload(zppVertexBuffer.buffer);
		EndBufferUpload(zppIndexBuffer.buffer);

		zppVertexBuffer.batchCount = firstVertex;
		zppIndexBuffer.batchCount = firstIndex;
		zppVertexBuffer.batchFirst = 0;
		zppIndexBuffer.batchFirst = 0;
	}

	statChunkCount = 1; // index 0 is invalid
	statIndexCount = 0;

	statBuffers.vertexBuffers.Rewind();
	statBuffers.indexBuffer.Rewind();

	statBuffers.vertexBuffers.BeginUpload();
	statBuffers.indexBuffer.BeginUpload();

	for(int s = 0; s < world.numsurfaces; ++s)
	{
		if(statChunkCount >= ARRAY_LEN(statChunks))
		{
			Q_assert(0);
			break;
		}

		msurface_t* const surf = &world.surfaces[s];
		surf->staticGeoChunk = 0;

		if(surf->shader->numStages == 0 ||
			surf->shader->isDynamic)
		{
			continue;
		}

		tess.numVertexes = 0;
		tess.numIndexes = 0;
		tess.shader = surf->shader; // needed by R_ComputeTexCoords at least
		R_TessellateSurface(surf->data);
		const int surfVertexCount = tess.numVertexes;
		const int surfIndexCount = tess.numIndexes;
		if(surfVertexCount <= 0 || surfIndexCount <= 0)
		{
			continue;
		}
		
		if(!statBuffers.vertexBuffers.CanAdd(surfVertexCount) ||
			!statBuffers.indexBuffer.CanAdd(surfIndexCount) ||
			statIndexCount + surfIndexCount > ARRAY_LEN(statIndices))
		{
			Q_assert(0);
			break;
		}

		for(int i = 0; i < surf->shader->numStages; ++i)
		{
			const shaderStage_t* const stage = surf->shader->stages[i];
			R_ComputeColors(stage, tess.svars[i], 0, tess.numVertexes);
			R_ComputeTexCoords(stage, tess.svars[i], 0, tess.numVertexes, qfalse);
		}

		// update GPU buffers
		statBuffers.vertexBuffers.Upload(0, surf->shader->numStages);
		statBuffers.indexBuffer.Upload();

		// update CPU buffer
		const uint32_t firstGPUVertex = statBuffers.vertexBuffers.batchFirst;
		const uint32_t firstCPUIndex = statIndexCount;
		uint32_t* cpuIndices = statIndices + firstCPUIndex;
		for(int i = 0; i < surfIndexCount; ++i)
		{
			cpuIndices[i] = tess.indexes[i] + firstGPUVertex;
		}
		statIndexCount += surfIndexCount;

		const uint32_t chunkIndex = statChunkCount++;
		StaticGeometryChunk& chunk = statChunks[chunkIndex];
		chunk.vertexCount = surfVertexCount;
		chunk.indexCount = surfIndexCount;
		chunk.firstGPUVertex = statBuffers.vertexBuffers.batchFirst;
		chunk.firstGPUIndex = statBuffers.indexBuffer.batchFirst;
		chunk.firstCPUIndex = firstCPUIndex;
		surf->staticGeoChunk = chunkIndex;

		statBuffers.vertexBuffers.EndBatch(surfVertexCount);
		statBuffers.indexBuffer.EndBatch(surfIndexCount);
	}

	statBuffers.vertexBuffers.EndUpload();
	statBuffers.indexBuffer.EndUpload();

	tess.numVertexes = 0;
	tess.numIndexes = 0;
}

void World::DrawSceneView(const drawSceneViewCommand_t& cmd)
{
	if(cmd.shouldClearColor)
	{
		const viewParms_t& vp = cmd.viewParms;
		const Rect rect(vp.viewportX, vp.viewportY, vp.viewportWidth, vp.viewportHeight);
		CmdClearColorTarget(grp.renderTarget, cmd.clearColor, &rect);
	}

	if(cmd.numDrawSurfs <= 0)
	{
		return;
	}

	backEnd.refdef = cmd.refdef;
	backEnd.viewParms = cmd.viewParms;

	Begin();

	CmdClearDepthTarget(depthTexture, 0.0f);

	boundVertexBuffers = BufferFamily::Invalid;
	boundIndexBuffer = BufferFamily::Invalid;

	// portals get the chance to write to depth and color before everyone else
	{
		const shader_t* shader = NULL;
		int fogNum;
		int entityNum;
		R_DecomposeSort(cmd.drawSurfs->shaderSort, &entityNum, &shader, &fogNum);
		if(shader->sort != SS_PORTAL)
		{
			DrawPrePass();
		}
	}

	grp.BeginRenderPass("3D Scene View", 1.0f, 0.5f, 0.5f);

	CmdBindRootSignature(grp.opaqueRootSignature);
	CmdBindDescriptorTable(grp.opaqueRootSignature, grp.descriptorTable);
	CmdBindRenderTargets(1, &grp.renderTarget, &depthTexture);
	HPipeline pso = RHI_MAKE_NULL_HANDLE();

	const drawSurf_t* drawSurfs = cmd.drawSurfs;
	const int surfCount = cmd.numDrawSurfs;
	const int opaqueCount = cmd.numDrawSurfs - cmd.numTranspSurfs;
	const int transpCount = cmd.numTranspSurfs;
	const double originalTime = backEnd.refdef.floatTime;

	const shader_t* shader = NULL;
	const shader_t* oldShader = NULL;
	int oldEntityNum = -1;
	bool oldHasStaticGeo = false;
	backEnd.currentEntity = &tr.worldEntity;

	GeometryBuffers& db = dynBuffers[GetFrameIndex()];
	db.vertexBuffers.BeginUpload();
	db.indexBuffer.BeginUpload();

	int ds;
	const drawSurf_t* drawSurf;
	for(ds = 0, drawSurf = drawSurfs; ds < surfCount; ++ds, ++drawSurf)
	{
		if(ds == opaqueCount)
		{
			EndBatch(pso);

			DrawFog();

			if(transpCount > 0)
			{
				CmdBindRootSignature(grp.opaqueRootSignature);
				CmdBindDescriptorTable(grp.opaqueRootSignature, grp.descriptorTable);
				CmdBindRenderTargets(1, &grp.renderTarget, &depthTexture);
				pso = RHI_MAKE_NULL_HANDLE();
				boundVertexBuffers = BufferFamily::Invalid;
				boundIndexBuffer = BufferFamily::Invalid;

				const TextureBarrier depthWriteBarrier(depthTexture, ResourceStates::DepthWriteBit);
				CmdBarrier(1, &depthWriteBarrier);
			}
		}

		int fogNum;
		int entityNum;
		R_DecomposeSort(drawSurf->sort, &entityNum, &shader, &fogNum);
		// @TODO:
		if(shader->numPipelines == 0 ||
			shader->pipelines[0].pipeline <= 0 ||
			shader->pipelines[0].numStages <= 0)
		{
			continue;
		}

		const bool hasStaticGeo = HasStaticGeo(drawSurf);
		const bool staticChanged = hasStaticGeo != oldHasStaticGeo;
		const bool shaderChanged = shader != oldShader;
		const bool entityChanged = entityNum != oldEntityNum;
		Q_assert(shader != NULL);
		if(!hasStaticGeo && !drawDynamic)
		{
			continue;
		}
		if(ds >= opaqueCount && !drawTransparents)
		{
			continue;
		}

		if(staticChanged || shaderChanged || entityChanged)
		{
			oldShader = shader;
			oldEntityNum = entityNum;
			oldHasStaticGeo = hasStaticGeo;
			EndBatch(pso);
			BeginBatch(shader, hasStaticGeo);
		}

		if(entityChanged)
		{
			UpdateEntityData(entityNum, originalTime);
		}

		int estVertexCount, estIndexCount;
		if(hasStaticGeo)
		{
			const StaticGeometryChunk& chunk = statChunks[drawSurf->staticGeoChunk];
			estVertexCount = chunk.vertexCount;
			estIndexCount = chunk.indexCount;
		}
		else
		{
			R_ComputeTessellatedSize(&estVertexCount, &estIndexCount, drawSurf->surface);
		}

		// >= shouldn't be necessary but it's the overflow check currently used within
		// R_TessellateSurface, so we have to be at least as aggressive as it is
		if(tess.numVertexes + estVertexCount >= SHADER_MAX_VERTEXES ||
			tess.numIndexes + estIndexCount >= SHADER_MAX_INDEXES)
		{
			EndBatch(pso);
			BeginBatch(tess.shader, batchHasStaticGeo);
		}

		if(hasStaticGeo)
		{
			const StaticGeometryChunk& chunk = statChunks[drawSurf->staticGeoChunk];
			memcpy(tess.indexes + tess.numIndexes, statIndices + chunk.firstCPUIndex, chunk.indexCount * sizeof(uint32_t));
			tess.numIndexes += chunk.indexCount;
		}
		else
		{
			const int firstVertex = tess.numVertexes;
			const int firstIndex = tess.numIndexes;
			R_TessellateSurface(drawSurf->surface);
			const int numVertexes = tess.numVertexes - firstVertex;
			const int numIndexes = tess.numIndexes - firstIndex;
			Q_assert(numVertexes >= 0);
			Q_assert(numIndexes >= 0);
			Q_assert(estVertexCount == numVertexes);
			Q_assert(estIndexCount == numIndexes);
			RB_DeformTessGeometry(firstVertex, numVertexes, firstIndex, numIndexes);
			for(int s = 0; s < shader->numStages; ++s)
			{
				R_ComputeColors(shader->stages[s], tess.svars[s], firstVertex, numVertexes);
				R_ComputeTexCoords(shader->stages[s], tess.svars[s], firstVertex, numVertexes, qfalse);
			}
		}
	}

	backEnd.refdef.floatTime = originalTime;

	EndBatch(pso);

	if(transpCount <= 0)
	{
		DrawFog();
	}

	db.vertexBuffers.EndUpload();
	db.indexBuffer.EndUpload();

	grp.EndRenderPass();

	// @TODO: go back to the world model-view matrix, restore depth range
}

void World::BindVertexBuffers(bool staticGeo, uint32_t firstStage, uint32_t stageCount)
{
	const BufferFamily::Id type = staticGeo ? BufferFamily::Static : BufferFamily::Dynamic;
	if(type == boundVertexBuffers &&
		firstStage == boundStaticVertexBuffersFirst &&
		stageCount == boundStaticVertexBuffersCount)
	{
		return;
	}

	VertexBuffers& vb = staticGeo ? statBuffers.vertexBuffers : dynBuffers[GetFrameIndex()].vertexBuffers;
	const uint32_t count = VertexBuffers::BaseCount + VertexBuffers::StageCount * stageCount;
	HBuffer buffers[VertexBuffers::BufferCount];
	memcpy(buffers, vb.buffers, VertexBuffers::BaseCount * sizeof(HBuffer));
	memcpy(
		buffers + VertexBuffers::BaseCount,
		vb.buffers + VertexBuffers::BaseCount + VertexBuffers::StageCount * firstStage,
		VertexBuffers::StageCount * stageCount * sizeof(HBuffer));
	CmdBindVertexBuffers(count, buffers, vb.strides, NULL);
	boundVertexBuffers = type;
	boundStaticVertexBuffersFirst = firstStage;
	boundStaticVertexBuffersCount = stageCount;
}

void World::BindIndexBuffer(bool staticGeo)
{
	const BufferFamily::Id type = staticGeo ? BufferFamily::Static : BufferFamily::Dynamic;
	if(type == boundIndexBuffer)
	{
		return;
	}

	IndexBuffer& ib = staticGeo ? statBuffers.indexBuffer : dynBuffers[GetFrameIndex()].indexBuffer;
	CmdBindIndexBuffer(ib.buffer, indexType, 0);
	boundIndexBuffer = type;
}

void World::DrawFog()
{
	// fog 0 is invalid
	if(!drawFog ||
		tr.world == NULL ||
		tr.world->numfogs <= 1)
	{
		return;
	}

	grp.BeginRenderPass("Fog", 0.25f, 0.125f, 0.0f);

	CmdBindPipeline(fogOutsidePipeline);
	CmdBindRootSignature(fogRootSignature);
	CmdBindDescriptorTable(fogRootSignature, fogDescriptorTable);

	const uint32_t stride = sizeof(vec3_t);
	CmdBindVertexBuffers(1, &boxVertexBuffer, &stride, NULL);
	CmdBindIndexBuffer(boxIndexBuffer, IndexType::UInt32, 0);

	CmdBindRenderTargets(1, &grp.renderTarget, NULL);
	const TextureBarrier depthReadBarrier(depthTexture, ResourceStates::PixelShaderAccessBit);
	CmdBarrier(1, &depthReadBarrier);

	int insideIndex = -1;
	for(int f = 1; f < tr.world->numfogs; ++f)
	{
		const fog_t& fog = tr.world->fogs[f];

		bool inside = true;
		for(int a = 0; a < 3; ++a)
		{
			if(backEnd.viewParms.orient.origin[a] <= fog.bounds[0][a] ||
				backEnd.viewParms.orient.origin[a] >= fog.bounds[1][a])
			{
				inside = false;
				break;
			}
		}

		if(inside)
		{
			insideIndex = f;
			break;
		}
	}

	for(int f = 1; f < tr.world->numfogs; ++f)
	{
		if(f == insideIndex)
		{
			continue;
		}

		const fog_t& fog = tr.world->fogs[f];

		FogVertexRC vertexRC = {};
		memcpy(vertexRC.modelViewMatrix, backEnd.viewParms.world.modelMatrix, sizeof(vertexRC.modelViewMatrix));
		memcpy(vertexRC.projectionMatrix, backEnd.viewParms.projectionMatrix, sizeof(vertexRC.projectionMatrix));
		VectorCopy(fog.bounds[0], vertexRC.boxMin);
		VectorCopy(fog.bounds[1], vertexRC.boxMax);
		CmdSetRootConstants(fogRootSignature, ShaderStage::Vertex, &vertexRC);

		FogPixelRC pixelRC = {};
		VectorCopy(fog.parms.color, pixelRC.colorDepth);
		pixelRC.colorDepth[3] = fog.parms.depthForOpaque;
		CmdSetRootConstants(fogRootSignature, ShaderStage::Pixel, &pixelRC);

		CmdDrawIndexed(36, 0, 0);
	}

	if(insideIndex > 0)
	{
		CmdBindPipeline(fogInsidePipeline);
		CmdBindRootSignature(fogRootSignature);
		CmdBindDescriptorTable(fogRootSignature, fogDescriptorTable);

		const fog_t& fog = tr.world->fogs[insideIndex];

		FogVertexRC vertexRC = {};
		memcpy(vertexRC.modelViewMatrix, backEnd.viewParms.world.modelMatrix, sizeof(vertexRC.modelViewMatrix));
		memcpy(vertexRC.projectionMatrix, backEnd.viewParms.projectionMatrix, sizeof(vertexRC.projectionMatrix));
		VectorCopy(fog.bounds[0], vertexRC.boxMin);
		VectorCopy(fog.bounds[1], vertexRC.boxMax);
		CmdSetRootConstants(fogRootSignature, ShaderStage::Vertex, &vertexRC);

		FogPixelRC pixelRC = {};
		VectorCopy(fog.parms.color, pixelRC.colorDepth);
		pixelRC.colorDepth[3] = fog.parms.depthForOpaque;
		CmdSetRootConstants(fogRootSignature, ShaderStage::Pixel, &pixelRC);

		CmdDrawIndexed(36, 0, 0);
	}

	grp.EndRenderPass();
}
