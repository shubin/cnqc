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


#pragma pack(push, 1)

struct DynamicVertexRC
{
	float modelViewMatrix[16];
	float projectionMatrix[16];
	float clipPlane[4];
};

struct DynamicPixelRC
{
	uint32_t stageIndices[8];
};

#pragma pack(pop)

struct BufferBase
{
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

	uint32_t totalCount = 0;
	uint32_t batchFirst = 0;
	uint32_t batchCount = 0;
};

struct VertexBuffers : BufferBase
{
	enum BaseId
	{
		BasePosition,
		BaseNormal,
		BaseCount
	};

	enum StageId
	{
		StageTexCoords,
		StageColors,
		StageCount
	};

	void Create(const char* name, MemoryUsage::Id memoryUsage, uint32_t vertexCount)
	{
		totalCount = vertexCount;

		BufferDesc desc = {};
		desc.initialState = ResourceStates::VertexBufferBit;
		desc.memoryUsage = memoryUsage;
		
		desc.name = va("%s position vertex", name);
		desc.byteCount = vertexCount * sizeof(vec3_t);
		buffers[BasePosition] = CreateBuffer(desc);
		strides[BasePosition] = sizeof(vec3_t);

		desc.name = va("%s normal vertex", name);
		desc.byteCount = vertexCount * sizeof(vec3_t);
		buffers[BaseNormal] = CreateBuffer(desc);
		strides[BaseNormal] = sizeof(vec3_t);

		for(uint32_t s = 0; s < MAX_SHADER_STAGES; ++s)
		{
			desc.name = va("%s tex coords #%d vertex", name, (int)s + 1);
			desc.byteCount = vertexCount * sizeof(vec2_t);
			buffers[BaseCount + s * StageCount + StageTexCoords] = CreateBuffer(desc);
			strides[BaseCount + s * StageCount + StageTexCoords] = sizeof(vec2_t);

			desc.name = va("%s color #%d vertex", name, (int)s + 1);
			desc.byteCount = vertexCount * sizeof(color4ub_t);
			buffers[BaseCount + s * StageCount + StageColors] = CreateBuffer(desc);
			strides[BaseCount + s * StageCount + StageColors] = sizeof(color4ub_t);
		}
	}

	void BeginUpload()
	{
		for(uint32_t b = 0; b < BufferCount; ++b)
		{
			mapped[b] = BeginBufferUpload(buffers[b]);
		}
	}

	void EndUpload()
	{
		for(uint32_t b = 0; b < BufferCount; ++b)
		{
			EndBufferUpload(buffers[b]);
			mapped[b] = NULL;
		}
	}

	void Upload(uint32_t firstStage, uint32_t stageCount)
	{
		Q_assert(mapped[0] != NULL);

		const uint32_t batchOffset = batchFirst + batchCount;

		float* pos = (float*)mapped[BasePosition] + 3 * batchOffset;
		for(int v = 0; v < tess.numVertexes; ++v)
		{
			pos[0] = tess.xyz[v][0];
			pos[1] = tess.xyz[v][1];
			pos[2] = tess.xyz[v][2];
			pos += 3;
		}

		float* nor = (float*)mapped[BaseNormal] + 3 * batchOffset;
		for(int v = 0; v < tess.numVertexes; ++v)
		{
			nor[0] = tess.normal[v][0];
			nor[1] = tess.normal[v][1];
			nor[2] = tess.normal[v][2];
			nor += 3;
		}

		for(uint32_t s = 0; s < stageCount; ++s)
		{
			const stageVars_t& sv = tess.svars[s + firstStage];

			uint8_t* const tcBuffer = mapped[BaseCount + s * StageCount + StageTexCoords];
			float* tc = (float*)tcBuffer + 2 * batchOffset;
			memcpy(tc, &sv.texcoords[0], tess.numVertexes * sizeof(vec2_t));

			uint8_t* const colBuffer = mapped[BaseCount + s * StageCount + StageColors];
			uint32_t* col = (uint32_t*)colBuffer + batchOffset;
			memcpy(col, &sv.colors[0], tess.numVertexes * sizeof(color4ub_t));
		}
	}

	static const uint32_t BufferCount = BaseCount + StageCount * MAX_SHADER_STAGES;
	HBuffer buffers[BufferCount] = {};
	uint32_t strides[BufferCount] = {};
	uint8_t* mapped[BufferCount] = {};
};

struct IndexBuffer : BufferBase
{
	void Create(const char* name, MemoryUsage::Id memoryUsage, uint32_t indexCount)
	{
		totalCount = indexCount;

		BufferDesc desc = {};
		desc.initialState = ResourceStates::IndexBufferBit;
		desc.memoryUsage = memoryUsage;
		desc.name = va("%s index", name);
		desc.byteCount = indexCount * sizeof(uint32_t);
		buffer = CreateBuffer(desc);
	}

	void BeginUpload()
	{
		mapped = (uint32_t*)BeginBufferUpload(buffer);
	}

	void EndUpload()
	{
		EndBufferUpload(buffer);
		mapped = NULL;
	}

	void Upload()
	{
		Q_assert(mapped != NULL);

		uint32_t* const idx = mapped + batchFirst + batchCount;
		memcpy(idx, &tess.indexes[0], tess.numIndexes * sizeof(uint32_t));
	}

	HBuffer buffer = RHI_MAKE_NULL_HANDLE();
	uint32_t* mapped = NULL;
};

struct GeometryBuffer : BufferBase
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

	HBuffer buffer = RHI_MAKE_NULL_HANDLE();
	uint32_t byteCount = 0;
	uint32_t stride = 0;
};

struct GeometryBuffers
{
	void Rewind()
	{
		vertexBuffers.Rewind();
		indexBuffer.Rewind();
	}

	VertexBuffers vertexBuffers;
	IndexBuffer indexBuffer;
};

struct World
{
	void Init();
	void BeginFrame();
	void Begin();
	void DrawPrePass();
	void BeginBatch(const shader_t* shader, bool hasStaticGeo);
	void EndBatch();
	void DrawGUI();
	void ProcessWorld(world_t& world);
	void DrawSceneView(const drawSceneViewCommand_t& cmd);
	void BindVertexBuffers(bool staticGeo, uint32_t count);
	void BindIndexBuffer(bool staticGeo);

	typedef uint32_t Index;
	const IndexType::Id indexType = IndexType::UInt32;

	// @TODO: in the future, once backEnd gets nuked
	//trRefdef_t refdef;
	//viewParms_t viewParms;

	HTexture depthTexture;
	uint32_t depthTextureIndex;

	float clipPlane[4];

	struct BufferFamily
	{
		enum Id
		{
			Invalid,
			PrePass,
			Static,
			Dynamic
		};
	};

	// Z pre-pass
	HRootSignature zppRootSignature;
	HDescriptorTable zppDescriptorTable;
	HPipeline zppPipeline; // @TODO: 1 per cull type
	GeometryBuffer zppIndexBuffer;
	GeometryBuffer zppVertexBuffer;

	// shared
	BufferFamily::Id boundVertexBuffers;
	BufferFamily::Id boundIndexBuffer;
	uint32_t boundStaticVertexBuffersCount;
	bool batchHasStaticGeo;

	// dynamic
	GeometryBuffers dynBuffers[FrameCount];

	// static
	GeometryBuffers statBuffers;
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

struct RenderPassQueries
{
	char name[64];
	uint32_t cpuDurationUS;
	uint32_t gpuDurationUS;
	int64_t cpuStartUS;
	HDurationQuery query;
};

struct RenderPassFrame
{
	RenderPassQueries passes[16];
	uint32_t count;
};

#pragma pack(push, 1)

struct PSODesc
{
	cullType_t cullType;
	// @TODO: qbool polygonOffset;
};

#pragma pack(pop)

struct CachedPSO
{
	PSODesc desc;
	uint32_t stageStateBits[MAX_SHADER_STAGES];
	uint32_t stageCount;
	HPipeline pipeline;
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
	void ProcessModel(model_t& model) override;
	void ProcessShader(shader_t& shader) override;

	void UISetColor(const uiSetColorCommand_t& cmd) override { ui.UISetColor(cmd); }
	void UIDrawQuad(const uiDrawQuadCommand_t& cmd) override { ui.UIDrawQuad(cmd); }
	void UIDrawTriangle(const uiDrawTriangleCommand_t& cmd) override { ui.UIDrawTriangle(cmd); }
	void DrawSceneView(const drawSceneViewCommand_t& cmd) override { ui.DrawBatch(); world.DrawSceneView(cmd); }

	uint32_t RegisterTexture(HTexture htexture);

	void BeginRenderPass(const char* name);
	void EndRenderPass();

	uint32_t CreatePSO(CachedPSO& cache);

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

	RenderPassFrame renderPasses[FrameCount];

	CachedPSO psos[1024];
	uint32_t psoCount;
	HRootSignature opaqueRootSignature;
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
