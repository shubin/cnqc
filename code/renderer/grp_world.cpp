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


void World::Init()
{
	{
		TextureDesc desc("depth buffer", glConfig.vidWidth, glConfig.vidHeight);
		desc.shortLifeTime = true;
		desc.initialState = ResourceStates::DepthWriteBit;
		desc.allowedState = ResourceStates::DepthAccessBits;
		desc.format = TextureFormat::DepthStencil32_UNorm24_UInt8;
		desc.SetClearDepthStencil(0.0f, 0);
		depthTexture = CreateTexture(desc);
	}

	if(!grp.firstInit)
	{
		return;
	}

	{
		RootSignatureDesc desc("Z pre-pass");
		desc.usingVertexBuffers = true;
		desc.constants[ShaderStage::Vertex].byteCount = 16 * sizeof(float); // MVP matrix
		desc.constants[ShaderStage::Pixel].byteCount = 0;
		desc.samplerVisibility = ShaderStages::None;
		desc.genericVisibility = ShaderStages::None;
		rootSignature = CreateRootSignature(desc);
	}
	{
		descriptorTable = CreateDescriptorTable(DescriptorTableDesc("Z pre-pass", rootSignature));
	}
	{
		GraphicsPipelineDesc desc("Z pre-pass", rootSignature);
		desc.vertexShader = CompileVertexShader(zpp_vs);
		desc.pixelShader = CompilePixelShader(zpp_ps);
		desc.vertexLayout.AddAttribute(0, ShaderSemantic::Position, DataType::Float32, 4, 0);
		desc.depthStencil.depthComparison = ComparisonFunction::GreaterEqual;
		desc.depthStencil.depthStencilFormat = TextureFormat::DepthStencil32_UNorm24_UInt8;
		desc.depthStencil.enableDepthTest = true;
		desc.depthStencil.enableDepthWrites = true;
		desc.rasterizer.cullMode = CullMode::Back;
		pipeline = CreateGraphicsPipeline(desc);
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
}

void World::BeginFrame()
{
	Begin();
}

void World::Begin()
{
	if(grp.renderMode == RenderMode::World)
	{
		return;
	}

	// @TODO: draw current batch...

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

	// @TODO: grab the right rects...
	CmdSetViewport(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	CmdSetScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	CmdBindRootSignature(rootSignature);
	CmdBindPipeline(pipeline);
	CmdBindDescriptorTable(rootSignature, descriptorTable);
	CmdBindRenderTargets(0, NULL, &depthTexture);

	float mvp[16];
	R_MultMatrix(tr.viewParms.world.modelMatrix, tr.viewParms.projectionMatrix, mvp);
	CmdSetRootConstants(rootSignature, ShaderStage::Vertex, mvp);
	CmdBindPipeline(pipeline);
	CmdBindIndexBuffer(prePassGeo.indexBuffer, indexType, 0);
	const uint32_t vertexStride = 4 * sizeof(float);
	CmdBindVertexBuffers(1, &prePassGeo.vertexBuffer, &vertexStride, NULL);
	CmdDrawIndexed(prePassGeo.indexCount, 0, 0);

#if 0
	image_t* image = NULL;
	for(int i = 0; i < tr.numImages; ++i)
	{
		if(tr.images[i]->texture == depthTexture)
		{
			image = tr.images[i];
			break;
		}
	}
	if(image != NULL)
	{
		TextureBarrier tb(depthTexture, ResourceStates::DepthReadBit);
		CmdBarrier(1, &tb);
		if(ImGui::Begin("depth"))
		{
			ImGui::Image((ImTextureID)image->textureIndex, ImVec2(640, 360));
		}
		ImGui::End();
	}
#endif
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

		// @TODO:
		//UploadVertexData(firstVertex, firstIndex, surfVertexCount, surfIndexCount);
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
