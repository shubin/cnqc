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
// Rendering Hardware Interface - private interface


#pragma once


#include "tr_local.h"


namespace RHI
{
	// @TODO: turn into uint32_t constants too
#define RHI_MAX_TEXTURES_2D 4096 // real max: unlimited
#define RHI_MAX_RW_TEXTURES_2D 64 // real max: 64
#define RHI_MAX_SAMPLERS 64 // real max: 2048

	// this has 2 meanings:
	// 1. maximum number of frames queued
	// 2. number of frames in the back buffer
	const uint32_t FrameCount = 2;
	const uint32_t MaxVertexBufferCount = 16;
	const uint32_t MaxVertexAttributeCount = 16;
	const uint32_t MaxRenderTargetCount = 8;
	const uint32_t MaxDurationQueries = 64;
	const uint32_t MaxTextureMips = 16;
	const uint32_t InvalidDescriptorIndex = UINT16_MAX;

#define RHI_ENUM_OPERATORS(EnumType) \
	inline EnumType operator|(EnumType a, EnumType b) { return (EnumType)((uint32_t)(a) | (uint32_t)(b)); } \
	inline EnumType operator&(EnumType a, EnumType b) { return (EnumType)((uint32_t)(a) & (uint32_t)(b)); } \
	inline EnumType operator|=(EnumType& a, EnumType b) { return a = (a | b); } \
	inline EnumType operator&=(EnumType& a, EnumType b) { return a = (a & b); } \
	inline EnumType operator~(EnumType a) { return (EnumType)(~(uint32_t)(a)); }

#define RHI_BIT(Bit) (1 << Bit)
#define RHI_BIT_MASK(BitCount) ((1 << BitCount) - 1)

#define RHI_GET_HANDLE_VALUE(Handle) (Handle.v)
#define RHI_MAKE_HANDLE(Value) { Value }
#define RHI_MAKE_NULL_HANDLE() { 0 }

	struct IndexType
	{
		enum Id
		{
			UInt32,
			UInt16,
			Count
		};
	};

	struct ResourceStates
	{
		enum Flags
		{
			Common = 0,
			VertexBufferBit = RHI_BIT(0),
			IndexBufferBit = RHI_BIT(1),
			ConstantBufferBit = RHI_BIT(2),
			RenderTargetBit = RHI_BIT(3),
			VertexShaderAccessBit = RHI_BIT(4),
			PixelShaderAccessBit = RHI_BIT(5),
			ComputeShaderAccessBit = RHI_BIT(6),
			CopySourceBit = RHI_BIT(7),
			CopyDestinationBit = RHI_BIT(8),
			DepthReadBit = RHI_BIT(9),
			DepthWriteBit = RHI_BIT(10),
			UnorderedAccessBit = RHI_BIT(11),
			ShaderAccessBits = VertexShaderAccessBit | PixelShaderAccessBit | ComputeShaderAccessBit,
			DepthAccessBits = DepthReadBit | DepthWriteBit
		};
	};
	RHI_ENUM_OPERATORS(ResourceStates::Flags);

	struct MemoryUsage
	{
		enum Id
		{
			CPU, // Host
			GPU, // DeviceLocal
			Upload, // CPU -> GPU
			Readback, // GPU -> CPU
			Count
		};
	};

	struct ShaderStage
	{
		enum Id
		{
			Vertex,
			Pixel,
			Compute,
			Count
		};
	};

	struct ShaderStages
	{
		enum Flags
		{
			None = 0,
			VertexBit = 1 << ShaderStage::Vertex,
			PixelBit = 1 << ShaderStage::Pixel,
			ComputeBit = 1 << ShaderStage::Compute,
			AllGraphicsBits = VertexBit | PixelBit
		};
	};
	RHI_ENUM_OPERATORS(ShaderStages::Flags);

	struct DataType
	{
		enum Id
		{
			Float32,
			UNorm8,
			UInt32,
			Count
		};
	};

	struct ShaderSemantic
	{
		enum Id
		{
			Position,
			Normal,
			TexCoord,
			Color,
			Count
		};
	};

	struct TextureFormat
	{
		enum Id
		{
			RGBA32_UNorm,
			RGBA64_Float,
			DepthStencil32_UNorm24_UInt8,
			Count
		};
	};

	struct ComparisonFunction
	{
		enum Id
		{
			Never,
			Less,
			Equal,
			LessEqual,
			Greater,
			NotEqual,
			GreaterEqual,
			Always,
			Count
		};
	};

	struct CullMode
	{
		enum Id
		{
			None,
			Front,
			Back,
			Count
		};
	};

	struct DescriptorType
	{
		enum Id
		{
			Buffer, // CBV, HBuffer
			RWBuffer, // UAV, HBuffer
			Texture, // SRV, HTexture
			RWTexture, // UAV, HTexture
			Sampler,
			Count
		};
	};

	struct TextureFilter
	{
		enum Id
		{
			Point,
			Linear,
			Anisotropic,
			Count
		};
	};

	struct PipelineType
	{
		enum Id
		{
			Graphics,
			Compute,
			Count
		};
	};

	struct RootSignatureDesc
	{
		RootSignatureDesc() = default;
		RootSignatureDesc(const char* name_)
		{
			name = name_;
		}

		const char* name = NULL;
		bool usingVertexBuffers = false;
		struct PerStageConstants
		{
			uint32_t byteCount = 0;
		}
		constants[ShaderStage::Count];
		struct DescriptorRange
		{
			DescriptorType::Id type = DescriptorType::Count;
			uint32_t firstIndex = 0;
			uint32_t count = 0;
		}
		genericRanges[64];
		uint32_t genericRangeCount = 0;
		uint32_t samplerCount = 0;
		ShaderStages::Flags genericVisibility = ShaderStages::None; // ignored by compute pipelines
		ShaderStages::Flags samplerVisibility = ShaderStages::None; // ignored by compute pipelines
		PipelineType::Id pipelineType = PipelineType::Graphics;

		void AddRange(DescriptorType::Id type, uint32_t firstIndex, uint32_t count)
		{
			Q_assert(genericRangeCount < ARRAY_LEN(genericRanges));
			DescriptorRange& r = genericRanges[genericRangeCount++];
			r.type = type;
			r.firstIndex = firstIndex;
			r.count = count;
		}
	};

	struct ShaderByteCode
	{
		const void* data = NULL;
		uint32_t byteCount = 0;
	};

	struct VertexAttribute
	{
		uint32_t vertexBufferIndex = 0; // also called "binding" or "input slot"
		ShaderSemantic::Id semantic = ShaderSemantic::Count; // intended usage
		DataType::Id dataType = DataType::Count; // for a single component of the vector
		uint32_t vectorLength = 4; // number of components per vector
		uint32_t structByteOffset = 0; // where in the struct to look when using interleaved data
	};

	struct GraphicsPipelineDesc
	{
		GraphicsPipelineDesc() = default;
		GraphicsPipelineDesc(const char* name_, HRootSignature rootSignature_)
		{
			name = name_;
			rootSignature = rootSignature_;
		}

		const char* name = NULL;
		HRootSignature rootSignature = RHI_MAKE_NULL_HANDLE();
		ShaderByteCode vertexShader;
		ShaderByteCode pixelShader;
		struct VertexLayout
		{
			VertexAttribute attributes[MaxVertexAttributeCount];
			uint32_t attributeCount = 0;
			uint32_t bindingStrides[MaxVertexBufferCount] = { 0 }; // total byte size of a vertex for each buffer

			void AddAttribute(
				uint32_t vertexBufferIndex,
				ShaderSemantic::Id semantic,
				DataType::Id dataType,
				uint32_t vectorLength,
				uint32_t structByteOffset)
			{
				Q_assert(attributeCount < MaxVertexAttributeCount);
				VertexAttribute& va = attributes[attributeCount++];
				va.dataType = dataType;
				va.semantic = semantic;
				va.structByteOffset = structByteOffset;
				va.vectorLength = vectorLength;
				va.vertexBufferIndex = vertexBufferIndex;
			}
		}
		vertexLayout;
		struct DepthStencil
		{
			bool enableDepthTest = true;
			bool enableDepthWrites = true;
			ComparisonFunction::Id depthComparison = ComparisonFunction::GreaterEqual;
			TextureFormat::Id depthStencilFormat = TextureFormat::DepthStencil32_UNorm24_UInt8;
		}
		depthStencil;
		struct Rasterizer
		{
			CullMode::Id cullMode = CullMode::Front;
			// @TODO: depth bias options for polygonOffset
		}
		rasterizer;
		struct RenderTarget
		{
			// @TODO:
			//uint32_t q3BlendMode = GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA;
			uint32_t q3BlendMode = 0x00000005 | 0x00000060;
			TextureFormat::Id format = TextureFormat::RGBA32_UNorm;
		}
		renderTargets[MaxRenderTargetCount];
		uint32_t renderTargetCount = 0;

		void AddRenderTarget(uint32_t q3BlendMode, TextureFormat::Id format)
		{
			Q_assert(renderTargetCount < MaxRenderTargetCount);
			RenderTarget& rt = renderTargets[renderTargetCount++];
			rt.q3BlendMode = q3BlendMode;
			rt.format = format;
		}
	};

	struct ComputePipelineDesc
	{
		ComputePipelineDesc() = default;
		ComputePipelineDesc(const char* name_, HRootSignature rootSignature_)
		{
			name = name_;
			rootSignature = rootSignature_;
		}

		const char* name = NULL;
		HRootSignature rootSignature = RHI_MAKE_NULL_HANDLE();
		ShaderByteCode shader;
	};

	struct BufferDesc
	{
		BufferDesc() = default;
		BufferDesc(const char* name_, uint32_t byteCount_, ResourceStates::Flags initialState_)
		{
			name = name_;
			byteCount = byteCount_;
			initialState = initialState_;
			memoryUsage = MemoryUsage::Upload;
			committedResource = true;
		}

		const char* name = NULL;
		uint32_t byteCount = 0;
		ResourceStates::Flags initialState = ResourceStates::Common;
		MemoryUsage::Id memoryUsage = MemoryUsage::Upload;
		bool committedResource = true;
	};

	struct TextureDesc
	{
		TextureDesc() = default;
		TextureDesc(const char* name_, uint32_t width_, uint32_t height_, uint32_t mipCount_ = 1)
		{
			name = name_;
			width = width_;
			height = height_;
			mipCount = mipCount_;
			sampleCount = 1;
			initialState = ResourceStates::PixelShaderAccessBit;
			allowedState = ResourceStates::PixelShaderAccessBit;
			format = TextureFormat::RGBA32_UNorm;
			committedResource = true;
			usePreferredClearValue = false;
		}

		const char* name = NULL;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t mipCount = 1;
		uint32_t sampleCount = 1;
		ResourceStates::Flags initialState = ResourceStates::PixelShaderAccessBit;
		ResourceStates::Flags allowedState = ResourceStates::PixelShaderAccessBit;
		TextureFormat::Id format = TextureFormat::RGBA32_UNorm;
		bool committedResource = true;
		bool usePreferredClearValue = false; // for render targets and depth/stencil buffers
		float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		float clearDepth = 1.0f;
		byte clearStencil = 0;

		void SetClearColor(const vec4_t rgba)
		{
			usePreferredClearValue = true;
			Vector4Copy(rgba, clearColor);
		}

		void SetClearDepthStencil(float depth, byte stencil = 0)
		{
			usePreferredClearValue = true;
			clearDepth = depth;
			clearStencil = stencil;
		}
	};

	struct SamplerDesc
	{
		SamplerDesc() = default;
		SamplerDesc(textureWrap_t wrapMode_, TextureFilter::Id filterMode_)
		{
			wrapMode = wrapMode_;
			filterMode = filterMode_;
		}

		// @TODO:
		//textureWrap_t wrapMode = TW_REPEAT;
		textureWrap_t wrapMode = (textureWrap_t)0;
		TextureFilter::Id filterMode = TextureFilter::Linear;
	};

	struct TextureUpload
	{
		TextureUpload() = default;

		TextureUpload(uint32_t width_, uint32_t height_, const void* data_)
		{
			x = 0;
			y = 0;
			width = width_;
			height = height_;
			data = data_;
		}

		TextureUpload(uint32_t x_, uint32_t y_, uint32_t width_, uint32_t height_, const void* data_)
		{
			x = x_;
			y = y_;
			width = width_;
			height = height_;
			data = data_;
		}

		const void* data;
		uint32_t x;
		uint32_t y;
		uint32_t width;
		uint32_t height;
	};

	struct DescriptorTableDesc
	{
		DescriptorTableDesc() = default;
		DescriptorTableDesc(const char* name_, HRootSignature rootSignature_)
		{
			name = name_;
			rootSignature = rootSignature_;
		}

		const char* name = NULL;
		HRootSignature rootSignature = RHI_MAKE_NULL_HANDLE();
	};

	struct BufferBarrier
	{
		BufferBarrier() = default;
		BufferBarrier(HBuffer buffer_, ResourceStates::Flags newState_)
		{
			buffer = buffer_;
			newState = newState_;
		}

		HBuffer buffer = RHI_MAKE_NULL_HANDLE();
		ResourceStates::Flags newState = ResourceStates::Common;
	};

	struct TextureBarrier
	{
		TextureBarrier() = default;
		TextureBarrier(HTexture texture_, ResourceStates::Flags newState_)
		{
			texture = texture_;
			newState = newState_;
		}

		HTexture texture = RHI_MAKE_NULL_HANDLE();
		ResourceStates::Flags newState = ResourceStates::Common;
	};

	struct DescriptorTableUpdate
	{
		uint32_t firstIndex = 0;
		uint32_t resourceCount = 0;
		DescriptorType::Id type = DescriptorType::Count;
		union // based on type
		{
			const HTexture* textures = NULL;
			const HBuffer* buffers;
			const HSampler* samplers;
		};
		uint32_t uavMipSlice = 0; // UAV textures: bind this specific mip
		bool uavMipChain = false; // UAV textures: bind all mips if true, the specific mip slice otherwise

		void SetSamplers(uint32_t count, const HSampler* samplers_, uint32_t tableIndex = 0)
		{
			firstIndex = tableIndex;
			resourceCount = count;
			type = DescriptorType::Sampler;
			samplers = samplers_;
		}

		void SetBuffers(uint32_t count, const HBuffer* buffers_, uint32_t tableIndex = 0)
		{
			firstIndex = tableIndex;
			resourceCount = count;
			type = DescriptorType::Buffer;
			buffers = buffers_;
		}

		void SetRWBuffers(uint32_t count, const HBuffer* buffers_, uint32_t tableIndex = 0)
		{
			firstIndex = tableIndex;
			resourceCount = count;
			type = DescriptorType::RWBuffer;
			buffers = buffers_;
		}

		void SetTextures(uint32_t count, const HTexture* textures_, uint32_t tableIndex = 0)
		{
			firstIndex = tableIndex;
			resourceCount = count;
			type = DescriptorType::Texture;
			textures = textures_;
		}

		void SetRWTexturesSlice(uint32_t count, const HTexture* textures_, uint32_t tableIndex = 0, uint32_t slice = 0)
		{
			firstIndex = tableIndex;
			resourceCount = count;
			type = DescriptorType::RWTexture;
			textures = textures_;
			uavMipChain = false;
			uavMipSlice = slice;
		}

		void SetRWTexturesChain(uint32_t count, const HTexture* textures_, uint32_t tableIndex = 0)
		{
			firstIndex = tableIndex;
			resourceCount = count;
			type = DescriptorType::RWTexture;
			textures = textures_;
			uavMipChain = true;
			uavMipSlice = 0;
		}
	};

	void Init();
	void ShutDown(qbool destroyWindow);

	void BeginFrame();
	void EndFrame();

	uint32_t GetFrameIndex();

	HBuffer CreateBuffer(const BufferDesc& desc);
	void DestroyBuffer(HBuffer buffer);
	void* MapBuffer(HBuffer buffer);
	void UnmapBuffer(HBuffer buffer);

	HTexture CreateTexture(const TextureDesc& desc);
	void UploadTextureMip0(HTexture texture, const TextureUpload& desc);
	void FinishTextureUpload(HTexture texture);
	void DestroyTexture(HTexture texture);

	HSampler CreateSampler(const SamplerDesc& sampler);
	void DestroySampler(HSampler sampler);

	HRootSignature CreateRootSignature(const RootSignatureDesc& desc);
	void DestroyRootSignature(HRootSignature signature);

	HDescriptorTable CreateDescriptorTable(const DescriptorTableDesc& desc);
	void UpdateDescriptorTable(HDescriptorTable table, const DescriptorTableUpdate& update);
	void DestroyDescriptorTable(HDescriptorTable table);

	HPipeline CreateGraphicsPipeline(const GraphicsPipelineDesc& desc);
	HPipeline CreateComputePipeline(const ComputePipelineDesc& desc);
	void DestroyPipeline(HPipeline pipeline);

	void CmdBindRenderTargets(uint32_t colorCount, const HTexture* colorTargets, const HTexture* depthStencilTarget);
	void CmdBindRootSignature(HRootSignature rootSignature);
	void CmdBindDescriptorTable(HRootSignature sigHandle, HDescriptorTable table);
	void CmdBindPipeline(HPipeline pipeline);
	void CmdBindVertexBuffers(uint32_t count, const HBuffer* vertexBuffers, const uint32_t* byteStrides, const uint32_t* startByteOffsets);
	void CmdBindIndexBuffer(HBuffer indexBuffer, IndexType::Id type, uint32_t startByteOffset);
	void CmdSetViewport(uint32_t x, uint32_t y, uint32_t w, uint32_t h, float minDepth = 0.0f, float maxDepth = 1.0f);
	void CmdSetScissor(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
	void CmdSetRootConstants(HRootSignature rootSignature, ShaderStage::Id shaderType, const void* constants);
	void CmdDraw(uint32_t vertexCount, uint32_t firstVertex);
	void CmdDrawIndexed(uint32_t indexCount, uint32_t firstIndex, uint32_t firstVertex);
	void CmdDispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
	HDurationQuery CmdBeginDurationQuery(const char* name);
	void CmdEndDurationQuery(HDurationQuery query);
	void CmdBarrier(uint32_t texCount, const TextureBarrier* textures, uint32_t buffCount = 0, const BufferBarrier* buffers = NULL);

#if 0
	void CmdCopyBuffer(HBuffer dst, uint32_t dstOffset, HBuffer src, uint32_t srcOffset, uint32_t byteCount);
	void CmdCopyBufferRegions(HBuffer dst, HBuffer src, uint32_t count, const BufferRegion* regions);

	void CmdClearColorTexture(HTexture texture, const uint32_t* clearColor);

	void CmdInsertDebugLabel(const char* name, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
	void CmdBeginDebugLabel(const char* name, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
	void CmdEndDebugLabel();
#endif

#define CNQ3_DEV
#if defined(_DEBUG) || defined(CNQ3_DEV)
	ShaderByteCode CompileVertexShader(const char* source);
	ShaderByteCode CompilePixelShader(const char* source);
	ShaderByteCode CompileComputeShader(const char* source);
#endif

	const Handle HandleIndexBitCount = 16;
	const Handle HandleIndexBitOffset = 0;
	const Handle HandleGenBitCount = 10;
	const Handle HandleGenBitOffset = 16;
	const Handle HandleTypeBitCount = 6;
	const Handle HandleTypeBitOffset = 26;

	inline Handle CreateHandle(Handle type, Handle index, Handle generation)
	{
		return
			(type << HandleTypeBitOffset) |
			(index << HandleIndexBitOffset) |
			(generation << HandleGenBitOffset);
	}

	inline void DecomposeHandle(Handle* type, Handle* index, Handle* generation, Handle handle)
	{
		*type = (handle >> HandleTypeBitOffset) & RHI_BIT_MASK(HandleTypeBitCount);
		*index = (handle >> HandleIndexBitOffset) & RHI_BIT_MASK(HandleIndexBitCount);
		*generation = (handle >> HandleGenBitOffset) & RHI_BIT_MASK(HandleGenBitCount);
	}

	template<typename T>
	bool IsNullHandle(T handle)
	{
		return RHI_GET_HANDLE_VALUE(handle) == 0;
	}

	template<typename T, typename HT, RHI::Handle RT, int N>
	struct StaticPool
	{
	private:
		struct Item
		{
			T item;
			uint16_t generation;
			uint16_t next : 15;
			uint16_t used : 1;
		};

	public:
		StaticPool()
		{
			Clear();
		}

		void Clear()
		{
			freeList = 0;
			for(int i = 0; i < N; ++i)
			{
				At(i).generation = 0;
				At(i).used = 0;
				At(i).next = i + 1;
			}
			At(N - 1).next = RHI_BIT_MASK(15);
		}

		HT Add(const T& item)
		{
			if(freeList >= N)
			{
				ri.Error(ERR_FATAL, "The memory pool is full\n");
			}
			At(freeList).item = item;
			At(freeList).used = qtrue;
			const Handle handle = CreateHandle(RT, freeList, At(freeList).generation);
			freeList = At(freeList).next;
			return RHI_MAKE_HANDLE(handle);
		}

		void Remove(HT handle)
		{
			Item& item = GetItemRef(handle);
			if(!item.used)
			{
				ri.Error(ERR_FATAL, "Memory pool item was already freed\n");
			}
			item.generation = (item.generation + 1) & RHI_BIT_MASK(HandleGenBitCount);
			item.used = 0;
			item.next = freeList;
			freeList = (uint16_t)(&item - &At(0));
		}

		T& Get(HT handle)
		{
			return GetItemRef(handle).item;
		}

		T* TryGet(HT handle)
		{
			if(handle == 0)
			{
				return NULL;
			}

			return &GetItemRef(handle).item;
		}

		bool FindNext(T** object, int* index)
		{
			Q_assert(object);
			Q_assert(index);

			for(int i = *index; i < N; ++i)
			{
				if(At(i).used)
				{
					*object = &At(i).item;
					*index = i + 1;
					return true;
				}
			}

			return false;
		}

		bool FindNext(Handle* handle, int* index)
		{
			Q_assert(handle);
			Q_assert(index);

			for(int i = *index; i < N; ++i)
			{
				if(At(i).used)
				{
					*handle = CreateHandle(RT, i, At(i).generation);
					*index = i + 1;
					return true;
				}
			}

			return false;
		}

		int CountUsedSlots() const
		{
			int used = 0;
			for(int i = 0; i < N; ++i)
			{
				if(At(i).used)
				{
					used++;
				}
			}

			return used;
		}

	private:
		StaticPool(const StaticPool<T, HT, RT, N>&);
		void operator=(const StaticPool<T, HT, RT, N>&);

		Item& GetItemRef(HT handle)
		{
			Handle type, index, gen;
			DecomposeHandle(&type, &index, &gen, RHI_GET_HANDLE_VALUE(handle));
			if(type != RT)
			{
				ri.Error(ERR_FATAL, "Invalid memory pool handle (wrong resource type)\n");
			}
			if(index > (Handle)N)
			{
				ri.Error(ERR_FATAL, "Invalid memory pool handle (bad index)\n");
			}

			Item& item = At(index);
			if(!item.used)
			{
				ri.Error(ERR_FATAL, "Invalid memory pool handle (unused slot)\n");
			}

			if(gen > (Handle)item.generation)
			{
				ri.Error(ERR_FATAL, "Invalid memory pool handle (allocation from the future)\n");
			}
			if(gen < (Handle)item.generation)
			{
				ri.Error(ERR_FATAL, "Invalid memory pool handle (the object has been freed)\n");
			}

			return item;
		}

		Item& At(uint32_t index)
		{
			return *(Item*)&items[index * sizeof(Item)];
		}

		const Item& At(uint32_t index) const
		{
			return *(Item*)&items[index * sizeof(Item)];
		}

		byte items[N * sizeof(Item)];
		uint16_t freeList;
	};

	template<typename T, uint32_t N>
	struct StaticUnorderedArray
	{
		StaticUnorderedArray()
		{
			Clear();
		}

		void Add(const T& value)
		{
			Q_assert(count < N);
			if(count >= N)
			{
				return;
			}

			items[count++] = value;
		}

		void Remove(uint32_t index)
		{
			Q_assert(index < N);
			if(count >= N)
			{
				return;
			}

			if(index < count - 1)
			{
				items[index] = items[count - 1];
			}
			count--;
		}

		void Clear()
		{
			count = 0;
		}

		T& operator[](uint32_t index)
		{
			Q_assert(index < N);

			return items[index];
		}

		const T& operator[](uint32_t index) const
		{
			Q_assert(index < N);

			return items[index];
		}

	private:
		StaticUnorderedArray(const StaticUnorderedArray<T, N>&);
		void operator=(const StaticUnorderedArray<T, N>&);

	public:
		T items[N];
		uint32_t count;
	};

	template<typename T, uint32_t N, uint32_t Invalid>
	struct StaticFreeList
	{
		StaticFreeList()
		{
			Clear();
		}

		uint32_t Allocate()
		{
			Q_assert(firstFree != Invalid);
			// @TODO: fatal error in release

			const T index = firstFree;
			firstFree = items[index];
			items[index] = Invalid;
			allocatedItemCount++;

			return index;
		}

		void Free(uint32_t index)
		{
			Q_assert(index < N);
			// @TODO: fatal error in release

			const T oldList = firstFree;
			firstFree = index;
			items[index] = oldList;
			allocatedItemCount--;
		}

		void Clear()
		{
			for(uint32_t i = 0; i < N - 1; ++i)
			{
				items[i] = i + 1;
			}
			items[N - 1] = Invalid;
			firstFree = 0;
			allocatedItemCount = 0;
		}

		T items[N];
		T firstFree;
		uint32_t allocatedItemCount;
	};
}
