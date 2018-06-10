
// by simon yeung, 06/05/2018
// all rights reserved

#include "RayTracer.h"
#include <stdio.h>

#include <dxgi1_4.h>
#include <D3Dcompiler.h>

#include <vector>


#define SCENE_VTX_MAX			(4096)
#define SCENE_IDX_MAX			(8192)
#define SCENE_MATERIAL_MAX		(128)
#define SCENE_MESH_MAX			(128)
#define MAX_LIGHT				(4)

#define PI						3.14159265358979323846f
#define DEGREE_TO_RADIAN(x)		((x) * (PI/ 180.0f))
#define RADIAN_TO_DEGREE(x)		((x) * (180.0f/ PI))

//#define PATH_TRACE_BUFFER_FORMAT	DXGI_FORMAT_R16G16B16A16_FLOAT
#define PATH_TRACE_BUFFER_FORMAT	DXGI_FORMAT_R32G32B32A32_FLOAT

#define USE_LIGHT_MESH		(0)

LONGLONG	timeGetClockFrequency();
LONGLONG	timeGetAbsoulteTime();

LONGLONG s_clockFreq	= timeGetClockFrequency();
LONGLONG s_clockTime	= timeGetAbsoulteTime();

struct AreaLight
{	// a rect light
	Matrix4x4	xform;
	Matrix4x4	xformInv;
	Vector4		radiance;
	float		halfWidth;
	float		halfHeight;
	float		oneOverArea;
	int			padding0;
};

struct Material
{
	Vector4	albedo;
	Vector4	emissive;
};

struct Vertex
{
	float posX;
	float posY;
};

struct SceneConstantBuffer
{
	AreaLight	areaLight[MAX_LIGHT];	// only hv 1 light for simplicity 
	int			numLight;
	int			numMesh;
	int			padding0;
	int			padding1;
};

struct ViewConstantBuffer
{
	Matrix4x4	projInv;
	Vector3		camPos;
	int			frameIdx;
	Vector2		camPixelOffset;
	int			viewportWidth;
	int			viewportHeight;
	int			randSeedInterval;	// pass different randSeed param per frame to have a better hash rand result
	int			randSeedOffset;
	int			randSeedAdd;
	int			isEnableBlur;
};

LONGLONG	timeGetClockFrequency()
{
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	return freq.QuadPart;
}

LONGLONG	timeGetAbsoulteTime()
{
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return t.QuadPart;
}

double		timeCalculateElapsedTime(LONGLONG clockFreqency, LONGLONG startTime, LONGLONG endTime)
{
	return ((double)(endTime - startTime)) / (double)clockFreqency;
}

void	print(const char* format, ...)
{
	const size_t STR_BUFFER_SIZE = 2048;
	char tmpStrBuffer[STR_BUFFER_SIZE];

	va_list argptr;
	va_start(argptr, format);
	int numOfByteWritten = vsnprintf_s(tmpStrBuffer, STR_BUFFER_SIZE, _TRUNCATE, format, argptr);
	va_end(argptr);

	OutputDebugStringA(tmpStrBuffer);

	if (numOfByteWritten< 0)
		OutputDebugStringA("\n\n\nlkPrintToStream Buffer Size not larger enough\n\n\n");

}

void	addMesh(const float*	pos,
				const float*	nor,
				int				numVtx,
				const int*		idx,
				int				numIdx,
				Material		material,
				std::vector<Vector4	>* triPos,
				std::vector<Vector4	>* triNor,
				std::vector<int		>* triIdx,
				std::vector<Material>* triMeshMaterial,
				std::vector<int2	>* triMeshIdxRange)
{
	int numVtxPrev = (int)triPos->size();
	int numIdxPrev = (int)triIdx->size();
	for(int i=0; i<numVtx; ++i)
	{
		int vtxIdx = i * 3;
		triPos->push_back(Vector4(pos[vtxIdx + 0], pos[vtxIdx + 1], pos[vtxIdx + 2], 1.0f));
		triNor->push_back(Vector4(nor[vtxIdx + 0], nor[vtxIdx + 1], nor[vtxIdx + 2], 0.0f));
	}
	for (int i = 0; i<numIdx; ++i)
		triIdx->push_back(idx[i] + numVtxPrev);
	triMeshMaterial->push_back(material);

	int2 meshRange = { numIdxPrev, numIdxPrev + numIdx };
	triMeshIdxRange->push_back(meshRange);
}

HRESULT D3D12HelperSerializeVersionedRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pRootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION MaxVersion, ID3DBlob** ppBlob, ID3DBlob** ppErrorBlob)
{
	if (MaxVersion == D3D_ROOT_SIGNATURE_VERSION_1_0)
	{
		D3D_ROOT_SIGNATURE_VERSION descVer = pRootSignatureDesc->Version;
		if (descVer == D3D_ROOT_SIGNATURE_VERSION_1_0)
			return D3D12SerializeRootSignature(&pRootSignatureDesc->Desc_1_0, D3D_ROOT_SIGNATURE_VERSION_1, ppBlob, ppErrorBlob);
		else if (descVer == D3D_ROOT_SIGNATURE_VERSION_1_1)
		{
			const int PARAM_NUM_MAX				= 32;
			const int DESCRIPTOR_RANGE_NUM_MAX	= 16;
			D3D12_ROOT_PARAMETER	pParameters_1_0[		PARAM_NUM_MAX];
			D3D12_DESCRIPTOR_RANGE	pDescriptorRanges_1_0[	PARAM_NUM_MAX][DESCRIPTOR_RANGE_NUM_MAX];

			HRESULT hr = S_OK;
			const D3D12_ROOT_SIGNATURE_DESC1& desc_1_1 = pRootSignatureDesc->Desc_1_1;
			if (desc_1_1.NumParameters > PARAM_NUM_MAX)
			{
				print("Root Signature PARAM_NUM_MAX overflow %i/%i.\n", desc_1_1.NumParameters, PARAM_NUM_MAX);
				return E_OUTOFMEMORY;
			}

			for (UINT n = 0; n < desc_1_1.NumParameters; n++)
			{
				pParameters_1_0[n].ParameterType		= desc_1_1.pParameters[n].ParameterType;
				pParameters_1_0[n].ShaderVisibility		= desc_1_1.pParameters[n].ShaderVisibility;

				D3D12_ROOT_PARAMETER_TYPE	paramType	= desc_1_1.pParameters[n].ParameterType;
				if (paramType == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
				{
					pParameters_1_0[n].Constants.Num32BitValues	= desc_1_1.pParameters[n].Constants.Num32BitValues;
					pParameters_1_0[n].Constants.RegisterSpace	= desc_1_1.pParameters[n].Constants.RegisterSpace;
					pParameters_1_0[n].Constants.ShaderRegister	= desc_1_1.pParameters[n].Constants.ShaderRegister;
				}
				else if (	paramType == D3D12_ROOT_PARAMETER_TYPE_CBV ||
							paramType ==  D3D12_ROOT_PARAMETER_TYPE_SRV||
							paramType ==  D3D12_ROOT_PARAMETER_TYPE_UAV)
				{
					pParameters_1_0[n].Descriptor.RegisterSpace	= desc_1_1.pParameters[n].Descriptor.RegisterSpace;
					pParameters_1_0[n].Descriptor.ShaderRegister= desc_1_1.pParameters[n].Descriptor.ShaderRegister;
				}
				else if (	paramType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
				{
					const D3D12_ROOT_DESCRIPTOR_TABLE1& table_1_1 = desc_1_1.pParameters[n].DescriptorTable;

					if (table_1_1.NumDescriptorRanges >= DESCRIPTOR_RANGE_NUM_MAX)
					{
						print("Root Signature DESCRIPTOR_RANGE_NUM_MAX overflow %i/%i.\n", table_1_1.NumDescriptorRanges, DESCRIPTOR_RANGE_NUM_MAX);
						return E_OUTOFMEMORY;
					}

					for (UINT x = 0; x < table_1_1.NumDescriptorRanges; x++)
					{
						pDescriptorRanges_1_0[n][x].BaseShaderRegister					= table_1_1.pDescriptorRanges[x].BaseShaderRegister;
						pDescriptorRanges_1_0[n][x].NumDescriptors						= table_1_1.pDescriptorRanges[x].NumDescriptors;
						pDescriptorRanges_1_0[n][x].OffsetInDescriptorsFromTableStart	= table_1_1.pDescriptorRanges[x].OffsetInDescriptorsFromTableStart;
						pDescriptorRanges_1_0[n][x].RangeType							= table_1_1.pDescriptorRanges[x].RangeType;
						pDescriptorRanges_1_0[n][x].RegisterSpace						= table_1_1.pDescriptorRanges[x].RegisterSpace;
					}

					D3D12_ROOT_DESCRIPTOR_TABLE& table_1_0	= pParameters_1_0[n].DescriptorTable;
					table_1_0.NumDescriptorRanges			= table_1_1.NumDescriptorRanges;
					table_1_0.pDescriptorRanges				= pDescriptorRanges_1_0[n];
				}
			}

			D3D12_ROOT_SIGNATURE_DESC desc_1_0;
			desc_1_0.NumParameters		= desc_1_1.NumParameters;
			desc_1_0.pParameters		= pParameters_1_0;
			desc_1_0.NumStaticSamplers	= desc_1_1.NumStaticSamplers;
			desc_1_0.pStaticSamplers	= desc_1_1.pStaticSamplers;
			desc_1_0.Flags				= desc_1_1.Flags;
			return D3D12SerializeRootSignature(&desc_1_0, D3D_ROOT_SIGNATURE_VERSION_1, ppBlob, ppErrorBlob);
		}
	}
	else if (MaxVersion == D3D_ROOT_SIGNATURE_VERSION_1_1)
	{
		return D3D12SerializeVersionedRootSignature(pRootSignatureDesc, ppBlob, ppErrorBlob);
	}

	return E_INVALIDARG;
}

void	RayTracer::init(int windowWidth, int windowHeight)
{
	m_windowWidth		= windowWidth;;
	m_windowHeight		= windowHeight;
	m_mousePos.x		= 0;
	m_mousePos.y		= 0;
	m_pathTraceFrameIdx	= 0;
	m_isFirstFrame		= true;
	m_isEnableBlur		= true;
	m_isMouseDown		= false;
	for(int i=0; i<Key_Num; ++i)
		m_isKeyDown[i]= false;

	m_camJitter			= Vector2(0, 0);
	m_randSeedOffset	= m_randSeedOffset;
	UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
	// Enable the debug layer (requires the Graphics Tools "optional feature").
	// NOTE: Enabling the debug layer after device creation will invalidate the active device.
	{
		ID3D12Debug*	debugController;
		// currently disable the GPUBasedValidation which may cause exception during root signature creation
//		ID3D12Debug1*	debugController1;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
		{
			debugController->EnableDebugLayer();
//			debugController->QueryInterface(IID_PPV_ARGS(&debugController1));
//			if (debugController1)
//			{
//				debugController1->SetEnableGPUBasedValidation(true);
//				debugController1->SetEnableSynchronizedCommandQueueValidation(true);
//				debugController1->Release();
//			}
			// Enable additional debug layers.
			dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
		}
		debugController->Release();

	}
#endif

	// enumurate adapter
	m_device = nullptr;
	IDXGIFactory4* factory;
	HRESULT hr= CreateDXGIFactory2(dxgiFactoryFlags, _uuidof(IDXGIFactory4), (void**)&factory);
	IDXGIAdapter1* adapter= nullptr;

	for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(adapterIndex, &adapter); ++adapterIndex)
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			// Don't select the Basic Render Driver adapter.
			adapter->Release();
			continue;
		}

		// Check to see if the adapter supports Direct3D 12, but don't create the
		// actual device yet.
		if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_1, _uuidof(ID3D12Device), nullptr)))
			break;
		else
		{
			adapter->Release();
			adapter = nullptr;
		}
	}

	if (adapter == nullptr)
	{
		// notify failed to create DX12 device
		factory->Release();
		return;
	}

	D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_12_1, _uuidof(ID3D12Device), (void**)&m_device);

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	m_device->CreateCommandQueue(&queueDesc, _uuidof(ID3D12CommandQueue), (void**)&m_commandQueue);
	m_commandQueue->SetName(L"direct command queue");

	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
	m_device->CreateCommandQueue(&queueDesc, _uuidof(ID3D12CommandQueue), (void**)&m_copyQueue);
	m_copyQueue->SetName(L"copy command queue");

	// Describe and create the swap chain.
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	memset(&swapChainDesc, 0, sizeof(swapChainDesc));
	swapChainDesc.BufferCount	= FRAME_CNT;
	swapChainDesc.Width			= windowWidth;
	swapChainDesc.Height		= windowHeight;
	swapChainDesc.Format		= DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage	= DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect	= DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;
	IDXGISwapChain1* swapChain;
	factory->CreateSwapChainForHwnd(m_commandQueue, m_hwnd, &swapChainDesc, nullptr, nullptr, &swapChain);
	swapChain->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&m_swapChain);
	swapChain->Release();

	// This sample does not support fullscreen transitions.
	factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// Create descriptor heaps.
	{
		// Describe and create a render target view (RTV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = FRAME_CNT + 1;		// swap chain back buffer RTVs, path tracer tex RTV
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		m_device->CreateDescriptorHeap(&rtvHeapDesc, __uuidof(ID3D12DescriptorHeap), (void**)&m_rtvHeap);

		// Describe and create a constant buffer view (CBV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
		cbvHeapDesc.NumDescriptors = 1 + 5 + 1 + FRAME_CNT;	// 1 path trace SRV, 5 scene buffer, 1 SceneConstantBuffer, FRAME_CNT ViewConstantBuffer
		cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		cbvHeapDesc.Flags= D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		m_device->CreateDescriptorHeap(&cbvHeapDesc, __uuidof(ID3D12DescriptorHeap), (void**)&m_cbSrvHeap);
		m_cbSrvHeap->SetName(L"CB SRV descriptor heap");

		m_rtvDescriptorSize		= m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		m_cbSrvDescriptorSize	= m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	// Create constant buffer.
	{
		D3D12_HEAP_PROPERTIES heapProp;
		heapProp.Type					= D3D12_HEAP_TYPE_UPLOAD;
		heapProp.CPUPageProperty		= D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProp.MemoryPoolPreference	= D3D12_MEMORY_POOL_UNKNOWN;
		heapProp.CreationNodeMask		= 0;
		heapProp.VisibleNodeMask		= 0;

		D3D12_RESOURCE_DESC resDesc;
		resDesc.Dimension			= D3D12_RESOURCE_DIMENSION_BUFFER;
		resDesc.Alignment			= 0;
		resDesc.Width				= 1024 * 64;
		resDesc.Height				= 1;
		resDesc.DepthOrArraySize	= 1;
		resDesc.MipLevels			= 1;
		resDesc.Format				= DXGI_FORMAT_UNKNOWN;
		resDesc.SampleDesc.Count	= 1;
		resDesc.SampleDesc.Quality	= 0;
		resDesc.Layout				= D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resDesc.Flags				= D3D12_RESOURCE_FLAG_NONE;

		m_device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, _uuidof(ID3D12Resource), (void**)&m_constantBuffer);

		// Describe and create a constant buffer view.
		int constantBufferSz[FRAME_CNT + 1];
		constantBufferSz[0] = sizeof(SceneConstantBuffer);
		for (int i = 1; i <= FRAME_CNT; ++i)
			constantBufferSz[i] = sizeof(ViewConstantBuffer);

		int bufferOffset = 0;
		for (int i = 0; i < FRAME_CNT+1; ++i)
		{
			const int cbSz = constantBufferSz[max(i - 1, 0)];
			bufferOffset += i==0 ? 0 : cbSz;
			bufferOffset = (bufferOffset + (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)) & (~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1));
			m_constantBufferOffset[i] = bufferOffset;

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
			cbvDesc.BufferLocation = m_constantBuffer->GetGPUVirtualAddress() + bufferOffset;
			cbvDesc.SizeInBytes = (cbSz + 255) & ~255;	// CB size is required to be 256-byte aligned.
			D3D12_CPU_DESCRIPTOR_HANDLE cbHandle;
			cbHandle.ptr = m_cbSrvHeap->GetCPUDescriptorHandleForHeapStart().ptr + m_cbSrvDescriptorSize * (1+5+i);
			m_device->CreateConstantBufferView(&cbvDesc, cbHandle);
		}

		// init constant buffer content
		BYTE*	pData;
		D3D12_RANGE noReadRange = { 0, 0 };
		m_constantBuffer->Map(0, &noReadRange, (void**)&pData);
		memset(pData, 0, (size_t)resDesc.Width);
		m_constantBuffer->Unmap(0, nullptr);

		m_currentCbIdx = 0;
	}

	// create path trace texture
	createPathTraceTex();

	// Create a RTV and a command allocator for each frame.
	createRTV();
	for (UINT n = 0; n < FRAME_CNT; ++n)
	{
		m_fenceValues[n] = 0;
		m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&m_commandAllocators[n]);
	}

	// Create copy queue resources.
	m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, __uuidof(ID3D12CommandAllocator), (void**)&m_copyCommandAllocator);

	adapter->Release();
	factory->Release();

	// Create a root signature
	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

		// This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

		if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
		{
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
			print("Root Signature 1.1 not supported.\n");
		}

		D3D12_DESCRIPTOR_RANGE1 rangesCBTable;
		rangesCBTable.RangeType							= D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		rangesCBTable.NumDescriptors					= 1;
		rangesCBTable.BaseShaderRegister				= 0;
		rangesCBTable.RegisterSpace						= 0;
		rangesCBTable.Flags								= D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
		rangesCBTable.OffsetInDescriptorsFromTableStart	= D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_DESCRIPTOR_RANGE1 rangesSRV_pathTraceTex;
		rangesSRV_pathTraceTex.RangeType								= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		rangesSRV_pathTraceTex.NumDescriptors							= 1;
		rangesSRV_pathTraceTex.BaseShaderRegister						= 0;
		rangesSRV_pathTraceTex.RegisterSpace							= 0;
		rangesSRV_pathTraceTex.Flags									= D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
		rangesSRV_pathTraceTex.OffsetInDescriptorsFromTableStart		= D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_DESCRIPTOR_RANGE1 rangesSRV;
		rangesSRV.RangeType								= D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		rangesSRV.NumDescriptors						= 5;
		rangesSRV.BaseShaderRegister					= 1;
		rangesSRV.RegisterSpace							= 0;
		rangesSRV.Flags									= D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE;
		rangesSRV.OffsetInDescriptorsFromTableStart		= D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER1 rootParameters[4];
		rootParameters[0].ParameterType							= D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParameters[0].ShaderVisibility						= D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[0].Descriptor.ShaderRegister				= 1;
		rootParameters[0].Descriptor.RegisterSpace				= 0;
		rootParameters[0].Descriptor.Flags						= D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
		
		rootParameters[1].ParameterType							= D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[1].ShaderVisibility						= D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[1].DescriptorTable.NumDescriptorRanges	= 1;
		rootParameters[1].DescriptorTable.pDescriptorRanges		= &rangesCBTable;
		
		rootParameters[2].ParameterType							= D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[2].ShaderVisibility						= D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[2].DescriptorTable.NumDescriptorRanges	= 1;
		rootParameters[2].DescriptorTable.pDescriptorRanges		= &rangesSRV_pathTraceTex;
		
		rootParameters[3].ParameterType							= D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[3].ShaderVisibility						= D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[3].DescriptorTable.NumDescriptorRanges	= 1;
		rootParameters[3].DescriptorTable.pDescriptorRanges		= &rangesSRV;

		// Allow input layout and deny uneccessary access to certain pipeline stages.
		D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =	D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
														D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
														D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
														D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
														D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

		D3D12_STATIC_SAMPLER_DESC samplerDesc;
		samplerDesc.ShaderRegister	= 0;
		samplerDesc.Filter			= D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU		= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplerDesc.AddressV		= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplerDesc.AddressW		= D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplerDesc.MipLODBias		= 0;
		samplerDesc.MaxAnisotropy	= 16;
		samplerDesc.ComparisonFunc	= D3D12_COMPARISON_FUNC_LESS_EQUAL;
		samplerDesc.BorderColor		= D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
		samplerDesc.MinLOD			= 0.f;
		samplerDesc.MaxLOD			= D3D12_FLOAT32_MAX;
		samplerDesc.ShaderVisibility= D3D12_SHADER_VISIBILITY_ALL;
		samplerDesc.RegisterSpace	= 0;

		D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		rootSignatureDesc.Desc_1_1.NumParameters	= _countof(rootParameters);
		rootSignatureDesc.Desc_1_1.pParameters		= rootParameters;
		rootSignatureDesc.Desc_1_1.NumStaticSamplers= 1;
		rootSignatureDesc.Desc_1_1.pStaticSamplers	= &samplerDesc;
		rootSignatureDesc.Desc_1_1.Flags			= rootSignatureFlags;

		ID3DBlob* signature;
		ID3DBlob* error;
		D3D12HelperSerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error);
		if (error)
		{
			OutputDebugString((LPCTSTR)error->GetBufferPointer());
			error->Release();
			error = nullptr;
			m_rootSignature = nullptr;
		}
		else
		{
			m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&m_rootSignature);
			m_rootSignature->SetName(L"Root Signature");
		}

		if (signature)
			signature->Release();
		if (error)
			error->Release();
	}

	// Create the pipeline state, which includes compiling and loading shaders.
	{
		m_pipelineStateToneMap		= createPipelineState("fullscreenQuad_vs"	, "tonemap_ps"	, L"PSO tone map"	, false	, D3D12_BLEND_ZERO			, DXGI_FORMAT_R8G8B8A8_UNORM		);
		m_pipelineStatePathTrace	= createPipelineState("pathTrace_vs"		, "pathTrace_ps", L"PSO path trace"	, true	, D3D12_BLEND_BLEND_FACTOR	, PATH_TRACE_BUFFER_FORMAT			);
	}

	// Create the command lists.
	m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocators[m_frameIndex], nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&m_commandList);
	m_commandList->SetName(L"Direct command list");

	m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, m_copyCommandAllocator, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&m_copyCommandList);
	m_copyCommandList->Close();
	m_copyCommandList->SetName(L"Copy command list");

	// Create the vertex buffer.
	ID3D12Resource* vertexBufferUpload;
	{
		// Create quads for all of the images that will be generated and drawn to the screen.
		Vertex quadVertices[4] =
		{
			-1.0f,  1.0f,
			-1.0f, -1.0f,
			 1.0f,  1.0f,
			 1.0f, -1.0f,
		};
		const UINT vertexBufferSize = sizeof(quadVertices);

		BufferResource	vtxBufRes	= createBufferResource(vertexBufferSize, L"Vertex buffer");
		m_vertexBuffer				= vtxBufRes.resourceDefault;
		vertexBufferUpload			= vtxBufRes.resourceUpload;

		// Copy data to the intermediate upload heap and then schedule a copy 
		// from the upload heap to the vertex buffer.
		D3D12_SUBRESOURCE_DATA vertexData = {};
		vertexData.pData		= (UINT8*)quadVertices;
		vertexData.RowPitch		= vertexBufferSize;
		vertexData.SlicePitch	= vertexData.RowPitch;

		// upload sub-resource
		{
			// copy to upload heap tmp resource
			{
				BYTE*	pData;
				D3D12_RANGE noReadRange = { 0, 0 };
				HRESULT hr = vertexBufferUpload->Map(0, &noReadRange, (void**)&pData);
				if (FAILED(hr))
					return;

				memcpy(pData, &quadVertices, vertexBufferSize);
				vertexBufferUpload->Unmap(0, nullptr);
			}

			// schedule a copy to the dest vertex buffer
			m_commandList->CopyBufferRegion(m_vertexBuffer, 0, vertexBufferUpload, 0, vertexBufferSize);
		}

		D3D12_RESOURCE_BARRIER resBarrier;
		resBarrier.Type						= D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		resBarrier.Flags					= D3D12_RESOURCE_BARRIER_FLAG_NONE;
		resBarrier.Transition.pResource		= m_vertexBuffer;
		resBarrier.Transition.StateBefore	= D3D12_RESOURCE_STATE_COPY_DEST;
		resBarrier.Transition.StateAfter	= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		resBarrier.Transition.Subresource	= D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		m_commandList->ResourceBarrier(1, &resBarrier);

		// Initialize the vertex buffer view.
		m_vertexBufferView.BufferLocation	= m_vertexBuffer->GetGPUVirtualAddress();
		m_vertexBufferView.StrideInBytes	= sizeof(Vertex);
		m_vertexBufferView.SizeInBytes		= sizeof(quadVertices);
	}

	// init scene
	BufferResource	scene_bufferTriPos		;
	BufferResource	scene_bufferTriNor		;
	BufferResource	scene_bufferTriIdx		;
	BufferResource	scene_bufferMeshMaterial;
	BufferResource	scene_bufferMeshIdxRange;
	{
		scene_bufferTriPos			= createBufferResource(SCENE_VTX_MAX		* sizeof(Vector4	), L"tri_pos");
		scene_bufferTriNor			= createBufferResource(SCENE_VTX_MAX		* sizeof(Vector4	), L"tri_nor");
		scene_bufferTriIdx			= createBufferResource(SCENE_IDX_MAX		* sizeof(int		), L"tri_idx");
		scene_bufferMeshMaterial	= createBufferResource(SCENE_MATERIAL_MAX	* sizeof(Material	), L"mesh_material");
		scene_bufferMeshIdxRange	= createBufferResource(SCENE_MESH_MAX		* sizeof(int2		), L"mesh_idx_range");

		m_scene_bufferTriPos		= scene_bufferTriPos.resourceDefault;
		m_scene_bufferTriNor		= scene_bufferTriNor.resourceDefault;
		m_scene_bufferTriIdx		= scene_bufferTriIdx.resourceDefault;
		m_scene_bufferMeshMaterial	= scene_bufferMeshMaterial.resourceDefault;
		m_scene_bufferMeshIdxRange	= scene_bufferMeshIdxRange.resourceDefault;

		// create SRV
		createBufferSRV(m_scene_bufferTriPos		, 0, SCENE_VTX_MAX		, sizeof(Vector4	));
		createBufferSRV(m_scene_bufferTriNor		, 1, SCENE_VTX_MAX		, sizeof(Vector4	));
		createBufferSRV(m_scene_bufferTriIdx		, 2, SCENE_IDX_MAX		, sizeof(int		));
		createBufferSRV(m_scene_bufferMeshMaterial	, 3, SCENE_MATERIAL_MAX	, sizeof(Material	));
		createBufferSRV(m_scene_bufferMeshIdxRange	, 4, SCENE_MESH_MAX		, sizeof(int2		));

		// set up mesh
		std::vector<Vector4	> triPos;
		std::vector<Vector4	> triNor;
		std::vector<int		> triIdx;
		std::vector<Material> triMeshMaterial;
		std::vector<int2	> triMeshIdxRange;

		Material redMaterial = { Vector4(0.7f	, 0.45f	, 0.45f	, 0.0f) / PI, Vector4(0.0f, 0.0f, 0.0f, 0.0f) };
		Material blueMaterial = { Vector4(0.45f	, 0.45f	, 0.7f	, 0.0f) / PI, Vector4(0.0f, 0.0f, 0.0f, 0.0f) };
		Material whiteMaterial = { Vector4(0.7f	, 0.7f	, 0.7f	, 0.0f) / PI, Vector4(0.0f, 0.0f, 0.0f, 0.0f) };
		float cornellBoxVtxData_white_pos[]=
		{			
			0.55f   , 0.0f		, 0.0f			, 0.0f    , 0.0f	, 0.0f			, 0.0f    , 0.0f	, 0.560f	, 0.55f   , 0.0f	, 0.560f  ,		// floor
			0.550f  , 0.550f	, 0.0f			, 0.550f  , 0.550f	, 0.560f		, 0.0f    , 0.550f	, 0.560f	, 0.0f    , 0.550f	, 0.0f    ,		// ceiling
			0.550f  , 0.0f		, 0.560f		, 0.0f    , 0.0f	, 0.560f		, 0.0f    , 0.550f	, 0.560f	, 0.550f  , 0.550f	, 0.560f  ,		// back wall
			0.550f  , 0.0f		, 0.0f			, 0.0f    , 0.0f	, 0.0f			, 0.0f    , 0.550f	, 0.0f		, 0.550f  , 0.550f	, 0.0f    ,		// front wall
		};
		float cornellBoxVtxData_white_nor[] =
		{
			0.0f, 1.0f, 0.0f	, 0.0f, 1.0f, 0.0f		, 0.0f, 1.0f, 0.0f		, 0.0f, 1.0f, 0.0f		,  // floor
			0.0f, -1.0f, 0.0f	, 0.0f, -1.0f, 0.0f		, 0.0f, -1.0f, 0.0f		, 0.0f, -1.0f, 0.0f		,  // ceiling  
			0.0f, 0.0f, -1.0f	, 0.0f, 0.0f, -1.0f		, 0.0f, 0.0f, -1.0f		, 0.0f, 0.0f, -1.0f		,  // back wall  
			0.0f, 0.0f, 1.0f	, 0.0f, 0.0f, 1.0f		, 0.0f, 0.0f, 1.0f		, 0.0f, 0.0f, 1.0f		,  // front wall   
		};
		int cornellBoxIdxData_white[] =
		{
			0, 1, 2				, 0, 2, 3		,	// floor
			4, 5, 6				, 4, 6, 7		,	// ceiling  
			8, 9, 10			, 8, 10, 11		,	// back wall  
			13, 12, 14			, 14, 12, 15	,	// front wall   
		};

		float cornellBoxVtxData_blue_pos[] =
		{	// right wall
			0.0f, 0.0f, 0.560f	, 0.0f, 0.0f, 0.0f	, 0.0f, 0.550f, 0.0f	, 0.0f, 0.550f, 0.560f,
		};
		float cornellBoxVtxData_blue_nor[] =
		{	// right wall
			1.0f, 0.0f, 0.0f	, 1.0f, 0.0f, 0.0f	, 1.0f, 0.0f, 0.0f		, 1.0f, 0.0f, 0.0f,
		};
		int cornellBoxIdxData_blue[] =
		{	// right wall
			0, 1, 2,        0, 2, 3,
		};

		float cornellBoxVtxData_red_pos[] =
		{	// left wall
			0.550f, 0.0f, 0.0f		, 0.550f, 0.0f, 0.560f		, 0.550f, 0.550f, 0.560f	, 0.550f, 0.550f, 0.0f,   
		};
		float cornellBoxVtxData_red_nor[] =
		{	// left wall
			-1.0f, 0.0f, 0.0f		, -1.0f, 0.0f, 0.0f			, -1.0f, 0.0f, 0.0f			, -1.0f, 0.0f, 0.0f,
		};
		int cornellBoxIdxData_red[] =
		{// left wall
			0, 1, 2,        0, 2, 3,
		};

		float shortBlockVtxData_pos[] =
		{
			0.130f, 0.165f, 0.065f					, 0.082f, 0.165f, 0.225f				, 0.240f, 0.165f, 0.272f				, 0.290f, 0.165f, 0.114f, 
			0.290f,   0.0f, 0.114f					, 0.290f, 0.165f, 0.114f				, 0.240f, 0.165f, 0.272f				, 0.240f,   0.0f, 0.272f, 
			0.130f,   0.0f, 0.065f					, 0.130f, 0.165f, 0.065f				, 0.290f, 0.165f, 0.114f				, 0.290f,   0.0f, 0.114f, 
			0.082f,   0.0f, 0.225f					, 0.082f, 0.165f, 0.225f				, 0.130f, 0.165f, 0.065f				, 0.130f,   0.0f, 0.065f, 
			0.240f,   0.0f, 0.272f					, 0.240f, 0.165f, 0.272f				, 0.082f, 0.165f, 0.225f				, 0.082f,   0.0f, 0.225f, 
		};

		float shortBlockVtxData_nor[] =
		{
			0.000000f, 1.000000f, -0.000000f		, 0.000000f, 1.000000f, -0.000000f		, 0.000000f, 1.000000f, -0.000000f		, 0.000000f, 1.000000f, -0.000000f	,
			0.953400f, -0.000000f, 0.301709f		, 0.953400f, -0.000000f, 0.301709f		, 0.953400f, -0.000000f, 0.301709f		, 0.953400f, -0.000000f, 0.301709f	,
			0.292826f, 0.000000f, -0.956166f		, 0.292826f, 0.000000f, -0.956166f		, 0.292826f, 0.000000f, -0.956166f		, 0.292826f, 0.000000f, -0.956166f	,
			-0.957826f, 0.000000f, -0.287348f		, -0.957826f, 0.000000f, -0.287348f		, -0.957826f, 0.000000f, -0.287348f		, -0.957826f, 0.000000f, -0.287348f	,
			-0.285121f, 0.000000f, 0.958492f		, -0.285121f, 0.000000f, 0.958492f		, -0.285121f, 0.000000f, 0.958492f		, -0.285121f, 0.000000f, 0.958492f	,
		};
		int shortBlockIdxData[] =
		{
			0, 1, 2,        0, 2, 3,
			4, 5, 6,        4, 6, 7,
			8, 9, 10,       8, 10, 11,
			12, 13, 14,     12, 14, 15,
			16, 17, 18,     16, 18, 19,
		};

		float tallBlockVtxData_pos[] =
		{
			0.423f,  0.330f,  0.247f		, 0.265f,  0.330f,  0.296f		, 0.314f,  0.330f,  0.456f		, 0.472f,  0.330f,  0.406f,  
			0.423f,    0.0f,  0.247f		, 0.423f,  0.330f,  0.247f		, 0.472f,  0.330f,  0.406f		, 0.472f,    0.0f,  0.406f,  
			0.472f,    0.0f,  0.406f		, 0.472f,  0.330f,  0.406f		, 0.314f,  0.330f,  0.456f		, 0.314f,    0.0f,  0.456f,  
			0.314f,    0.0f,  0.456f		, 0.314f,  0.330f,  0.456f		, 0.265f,  0.330f,  0.296f		, 0.265f,    0.0f,  0.296f,  
			0.265f,    0.0f,  0.296f		, 0.265f,  0.330f,  0.296f		, 0.423f,  0.330f,  0.247f		, 0.423f,    0.0f,  0.247f,  
		};

		float tallBlockVtxData_nor[] =
		{
			0.000000f, 1.000000f, 0.000000f			, 0.000000f, 1.000000f, 0.000000f		, 0.000000f, 1.000000f, 0.000000f		, 0.000000f, 1.000000f, 0.000000f			,
			0.955649f, 0.000000f, -0.294508f		, 0.955649f, 0.000000f, -0.294508f		, 0.955649f, 0.000000f, -0.294508f		, 0.955649f, 0.000000f, -0.294508f		,
			0.301709f, -0.000000f, 0.953400f		, 0.301709f, -0.000000f, 0.953400f		, 0.301709f, -0.000000f, 0.953400f		, 0.301709f, -0.000000f, 0.953400f		,
			-0.956166f, 0.000000f, 0.292826f		, -0.956166f, 0.000000f, 0.292826f		, -0.956166f, 0.000000f, 0.292826f		, -0.956166f, 0.000000f, 0.292826f		,
			-0.296209f, 0.000000f, -0.955123f		, -0.296209f, 0.000000f, -0.955123f		, -0.296209f, 0.000000f, -0.955123f		, -0.296209f, 0.000000f, -0.955123f		,
		};
		int tallBlockIdxData[] =
		{
			0, 1, 2,        0, 2, 3,
			4, 5, 6,        4, 6, 7,
			8, 9, 10,       8, 10, 11,
			12, 13, 14,     12, 14, 15,
			16, 17, 18,     16, 18, 19,
		};

		addMesh(cornellBoxVtxData_white_pos	, cornellBoxVtxData_white_nor	, sizeof(cornellBoxVtxData_white_pos) / (sizeof(float)*3)	, cornellBoxIdxData_white	, sizeof(cornellBoxIdxData_white)/sizeof(int)	, whiteMaterial	, &triPos,	&triNor,	&triIdx,	&triMeshMaterial,	&triMeshIdxRange);
		addMesh(cornellBoxVtxData_blue_pos	, cornellBoxVtxData_blue_nor	, sizeof(cornellBoxVtxData_blue_pos	) / (sizeof(float)*3)	, cornellBoxIdxData_blue	, sizeof(cornellBoxIdxData_blue	)/sizeof(int)	, blueMaterial	, &triPos,	&triNor,	&triIdx,	&triMeshMaterial,	&triMeshIdxRange);
		addMesh(cornellBoxVtxData_red_pos	, cornellBoxVtxData_red_nor		, sizeof(cornellBoxVtxData_red_pos	) / (sizeof(float)*3)	, cornellBoxIdxData_red		, sizeof(cornellBoxIdxData_red	)/sizeof(int)	, redMaterial	, &triPos,	&triNor,	&triIdx,	&triMeshMaterial,	&triMeshIdxRange);
		addMesh(shortBlockVtxData_pos		, shortBlockVtxData_nor			, sizeof(shortBlockVtxData_pos		) / (sizeof(float)*3)	, shortBlockIdxData			, sizeof(shortBlockIdxData		)/sizeof(int)	, whiteMaterial	, &triPos,	&triNor,	&triIdx,	&triMeshMaterial,	&triMeshIdxRange);
		addMesh(tallBlockVtxData_pos		, tallBlockVtxData_nor			, sizeof(tallBlockVtxData_pos		) / (sizeof(float)*3)	, tallBlockIdxData			, sizeof(tallBlockIdxData		)/sizeof(int)	, whiteMaterial	, &triPos,	&triNor,	&triIdx,	&triMeshMaterial,	&triMeshIdxRange);

		// set up light
		const float lightWidth		= 0.130f;
		const float lightHeight		= 0.105f;
		const float lightIntensity	= 0.2f;
		Vector3		lightRadiance	= Vector3(lightIntensity, lightIntensity, lightIntensity)*(PI / (lightWidth*lightHeight));
#if USE_LIGHT_MESH
		{
			// light mesh
			float lightVtxData_pos[] =
			{
				(lightWidth * ( 0.5f)	+ 0.278f), 0.549f, (lightHeight * (-0.5f) + 0.2795f), 
				(lightWidth * ( 0.5f)	+ 0.278f), 0.549f, (lightHeight * ( 0.5f) + 0.2795f), 
				(lightWidth * (-0.5f)   + 0.278f), 0.549f, (lightHeight * ( 0.5f) + 0.2795f), 
				(lightWidth * (-0.5f)   + 0.278f), 0.549f, (lightHeight * (-0.5f) + 0.2795f), 
			};
			
			float lightVtxData_nor[] =
			{
				0, -1, 0,
				0, -1, 0,
				0, -1, 0,
				0, -1, 0,
			};
			int lightdxData_white[] =
			{
				0, 1, 2,        0, 2, 3,
			};
			Material lightMaterial = { whiteMaterial.albedo, Vector4(lightRadiance.x, lightRadiance.y, lightRadiance.z, 0) };
			addMesh(lightVtxData_pos		, lightVtxData_nor			, sizeof(lightVtxData_pos		) / (sizeof(float)*3)	, lightdxData_white			, sizeof(lightdxData_white		)/sizeof(int)	, lightMaterial	, &triPos,	&triNor,	&triIdx,	&triMeshMaterial,	&triMeshIdxRange);
		}
#endif

		Matrix4x4 lightTransform = Matrix4x4::CreateRotationX(DEGREE_TO_RADIAN(180.0f));
		lightTransform.setTranslation(Vector3(0.278f, 0.549f, 0.2795f));
//		lightTransform.setTranslation(Vector3(0.275f, 0.549f, 0.28f));

		SceneConstantBuffer	sceneCB;
		sceneCB.areaLight[0].xform		= lightTransform;
		sceneCB.areaLight[0].xformInv	= lightTransform.inverse();
		sceneCB.areaLight[0].radiance	= Vector4(lightRadiance.x, lightRadiance.y, lightRadiance.z, 0.0f);
		sceneCB.areaLight[0].halfWidth	= lightWidth	* 0.5f;
		sceneCB.areaLight[0].halfHeight	= lightHeight	* 0.5f;
		sceneCB.areaLight[0].oneOverArea= 1.0f/(lightWidth * lightHeight);
#if USE_LIGHT_MESH
		sceneCB.numLight				= 0;
#else
		sceneCB.numLight				= 1;
#endif
		sceneCB.numMesh					= (int)triMeshIdxRange.size();

		// set up camera
		resetCamera();

		// copy constnat buffer to heap memory
		BYTE*	pData;
		D3D12_RANGE noReadRange = { 0, 0 };
		m_constantBuffer->Map(0, &noReadRange, (void**)&pData);
		memcpy(pData								, &sceneCB	, sizeof(SceneConstantBuffer));
		m_constantBuffer->Unmap(0, nullptr);
		updateViewConstantBuffer();

		// copy data from system to upload 
		const int			numSceneBuffer = 5;
		void*				data[	] = { triPos.data()						, triNor.data()						, triIdx.data()					, triMeshMaterial.data()					, triMeshIdxRange.data()				};
		size_t				dataSz[	] = { triPos.size() * sizeof(Vector4)	, triNor.size() * sizeof(Vector4)	, triIdx.size() * sizeof(int)	, triMeshMaterial.size() * sizeof(Material)	, triMeshIdxRange.size() * sizeof(int2) };
		BufferResource		res[	] = { scene_bufferTriPos				, scene_bufferTriNor				, scene_bufferTriIdx			, scene_bufferMeshMaterial					, scene_bufferMeshIdxRange				};
		for (int i = 0; i<numSceneBuffer; ++i)
		{
			BYTE*	pData;
			D3D12_RANGE noReadRange = { 0, 0 };
			HRESULT hr = res[i].resourceUpload->Map(0, &noReadRange, (void**)&pData);
			if (FAILED(hr))
				return;
			memcpy(pData, data[i], dataSz[i]);
			res[i].resourceUpload->Unmap(0, nullptr);
		}

		// copy data from upload to default
		for (int i = 0; i<numSceneBuffer; ++i)
			m_commandList->CopyBufferRegion(res[i].resourceDefault, 0, res[i].resourceUpload, 0, dataSz[i]);

		// transit resource to SRV
		D3D12_RESOURCE_BARRIER	resBarrier[numSceneBuffer];
		for(int i=0; i<numSceneBuffer; ++i)
		{
			resBarrier[i].Type						= D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			resBarrier[i].Flags						= D3D12_RESOURCE_BARRIER_FLAG_NONE;
			resBarrier[i].Transition.pResource		= res[i].resourceDefault;
			resBarrier[i].Transition.StateBefore	= D3D12_RESOURCE_STATE_COPY_DEST;
			resBarrier[i].Transition.StateAfter		= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			resBarrier[i].Transition.Subresource	= D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		}
		m_commandList->ResourceBarrier(numSceneBuffer, resBarrier);
	}

	// Close the command list and execute it to begin the vertex buffer copy into the default heap.
	m_commandList->Close();
	ID3D12CommandList* commandList= m_commandList;
	m_commandQueue->ExecuteCommandLists(1, &commandList);

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		m_device->CreateFence(m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE, _uuidof(ID3D12Fence), (void**)&m_fence);
		m_fenceValues[m_frameIndex]++;

		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		
		// Wait for the command list to execute; we are reusing the same command 
		// list in our main loop but for now, we just want to wait for setup to 
		// complete before continuing.
		waitForGpu();
	}

	vertexBufferUpload->Release();
	scene_bufferTriPos.resourceUpload->Release();
	scene_bufferTriNor.resourceUpload->Release();
	scene_bufferTriIdx.resourceUpload->Release();
	scene_bufferMeshMaterial.resourceUpload->Release();
	scene_bufferMeshIdxRange.resourceUpload->Release();
}

RayTracer::BufferResource	RayTracer::createBufferResource(int sizeByte, LPCWSTR debugName)
{
	BufferResource res;
	
	{
		D3D12_HEAP_PROPERTIES heapProp;
		heapProp.Type					= D3D12_HEAP_TYPE_DEFAULT;
		heapProp.CPUPageProperty		= D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProp.MemoryPoolPreference	= D3D12_MEMORY_POOL_UNKNOWN;
		heapProp.CreationNodeMask		= 0;
		heapProp.VisibleNodeMask		= 0;

		D3D12_RESOURCE_DESC resDesc;
		resDesc.Dimension			= D3D12_RESOURCE_DIMENSION_BUFFER;
		resDesc.Alignment			= 0;
		resDesc.Width				= sizeByte;
		resDesc.Height				= 1;
		resDesc.DepthOrArraySize	= 1;
		resDesc.MipLevels			= 1;
		resDesc.Format				= DXGI_FORMAT_UNKNOWN;
		resDesc.SampleDesc.Count	= 1;
		resDesc.SampleDesc.Quality	= 0;
		resDesc.Layout				= D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resDesc.Flags				= D3D12_RESOURCE_FLAG_NONE;

		m_device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, _uuidof(ID3D12Resource), (void**)&res.resourceDefault);
	}
	{
		D3D12_HEAP_PROPERTIES heapProp;
		heapProp.Type					= D3D12_HEAP_TYPE_UPLOAD;
		heapProp.CPUPageProperty		= D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProp.MemoryPoolPreference	= D3D12_MEMORY_POOL_UNKNOWN;
		heapProp.CreationNodeMask		= 0;
		heapProp.VisibleNodeMask		= 0;

		D3D12_RESOURCE_DESC resDesc;
		resDesc.Dimension			= D3D12_RESOURCE_DIMENSION_BUFFER;
		resDesc.Alignment			= 0;
		resDesc.Width				= sizeByte;
		resDesc.Height				= 1;
		resDesc.DepthOrArraySize	= 1;
		resDesc.MipLevels			= 1;
		resDesc.Format				= DXGI_FORMAT_UNKNOWN;
		resDesc.SampleDesc.Count	= 1;
		resDesc.SampleDesc.Quality	= 0;
		resDesc.Layout				= D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resDesc.Flags				= D3D12_RESOURCE_FLAG_NONE;

		m_device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, _uuidof(ID3D12Resource), (void**)&res.resourceUpload);
	}

	res.resourceDefault->SetName(debugName);
	return res;
}

void	RayTracer::createBufferSRV(ID3D12Resource* res, int descriptorHeapOffset, int numElements, int structSzByte)
{
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_cbSrvHeap->GetCPUDescriptorHandleForHeapStart();
	cpuHandle.ptr += m_cbSrvDescriptorSize * (1 + descriptorHeapOffset);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format							= DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension					= D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Shader4ComponentMapping			= D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Buffer.NumElements				= numElements;
	srvDesc.Buffer.StructureByteStride		= structSzByte;
	srvDesc.Buffer.Flags					= D3D12_BUFFER_SRV_FLAG_NONE;

	m_device->CreateShaderResourceView(res, &srvDesc, cpuHandle);
}

ID3D12PipelineState*	RayTracer::createPipelineState(LPCSTR vs, LPCSTR ps, LPCWSTR debugName, bool isBlendEnable, D3D12_BLEND destBlend, DXGI_FORMAT rtvFormat)
{
	ID3D12PipelineState*	pipelineState	= nullptr;
	ID3DBlob*				vertexShader	= nullptr;
	ID3DBlob*				pixelShader		= nullptr;

#if defined(_DEBUG)
	// Enable better shader debugging with the graphics debugging tools.
	UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	UINT compileFlags = 0;
#endif

	bool			hasError = false;
	ID3DBlob*		error	= nullptr;
	HRESULT			compileResult;
	compileResult	= D3DCompileFromFile(	 L"shader/path_tracer.hlsl"	, nullptr, nullptr, vs, "vs_5_0", compileFlags, 0, &vertexShader	, &error);
	if (compileResult != S_OK)
	{
		hasError = true;
		if (error)
		{
			OutputDebugString((LPCTSTR)error->GetBufferPointer());
			error->Release();
			error = nullptr;
		}
	}

	compileResult	= D3DCompileFromFile(	 L"shader/path_tracer.hlsl"	, nullptr, nullptr, ps, "ps_5_0", compileFlags, 0, &pixelShader	, &error);
	if (compileResult != S_OK)
	{
		hasError = true;
		if (error)
		{
			OutputDebugString((LPCTSTR)error->GetBufferPointer());
			error->Release();
			error = nullptr;
		}
	}

	if (!hasError)
	{
		// Define the vertex input layout.
		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT	, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		// Describe and create the graphics pipeline state objects (PSO).
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout			= { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.pRootSignature		= m_rootSignature;
		psoDesc.VS.pShaderBytecode	= vertexShader->GetBufferPointer();
		psoDesc.VS.BytecodeLength	= vertexShader->GetBufferSize();
		psoDesc.PS.pShaderBytecode	= pixelShader->GetBufferPointer();
		psoDesc.PS.BytecodeLength	= pixelShader->GetBufferSize();

		psoDesc.RasterizerState.FillMode				= D3D12_FILL_MODE_SOLID;
		psoDesc.RasterizerState.CullMode				= D3D12_CULL_MODE_BACK;
		psoDesc.RasterizerState.FrontCounterClockwise	= TRUE;
		psoDesc.RasterizerState.DepthBias				= D3D12_DEFAULT_DEPTH_BIAS;
		psoDesc.RasterizerState.DepthBiasClamp			= D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
		psoDesc.RasterizerState.SlopeScaledDepthBias	= D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
		psoDesc.RasterizerState.DepthClipEnable			= TRUE;
		psoDesc.RasterizerState.MultisampleEnable		= FALSE;
		psoDesc.RasterizerState.AntialiasedLineEnable	= FALSE;
		psoDesc.RasterizerState.ForcedSampleCount		= 0;
		psoDesc.RasterizerState.ConservativeRaster		= D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

		psoDesc.BlendState.AlphaToCoverageEnable		= FALSE;
		psoDesc.BlendState.IndependentBlendEnable		= FALSE;
		const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc =
		{
			isBlendEnable, FALSE,
			D3D12_BLEND_ONE, destBlend			, D3D12_BLEND_OP_ADD,
			D3D12_BLEND_ONE, D3D12_BLEND_ZERO	, D3D12_BLEND_OP_ADD,
			D3D12_LOGIC_OP_NOOP,
			D3D12_COLOR_WRITE_ENABLE_ALL,
		};
		for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
			psoDesc.BlendState.RenderTarget[i] = defaultRenderTargetBlendDesc;

		psoDesc.DepthStencilState.DepthEnable	= FALSE;
		psoDesc.DepthStencilState.StencilEnable	= FALSE;
		psoDesc.SampleMask						= UINT_MAX;
		psoDesc.PrimitiveTopologyType			= D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets				= 1;
		psoDesc.RTVFormats[0]					= rtvFormat;
		psoDesc.SampleDesc.Count = 1;

		m_device->CreateGraphicsPipelineState(&psoDesc, __uuidof(ID3D12PipelineState), (void**)&pipelineState);
		pipelineState->SetName(debugName);
	}

	if (vertexShader)
		vertexShader->Release();
	if (pixelShader)
		pixelShader->Release();

	return pipelineState;
}

void	RayTracer::createRTV()
{
	D3D12_CPU_DESCRIPTOR_HANDLE	rtvHeapHandleStart = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	for (UINT n = 0; n < FRAME_CNT; ++n)
	{
		m_swapChain->GetBuffer(n, __uuidof(ID3D12Resource), (void**)&m_renderTargets[n]);

		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeapHandleStart;
		rtvHandle.ptr += n * m_rtvDescriptorSize;
		m_device->CreateRenderTargetView(m_renderTargets[n], nullptr, rtvHandle);
		m_renderTargets[n]->SetName(L"Swap chain render target");
	}
}

void	RayTracer::createPathTraceTex()
{
	// create path trace texture
	{
		D3D12_HEAP_PROPERTIES heapProp;
		heapProp.Type					= D3D12_HEAP_TYPE_DEFAULT;
		heapProp.CPUPageProperty		= D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		heapProp.MemoryPoolPreference	= D3D12_MEMORY_POOL_UNKNOWN;
		heapProp.CreationNodeMask		= 0;
		heapProp.VisibleNodeMask		= 0;

		D3D12_RESOURCE_DESC resDesc;
		resDesc.Dimension			= D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		resDesc.Alignment			= 0;
		resDesc.Width				= m_windowWidth;
		resDesc.Height				= m_windowHeight;
		resDesc.DepthOrArraySize	= 1;
		resDesc.MipLevels			= 1;
		resDesc.Format				= PATH_TRACE_BUFFER_FORMAT;
		resDesc.SampleDesc.Count	= 1;
		resDesc.SampleDesc.Quality	= 0;
		resDesc.Layout				= D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resDesc.Flags				= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		D3D12_CLEAR_VALUE optimizedClearValue;
		optimizedClearValue.Format = PATH_TRACE_BUFFER_FORMAT;
		optimizedClearValue.Color[0] = 0.0f;
		optimizedClearValue.Color[1] = 0.0f;
		optimizedClearValue.Color[2] = 0.0f;
		optimizedClearValue.Color[3] = 0.0f;
		m_device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, &optimizedClearValue, _uuidof(ID3D12Resource), (void**)&m_pathTraceTex);

		m_pathTraceTex->SetName(L"Path Trace Tex");
	}
	
	// create path trace buffer RTV
	{
		D3D12_CPU_DESCRIPTOR_HANDLE	rtvHeapHandleStart = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = rtvHeapHandleStart;
		rtvHandle.ptr += FRAME_CNT * m_rtvDescriptorSize;
		m_device->CreateRenderTargetView(m_pathTraceTex, nullptr, rtvHandle);
	}

	// create path trace buffer SRV
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle	= m_cbSrvHeap->GetCPUDescriptorHandleForHeapStart();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping			= D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format							= PATH_TRACE_BUFFER_FORMAT;
	srvDesc.ViewDimension					= D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip		= 0;
	srvDesc.Texture2D.MipLevels				= -1;
	srvDesc.Texture2D.PlaneSlice			= 0;
	srvDesc.Texture2D.ResourceMinLODClamp	= 0.0f;

	m_device->CreateShaderResourceView(m_pathTraceTex, &srvDesc, cpuHandle);
}


void	RayTracer::release()
{
	if (m_device)
	{
		waitForGpu();
		CloseHandle(m_fenceEvent);

		m_scene_bufferTriPos->Release();
		m_scene_bufferTriNor->Release();
		m_scene_bufferTriIdx->Release();
		m_scene_bufferMeshMaterial->Release();
		m_scene_bufferMeshIdxRange->Release();

		m_pathTraceTex->Release();
		m_constantBuffer->Release();
		m_cbSrvHeap->Release();
		if (m_pipelineStateToneMap)
			m_pipelineStateToneMap->Release();
		if (m_pipelineStatePathTrace)
			m_pipelineStatePathTrace->Release();
		m_fence->Release();
		m_vertexBuffer->Release();
		m_commandList->Release();
		m_copyCommandList->Release();
		m_rootSignature->Release();
		for (UINT n = 0; n < FRAME_CNT; n++)
		{
			m_renderTargets[n]->Release();
			m_commandAllocators[n]->Release();
		}
		m_copyCommandAllocator->Release();
		m_rtvHeap->Release();
		m_commandQueue->Release();
		m_copyQueue->Release();
		m_swapChain->Release();
		m_device->Release();
		m_device= nullptr;
	}
}

void RayTracer::waitForGpu()
{
	// Schedule a Signal command in the queue.
	m_commandQueue->Signal(m_fence, m_fenceValues[m_frameIndex]);

	// Wait until the fence has been processed.
	m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent);
	WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);

	// Increment the fence value for the current frame.
	m_fenceValues[m_frameIndex]++;
}

void RayTracer::moveToNextFrame()
{
	// Schedule a Signal command in the queue.
	const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
	m_commandQueue->Signal(m_fence, currentFenceValue);

	// Update the frame index.
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// If the next frame is not ready to be rendered yet, wait until it is ready.
	if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
	{
		m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent);
		WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
	}

	// Set the fence value for the next frame.
	m_fenceValues[m_frameIndex] = currentFenceValue + 1;
}

void	RayTracer::update()
{
	// timer update
	LONGLONG newTime = timeGetAbsoulteTime();
	float elapsedTime = (float)timeCalculateElapsedTime(s_clockFreq, s_clockTime, newTime);
	s_clockTime = newTime;

	// update input
	{
		Vector3	camDir= m_camLookAt - m_camPos;
		camDir.normalize();
		Vector3 camRight= camDir.cross(Vector3(0, 1, 0));
		camRight.normalize();
		Vector3 worldUp= { 0, 1, 0 };

		// translate camera
		bool isCamMoved;
		const float moveSpeed= 1.0f;
		Vector3 camMoveDelta= {0, 0, 0};
		if (	m_isKeyDown[Key_CamForward	])
			camMoveDelta= camMoveDelta + camDir * elapsedTime * moveSpeed;
		if (	m_isKeyDown[Key_CamBackward	])
			camMoveDelta= camMoveDelta - camDir * elapsedTime * moveSpeed;
		if (	m_isKeyDown[Key_CamLeft		])
			camMoveDelta= camMoveDelta - camRight * elapsedTime * moveSpeed;
		if (	m_isKeyDown[Key_CamRight	])
			camMoveDelta= camMoveDelta + camRight * elapsedTime * moveSpeed;
		if (	m_isKeyDown[Key_CamUp		])
			camMoveDelta= camMoveDelta + worldUp * elapsedTime * moveSpeed;
		if (	m_isKeyDown[Key_CamDown	])
			camMoveDelta= camMoveDelta - worldUp * elapsedTime * moveSpeed;
		isCamMoved= camMoveDelta.x != 0 || camMoveDelta.y != 0 || camMoveDelta.z != 0;
		
		m_camPos		+= camMoveDelta;
		m_camLookAt		+= camMoveDelta;

		// update mouse input
		int2		mousePosPrev= m_mousePos;
		POINT pt;
		GetCursorPos(&pt);
		m_mousePos.x			= pt.x - m_windowPos.x;
		m_mousePos.y			= pt.y - m_windowPos.y;
		
		// rotate camera
		if (m_isMouseDown)
		{
			// rotate horizontally
			int			deltaX		= m_mousePos.x - mousePosPrev.x;
			if (deltaX != 0)
			{
				Vector3		newCamDir	= m_camLookAt - m_camPos;
				const float rotateSpeed= -0.008f;
				Vector4 rotatedCamDir= Matrix4x4::CreateRotationY(deltaX * rotateSpeed) * Vector4(newCamDir.x, newCamDir.y, newCamDir.z, 0.0f);
				Vector3 rotatedCamDir3= { rotatedCamDir.x, rotatedCamDir.y, rotatedCamDir.z };
				m_camLookAt= m_camPos + rotatedCamDir3;
				isCamMoved= true;
			}
			
			// rotate vertically
			int			deltaY		= m_mousePos.y - mousePosPrev.y;
			if (deltaY != 0)
			{
				Vector3		newCamDir	= m_camLookAt - m_camPos;
				Vector3		camRight	= newCamDir.cross(Vector3(0, 1, 0));
				camRight.normalize();

				const float rotateSpeed= -0.008f;
				Vector4 rotatedCamDir= Matrix4x4::CreateRotation(camRight, deltaY * rotateSpeed) * Vector4(newCamDir.x, newCamDir.y, newCamDir.z, 0.0f);
				Vector3 rotatedCamDir3= { rotatedCamDir.x, rotatedCamDir.y, rotatedCamDir.z };
				m_camLookAt= m_camPos + rotatedCamDir3;
				isCamMoved= true;
			}
		}
		
//		if (1)
		if (isCamMoved || m_isFirstFrame)
		{
			m_pathTraceFrameIdx	= 0;
			m_randSeedOffset	= 0;
			m_randSeedInterval	= 100;
			m_randSeedAdd		= 10;
			m_camJitter			= Vector2(0, 0);
		}
		else
		{
			++m_pathTraceFrameIdx;
			m_randSeedOffset	= rand();
			m_randSeedInterval	= (int)randf(100, 1000);
			m_randSeedAdd		= (int)randf(10, 100);
			m_camJitter			= Vector2(	randf(-0.5f, 0.5f) / m_windowWidth	,
											randf(-0.5f, 0.5f) / m_windowHeight	);
		}
	}

	m_currentCbIdx= (m_currentCbIdx + 1) % FRAME_CNT;
	updateViewConstantBuffer();

	m_isFirstFrame= false;
}

void	RayTracer::render()
{
	// Command list allocators can only be reset when the associated 
	// command lists have finished execution on the GPU; apps should use 
	// fences to determine GPU execution progress.
	m_commandAllocators[m_frameIndex]->Reset();

	// However, when ExecuteCommandList() is called on a particular command 
	// list, that command list can then be reset at any time and must be before 
	// re-recording.
	m_commandList->Reset(m_commandAllocators[m_frameIndex], nullptr);

	// Set necessary state.
	m_commandList->SetGraphicsRootSignature(m_rootSignature);
	
	D3D12_VIEWPORT viewport;
	viewport.TopLeftX	= 0.0f;
	viewport.TopLeftY	= 0.0f;
	viewport.Width		= (float)m_windowWidth;
	viewport.Height		= (float)m_windowHeight;
	viewport.MinDepth	= D3D12_MIN_DEPTH;
	viewport.MaxDepth	= D3D12_MAX_DEPTH;

	D3D12_RECT scissorRect;
	scissorRect.left	= 0;
	scissorRect.top		= 0;
	scissorRect.right	= m_windowWidth;
	scissorRect.bottom	= m_windowHeight;

	m_commandList->RSSetViewports(1, &viewport);
	m_commandList->RSSetScissorRects(1, &scissorRect);
	m_commandList->SetDescriptorHeaps(1, &m_cbSrvHeap);
	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
	m_commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer->GetGPUVirtualAddress() + m_constantBufferOffset[1 + m_currentCbIdx]);
	D3D12_GPU_DESCRIPTOR_HANDLE cbHandle = m_cbSrvHeap->GetGPUDescriptorHandleForHeapStart();
	cbHandle.ptr += m_cbSrvDescriptorSize * (1+5);
	m_commandList->SetGraphicsRootDescriptorTable(1, cbHandle);
	D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = m_cbSrvHeap->GetGPUDescriptorHandleForHeapStart();
	srvHandle.ptr += m_cbSrvDescriptorSize;
	m_commandList->SetGraphicsRootDescriptorTable(3, srvHandle);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;

	// path trace buffer
	{
		rtvHandle.ptr = m_rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + FRAME_CNT * m_rtvDescriptorSize;

		// clear if needed
		if (m_pathTraceFrameIdx == 0)
		{
			const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
			m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
		}
		
		m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
		m_commandList->SetPipelineState(m_pipelineStatePathTrace);
		
		float blend= m_pathTraceFrameIdx/ (float)(m_pathTraceFrameIdx+1.0f);
		float blendFactor[4]= { blend, blend, blend, 0.0f};
		m_commandList->OMSetBlendFactor(blendFactor);
		m_commandList->DrawInstanced(4, 1, 0, 0);
	}
	
	{
		D3D12_RESOURCE_BARRIER resBarrier[2];

		// Indicate that the back buffer will be used as a render target.
		resBarrier[0].Type						= D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		resBarrier[0].Flags						= D3D12_RESOURCE_BARRIER_FLAG_NONE;
		resBarrier[0].Transition.pResource		= m_renderTargets[m_frameIndex];
		resBarrier[0].Transition.StateBefore	= D3D12_RESOURCE_STATE_PRESENT;
		resBarrier[0].Transition.StateAfter		= D3D12_RESOURCE_STATE_RENDER_TARGET;
		resBarrier[0].Transition.Subresource	= D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		
		// change path trace RT to SRV
		resBarrier[1].Type						= D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		resBarrier[1].Flags						= D3D12_RESOURCE_BARRIER_FLAG_NONE;
		resBarrier[1].Transition.pResource		= m_pathTraceTex;
		resBarrier[1].Transition.StateBefore	= D3D12_RESOURCE_STATE_RENDER_TARGET;
		resBarrier[1].Transition.StateAfter		= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		resBarrier[1].Transition.Subresource	= D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		m_commandList->ResourceBarrier(2, resBarrier);
	}

	rtvHandle.ptr = m_rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + m_frameIndex * m_rtvDescriptorSize;
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	// Record drawing commands.
	const float clearColor[] = { 0.8f, 0.8f, 1.0f, 1.0f };
	m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

	// draw to back buffer
	m_commandList->SetPipelineState(m_pipelineStateToneMap);
	m_commandList->SetGraphicsRootDescriptorTable(2, m_cbSrvHeap->GetGPUDescriptorHandleForHeapStart());
	m_commandList->DrawInstanced(4, 1, 0, 0);

	// Indicate that the back buffer will now be used to present.
	{
		D3D12_RESOURCE_BARRIER resBarrier[2];

		// back buffer
		resBarrier[0].Type						= D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		resBarrier[0].Flags						= D3D12_RESOURCE_BARRIER_FLAG_NONE;
		resBarrier[0].Transition.pResource		= m_renderTargets[m_frameIndex];
		resBarrier[0].Transition.StateBefore	= D3D12_RESOURCE_STATE_RENDER_TARGET;
		resBarrier[0].Transition.StateAfter		= D3D12_RESOURCE_STATE_PRESENT;
		resBarrier[0].Transition.Subresource	= D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		// path trace tex
		resBarrier[1].Type						= D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		resBarrier[1].Flags						= D3D12_RESOURCE_BARRIER_FLAG_NONE;
		resBarrier[1].Transition.pResource		= m_pathTraceTex;
		resBarrier[1].Transition.StateBefore	= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		resBarrier[1].Transition.StateAfter		= D3D12_RESOURCE_STATE_RENDER_TARGET;
		resBarrier[1].Transition.Subresource	= D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

		m_commandList->ResourceBarrier(2, resBarrier);
	}
	m_commandList->Close();

	// present
	ID3D12CommandList* commandList = m_commandList;
	m_commandQueue->ExecuteCommandLists(1, &commandList);
	m_swapChain->Present(1, 0);

	moveToNextFrame();
}

void	RayTracer::setWindowPos(int x, int y)
{
	m_windowPos.x= x;
	m_windowPos.y= y;
}

void	RayTracer::resize(int w, int h)
{
	// skip if resolution is not changed
	if (m_windowWidth == w && m_windowHeight == h)
		return;

	// flush GPU command
	waitForGpu();

	m_windowWidth	= w;
	m_windowHeight	= h;
	
	// resize back buffer
	{
		for (UINT n = 0; n < FRAME_CNT; ++n)
		{
			m_fenceValues[n] = m_fenceValues[m_frameIndex];
			m_renderTargets[n]->Release();
		}

		DXGI_SWAP_CHAIN_DESC desc = {};
		m_swapChain->GetDesc(&desc);
		m_swapChain->ResizeBuffers(FRAME_CNT, w, h, desc.BufferDesc.Format, desc.Flags);
		createRTV();
	}

	// resize path trace texture
	{
		if (m_pathTraceTex)
			m_pathTraceTex->Release();
		createPathTraceTex();
	}
	
	m_frameIndex	= m_swapChain->GetCurrentBackBufferIndex();
	m_isFirstFrame	= true;
}

void	RayTracer::onKeyUp(UINT8 key)
{
	if (		key == 'W')
		m_isKeyDown[Key_CamForward]= false;
	else if (	key == 'A')
		m_isKeyDown[Key_CamLeft		]= false;
	else if (	key == 'S')
		m_isKeyDown[Key_CamBackward	]= false;
	else if (	key == 'D')
		m_isKeyDown[Key_CamRight	]= false;
	else if (	key == 'Q')
		m_isKeyDown[Key_CamUp		]= false;
	else if (	key == 'E')
		m_isKeyDown[Key_CamDown		]= false;
	else if (	key == 'C')
		resetCamera();
	else if (	key == 'B')
		m_isEnableBlur				= !m_isEnableBlur;
}

void	RayTracer::onKeyDown(UINT8 key)
{
	if (		key == 'W')
		m_isKeyDown[Key_CamForward	]= true;
	else if (	key == 'A')
		m_isKeyDown[Key_CamLeft		]= true;
	else if (	key == 'S')
		m_isKeyDown[Key_CamBackward	]= true;
	else if (	key == 'D')
		m_isKeyDown[Key_CamRight	]= true;
	else if (	key == 'Q')
		m_isKeyDown[Key_CamUp		]= true;
	else if (	key == 'E')
		m_isKeyDown[Key_CamDown		]= true;
}

void	RayTracer::onMouseUp()
{
	m_isMouseDown= false;
}

void	RayTracer::onMouseDown()
{
	m_isMouseDown= true;
}

void	RayTracer::resetCamera()
{
	m_camPos				= Vector3(0.278f, 0.273f, -0.800f);
	m_camLookAt				= Vector3(0.278f, 0.273f, 0.0f);
	m_isFirstFrame			= true;
}

void	RayTracer::updateViewConstantBuffer()
{
	Matrix4x4	camLookAt	= Matrix4x4::CreateLookAt(	m_camPos,
														m_camLookAt,
														Vector3(0.0f, 1.0f, 0.0f)			);
	Matrix4x4	camProj		= Matrix4x4::CreatePerspectiveProjection(DEGREE_TO_RADIAN(60.0f), m_windowWidth / (float)m_windowHeight, 0.100f, 1.000f);
	Matrix4x4	viewProjInv = (camProj * camLookAt).inverse();

	ViewConstantBuffer viewCB;
	viewCB.projInv			= viewProjInv;
	viewCB.camPos			= m_camPos;
	viewCB.camPixelOffset	= m_camJitter;
	viewCB.frameIdx			= m_pathTraceFrameIdx;
	viewCB.viewportWidth	= m_windowWidth;
	viewCB.viewportHeight	= m_windowHeight;
	viewCB.randSeedInterval = m_randSeedInterval;
	viewCB.randSeedOffset	= m_randSeedOffset;
	viewCB.randSeedAdd		= m_randSeedAdd;
	viewCB.isEnableBlur		= m_isEnableBlur;

	BYTE*	pData;
	D3D12_RANGE noReadRange = { 0, 0 };
	m_constantBuffer->Map(0, &noReadRange, (void**)&pData);
	memcpy(pData+ m_constantBufferOffset[1+m_currentCbIdx]		, &viewCB	, sizeof(ViewConstantBuffer));
	m_constantBuffer->Unmap(0, nullptr);
}

#undef SCENE_VTX_MAX
#undef SCENE_IDX_MAX
#undef SCENE_MATERIAL_MAX
#undef SCENE_MESH_MAX
