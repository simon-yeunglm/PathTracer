#pragma once

// by simon yeung, 06/05/2018
// all rights reserved

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include "math.h"

#define FRAME_CNT		(2)

enum Key
{
	Key_CamForward,
	Key_CamBackward,
	Key_CamLeft,
	Key_CamRight,
	Key_CamUp,
	Key_CamDown,
	
	Key_Num
};

class RayTracer
{
private:
	struct BufferResource
	{
		ID3D12Resource*	resourceDefault;
		ID3D12Resource*	resourceUpload;
	};

	BufferResource				createBufferResource(int sizeByte, LPCWSTR debugName);
	void						createBufferSRV(ID3D12Resource* res, int descriptorHeapOffset, int numElements, int structSzByte);
	ID3D12PipelineState*		createPipelineState(LPCSTR vs, LPCSTR ps, LPCWSTR debugName, bool isBlendEnable, D3D12_BLEND destBlend, DXGI_FORMAT rtvFormat);
	
	void						waitForGpu();
	void						moveToNextFrame();

	void						createRTV();
	void						createPathTraceTex();
	void						updateViewConstantBuffer();
	void						resetCamera();

	void						allocaConsole();

public:
	HWND						m_hwnd;
	ID3D12Device*				m_device;
	ID3D12CommandQueue*			m_commandQueue;
	ID3D12CommandQueue*			m_copyQueue;
	struct IDXGISwapChain3*		m_swapChain;
	ID3D12DescriptorHeap*		m_rtvHeap;
	ID3D12DescriptorHeap*		m_cbSrvHeap;
	UINT						m_rtvDescriptorSize;
	UINT						m_cbSrvDescriptorSize;
	int							m_currentCbIdx;
	UINT						m_frameIndex;
	ID3D12Resource*				m_renderTargets[FRAME_CNT];
	ID3D12CommandAllocator*		m_commandAllocators[FRAME_CNT];
	ID3D12CommandAllocator*		m_copyCommandAllocator;
	ID3D12RootSignature*		m_rootSignature;
	ID3D12GraphicsCommandList*	m_commandList;
	ID3D12GraphicsCommandList*	m_copyCommandList;
	ID3D12Resource*				m_vertexBuffer;
	ID3D12Resource*				m_constantBuffer;
	ID3D12Resource*				m_pathTraceTex;

	ID3D12Resource*				m_scene_bufferTriPos;
	ID3D12Resource*				m_scene_bufferTriNor;
	ID3D12Resource*				m_scene_bufferTriIdx;
	ID3D12Resource*				m_scene_bufferMeshMaterial;
	ID3D12Resource*				m_scene_bufferMeshIdxRange;

	D3D12_VERTEX_BUFFER_VIEW	m_vertexBufferView;
	ID3D12PipelineState*		m_pipelineStateToneMap;
	ID3D12PipelineState*		m_pipelineStatePathTrace;

	int							m_pathTraceFrameIdx;	// for averaging the result

	int							m_constantBufferOffset[FRAME_CNT + 1];

	// Synchronization objects.
	ID3D12Fence*				m_fence;
	UINT64						m_fenceValues[FRAME_CNT];
	HANDLE						m_fenceEvent;

	int							m_windowWidth;
	int							m_windowHeight;
	
	int2						m_windowPos;
	int2						m_mousePos;
	
	Vector3						m_camPos;
	Vector3						m_camLookAt;
	Vector2						m_camJitter;
	int							m_randSeedOffset;
	int							m_randSeedInterval;
	int							m_randSeedAdd;
	bool						m_isFirstFrame;
	bool						m_isEnableBlur;

	bool						m_isKeyDown[Key_Num];
	bool						m_isMouseDown;

	void	init(int windowWidth, int windowHeight);
	void	release();

	void	update();
	void	render();

	void	onKeyUp(UINT8 key);
	void	onKeyDown(UINT8 key);
	
	void	onMouseUp();
	void	onMouseDown();

	void	setWindowPos(int x, int y);
	void	resize(int w, int h);
};
