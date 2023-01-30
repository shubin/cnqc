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
// Gameplay Rendering Pipeline - Enhanced Subpixel Morphological Antialiasing


#include "grp_local.h"
#include "smaa_area_texture.h"
#include "smaa_search_texture.h"
namespace pass_1
{
#include "hlsl/smaa_1_vs.h"
#include "hlsl/smaa_1_ps.h"
}
namespace pass_2
{
#include "hlsl/smaa_2_vs.h"
#include "hlsl/smaa_2_ps.h"
}
namespace pass_3
{
#include "hlsl/smaa_3_vs.h"
#include "hlsl/smaa_3_ps.h"
}


// SMAA has 3 passes:
// 1. edge detection
// 2. blend weight computation
// 3. neighborhood blending


/*
to do:

- render the result into grp.renderTarget
- transform into and away from gamma space
- evaluate perf. using a 2-channel edge texture
- evaluate perf. using the depth/stencil texture
- CVar (r_aa to pick a mode? none, SMAA, CMAA 2, etc)
*/


#pragma pack(push, 4)
struct RootConstants
{
	vec4_t rtMetrics;
};
#pragma pack(pop)


void SMAA::Init()
{
	if(grp.firstInit)
	{
		{
			TextureDesc desc("SMAA area", AREATEX_WIDTH, AREATEX_HEIGHT);
			desc.initialState = ResourceStates::PixelShaderAccessBit;
			desc.allowedState = ResourceStates::PixelShaderAccessBit;
			desc.committedResource = true;
			desc.format = TextureFormat::RG16_UNorm;
			areaTexture = CreateTexture(desc);

			MappedTexture texture;
			BeginTextureUpload(texture, areaTexture);
			Q_assert(texture.srcRowByteCount == AREATEX_PITCH);
			for(uint32_t r = 0; r < AREATEX_HEIGHT; ++r)
			{
				memcpy(texture.mappedData + r * texture.dstRowByteCount, areaTexBytes + r * texture.srcRowByteCount, texture.srcRowByteCount);
			}
			EndTextureUpload(areaTexture);
		}
		{
			TextureDesc desc("SMAA search", SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT);
			desc.initialState = ResourceStates::PixelShaderAccessBit;
			desc.allowedState = ResourceStates::PixelShaderAccessBit;
			desc.committedResource = true;
			desc.format = TextureFormat::R8_UNorm;
			searchTexture = CreateTexture(desc);

			MappedTexture texture;
			BeginTextureUpload(texture, searchTexture);
			Q_assert(texture.srcRowByteCount == SEARCHTEX_PITCH);
			for(uint32_t r = 0; r < SEARCHTEX_HEIGHT; ++r)
			{
				memcpy(texture.mappedData + r * texture.dstRowByteCount, searchTexBytes + r * texture.srcRowByteCount, texture.srcRowByteCount);
			}
			EndTextureUpload(searchTexture);
		}
		{
			RootSignatureDesc desc("SMAA");
			desc.constants[ShaderStage::Vertex].byteCount = sizeof(RootConstants);
			desc.constants[ShaderStage::Pixel].byteCount = sizeof(RootConstants);
			desc.samplerVisibility = ShaderStages::PixelBit;
			desc.samplerCount = 2;
			desc.genericVisibility = ShaderStages::PixelBit;
			desc.AddRange(DescriptorType::Texture, 0, 5);
			rootSignature = CreateRootSignature(desc);
		}
		{
			DescriptorTableDesc desc("SMAA", rootSignature);
			descriptorTable = CreateDescriptorTable(desc);

			const HSampler samplers[] =
			{
				// can do linear & point or linear & linear
				// it seems point was some GCN-specific optimization
				grp.samplers[GetSamplerIndex(TW_CLAMP_TO_EDGE, TextureFilter::Linear)],
				grp.samplers[GetSamplerIndex(TW_CLAMP_TO_EDGE, TextureFilter::Linear)]
			};
			DescriptorTableUpdate update;
			update.SetSamplers(ARRAY_LEN(samplers), samplers);
			UpdateDescriptorTable(descriptorTable, update);
		}
		{
			
			GraphicsPipelineDesc desc("SMAA edge detection", rootSignature);
			desc.vertexShader = ShaderByteCode(pass_1::g_vs);
			desc.pixelShader = ShaderByteCode(pass_1::g_ps);
			desc.depthStencil.DisableDepth();
			desc.rasterizer.cullMode = CT_TWO_SIDED;
			desc.AddRenderTarget(0, TextureFormat::RG16_UNorm);
			firstPassPipeline = CreateGraphicsPipeline(desc);
		}
		{
			GraphicsPipelineDesc desc("SMAA blend weight computation", rootSignature);
			desc.vertexShader = ShaderByteCode(pass_2::g_vs);
			desc.pixelShader = ShaderByteCode(pass_2::g_ps);
			desc.depthStencil.DisableDepth();
			desc.rasterizer.cullMode = CT_TWO_SIDED;
			desc.AddRenderTarget(0, TextureFormat::RGBA32_UNorm);
			secondPassPipeline = CreateGraphicsPipeline(desc);
		}
		{
			GraphicsPipelineDesc desc("SMAA neighborhood blending", rootSignature);
			desc.vertexShader = ShaderByteCode(pass_3::g_vs);
			desc.pixelShader = ShaderByteCode(pass_3::g_ps);
			desc.depthStencil.DisableDepth();
			desc.rasterizer.cullMode = CT_TWO_SIDED;
			desc.AddRenderTarget(0, TextureFormat::RGBA32_UNorm);
			thirdPassPipeline = CreateGraphicsPipeline(desc);
		}
	}

	{
		TextureDesc desc("SMAA edges", glConfig.vidWidth, glConfig.vidHeight);
		desc.initialState = ResourceStates::RenderTargetBit;
		desc.allowedState = ResourceStates::RenderTargetBit | ResourceStates::PixelShaderAccessBit;
		Vector4Clear(desc.clearColor);
		desc.usePreferredClearValue = true;
		desc.committedResource = true;
		desc.format = TextureFormat::RG16_UNorm;
		desc.shortLifeTime = true;
		edgeTexture = CreateTexture(desc);
	}
	{
		TextureDesc desc("SMAA blend", glConfig.vidWidth, glConfig.vidHeight);
		desc.initialState = ResourceStates::RenderTargetBit;
		desc.allowedState = ResourceStates::RenderTargetBit | ResourceStates::PixelShaderAccessBit;
		Vector4Clear(desc.clearColor);
		desc.usePreferredClearValue = true;
		desc.committedResource = true;
		desc.format = TextureFormat::RGBA32_UNorm;
		desc.shortLifeTime = true;
		blendTexture = CreateTexture(desc);
	}
	{
		TextureDesc desc("SMAA destination", glConfig.vidWidth, glConfig.vidHeight);
		desc.initialState = ResourceStates::RenderTargetBit;
		desc.allowedState = ResourceStates::RenderTargetBit | ResourceStates::PixelShaderAccessBit;
		desc.committedResource = true;
		desc.format = TextureFormat::RGBA32_UNorm;
		desc.shortLifeTime = true;
		destTexture = CreateTexture(desc);
	}
	{
		//Depth24_Stencil8:
		//DXGI_FORMAT_R24G8_TYPELESS
		//DXGI_FORMAT_D24_UNORM_S8_UINT
		//DXGI_FORMAT_R24_UNORM_X8_TYPELESS
		TextureDesc desc("SMAA stencil buffer", glConfig.vidWidth, glConfig.vidHeight);
		desc.initialState = ResourceStates::DepthWriteBit;
		desc.allowedState = ResourceStates::DepthAccessBits | ResourceStates::PixelShaderAccessBit;
		desc.committedResource = true;
		desc.format = TextureFormat::Depth24_Stencil8;
		desc.shortLifeTime = true;
		stencilTexture = CreateTexture(desc);
	}
	{
		const HTexture textures[] =
		{
			destTexture, edgeTexture, areaTexture, searchTexture, blendTexture
		};

		DescriptorTableUpdate update;
		update.SetTextures(ARRAY_LEN(textures), textures);
		UpdateDescriptorTable(descriptorTable, update);
	}
}

void SMAA::Draw(const viewParms_t& parms)
{
	if(r_smaa->integer == 0)
	{
		return;
	}

	SCOPED_RENDER_PASS("SMAA", 0.5f, 0.25f, 0.75f);

	// can't clear targets if they're not in render target state
	const TextureBarrier clearBarriers[2] =
	{
		TextureBarrier(edgeTexture, ResourceStates::RenderTargetBit),
		TextureBarrier(blendTexture, ResourceStates::RenderTargetBit)
	};
	CmdBarrier(ARRAY_LEN(clearBarriers), clearBarriers);

	CmdClearColorTarget(edgeTexture, vec4_zero);
	CmdClearColorTarget(blendTexture, vec4_zero);

	CmdSetViewport(0, 0, glConfig.vidWidth, glConfig.vidHeight);
	CmdSetScissor(parms.viewportX, parms.viewportY, parms.viewportWidth, parms.viewportHeight);

	// move to gamma space for higher quality AA
	{
		const TextureBarrier barriers[2] =
		{
			TextureBarrier(grp.renderTarget, ResourceStates::PixelShaderAccessBit),
			TextureBarrier(destTexture, ResourceStates::RenderTargetBit)
		};
		CmdBarrier(ARRAY_LEN(barriers), barriers);

		CmdBindRenderTargets(1, &destTexture, NULL);
		grp.post.ToneMap(grp.renderTarget);
	}

	CmdBindRootSignature(rootSignature);
	CmdBindDescriptorTable(rootSignature, descriptorTable);

	RootConstants rc = {};
	rc.rtMetrics[0] = 1.0f / (float)glConfig.vidWidth;
	rc.rtMetrics[1] = 1.0f / (float)glConfig.vidHeight;
	rc.rtMetrics[2] = (float)glConfig.vidWidth;
	rc.rtMetrics[3] = (float)glConfig.vidHeight;
	CmdSetRootConstants(rootSignature, ShaderStage::Vertex, &rc);
	CmdSetRootConstants(rootSignature, ShaderStage::Pixel, &rc);

	// run edge detection
	{
		const TextureBarrier barrier(destTexture, ResourceStates::PixelShaderAccessBit);
		CmdBarrier(1, &barrier);

		// @TODO: stencilTexture
		CmdBindRenderTargets(1, &edgeTexture, NULL);
		CmdBindPipeline(firstPassPipeline);
		CmdDraw(3, 0);
	}

	// compute blend weights
	{
		const TextureBarrier barrier(edgeTexture, ResourceStates::PixelShaderAccessBit);
		CmdBarrier(1, &barrier);

		// @TODO: stencilTexture
		CmdBindRenderTargets(1, &blendTexture, NULL);
		CmdBindPipeline(secondPassPipeline);
		CmdDraw(3, 0);
	}

	// blend pass
	{
		const TextureBarrier barriers[2] =
		{
			TextureBarrier(blendTexture, ResourceStates::PixelShaderAccessBit),
			TextureBarrier(destTexture, ResourceStates::RenderTargetBit)
		};
		CmdBarrier(ARRAY_LEN(barriers), barriers);

		// @TODO: stencilTexture
		CmdBindRenderTargets(1, &destTexture, NULL);
		CmdBindPipeline(thirdPassPipeline);
		CmdDraw(3, 0);
	}

	// apply the inverse of our post-process to the SMAA result
	// to move back into linear space (since we must still render UI/HUD etc.)
	{
		const TextureBarrier barriers[2] =
		{
			TextureBarrier(destTexture, ResourceStates::PixelShaderAccessBit),
			TextureBarrier(grp.renderTarget, ResourceStates::RenderTargetBit)
		};
		CmdBarrier(ARRAY_LEN(barriers), barriers);

		CmdBindRenderTargets(1, &grp.renderTarget, NULL);
		grp.post.InverseToneMap(destTexture);
	}
}
