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
// Gameplay Rendering Pipeline - private declarations


#pragma once


#include "tr_local.h"
#include "rhi_local.h"


using namespace RHI;


struct GeometryBuffer
{
	void Init(uint32_t count_, uint32_t stride_)
	{
		buffer = RHI_MAKE_NULL_HANDLE();
		byteCount = count_ * stride_;
		stride = stride_;
		totalCount = count_;
		batchFirst = 0;
		batchCount = 0;
	}

	bool CanAdd(uint32_t count_)
	{
		return batchFirst + batchCount + count_ <= totalCount;
	}

	void EndBatch()
	{
		batchFirst += batchCount;
		batchCount = 0;
	}

	void EndBatch(uint32_t size)
	{
		batchFirst += size;
		batchCount = 0;
	}

	void Rewind()
	{
		batchFirst = 0;
		batchCount = 0;
	}

	HBuffer buffer = RHI_MAKE_NULL_HANDLE();
	uint32_t byteCount = 0;
	uint32_t stride = 0;
	uint32_t totalCount = 0;
	uint32_t batchFirst = 0;
	uint32_t batchCount = 0;
};

struct World
{
	void Init();
	void BeginFrame();
	void Begin();
	void DrawPrePass();
	void DrawBatch();
	void DrawGUI();
	void ProcessWorld(world_t& world);
	void DrawSceneView(const drawSceneViewCommand_t& cmd);

	typedef uint32_t Index;
	const IndexType::Id indexType = IndexType::UInt32;

	// @TODO: in the future, once backEnd gets nuked
	//trRefdef_t refdef;
	//viewParms_t viewParms;

	HTexture depthTexture;
	uint32_t depthTextureIndex;

	float clipPlane[4];

	// Z pre-pass
	HRootSignature zppRootSignature;
	HDescriptorTable zppDescriptorTable;
	HPipeline zppPipeline; // @TODO: 1 per cull type
	GeometryBuffer zppIndexBuffer;
	GeometryBuffer zppVertexBuffer;

	// dynamic
	struct DynamicBuffers
	{
		void Rewind()
		{
			indices.Rewind();
			positions.Rewind();
			normals.Rewind();
			for(uint32_t s = 0; s < ARRAY_LEN(stages); ++s)
			{
				stages[s].Rewind();
			}
		}

		struct Stage
		{
			void Rewind()
			{
				texCoords.Rewind();
				colors.Rewind();
			}

			GeometryBuffer texCoords;
			GeometryBuffer colors;
		};
		GeometryBuffer indices;
		GeometryBuffer positions;
		GeometryBuffer normals;
		Stage stages[MAX_SHADER_STAGES];
	};
	HRootSignature dynRootSignature;
	HPipeline dynPipeline; // @TODO: 1 per cull type
	DynamicBuffers dynBuffers[FrameCount];
};

struct UI
{
	void Init();
	void BeginFrame();
	void Begin();
	void DrawBatch();
	void UISetColor(const uiSetColorCommand_t& cmd);
	void UIDrawQuad(const uiDrawQuadCommand_t& cmd);
	void UIDrawTriangle(const uiDrawTriangleCommand_t& cmd);

	// 32-bit needed until the render logic is fixed!
	typedef uint32_t Index;
	const IndexType::Id indexType = IndexType::UInt32;

#pragma pack(push, 1)
	struct Vertex
	{
		vec2_t position;
		vec2_t texCoords;
		uint32_t color;
	};
#pragma pack(pop)
	int maxIndexCount;
	int maxVertexCount;
	int firstIndex;
	int firstVertex;
	int indexCount;
	int vertexCount;
	HRootSignature rootSignature;
	HPipeline pipeline;
	HBuffer indexBuffer;
	HBuffer vertexBuffer;
	Index* indices;
	Vertex* vertices;
	uint32_t color;
	const shader_t* shader;
};

struct MipMapGenerator
{
	void Init();
	void GenerateMipMaps(HTexture texture);

	struct Stage
	{
		enum Id
		{
			Start,      // gamma to linear
			DownSample, // down sample on 1 axis
			End,        // linear to gamma
			Count
		};

		HRootSignature rootSignature;
		HDescriptorTable descriptorTable;
		HPipeline pipeline;
	};

	struct MipSlice
	{
		enum Id
		{
			Float16_0,
			Float16_1,
			UNorm_0,
			Count
		};
	};

	HTexture textures[MipSlice::Count];
	Stage stages[3];
};

struct ImGUI
{
	void Init();
	void RegisterFontAtlas();
	void Draw();

	struct FrameResources
	{
		HBuffer indexBuffer;
		HBuffer vertexBuffer;
	};

	HRootSignature rootSignature;
	HPipeline pipeline;
	HTexture fontAtlas;
	FrameResources frameResources[FrameCount];
};

struct RenderMode
{
	enum Id
	{
		None,
		UI,
		World,
		ImGui,
		Count
	};
};

struct GRP : IRenderPipeline
{
	void Init() override;
	void ShutDown(bool fullShutDown) override;
	void BeginFrame() override;
	void EndFrame() override;
	void AddDrawSurface(const surfaceType_t* surface, const shader_t* shader) override;
	void CreateTexture(image_t* image, int mipCount, int width, int height) override;
	void UpoadTextureAndGenerateMipMaps(image_t* image, const byte* data) override;
	void BeginTextureUpload(MappedTexture& mappedTexture, image_t* image) override;
	void EndTextureUpload(image_t* image) override;
	void ProcessWorld(world_t& world) override;

	void UISetColor(const uiSetColorCommand_t& cmd) override { ui.UISetColor(cmd); }
	void UIDrawQuad(const uiDrawQuadCommand_t& cmd) override { ui.UIDrawQuad(cmd); }
	void UIDrawTriangle(const uiDrawTriangleCommand_t& cmd) override { ui.UIDrawTriangle(cmd); }
	void DrawSceneView(const drawSceneViewCommand_t& cmd) override { ui.DrawBatch(); world.DrawSceneView(cmd); }

	uint32_t RegisterTexture(HTexture htexture);

	UI ui;
	World world;
	MipMapGenerator mipMapGen;
	ImGUI imgui;
	bool firstInit = true;
	RenderMode::Id renderMode;

	RootSignatureDesc rootSignatureDesc;
	HRootSignature rootSignature;
	HDescriptorTable descriptorTable;
	uint32_t textureIndex;
	HSampler samplers[2];
};

extern GRP grp;

inline void CmdSetViewportAndScissor(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	CmdSetViewport(x, y, w, h);
	CmdSetScissor(x, y, w, h);
}

inline void CmdSetViewportAndScissor(const viewParms_t& vp)
{
	CmdSetViewportAndScissor(vp.viewportX, vp.viewportY, vp.viewportWidth, vp.viewportHeight);
}
