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
// Gameplay Rendering Pipeline - UI/2D rendering


#include "grp_local.h"


void ui_t::Begin2D()
{
	if(grp.projection == PROJECTION_2D)
	{
		return;
	}

	// @TODO: grab the right rects...
	RHI::CmdSetViewport(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	RHI::CmdSetScissor(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	RHI::CmdBindRootSignature(grp.ui.rootSignature);
	RHI::CmdBindPipeline(grp.ui.pipeline);
	RHI::CmdBindDescriptorTable(grp.ui.rootSignature, grp.ui.descriptorTable);
	const uint32_t stride = sizeof(ui_t::vertex_t);
	RHI::CmdBindVertexBuffers(1, &grp.ui.vertexBuffer, &stride, NULL);
	RHI::CmdBindIndexBuffer(grp.ui.indexBuffer, RHI::IndexType::UInt32, 0);
	const float scale[2] = { 2.0f / glConfig.vidWidth, 2.0f / glConfig.vidHeight };
	RHI::CmdSetRootConstants(grp.ui.rootSignature, RHI::ShaderType::Vertex, scale);

	grp.projection = PROJECTION_2D;
}

void ui_t::Draw()
{
	if(grp.ui.indexCount <= 0)
	{
		return;
	}

	const uint32_t textureIndex = grp.ui.shader->stages[0]->bundle.image[0]->textureIndex;
	const uint32_t pixelConstants[2] = { textureIndex, 0 }; // second one is the sampler index
	RHI::CmdSetRootConstants(grp.ui.rootSignature, RHI::ShaderType::Pixel, pixelConstants);
	RHI::CmdDrawIndexed(grp.ui.indexCount, grp.ui.firstIndex, 0);
	grp.ui.firstIndex += grp.ui.indexCount;
	grp.ui.firstVertex += grp.ui.vertexCount;
	grp.ui.indexCount = 0;
	grp.ui.vertexCount = 0;
}

const void* ui_t::SetColor(const void* data)
{
	const setColorCommand_t* cmd = (const setColorCommand_t*)data;

	byte* const colors = (byte*)&grp.ui.color;
	colors[0] = (byte)(cmd->color[0] * 255.0f);
	colors[1] = (byte)(cmd->color[1] * 255.0f);
	colors[2] = (byte)(cmd->color[2] * 255.0f);
	colors[3] = (byte)(cmd->color[3] * 255.0f);

	return (const void*)(cmd + 1);
}

const void* ui_t::StretchPic(const void* data)
{
	const stretchPicCommand_t* cmd = (const stretchPicCommand_t*)data;

	if(grp.ui.vertexCount + 4 > grp.ui.maxVertexCount ||
		grp.ui.indexCount + 6 > grp.ui.maxIndexCount)
	{
		return (const void*)(cmd + 1);
	}

	Begin2D();

	if(grp.ui.shader != cmd->shader)
	{
		Draw();
	}

	grp.ui.shader = cmd->shader;

	const int v = grp.ui.firstVertex + grp.ui.vertexCount;
	const int i = grp.ui.firstIndex + grp.ui.indexCount;
	grp.ui.vertexCount += 4;
	grp.ui.indexCount += 6;

	grp.ui.indices[i + 0] = v + 3;
	grp.ui.indices[i + 1] = v + 0;
	grp.ui.indices[i + 2] = v + 2;
	grp.ui.indices[i + 3] = v + 2;
	grp.ui.indices[i + 4] = v + 0;
	grp.ui.indices[i + 5] = v + 1;

	grp.ui.vertices[v + 0].position[0] = cmd->x;
	grp.ui.vertices[v + 0].position[1] = cmd->y;
	grp.ui.vertices[v + 0].texCoords[0] = cmd->s1;
	grp.ui.vertices[v + 0].texCoords[1] = cmd->t1;
	grp.ui.vertices[v + 0].color = grp.ui.color;

	grp.ui.vertices[v + 1].position[0] = cmd->x + cmd->w;
	grp.ui.vertices[v + 1].position[1] = cmd->y;
	grp.ui.vertices[v + 1].texCoords[0] = cmd->s2;
	grp.ui.vertices[v + 1].texCoords[1] = cmd->t1;
	grp.ui.vertices[v + 1].color = grp.ui.color;

	grp.ui.vertices[v + 2].position[0] = cmd->x + cmd->w;
	grp.ui.vertices[v + 2].position[1] = cmd->y + cmd->h;
	grp.ui.vertices[v + 2].texCoords[0] = cmd->s2;
	grp.ui.vertices[v + 2].texCoords[1] = cmd->t2;
	grp.ui.vertices[v + 2].color = grp.ui.color;

	grp.ui.vertices[v + 3].position[0] = cmd->x;
	grp.ui.vertices[v + 3].position[1] = cmd->y + cmd->h;
	grp.ui.vertices[v + 3].texCoords[0] = cmd->s1;
	grp.ui.vertices[v + 3].texCoords[1] = cmd->t2;
	grp.ui.vertices[v + 3].color = grp.ui.color;

	return (const void*)(cmd + 1);
}

const void* ui_t::Triangle(const void* data)
{
	const triangleCommand_t* cmd = (const triangleCommand_t*)data;

	if(grp.ui.vertexCount + 3 > grp.ui.maxVertexCount ||
		grp.ui.indexCount + 3 > grp.ui.maxIndexCount)
	{
		return (const void*)(cmd + 1);
	}

	Begin2D();

	if(grp.ui.shader != cmd->shader)
	{
		Draw();
	}

	grp.ui.shader = cmd->shader;

	const int v = grp.ui.firstVertex + grp.ui.vertexCount;
	const int i = grp.ui.firstIndex + grp.ui.indexCount;
	grp.ui.vertexCount += 3;
	grp.ui.indexCount += 3;

	grp.ui.indices[i + 0] = v + 0;
	grp.ui.indices[i + 1] = v + 1;
	grp.ui.indices[i + 2] = v + 2;

	grp.ui.vertices[v + 0].position[0] = cmd->x0;
	grp.ui.vertices[v + 0].position[1] = cmd->y0;
	grp.ui.vertices[v + 0].texCoords[0] = cmd->s0;
	grp.ui.vertices[v + 0].texCoords[1] = cmd->t0;
	grp.ui.vertices[v + 0].color = grp.ui.color;

	grp.ui.vertices[v + 1].position[0] = cmd->x1;
	grp.ui.vertices[v + 1].position[1] = cmd->y1;
	grp.ui.vertices[v + 1].texCoords[0] = cmd->s1;
	grp.ui.vertices[v + 1].texCoords[1] = cmd->t1;
	grp.ui.vertices[v + 1].color = grp.ui.color;

	grp.ui.vertices[v + 2].position[0] = cmd->x2;
	grp.ui.vertices[v + 2].position[1] = cmd->y2;
	grp.ui.vertices[v + 2].texCoords[0] = cmd->s2;
	grp.ui.vertices[v + 2].texCoords[1] = cmd->t2;
	grp.ui.vertices[v + 2].color = grp.ui.color;

	return (const void*)(cmd + 1);
}
