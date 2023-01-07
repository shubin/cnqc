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
// Gameplay Rendering Pipeline - main interface


#pragma once


#include "tr_local.h"


using namespace RHI;


struct ui_t
{
	void Init();
	void BeginFrame();
	void Begin();
	void Draw();
	const void* SetColor(const void* data);
	const void* StretchPic(const void* data);
	const void* Triangle(const void* data);

	typedef uint32_t index_t;
#pragma pack(push, 1)
	struct vertex_t
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
	HDescriptorTable descriptorTable;
	HPipeline pipeline;
	HBuffer indexBuffer;
	HBuffer vertexBuffer;
	index_t* indices; // @TODO: 16-bit indices
	vertex_t* vertices;
	uint32_t color;
	const shader_t* shader;
};

struct mipMapGen_t
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

enum projection_t
{
	PROJECTION_NONE,
	PROJECTION_2D,
	PROJECTION_3D
};

struct grp_t
{
	ui_t ui;
	mipMapGen_t mipMapGen;
	projection_t projection;
	uint32_t textureIndex;
	HSampler samplers[2];
};

extern grp_t grp;
