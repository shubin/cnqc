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


// VS macros to define:
// STAGE_COUNT 1-8
//
// PS macros to define:
// STAGE_COUNT 1-8
// STAGE#_BITS
static const char* opaqueShaderSource = R"grml(
#define STAGE_ATTRIBS(Index) \
	float2 texCoords##Index : TEXCOORD##Index; \
	float4 color##Index : COLOR##Index;

#if VERTEX_SHADER
struct VIn
{
	float3 position : POSITION;
	float3 normal : NORMAL;
#if STAGE_COUNT >= 1
	STAGE_ATTRIBS(0)
#endif
#if STAGE_COUNT >= 2
	STAGE_ATTRIBS(1)
#endif
#if STAGE_COUNT >= 3
	STAGE_ATTRIBS(2)
#endif
#if STAGE_COUNT >= 4
	STAGE_ATTRIBS(3)
#endif
#if STAGE_COUNT >= 5
	STAGE_ATTRIBS(4)
#endif
#if STAGE_COUNT >= 6
	STAGE_ATTRIBS(5)
#endif
#if STAGE_COUNT >= 7
	STAGE_ATTRIBS(6)
#endif
#if STAGE_COUNT >= 8
	STAGE_ATTRIBS(7)
#endif
};
#endif

struct VOut
{
	float4 position : SV_Position;
	float3 normal : NORMAL;
#if STAGE_COUNT >= 1
	STAGE_ATTRIBS(0)
#endif
#if STAGE_COUNT >= 2
	STAGE_ATTRIBS(1)
#endif
#if STAGE_COUNT >= 3
	STAGE_ATTRIBS(2)
#endif
#if STAGE_COUNT >= 4
	STAGE_ATTRIBS(3)
#endif
#if STAGE_COUNT >= 5
	STAGE_ATTRIBS(4)
#endif
#if STAGE_COUNT >= 6
	STAGE_ATTRIBS(5)
#endif
#if STAGE_COUNT >= 7
	STAGE_ATTRIBS(6)
#endif
#if STAGE_COUNT >= 8
	STAGE_ATTRIBS(7)
#endif
	float clipDist : SV_ClipDistance0;
};

#undef STAGE_ATTRIBS

#if VERTEX_SHADER

cbuffer RootConstants
{
	matrix modelViewMatrix;
	matrix projectionMatrix;
	float4 clipPlane;
};

#define STAGE_ATTRIBS(Index) \
	output.texCoords##Index = input.texCoords##Index; \
	output.color##Index = input.color##Index;

VOut main(VIn input)
{
	float4 positionVS = mul(modelViewMatrix, float4(input.position.xyz, 1));

	VOut output;
	output.position = mul(projectionMatrix, positionVS);
	output.normal = input.normal;
#if STAGE_COUNT >= 1
	STAGE_ATTRIBS(0)
#endif
#if STAGE_COUNT >= 2
	STAGE_ATTRIBS(1)
#endif
#if STAGE_COUNT >= 3
	STAGE_ATTRIBS(2)
#endif
#if STAGE_COUNT >= 4
	STAGE_ATTRIBS(3)
#endif
#if STAGE_COUNT >= 5
	STAGE_ATTRIBS(4)
#endif
#if STAGE_COUNT >= 6
	STAGE_ATTRIBS(5)
#endif
#if STAGE_COUNT >= 7
	STAGE_ATTRIBS(6)
#endif
#if STAGE_COUNT >= 8
	STAGE_ATTRIBS(7)
#endif
	output.clipDist = dot(positionVS, clipPlane);

	return output;
}

#endif

#if PIXEL_SHADER

cbuffer RootConstants
{
	// @TODO: 16 bits per stage: low 12 = texture, high 4 = sampler
	//uint stageIndices[4];
	// low 16 = texture, high 16 = sampler
	uint4 stageIndices0;
	uint4 stageIndices1;
};

Texture2D textures2D[2048] : register(t0);
SamplerState samplers[2] : register(s0);

#define GLS_SRCBLEND_ZERO						0x00000001
#define GLS_SRCBLEND_ONE						0x00000002
#define GLS_SRCBLEND_DST_COLOR					0x00000003
#define GLS_SRCBLEND_ONE_MINUS_DST_COLOR		0x00000004
#define GLS_SRCBLEND_SRC_ALPHA					0x00000005
#define GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA		0x00000006
#define GLS_SRCBLEND_DST_ALPHA					0x00000007
#define GLS_SRCBLEND_ONE_MINUS_DST_ALPHA		0x00000008
#define GLS_SRCBLEND_ALPHA_SATURATE				0x00000009
#define		GLS_SRCBLEND_BITS					0x0000000f

#define GLS_DSTBLEND_ZERO						0x00000010
#define GLS_DSTBLEND_ONE						0x00000020
#define GLS_DSTBLEND_SRC_COLOR					0x00000030
#define GLS_DSTBLEND_ONE_MINUS_SRC_COLOR		0x00000040
#define GLS_DSTBLEND_SRC_ALPHA					0x00000050
#define GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA		0x00000060
#define GLS_DSTBLEND_DST_ALPHA					0x00000070
#define GLS_DSTBLEND_ONE_MINUS_DST_ALPHA		0x00000080
#define		GLS_DSTBLEND_BITS					0x000000f0

#define GLS_ATEST_GT_0							0x10000000
#define GLS_ATEST_LT_80							0x20000000
#define GLS_ATEST_GE_80							0x40000000
#define		GLS_ATEST_BITS						0x70000000

float4 BlendSource(float4 src, float4 dst, uint stateBits)
{
	if(stateBits == GLS_SRCBLEND_ZERO)
		return float4(0.0, 0.0, 0.0, 0.0);
	else if(stateBits == GLS_SRCBLEND_ONE)
		return src;
	else if(stateBits == GLS_SRCBLEND_DST_COLOR)
		return src * dst;
	else if(stateBits == GLS_SRCBLEND_ONE_MINUS_DST_COLOR)
		return src * (float4(1.0, 1.0, 1.0, 1.0) - dst);
	else if(stateBits == GLS_SRCBLEND_SRC_ALPHA)
		return src * float4(src.a, src.a, src.a, 1.0);
	else if(stateBits == GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA)
		return src * float4(1.0 - src.a, 1.0 - src.a, 1.0 - src.a, 1.0);
	else if(stateBits == GLS_SRCBLEND_DST_ALPHA)
		return src * float4(dst.a, dst.a, dst.a, 1.0);
	else if(stateBits == GLS_SRCBLEND_ONE_MINUS_DST_ALPHA)
		return src * float4(1.0 - dst.a, 1.0 - dst.a, 1.0 - dst.a, 1.0);
	else if(stateBits == GLS_SRCBLEND_ALPHA_SATURATE)
		return src * float4(src.a, src.a, src.a, 1.0); // ?????????
	else
		return src;
}

float4 BlendDest(float4 src, float4 dst, uint stateBits)
{
	if(stateBits == GLS_DSTBLEND_ZERO)
		return float4(0.0, 0.0, 0.0, 0.0);
	else if(stateBits == GLS_DSTBLEND_ONE)
		return dst;
	else if(stateBits == GLS_DSTBLEND_SRC_COLOR)
		return dst * src;
	else if(stateBits == GLS_DSTBLEND_ONE_MINUS_SRC_COLOR)
		return dst * float4(1.0 - src.r, 1.0 - src.g, 1.0 - src.b, 1.0 - src.a);
	else if(stateBits == GLS_DSTBLEND_SRC_ALPHA)
		return dst * float4(src.a, src.a, src.a, 1.0);
	else if(stateBits == GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA)
		return dst * float4(1.0 - src.a, 1.0 - src.a, 1.0 - src.a, 0.0);
	else if(stateBits == GLS_DSTBLEND_DST_ALPHA)
		return dst * float4(dst.a, dst.a, dst.a, 1.0);
	else if(stateBits == GLS_DSTBLEND_ONE_MINUS_DST_ALPHA)
		return dst * float4(1.0 - dst.a, 1.0 - dst.a, 1.0 - dst.a, 1.0);
	else
		return dst;
}

float4 Blend(float4 src, float4 dst, uint stateBits)
{
	float4 srcOut = BlendSource(src, dst, stateBits & GLS_SRCBLEND_BITS);
	float4 dstOut = BlendDest(src, dst, stateBits & GLS_DSTBLEND_BITS);

	return srcOut + dstOut;
}

bool FailsAlphaTest(float alpha, uint stateBits)
{
	if(stateBits == GLS_ATEST_GT_0)
		return alpha == 0.0;
	else if(stateBits == GLS_ATEST_LT_80)
		return alpha >= 0.5;
	else if(stateBits == GLS_ATEST_GE_80)
		return alpha < 0.5;
	else
		return false;
}

float4 ProcessStage(float4 color, float2 texCoords, uint textureIndex, uint samplerIndex)
{
	return color * textures2D[textureIndex].Sample(samplers[samplerIndex], texCoords);
}

void ProcessFullStage(inout float4 dst, float4 color, float2 texCoords, uint textureIndex, uint samplerIndex, uint stateBits)
{
	float4 src = ProcessStage(color, texCoords, textureIndex, samplerIndex);
	if(!FailsAlphaTest(src.a, stateBits & GLS_ATEST_BITS))
	{
		dst = Blend(src, dst, stateBits);
	}
}

// reminder: early-Z is early depth test AND early depth write
// therefore, the attribute should be gone if opaque stage #1 does alpha testing (discard)
#if (STAGE0_BITS & GLS_ATEST_BITS) == 0
[earlydepthstencil]
#endif
float4 main(VOut input) : SV_TARGET
{
	float4 dst = ProcessStage(input.color0, input.texCoords0, stageIndices0.x & 0xFFFF, stageIndices0.x >> 16);
	if(FailsAlphaTest(dst.a, STAGE0_BITS & GLS_ATEST_BITS))
	{
		discard;
	}
#if STAGE_COUNT >= 2
	ProcessFullStage(dst, input.color1, input.texCoords1, stageIndices0.y & 0xFFFF, stageIndices0.y >> 16, STAGE1_BITS);
#endif
#if STAGE_COUNT >= 3
	ProcessFullStage(dst, input.color2, input.texCoords2, stageIndices0.z & 0xFFFF, stageIndices0.z >> 16, STAGE2_BITS);
#endif
#if STAGE_COUNT >= 4
	ProcessFullStage(dst, input.color3, input.texCoords3, stageIndices0.w & 0xFFFF, stageIndices0.w >> 16, STAGE3_BITS);
#endif
#if STAGE_COUNT >= 5
	ProcessFullStage(dst, input.color4, input.texCoords4, stageIndices1.x & 0xFFFF, stageIndices1.x >> 16, STAGE4_BITS);
#endif
#if STAGE_COUNT >= 6
	ProcessFullStage(dst, input.color5, input.texCoords5, stageIndices1.y & 0xFFFF, stageIndices1.y >> 16, STAGE5_BITS);
#endif
#if STAGE_COUNT >= 7
	ProcessFullStage(dst, input.color6, input.texCoords6, stageIndices1.z & 0xFFFF, stageIndices1.z >> 16, STAGE6_BITS);
#endif
#if STAGE_COUNT >= 8
	ProcessFullStage(dst, input.color7, input.texCoords7, stageIndices1.w & 0xFFFF, stageIndices1.w >> 16, STAGE7_BITS);
#endif

	return dst;
}

#endif
)grml";


GRP grp;


template<typename T>
static const void* SkipCommand(const void* data)
{
	const T* const cmd = (const T*)data;

	return (const void*)(cmd + 1);
}


void GRP::Init()
{
	RHI::Init();

	if(firstInit)
	{
		samplers[0] = CreateSampler(SamplerDesc(TW_REPEAT, TextureFilter::Anisotropic));
		samplers[1] = CreateSampler(SamplerDesc(TW_CLAMP_TO_EDGE, TextureFilter::Anisotropic));

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

		desc.name = "opaque";
		desc.usingVertexBuffers = true;
		desc.constants[ShaderStage::Vertex].byteCount = sizeof(DynamicVertexRC);
		desc.constants[ShaderStage::Pixel].byteCount = sizeof(DynamicPixelRC);
		desc.samplerVisibility = ShaderStages::PixelBit;
		desc.genericVisibility = ShaderStages::VertexBit | ShaderStages::PixelBit;
		opaqueRootSignature = CreateRootSignature(desc);
	}

	textureIndex = 0;
	psoCount = 1; // we treat index 0 as invalid

	{
		TextureDesc desc("render target", glConfig.vidWidth, glConfig.vidHeight);
		desc.initialState = ResourceStates::RenderTargetBit;
		desc.allowedState = ResourceStates::RenderTargetBit | ResourceStates::PixelShaderAccessBit;
		Vector4Set(desc.clearColor, 0.0f, 0.0f, 0.0f, 1.0f);
		desc.usePreferredClearValue = true;
		desc.committedResource = true;
		desc.format = TextureFormat::RGBA32_UNorm;
		desc.shortLifeTime = true;
		renderTarget = RHI::CreateTexture(desc);
	}

	ui.Init();
	world.Init();
	mipMapGen.Init();
	imgui.Init();
	post.Init();

	firstInit = false;
}

void GRP::ShutDown(bool fullShutDown)
{
	RHI::ShutDown(fullShutDown);
}

void GRP::BeginFrame()
{
	renderPasses[GetFrameIndex()].count = 0;

	RHI::BeginFrame();
	ui.BeginFrame();
	world.BeginFrame();

	const TextureBarrier barrier(renderTarget, ResourceStates::RenderTargetBit);
	CmdBarrier(1, &barrier);
	CmdBindRenderTargets(1, &renderTarget, NULL);
	CmdClearColorTarget(renderTarget, colorPink);

	// nothing is bound to the command list yet!
	renderMode = RenderMode::None;
}

void GRP::EndFrame()
{
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
		cache.desc.polygonOffset = shader.polygonOffset;
		cache.stageStateBits[0] = shader.stages[0]->stateBits & (~GLS_POLYMODE_LINE);
		for(int s = 1; s < shader.numStages; ++s)
		{
			cache.stageStateBits[s] = shader.stages[s]->stateBits & (GLS_BLEND_BITS | GLS_ATEST_BITS);
		}
		cache.stageCount = shader.numStages;

		shader.pipelines[0].pipeline = CreatePSO(cache);
		shader.pipelines[0].firstStage = 0;
		shader.pipelines[0].numStages = shader.numStages;
		shader.numPipelines = 1;
	}
	else
	{
		// @TODO: collapse consecutive stages with the same commutative blend state
		CachedPSO cache = {};
		cache.desc.cullType = shader.cullType;
		cache.desc.polygonOffset = shader.polygonOffset;
		cache.stageCount = 1;
		for(int s = 0; s < shader.numStages; ++s)
		{
			cache.stageStateBits[0] = shader.stages[s]->stateBits & (~(GLS_POLYMODE_LINE | GLS_ATEST_BITS));

			shader.pipelines[s].pipeline = CreatePSO(cache);
			shader.pipelines[s].firstStage = s;
			shader.pipelines[s].numStages = 1;
		}
		shader.numPipelines = shader.numStages;
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

void GRP::BeginRenderPass(const char* name, float r, float g, float b)
{
	CmdBeginDebugLabel(name, r, g, b);

	/*RenderPassFrame& f = renderPasses[GetFrameIndex()];
	if(f.count >= ARRAY_LEN(f.passes))
	{
		Q_assert(0);
		return;
	}

	CmdBeginDebugLabel(name, r, g, b);

	RenderPassQueries& q = f.passes[f.count++];
	Q_strncpyz(q.name, name, sizeof(q.name));
	q.cpuStartUS = Sys_Microseconds();
	q.query = CmdBeginDurationQuery(name);*/
}

void GRP::EndRenderPass()
{
	CmdEndDebugLabel();

	/*RenderPassFrame& f = renderPasses[GetFrameIndex()];
	if(f.count == 0)
	{
		Q_assert(0);
		return;
	}

	CmdEndDebugLabel();

	RenderPassQueries& q = f.passes[f.count - 1];
	q.cpuDurationUS = (uint32_t)(Sys_Microseconds() - q.cpuStartUS);
	CmdEndDurationQuery(q.query);*/
}

void GRP::EndUI()
{
	ui.DrawBatch();
	if(renderMode == RenderMode::UI)
	{
		grp.EndRenderPass();
	}
}

uint32_t GRP::CreatePSO(CachedPSO& cache)
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
	ShaderByteCode vertexShader = CompileShader(ShaderStage::Vertex, opaqueShaderSource, "main", macroCount, macros);

	for(int s = 0; s < cache.stageCount; ++s)
	{
		macros[macroCount].name = va("STAGE%d_BITS", s);
		macros[macroCount].value = va("%d", (int)cache.stageStateBits[s]);
		macroCount++;
	}
	ShaderByteCode pixelShader = CompileShader(ShaderStage::Pixel, opaqueShaderSource, "main", macroCount, macros);

	Q_assert(macroCount <= ARRAY_LEN(macros));

	uint32_t a = 0;
	GraphicsPipelineDesc desc("uber", opaqueRootSignature);
	desc.shortLifeTime = true; // the PSO cache is only valid for this map!
	desc.vertexShader = vertexShader;
	desc.pixelShader = pixelShader;
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
	desc.AddRenderTarget(cache.stageStateBits[0] & GLS_BLEND_BITS, TextureFormat::RGBA32_UNorm);
	cache.pipeline = CreateGraphicsPipeline(desc);

	const uint32_t index = psoCount++;
	psos[index] = cache;

	return index;
}

// @TODO: move out
IRenderPipeline* renderPipeline = &grp;
