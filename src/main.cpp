
// by simon yeung, 06/05/2018
// all rights reserved

#include <stdio.h>
#include <conio.h>

#include "RayTracer.h"

RayTracer		s_rayTracer;
volatile bool	s_isQuit			= false;
volatile bool	s_isQuitCompleted	= false;

static BOOL consoleHandlerRoutine(DWORD dwCtrlType)
{
	s_isQuit= true;

	// wait for the main thread to be completed
	while(!s_isQuitCompleted)
		Sleep(10);

	return FALSE;
}

static void	allocConsole()
{
	AllocConsole();
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)consoleHandlerRoutine, TRUE);
	SetConsoleTitle("Console");
	
	_cprintf("------------  Key Config  ------------\n");
	_cprintf("Mouse drag           : Rotate camera\n");
	_cprintf("Key W, A, S, D, Q, E : Move camera\n");
	_cprintf("Key C                : Reset camera\n");
	_cprintf("Key B                : Toggle simple de-noise\n");
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	RayTracer* rayTracer = reinterpret_cast<RayTracer*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

	switch (message)
	{
	case WM_CREATE:
	{
		// Save the RayTracer* passed in to CreateWindow.
		LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
		SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
	}
	return 0;

	case WM_KEYDOWN:
		if (rayTracer)
			rayTracer->onKeyDown(static_cast<UINT8>(wParam));
		return 0;

	case WM_KEYUP:
	{
		UINT8 key = static_cast<UINT8>(wParam);
		if (rayTracer)
			rayTracer->onKeyUp(key);
		if (key == VK_ESCAPE)
			PostQuitMessage(0);
		return 0;
	}
	
	case WM_LBUTTONDOWN:
		if (rayTracer)
			rayTracer->onMouseDown();
		return 0;

	case WM_LBUTTONUP:
		if (rayTracer)
			rayTracer->onMouseUp();
		return 0;

	case WM_PAINT:
		if (rayTracer)
		{
			rayTracer->update();
			rayTracer->render();
		}
		return 0;
	case WM_MOVE:
		{
			int xPos = (int) LOWORD(lParam);
			int yPos = (int) HIWORD(lParam);
			if (rayTracer)
				rayTracer->setWindowPos(xPos, yPos);
		}
		return 0;
	case WM_SIZE:
		{
			int w = (int) LOWORD(lParam);
			int h = (int) HIWORD(lParam);
			if (rayTracer)
				rayTracer->resize(w, h);
		}
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	// Handle any messages the switch statement didn't.
	return DefWindowProc(hWnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	// Initialize the window class.
	WNDCLASSEX windowClass = { 0 };
	windowClass.cbSize = sizeof(WNDCLASSEX);
	windowClass.style = CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc = WindowProc;
	windowClass.hInstance = hInstance;
	windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	windowClass.lpszClassName = "Path Tracer DX12";
	RegisterClassEx(&windowClass);

	int windowWidth	= 512;
	int windowHeight= 512;
	RECT windowRect = { 0, 0, (LONG)windowWidth, (LONG)windowHeight };
	AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

	// Create the window and store a handle to it.
	s_rayTracer.m_hwnd = CreateWindow(		windowClass.lpszClassName,
											"DX12 Path Tracer",
											WS_OVERLAPPEDWINDOW,
											CW_USEDEFAULT,
											CW_USEDEFAULT,
											windowRect.right - windowRect.left,
											windowRect.bottom - windowRect.top,
											nullptr,		// We have no parent window.
											nullptr,		// We aren't using menus.
											hInstance,
											&s_rayTracer);

	s_rayTracer.init(windowWidth, windowHeight);

	allocConsole();
	ShowWindow(s_rayTracer.m_hwnd, nCmdShow);

	// Main loop.
	MSG msg = {};
	while (msg.message != WM_QUIT && !s_isQuit)
	{
		// Process any messages in the queue.
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	s_rayTracer.release();
	s_isQuitCompleted= true;

	FreeConsole();
	return 0;
}

