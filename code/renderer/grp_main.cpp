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


static const char* opaqueShaderSource = R"grml(
// @TODO: to define outside the ubershader itself!
#define STAGE_COUNT 1

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
	// 16 bits per stage: low 12 = texture, high 4 sampler
	uint stageIndices[4];
};

#define TexIdx(StageIndex)  (stageIndices[0] & 2047)
#define SampIdx(StageIndex) (stageIndices[0] >> 12)

Texture2D textures2D[2048] : register(t0);
SamplerState samplers[2] : register(s0);

bool FailsAlphaTest(uint alphaTest, float alpha)
{
	if( (alphaTest == 1 && alpha == 0.0) ||
	    (alphaTest == 2 && alpha >= 0.5) ||
	    (alphaTest == 3 && alpha <  0.5))
	{
		return true;
	}

	return false;
}

// reminder: early-Z is early depth test AND early depth write
// therefore, the attribute should be gone if opaque stage #1 does alpha testing (discard)
[earlydepthstencil]
float4 main(VOut input) : SV_TARGET
{
	return textures2D[TexIdx(0)].Sample(samplers[SampIdx(0)], input.texCoords0) * input.color0;
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
	
	// @TODO: remove
	{
		CachedPSO cache = {};
		cache.mainDesc.cullType = CT_BACK_SIDED;
		cache.stageDescs[0].stateBits = GLS_DEFAULT;
		cache.stageCount = 1;
		CreatePSO(cache);
	}

	ui.Init();
	world.Init();
	mipMapGen.Init();
	imgui.Init();

	firstInit = false;
}

void GRP::ShutDown(bool fullShutDown)
{
	RHI::ShutDown(fullShutDown);
}

void GRP::BeginFrame()
{
	RHI::BeginFrame();
	renderPasses[GetFrameIndex()].count = 0;
	ui.BeginFrame();
	world.BeginFrame();

	HTexture swapChain = GetSwapChainTexture();
	CmdBindRenderTargets(1, &swapChain, NULL);
	CmdClearColorTarget(swapChain, colorPink);

	// nothing is bound to the command list yet!
	renderMode = RenderMode::None;
}

void GRP::EndFrame()
{
	ui.DrawBatch();
	if(renderMode == RenderMode::UI)
	{
		grp.EndRenderPass();
	}

	imgui.Draw();
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
}

void GRP::ProcessShader(shader_t& shader)
{
	shader.psoIndex = 0;
	if(!shader.isOpaque)
	{
		return;
	}

	CachedPSO cache = {};
	cache.mainDesc.cullType = shader.cullType;
	for(int s = 0; s < shader.numStages; ++s)
	{
		cache.stageDescs[s].stateBits = shader.stages[s]->stateBits;
	}
	cache.stageCount = shader.numStages;

	for(uint32_t p = 1; p < psoCount; ++p)
	{
		if(memcmp(&cache.mainDesc, &psos[p].mainDesc, sizeof(cache.mainDesc)) == 0 &&
			memcmp(&cache.stageDescs[0], &psos[p].stageDescs[0], cache.stageCount * sizeof(cache.stageDescs[0])) == 0)
		{
			shader.psoIndex = p;
			return;
		}
	}

	shader.psoIndex = CreatePSO(cache);
}

uint32_t GRP::RegisterTexture(HTexture htexture)
{
	const uint32_t index = textureIndex++;

	DescriptorTableUpdate update;
	update.SetTextures(1, &htexture, index);
	UpdateDescriptorTable(descriptorTable, update);

	return index;
}

void GRP::BeginRenderPass(const char* name)
{
	RenderPassFrame& f = renderPasses[GetFrameIndex()];
	if(f.count >= ARRAY_LEN(f.passes))
	{
		Q_assert(0);
		return;
	}

	CmdBeginDebugLabel(name);

	RenderPassQueries& q = f.passes[f.count++];
	Q_strncpyz(q.name, name, sizeof(q.name));
	q.cpuStartUS = Sys_Microseconds();
	q.query = CmdBeginDurationQuery(name);
}

void GRP::EndRenderPass()
{
	RenderPassFrame& f = renderPasses[GetFrameIndex()];
	if(f.count == 0)
	{
		Q_assert(0);
		return;
	}

	CmdEndDebugLabel();

	RenderPassQueries& q = f.passes[f.count - 1];
	q.cpuDurationUS = (uint32_t)(Sys_Microseconds() - q.cpuStartUS);
	CmdEndDurationQuery(q.query);
}

uint32_t GRP::CreatePSO(CachedPSO& cache)
{
	Q_assert(psoCount < ARRAY_LEN(psos));

	uint32_t a = 0;
	GraphicsPipelineDesc desc("opaque", opaqueRootSignature);
	desc.vertexShader = CompileShader(ShaderStage::Vertex, opaqueShaderSource, "main");
	desc.pixelShader = CompileShader(ShaderStage::Pixel, opaqueShaderSource, "main");
	desc.vertexLayout.AddAttribute(a++, ShaderSemantic::Position, DataType::Float32, 3, 0);
	desc.vertexLayout.AddAttribute(a++, ShaderSemantic::Normal, DataType::Float32, 2, 0);
	for(int s = 0; s < cache.stageCount; ++s)
	{
		desc.vertexLayout.AddAttribute(a++, ShaderSemantic::TexCoord, DataType::Float32, 2, 0);
		desc.vertexLayout.AddAttribute(a++, ShaderSemantic::Color, DataType::UNorm8, 4, 0);
	}
	desc.depthStencil.depthComparison = ComparisonFunction::GreaterEqual;
	desc.depthStencil.depthStencilFormat = TextureFormat::Depth32_Float;
	desc.depthStencil.enableDepthTest = true;
	desc.depthStencil.enableDepthWrites = true;
	desc.rasterizer.cullMode = CT_FRONT_SIDED;
	desc.AddRenderTarget(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO, TextureFormat::RGBA32_UNorm);
	cache.pipeline = CreateGraphicsPipeline(desc);

	const uint32_t index = psoCount++;
	psos[index] = cache;

	return index;
}

// @TODO: move out
IRenderPipeline* renderPipeline = &grp;
