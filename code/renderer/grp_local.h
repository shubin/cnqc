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


struct World
{
	void Init();
	void BeginFrame();
	void Begin();
	void DrawPrePass();
	void DrawGUI();
	void ProcessWorld(world_t& world);

	typedef uint32_t Index;
	const IndexType::Id indexType = IndexType::UInt32;

	struct GeometryBuffer
	{
		void EndBatch()
		{
			firstIndex += indexCount;
			firstVertex += vertexCount;
			indexCount = 0;
			vertexCount = 0;
		}

		HBuffer indexBuffer;
		HBuffer vertexBuffer;
		int maxIndexCount;
		int maxVertexCount;
		int firstIndex;
		int firstVertex;
		int indexCount;
		int vertexCount;
	};

	//GeometryBuffer dynamicGeo;
	//GeometryBuffer staticGeo;
	GeometryBuffer prePassGeo;

	HTexture depthTexture;
	uint32_t depthTextureIndex;

	// Z pre-pass
	HRootSignature rootSignature;
	HDescriptorTable descriptorTable;
	HPipeline pipeline; // @TODO: 1 per cull type
};

struct UI
{
	void Init();
	void BeginFrame();
	void Begin();
	void DrawBatch();
	const void* SetColor(const void* data);
	const void* StretchPic(const void* data);
	const void* Triangle(const void* data);

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

struct GRP
{
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
