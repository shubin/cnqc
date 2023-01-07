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


struct ui_t
{
	void Begin2D();
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
	RHI::HRootSignature rootSignature;
	RHI::HDescriptorTable descriptorTable;
	RHI::HPipeline pipeline;
	RHI::HBuffer indexBuffer;
	RHI::HBuffer vertexBuffer;
	index_t* indices; // @TODO: 16-bit indices
	vertex_t* vertices;
	uint32_t color;
	const shader_t* shader;
};

struct mipMapGen_t
{
	RHI::HRootSignature rootSignature;
	RHI::HDescriptorTable descriptorTable;
	RHI::HPipeline pipeline;
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
	projection_t projection;
	uint32_t textureIndex;
	RHI::HSampler samplers[2];
};

extern grp_t grp;
