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


#include "grp_local.h"
#include "hlsl/uber_shader.h"
#include "../client/cl_imgui.h"


GRP grp;


template<typename T>
static const void* SkipCommand(const void* data)
{
	const T* const cmd = (const T*)data;

	return (const void*)(cmd + 1);
}

static void UpdateAnimatedImage(image_t* image, int w, int h, const byte* data, qbool dirty)
{
	if(w != image->width || h != image->height)
	{
		// @TODO: ?
		/*image->width = w;
		image->height = h;
		CreateTexture(&d3d.textures[image->texnum], image, 1, w, h);
		GAL_UpdateTexture(image, 0, 0, 0, w, h, data);*/
	}
	else if(dirty)
	{
		// @TODO: ?
		//GAL_UpdateTexture(image, 0, 0, 0, w, h, data);
	}
}

const image_t* GetBundleImage(const textureBundle_t& bundle)
{
	return R_UpdateAndGetBundleImage(&bundle, &UpdateAnimatedImage);
}

uint32_t GetSamplerIndex(textureWrap_t wrap, TextureFilter::Id filter, uint32_t minLOD)
{
	Q_assert((uint32_t)wrap < TW_COUNT);
	Q_assert((uint32_t)filter < TextureFilter::Count);

	const uint32_t index =
		(uint32_t)filter +
		(uint32_t)TextureFilter::Count * (uint32_t)wrap +
		(uint32_t)TextureFilter::Count * (uint32_t)TW_COUNT * minLOD;

	return index;
}

uint32_t GetSamplerIndex(const image_t* image)
{
	TextureFilter::Id filter = TextureFilter::Anisotropic;
	if(r_lego->integer &&
		grp.renderMode == RenderMode::World &&
		(image->flags & (IMG_LMATLAS | IMG_EXTLMATLAS | IMG_NOPICMIP)) == 0)
	{
		filter = TextureFilter::Point;
	}
	else if((image->flags & IMG_NOAF) != 0 ||
		grp.renderMode != RenderMode::World)
	{
		filter = TextureFilter::Linear;
	}

	int minLOD = 0;
	if(grp.renderMode == RenderMode::World &&
		(image->flags & IMG_NOPICMIP) == 0)
	{
		minLOD = Com_ClampInt(0, MaxTextureMips - 1, r_picmip->integer);
	}

	return GetSamplerIndex(image->wrapClampMode, filter, (uint32_t)minLOD);
}

static bool IsCommutativeBlendState(unsigned int stateBits)
{
	const unsigned int blendStates[] =
	{
		GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE, // additive
		GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO, // modulate
		GLS_SRCBLEND_ZERO | GLS_DSTBLEND_SRC_COLOR, // modulate
		GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE // pre-multiplied alpha blend
	};

	const unsigned int blendBits = stateBits & GLS_BLEND_BITS;
	for(int b = 0; b < ARRAY_LEN(blendStates); ++b)
	{
		if(blendBits == blendStates[b])
		{
			return true;
		}
	}

	return false;
}


void GRP::Init()
{
	RHI::Init();

	if(firstInit)
	{
		for(uint32_t w = 0; w < TW_COUNT; ++w)
		{
			for(uint32_t f = 0; f < TextureFilter::Count; ++f)
			{
				for(uint32_t m = 0; m < MaxTextureMips; ++m)
				{
					const textureWrap_t wrap = (textureWrap_t)w;
					const TextureFilter::Id filter = (TextureFilter::Id)f;
					const uint32_t s = GetSamplerIndex(wrap, filter, m);
					samplers[s] = CreateSampler(SamplerDesc(wrap, filter, (float)m));
				}
			}
		}

		RootSignatureDesc desc("main");
		desc.usingVertexBuffers = qtrue;
		desc.samplerCount = ARRAY_LEN(samplers);
		desc.samplerVisibility = ShaderStages::PixelBit;
		desc.genericVisibility = ShaderStages::PixelBit;
		desc.AddRange(DescriptorType::Texture, 0, MAX_DRAWIMAGES * 2);
		rootSignatureDesc = desc;
		rootSignature = CreateRootSignature(desc);

		descriptorTable = CreateDescriptorTable(DescriptorTableDesc("game textures", rootSignature));

		DescriptorTableUpdate update;
		update.SetSamplers(ARRAY_LEN(samplers), samplers);
		UpdateDescriptorTable(descriptorTable, update);

		desc.name = "world";
		desc.usingVertexBuffers = true;
		desc.constants[ShaderStage::Vertex].byteCount = sizeof(WorldVertexRC);
		desc.constants[ShaderStage::Pixel].byteCount = sizeof(WorldPixelRC);
		desc.samplerVisibility = ShaderStages::PixelBit;
		desc.genericVisibility = ShaderStages::VertexBit | ShaderStages::PixelBit;
		opaqueRootSignature = CreateRootSignature(desc);
	}

	textureIndex = 0;
	psoCount = 1; // we treat index 0 as invalid

	{
		switch(r_rtColorFormat->integer)
		{
			case RTCF_R10G10B10A2:
				renderTargetFormat = TextureFormat::R10G10B10A2_UNorm;
				break;
			case RTCF_R16G16B16A16:
				renderTargetFormat = TextureFormat::RGBA64_Float;
				break;
			case RTCF_R8G8B8A8:
			default:
				renderTargetFormat = TextureFormat::RGBA32_UNorm;
				break;
		}

		TextureDesc desc("render target", glConfig.vidWidth, glConfig.vidHeight);
		desc.initialState = ResourceStates::RenderTargetBit;
		desc.allowedState = ResourceStates::RenderTargetBit | ResourceStates::PixelShaderAccessBit;
		//Vector4Set(desc.clearColor, 0.0f, 0.0f, 0.0f, 1.0f);
		Vector4Copy(colorPink, desc.clearColor);
		desc.usePreferredClearValue = true;
		desc.committedResource = true;
		desc.format = renderTargetFormat;
		desc.shortLifeTime = true;
		renderTarget = RHI::CreateTexture(desc);
	}

	ui.Init();
	world.Init();
	mipMapGen.Init();
	imgui.Init();
	post.Init();
	smaa.Init();

	firstInit = false;
}

void GRP::ShutDown(bool fullShutDown)
{
	RHI::ShutDown(fullShutDown);
}

void GRP::BeginFrame()
{
	renderPasses[GetFrameIndex()].count = 0;

	// have it be first to we can use ImGUI in the other components too
	grp.imgui.SafeBeginFrame();

	RHI::BeginFrame();
	ui.BeginFrame();
	world.BeginFrame();

	const TextureBarrier barrier(renderTarget, ResourceStates::RenderTargetBit);
	CmdBarrier(1, &barrier);
	CmdBindRenderTargets(1, &renderTarget, NULL);
	CmdClearColorTarget(renderTarget, colorPink);

	// nothing is bound to the command list yet!
	renderMode = RenderMode::None;

	frameSeed = (float)rand() / (float)RAND_MAX;
}

void GRP::EndFrame()
{
	// @TODO: move
	{
		uint32_t durations[MaxDurationQueries];
		GetDurations(durations);

		if(BeginTable("GPU timings", 2))
		{
			TableHeader(2, "Pass", "Micro-seconds");

			TableRow(2, "Whole frame", va("%d", (int)durations[0]));

			RenderPassFrame& f = renderPasses[GetFrameIndex() ^ 1];
			for(uint32_t p = 0; p < f.count; ++p)
			{
				const uint32_t index = f.passes[p].queryIndex;
				if(index < MaxDurationQueries)
				{
					TableRow(2, f.passes[p].name, va("%d", (int)durations[index]));
				}
			}

			ImGui::EndTable();
		}
	}

	EndUI();
	imgui.Draw();
	post.Draw();
	RHI::EndFrame();
}

void GRP::AddDrawSurface(const surfaceType_t* surface, const shader_t* shader)
{
}

void GRP::CreateTexture(image_t* image, int mipCount, int width, int height)
{
	TextureDesc desc(image->name, width, height, mipCount);
	desc.shortLifeTime = true;
	if(mipCount > 1)
	{
		desc.allowedState |= ResourceStates::UnorderedAccessBit; // for mip-map generation
	}

	image->texture = ::RHI::CreateTexture(desc);
	image->textureIndex = RegisterTexture(image->texture);
}

void GRP::UpoadTextureAndGenerateMipMaps(image_t* image, const byte* data)
{
	MappedTexture texture;
	RHI::BeginTextureUpload(texture, image->texture);
	for(uint32_t r = 0; r < texture.rowCount; ++r)
	{
		memcpy(texture.mappedData + r * texture.dstRowByteCount, data + r * texture.srcRowByteCount, texture.srcRowByteCount);
	}
	RHI::EndTextureUpload(image->texture);

	mipMapGen.GenerateMipMaps(image->texture);
}

void GRP::BeginTextureUpload(MappedTexture& mappedTexture, image_t* image)
{
	RHI::BeginTextureUpload(mappedTexture, image->texture);
}

void GRP::EndTextureUpload(image_t* image)
{
	RHI::EndTextureUpload(image->texture);
}

void GRP::ProcessWorld(world_t& world_)
{
	world.ProcessWorld(world_);
}

void GRP::ProcessModel(model_t& model)
{
	// @TODO: !!!
	//__debugbreak();
}

void GRP::ProcessShader(shader_t& shader)
{
	shader.numPipelines = 0;
	if(shader.numStages < 1)
	{
		return;
	}

	// @TODO: GLS_POLYMODE_LINE

	if(shader.isOpaque)
	{
		// @TODO: fix up cache.stageStateBits[0] based on depth state from follow-up states
		CachedPSO cache = {};
		cache.desc.cullType = shader.cullType;
		cache.desc.polygonOffset = !!shader.polygonOffset;
		cache.desc.clampDepth = r_depthClamp->integer != 0;
		cache.stageStateBits[0] = shader.stages[0]->stateBits & (~GLS_POLYMODE_LINE);
		for(int s = 1; s < shader.numStages; ++s)
		{
			cache.stageStateBits[s] = shader.stages[s]->stateBits & (GLS_BLEND_BITS | GLS_ATEST_BITS);
		}
		cache.stageCount = shader.numStages;

		shader.pipelines[0].pipeline = CreatePSO(cache, shader.name);
		shader.pipelines[0].firstStage = 0;
		shader.pipelines[0].numStages = shader.numStages;
		shader.numPipelines = 1;
	}
	else
	{
		CachedPSO cache = {};
		cache.desc.cullType = shader.cullType;
		cache.desc.polygonOffset = !!shader.polygonOffset;
		cache.desc.clampDepth = r_depthClamp->integer != 0;
		cache.stageCount = 0;

		unsigned int prevStateBits = 0xFFFFFFFF;
		int firstStage = 0;
		for(int s = 0; s < shader.numStages; ++s)
		{
			const unsigned int currStateBits = shader.stages[s]->stateBits & (~(GLS_POLYMODE_LINE | GLS_ATEST_BITS));
			if(cache.stageCount > 0)
			{
				if(currStateBits == prevStateBits && IsCommutativeBlendState(currStateBits))
				{
					cache.stageStateBits[cache.stageCount++] = currStateBits;
				}
				else
				{
					pipeline_t& p = shader.pipelines[shader.numPipelines++];
					p.pipeline = CreatePSO(cache, va("%s #%d", shader.name, shader.numPipelines));
					p.firstStage = firstStage;
					p.numStages = cache.stageCount;
					cache.stageStateBits[0] = currStateBits;
					cache.stageCount = 1;
					firstStage = s;
				}
			}
			else
			{
				cache.stageStateBits[0] = currStateBits;
				cache.stageCount = 1;
			}
			prevStateBits = currStateBits;
		}

		if(cache.stageCount > 0)
		{
			pipeline_t& p = shader.pipelines[shader.numPipelines++];
			p.pipeline = CreatePSO(cache, va("%s #%d", shader.name, shader.numPipelines));
			p.firstStage = firstStage;
			p.numStages = cache.stageCount;
		}
	}
}

uint32_t GRP::RegisterTexture(HTexture htexture)
{
	const uint32_t index = textureIndex++;

	DescriptorTableUpdate update;
	update.SetTextures(1, &htexture, index);
	UpdateDescriptorTable(descriptorTable, update);

	return index;
}

uint32_t GRP::BeginRenderPass(const char* name, float r, float g, float b)
{
	RenderPassFrame& f = renderPasses[GetFrameIndex()];
	if(f.count >= ARRAY_LEN(f.passes))
	{
		Q_assert(0);
		return UINT32_MAX;
	}

	CmdBeginDebugLabel(name, r, g, b);

	const uint32_t index = f.count++;
	RenderPassQueries& q = f.passes[index];
	Q_strncpyz(q.name, name, sizeof(q.name));
	q.cpuStartUS = Sys_Microseconds();
	q.queryIndex = CmdBeginDurationQuery();

	return index;
}

void GRP::EndRenderPass(uint32_t index)
{
	RenderPassFrame& f = renderPasses[GetFrameIndex()];
	if(index >= f.count)
	{
		Q_assert(0);
		return;
	}

	CmdEndDebugLabel();

	RenderPassQueries& q = f.passes[index];
	q.cpuDurationUS = (uint32_t)(Sys_Microseconds() - q.cpuStartUS);
	CmdEndDurationQuery(q.queryIndex);
}

void GRP::EndUI()
{
	ui.DrawBatch();
	if(renderMode == RenderMode::UI)
	{
		EndRenderPass(ui.renderPassIndex);
	}
}

uint32_t GRP::CreatePSO(CachedPSO& cache, const char* name)
{
	for(uint32_t p = 1; p < psoCount; ++p)
	{
		if(cache.stageCount == psos[p].stageCount &&
			memcmp(&cache.desc, &psos[p].desc, sizeof(cache.desc)) == 0 &&
			memcmp(&cache.stageStateBits, &psos[p].stageStateBits, cache.stageCount * sizeof(cache.stageStateBits[0])) == 0)
		{
			return p;
		}
	}

	Q_assert(psoCount < ARRAY_LEN(psos));

	uint32_t macroCount = 0;
	ShaderMacro macros[64];

	macros[macroCount].name = "STAGE_COUNT";
	macros[macroCount].value = va("%d", cache.stageCount);
	macroCount++;
	const HShader vertexShader = CreateShader(ShaderDesc(ShaderStage::Vertex, sizeof(uber_shader_string), uber_shader_string, "main", macroCount, macros));

	for(int s = 0; s < cache.stageCount; ++s)
	{
		macros[macroCount].name = va("STAGE%d_BITS", s);
		macros[macroCount].value = va("%d", (int)cache.stageStateBits[s]);
		macroCount++;
	}
	if(r_dither->integer)
	{
		macros[macroCount].name = "DITHER";
		macros[macroCount].value = "1";
		macroCount++;
	}
	const HShader pixelShader = CreateShader(ShaderDesc(ShaderStage::Pixel, sizeof(uber_shader_string), uber_shader_string, "main", macroCount, macros));

	Q_assert(macroCount <= ARRAY_LEN(macros));

	uint32_t a = 0;
	GraphicsPipelineDesc desc(name, opaqueRootSignature);
	desc.shortLifeTime = true; // the PSO cache is only valid for this map!
	desc.vertexShader = GetShaderByteCode(vertexShader);
	desc.pixelShader = GetShaderByteCode(pixelShader);
	desc.vertexLayout.AddAttribute(a++, ShaderSemantic::Position, DataType::Float32, 3, 0);
	desc.vertexLayout.AddAttribute(a++, ShaderSemantic::Normal, DataType::Float32, 2, 0);
	for(int s = 0; s < cache.stageCount; ++s)
	{
		desc.vertexLayout.AddAttribute(a++, ShaderSemantic::TexCoord, DataType::Float32, 2, 0);
		desc.vertexLayout.AddAttribute(a++, ShaderSemantic::Color, DataType::UNorm8, 4, 0);
	}
	desc.depthStencil.depthStencilFormat = TextureFormat::Depth32_Float;
	desc.depthStencil.depthComparison =
		(cache.stageStateBits[0] & GLS_DEPTHFUNC_EQUAL) != 0 ?
		ComparisonFunction::Equal :
		ComparisonFunction::GreaterEqual;
	desc.depthStencil.enableDepthTest = (cache.stageStateBits[0] & GLS_DEPTHTEST_DISABLE) == 0;
	desc.depthStencil.enableDepthWrites = (cache.stageStateBits[0] & GLS_DEPTHMASK_TRUE) != 0;
	desc.rasterizer.cullMode = cache.desc.cullType;
	desc.rasterizer.polygonOffset = cache.desc.polygonOffset;
	desc.rasterizer.clampDepth = cache.desc.clampDepth;
	desc.AddRenderTarget(cache.stageStateBits[0] & GLS_BLEND_BITS, renderTargetFormat);
	cache.pipeline = CreateGraphicsPipeline(desc);

	DestroyShader(vertexShader);
	DestroyShader(pixelShader);

	const uint32_t index = psoCount++;
	psos[index] = cache;

	return index;
}

// @TODO: move out
IRenderPipeline* renderPipeline = &grp;
