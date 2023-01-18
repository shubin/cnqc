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

struct ZPPVertexRC
{
	float mvp[16];
};

struct DynamicVertexRC
{
	float modelViewMatrix[16];
	float projectionMatrix[16];
	float clipPlane[4];
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
	matrix modelViewMatrix;
	matrix projectionMatrix;
	float4 clipPlane;
};

#define STAGE_ATTRIBS(Index) \
	float2 texCoords##Index : TEXCOORD##Index; \
	float4 color##Index : COLOR##Index;

struct VIn
{
	float3 position : POSITION;
	float3 normal : NORMAL;
	STAGE_ATTRIBS(0)
	STAGE_ATTRIBS(1)
	STAGE_ATTRIBS(2)
	STAGE_ATTRIBS(3)
	STAGE_ATTRIBS(4)
	STAGE_ATTRIBS(5)
	STAGE_ATTRIBS(6)
	STAGE_ATTRIBS(7)
};

struct VOut
{
	float4 position : SV_Position;
	float3 normal : NORMAL;
	STAGE_ATTRIBS(0)
	STAGE_ATTRIBS(1)
	STAGE_ATTRIBS(2)
	STAGE_ATTRIBS(3)
	STAGE_ATTRIBS(4)
	STAGE_ATTRIBS(5)
	STAGE_ATTRIBS(6)
	STAGE_ATTRIBS(7)
	float clipDist : SV_ClipDistance0;
};

#undef STAGE_ATTRIBS
#define STAGE_ATTRIBS(Index) \
	output.texCoords##Index = input.texCoords##Index; \
	output.color##Index = input.color##Index;

VOut main(VIn input)
{
	float4 positionVS = mul(modelViewMatrix, float4(input.position.xyz, 1));

	VOut output;
	output.position = mul(projectionMatrix, positionVS);
	output.normal = input.normal;
	STAGE_ATTRIBS(0)
	STAGE_ATTRIBS(1)
	STAGE_ATTRIBS(2)
	STAGE_ATTRIBS(3)
	STAGE_ATTRIBS(4)
	STAGE_ATTRIBS(5)
	STAGE_ATTRIBS(6)
	STAGE_ATTRIBS(7)
	output.clipDist = dot(positionVS, clipPlane);

	return output;
}
)grml";

static const char* dyn_ps = R"grml(
cbuffer RootConstants
{
	uint textureIndex;
	uint samplerIndex;
};

#define STAGE_ATTRIBS(Index) \
	float2 texCoords##Index : TEXCOORD##Index; \
	float4 color##Index : COLOR##Index;

struct VOut
{
	float4 position : SV_Position;
	float3 normal : NORMAL;
	STAGE_ATTRIBS(0)
	STAGE_ATTRIBS(1)
	STAGE_ATTRIBS(2)
	STAGE_ATTRIBS(3)
	STAGE_ATTRIBS(4)
	STAGE_ATTRIBS(5)
	STAGE_ATTRIBS(6)
	STAGE_ATTRIBS(7)
};

Texture2D textures2D[2048] : register(t0);
SamplerState samplers[2] : register(s0);

float4 main(VOut input) : SV_TARGET
{
	return textures2D[textureIndex].Sample(samplers[samplerIndex], input.texCoords0) * input.color0;
}
)grml";


static bool drawPrePass = true;


static bool HasStaticGeo(const drawSurf_t* drawSurf)
{
	return
		drawSurf->msurface != NULL &&
		drawSurf->msurface->numIndexes > 0 &&
		drawSurf->msurface->numVertexes > 0;
}

static void UpdateModelViewMatrix(int entityNum, double originalTime)
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
	{
		RootSignatureDesc desc = grp.rootSignatureDesc;
		desc.name = "dynamic";
		desc.usingVertexBuffers = true;
		desc.constants[ShaderStage::Vertex].byteCount = sizeof(DynamicVertexRC);
		desc.constants[ShaderStage::Pixel].byteCount = sizeof(DynamicPixelRC);
		desc.samplerVisibility = ShaderStages::PixelBit;
		desc.genericVisibility = ShaderStages::VertexBit | ShaderStages::PixelBit;
		rootSignature = CreateRootSignature(desc);
	}
	{
		uint32_t a = 0;
		GraphicsPipelineDesc desc("dynamic", rootSignature);
		desc.vertexShader = CompileVertexShader(dyn_vs);
		desc.pixelShader = CompilePixelShader(dyn_ps);
		desc.vertexLayout.AddAttribute(a++, ShaderSemantic::Position, DataType::Float32, 3, 0);
		desc.vertexLayout.AddAttribute(a++, ShaderSemantic::Normal, DataType::Float32, 2, 0);
		for(int s = 0; s < MAX_SHADER_STAGES; ++s)
		{
			desc.vertexLayout.AddAttribute(a++, ShaderSemantic::TexCoord, DataType::Float32, 2, 0);
			desc.vertexLayout.AddAttribute(a++, ShaderSemantic::Color, DataType::UNorm8, 4, 0);
		}
		desc.depthStencil.depthComparison = ComparisonFunction::GreaterEqual;
		desc.depthStencil.depthStencilFormat = TextureFormat::Depth32_Float;
		desc.depthStencil.enableDepthTest = true;
		desc.depthStencil.enableDepthWrites = true;
		desc.rasterizer.cullMode = CullMode::Back;
		desc.AddRenderTarget(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO, TextureFormat::RGBA32_UNorm);
		pipeline = CreateGraphicsPipeline(desc);
	}
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
}

void World::BeginFrame()
{
	dynBuffers[GetFrameIndex()].Rewind();

	boundVertexBuffers = BufferFamily::Invalid;
	boundIndexBuffer = BufferFamily::Invalid;
	boundStaticVertexBuffersCount = UINT32_MAX;
}

void World::Begin()
{
	if(grp.renderMode == RenderMode::World)
	{
		return;
	}

	// copy backEnd.viewParms.projectionMatrix
	CmdSetViewportAndScissor(backEnd.viewParms);

	TextureBarrier tb(depthTexture, ResourceStates::DepthWriteBit);
	CmdBarrier(1, &tb);

	// @TODO: evaluate later whether binding the color target here is OK?
	CmdBindRenderTargets(0, NULL, &depthTexture);
	CmdClearDepthTarget(depthTexture, 0.0f);

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
		CmdClearColorTarget();
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
	if(!drawPrePass ||
		tr.world == NULL ||
		zppIndexBuffer.batchCount == 0 ||
		zppVertexBuffer.batchCount == 0)
	{
		return;
	}

	CmdBindRootSignature(zppRootSignature);
	CmdBindPipeline(zppPipeline);
	CmdBindDescriptorTable(zppRootSignature, zppDescriptorTable);

	float mvp[16];
	R_MultMatrix(backEnd.viewParms.world.modelMatrix, backEnd.viewParms.projectionMatrix, mvp);
	CmdSetRootConstants(zppRootSignature, ShaderStage::Vertex, mvp);
	CmdBindPipeline(zppPipeline);
	CmdBindIndexBuffer(zppIndexBuffer.buffer, indexType, 0);
	const uint32_t vertexStride = 4 * sizeof(float);
	CmdBindVertexBuffers(1, &zppVertexBuffer.buffer, &vertexStride, NULL);
	CmdDrawIndexed(zppIndexBuffer.batchCount, 0, 0);
	boundVertexBuffers = BufferFamily::PrePass;
	boundIndexBuffer = BufferFamily::PrePass;
}

void World::BeginBatch(const shader_t* shader, bool hasStaticGeo)
{
	tess.numVertexes = 0;
	tess.numIndexes = 0;
	tess.shader = shader;
	batchHasStaticGeo = hasStaticGeo;
}

void World::EndBatch()
{
	if(tess.numVertexes <= 0 ||
		tess.numIndexes <= 0 ||
		tess.shader->numStages == 0)
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

	if(!batchHasStaticGeo)
	{
		db.vertexBuffers.Upload(0, 1);
	}
	db.indexBuffer.Upload();

	DynamicVertexRC vertexRC;
	memcpy(vertexRC.modelViewMatrix, backEnd.orient.modelMatrix, sizeof(vertexRC.modelViewMatrix));
	memcpy(vertexRC.projectionMatrix, backEnd.viewParms.projectionMatrix, sizeof(vertexRC.projectionMatrix));
	memcpy(vertexRC.clipPlane, clipPlane, sizeof(vertexRC.clipPlane));
	CmdSetRootConstants(rootSignature, ShaderStage::Vertex, &vertexRC);

	DynamicPixelRC pixelRC;
	pixelRC.textureIndex = tess.shader->stages[0]->bundle.image[0]->textureIndex;
	pixelRC.samplerIndex = 0;
	Q_assert(pixelRC.textureIndex > 0);
	CmdSetRootConstants(rootSignature, ShaderStage::Pixel, &pixelRC);

	BindVertexBuffers(batchHasStaticGeo, 4);
	BindIndexBuffer(false);
	CmdDrawIndexed(tess.numIndexes, db.indexBuffer.batchFirst, batchHasStaticGeo ? 0 : db.vertexBuffers.batchFirst);

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
			surf->numVertexes = 0;
			surf->numIndexes = 0;
			surf->firstVertex = 0;
			surf->firstIndex = 0;

			// @TODO: make sure it's static as well!
			if(surf->shader->numStages == 0)
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

			surf->numVertexes = tess.numVertexes;
			surf->numIndexes = tess.numIndexes;
			surf->firstVertex = firstVertex;
			surf->firstIndex = firstIndex;
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

	statBuffers.vertexBuffers.Rewind();
	statBuffers.indexBuffer.Rewind();

	statBuffers.vertexBuffers.BeginUpload();
	statBuffers.indexBuffer.BeginUpload();

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
			!statBuffers.indexBuffer.CanAdd(surfIndexCount))
		{
			break;
		}

		for(int i = 0; i < surf->shader->numStages; ++i)
		{
			const shaderStage_t* const stage = surf->shader->stages[i];
			R_ComputeColors(stage, tess.svars[i], 0, tess.numVertexes);
			R_ComputeTexCoords(stage, tess.svars[i], 0, tess.numVertexes, qfalse);
		}

		statBuffers.vertexBuffers.Upload(0, surf->shader->numStages);
		statBuffers.indexBuffer.Upload();

		surf->numVertexes = surfVertexCount;
		surf->numIndexes = surfIndexCount;
		surf->firstVertex = statBuffers.vertexBuffers.batchFirst;
		surf->firstIndex = statBuffers.indexBuffer.batchFirst;

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
	if(cmd.numDrawSurfs <= 0)
	{
		return;
	}

	backEnd.refdef = cmd.refdef;
	backEnd.viewParms = cmd.viewParms;

	Begin();

	DrawPrePass();

	CmdBindRootSignature(rootSignature);
	CmdBindPipeline(pipeline);
	CmdBindDescriptorTable(rootSignature, grp.descriptorTable);

	const HTexture swapChain = GetSwapChainTexture();
	CmdBindRenderTargets(1, &swapChain, &depthTexture);

	const drawSurf_t* drawSurfs = cmd.drawSurfs;
	const int opaqueCount = cmd.numDrawSurfs - cmd.numTranspSurfs;
	//const int transpCount = cmd.numTranspSurfs;
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
	for(ds = 0, drawSurf = drawSurfs; ds < opaqueCount; ++ds, ++drawSurf)
	{
		int fogNum;
		int entityNum;
		R_DecomposeSort(drawSurf->sort, &entityNum, &shader, &fogNum);
		const bool hasStaticGeo = HasStaticGeo(drawSurf);
		const bool staticChanged = hasStaticGeo != oldHasStaticGeo;
		const bool shaderChanged = shader != oldShader;
		const bool entityChanged = entityNum != oldEntityNum;
		Q_assert(shader != NULL);

		if(staticChanged || shaderChanged || entityChanged)
		{
			oldShader = shader;
			oldEntityNum = entityNum;
			oldHasStaticGeo = hasStaticGeo;
			EndBatch();
			BeginBatch(shader, hasStaticGeo);
		}

		if(entityChanged)
		{
			UpdateModelViewMatrix(entityNum, originalTime);
		}

		int estVertexCount, estIndexCount;
		R_ComputeTessellatedSize(&estVertexCount, &estIndexCount, drawSurf->surface);
		// >= shouldn't be necessary but it's the overflow check currently used within
		// R_TessellateSurface, so we have to be at least as aggressive as it is
		if(tess.numVertexes + estVertexCount >= SHADER_MAX_VERTEXES ||
			tess.numIndexes + estIndexCount >= SHADER_MAX_INDEXES)
		{
			EndBatch();
			BeginBatch(tess.shader, batchHasStaticGeo);
		}

		if(hasStaticGeo)
		{
			// @TODO: grab a CPU-resident pre-computed index buffer instead of tessellating on demand...
			const int firstCPUVertex = tess.numVertexes;
			const int firstCPUIndex = tess.numIndexes;
			R_TessellateSurface(drawSurf->surface);
			const int numIndexes = tess.numIndexes - firstCPUIndex;
			//Q_assert(numIndexes == drawSurf->msurface->numIndexes); // will fail on occasion...
			if(numIndexes == drawSurf->msurface->numIndexes)
			{
				const int firstGPUVertex = drawSurf->msurface->firstVertex;
				for(int i = firstCPUIndex; i < firstCPUIndex + numIndexes; ++i)
				{
					tess.indexes[i] -= firstCPUVertex;
					tess.indexes[i] += firstGPUVertex;
				}
			}
			else
			{
				// cancel this surface to not mess things up
				tess.numVertexes = firstCPUVertex;
				tess.numIndexes = firstCPUIndex;
			}
		}
		else
		{
			const int firstVertex = tess.numVertexes;
			const int firstIndex = tess.numIndexes;
			R_TessellateSurface(drawSurf->surface);
			const int numVertexes = tess.numVertexes - firstVertex;
			const int numIndexes = tess.numIndexes - firstIndex;
			Q_assert(estVertexCount == numVertexes);
			Q_assert(estIndexCount == numIndexes);
			RB_DeformTessGeometry(firstVertex, numVertexes, firstIndex, numIndexes);
			for(int s = 0; s < shader->numStages; ++s)
			{
				R_ComputeColors(shader->stages[s], tess.svars[s], firstVertex, numVertexes);
				R_ComputeTexCoords(shader->stages[s], tess.svars[s], firstVertex, numVertexes, qfalse);
			}
		}

#if 0
		if(hasStaticGeo)
		{
			DynamicVertexRC vertexRC;
			R_MultMatrix(backEnd.orient.modelMatrix, backEnd.viewParms.projectionMatrix, vertexRC.mvp);
			memcpy(vertexRC.clipPlane, clipPlane, sizeof(vertexRC.clipPlane));
			CmdSetRootConstants(rootSignature, ShaderStage::Vertex, &vertexRC);

			DynamicPixelRC pixelRC;
			pixelRC.textureIndex = tess.shader->stages[0]->bundle.image[0]->textureIndex;
			pixelRC.samplerIndex = 0;
			CmdSetRootConstants(rootSignature, ShaderStage::Pixel, &pixelRC);

			BindVertexBuffers(true, 4);
			BindIndexBuffer(true);
			CmdDrawIndexed(drawSurf->msurface->numIndexes, drawSurf->msurface->firstIndex, drawSurf->msurface->firstVertex);

			tess.numVertexes = 0;
			tess.numIndexes = 0;
		}
		else
		{
			const int firstVertex = 0;
			const int firstIndex = 0;
			R_TessellateSurface(drawSurf->surface);
			const int numVertexes = tess.numVertexes - firstVertex;
			const int numIndexes = tess.numIndexes - firstIndex;
			RB_DeformTessGeometry(firstVertex, numVertexes, firstIndex, numIndexes);
			for(int s = 0; s < shader->numStages; ++s)
			{
				R_ComputeColors(shader->stages[s], tess.svars[s], firstVertex, numVertexes);
				R_ComputeTexCoords(shader->stages[s], tess.svars[s], firstVertex, numVertexes, qfalse);
			}
			EndBatch();
			BeginBatch(tess.shader, batchHasStaticGeo);
		}
#endif
	}

	backEnd.refdef.floatTime = originalTime;

	EndBatch();

	db.vertexBuffers.EndUpload();
	db.indexBuffer.EndUpload();

	// @TODO: go back to the world model-view matrix, restore depth range
}

void World::BindVertexBuffers(bool staticGeo, uint32_t count)
{
	const BufferFamily::Id type = staticGeo ? BufferFamily::Static : BufferFamily::Dynamic;
	if(type == boundVertexBuffers && count == boundStaticVertexBuffersCount)
	{
		return;
	}

	VertexBuffers& vb = staticGeo ? statBuffers.vertexBuffers : dynBuffers[GetFrameIndex()].vertexBuffers;
	CmdBindVertexBuffers(count, vb.buffers, vb.strides, NULL);
	boundVertexBuffers = type;
	boundStaticVertexBuffersCount = count;
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
