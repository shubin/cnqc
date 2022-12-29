/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
/*
** WIN_GLIMP.C
**
** This file contains ALL Win32 specific stuff having to do with the
** OpenGL refresh. When a port is being made the following functions
** must be implemented by the port:
**
** Sys_V_EndFrame
** Sys_V_Init
** Sys_V_Shutdown
** Sys_V_IsVSynced
**
** Note that the GLW_xxx functions are Windows specific GL-subsystem
** related functions that are relevant ONLY to win_glimp.c
*/

#if _MSC_VER
#pragma warning (disable: 4996) // deprecated ZOMGOVERRUN! nannywhine
#endif

#include "../renderer/tr_local.h"
#include "../client/client.h"
#include "resource.h"
#include "win_local.h"
#include "glw_win.h"


glwstate_t glw_state;


// responsible for creating the Win32 window and initializing the OpenGL driver.

static qbool GLW_CreateWindow()
{
	static qbool s_classRegistered = qfalse;

	if ( !s_classRegistered )
	{
		WNDCLASS wc;
		memset( &wc, 0, sizeof( wc ) );

		wc.style         = CS_OWNDC;
		wc.lpfnWndProc   = MainWndProc;
		wc.cbClsExtra    = 0;
		wc.cbWndExtra    = 0;
		wc.hInstance     = g_wv.hInstance;
		wc.hIcon         = LoadIcon( g_wv.hInstance, MAKEINTRESOURCE(IDI_ICON1));
		wc.hCursor       = LoadCursor (NULL,IDC_ARROW);
		wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
		wc.lpszMenuName  = 0;
		wc.lpszClassName = CLIENT_WINDOW_TITLE;

		if ( !RegisterClass( &wc ) )
			ri.Error( ERR_FATAL, "GLW_CreateWindow: could not register window class" );

		s_classRegistered = qtrue;
		ri.Printf( PRINT_DEVELOPER, "...registered window class\n" );
	}

	//
	// create the HWND if one does not already exist
	//
	if ( !g_wv.hWnd )
	{
		g_wv.inputInitialized = qfalse;

		RECT r;
		r.left = 0;
		r.top = 0;
		r.right  = glInfo.winWidth;
		r.bottom = glInfo.winHeight;

		int style = WS_VISIBLE | WS_CLIPCHILDREN;
		int exstyle = 0;

		if ( glInfo.winFullscreen )
		{
			style |= WS_POPUP;
			exstyle |= WS_EX_TOPMOST;
		}
		else
		{
			style |= WS_OVERLAPPED | WS_BORDER | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
			AdjustWindowRect( &r, style, FALSE );
		}

		const int w = r.right - r.left;
		const int h = r.bottom - r.top;

		const RECT& monRect = g_wv.monitorRects[g_wv.monitor];

		int dx = 0;
		int dy = 0;

		if ( !glInfo.winFullscreen )
		{
			dx = ri.Cvar_Get( "vid_xpos", "0", 0 )->integer;
			dy = ri.Cvar_Get( "vid_ypos", "0", 0 )->integer;
			dx = Com_ClampInt( 0, max( 0, monRect.right - monRect.left - w ), dx );
			dy = Com_ClampInt( 0, max( 0, monRect.bottom - monRect.top - h ), dy );
		}

		const int x = monRect.left + dx;
		const int y = monRect.top + dy;

		g_wv.duringCreateWindow = qtrue;
		g_wv.hWnd = CreateWindowEx( exstyle, CLIENT_WINDOW_TITLE, " " CLIENT_WINDOW_TITLE, style,
				x, y, w, h, NULL, NULL, g_wv.hInstance, NULL );
		g_wv.duringCreateWindow = qfalse;

		if ( !g_wv.hWnd )
			ri.Error( ERR_FATAL, "GLW_CreateWindow() - Couldn't create window" );

		ShowWindow( g_wv.hWnd, SW_SHOW );
		UpdateWindow( g_wv.hWnd );
		ri.Printf( PRINT_DEVELOPER, "...created window@%d,%d (%dx%d)\n", x, y, w, h );
	}
	else
	{
		//ri.Printf( PRINT_DEVELOPER, "...window already present, CreateWindowEx skipped\n" );

		// @TODO: unify this crap
		// @TODO: full-screen to windowed has a delay to show the title bar

		const LONG oldStyle = GetWindowLongA(g_wv.hWnd, GWL_STYLE);
		const qbool fullScreen = (oldStyle & WS_POPUP) != 0;
		RECT r;
		GetClientRect(g_wv.hWnd, &r);
		if(r.right - r.left != glInfo.winWidth ||
			r.bottom - r.top != glInfo.winHeight ||
			fullScreen != glInfo.winFullscreen)
		{
			int style = WS_VISIBLE | WS_CLIPCHILDREN;
			int exstyle = 0;

			if(glInfo.winFullscreen)
			{
				style |= WS_POPUP;
				exstyle |= WS_EX_TOPMOST;
			}
			else
			{
				style |= WS_OVERLAPPED | WS_BORDER | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
			}

			r.left = 0;
			r.top = 0;
			r.right = glInfo.winWidth;
			r.bottom = glInfo.winHeight;

			const int w = r.right - r.left;
			const int h = r.bottom - r.top;

			const RECT& monRect = g_wv.monitorRects[g_wv.monitor];

			int dx = 0;
			int dy = 0;

			if(!glInfo.winFullscreen)
			{
				dx = ri.Cvar_Get("vid_xpos", "0", 0)->integer;
				dy = ri.Cvar_Get("vid_ypos", "0", 0)->integer;
				dx = Com_ClampInt(0, max(0, monRect.right - monRect.left - w), dx);
				dy = Com_ClampInt(0, max(0, monRect.bottom - monRect.top - h), dy);
			}

			const int x = monRect.left + dx;
			const int y = monRect.top + dy;
			
			SetWindowPos(g_wv.hWnd, 0, x, y, w, h, SWP_NOREDRAW | SWP_NOZORDER);
			SetWindowLongA(g_wv.hWnd, GWL_STYLE, style);
			SetWindowLongA(g_wv.hWnd, GWL_EXSTYLE, exstyle);
			RedrawWindow(g_wv.hWnd, 0, 0, RDW_INVALIDATE | RDW_FRAME);
			//ShowWindow(g_wv.hWnd, SW_SHOW);
			//UpdateWindow(g_wv.hWnd);
		}
	}

	SetForegroundWindow( g_wv.hWnd );
	SetFocus( g_wv.hWnd );

	return qtrue;
}


static const char* GLW_GetCurrentDisplayDeviceName()
{
	static char deviceName[CCHDEVICENAME + 1];

	const HMONITOR hMonitor = g_wv.hMonitors[g_wv.monitor];
	if ( hMonitor == NULL )
		return NULL;

	MONITORINFOEXA info;
	ZeroMemory( &info, sizeof(info) );
	info.cbSize = sizeof(info);
	if ( GetMonitorInfoA(hMonitor, &info) == 0 )
		return NULL;

	Q_strncpyz( deviceName, info.szDevice, sizeof(deviceName) );
	
	return deviceName;
}


static void GLW_UpdateMonitorRect( const char* deviceName )
{
	if ( deviceName == NULL )
		return;

	DEVMODEA dm;
	ZeroMemory( &dm, sizeof(dm) );
	dm.dmSize = sizeof(dm);
	if ( EnumDisplaySettingsExA(deviceName, ENUM_CURRENT_SETTINGS, &dm, 0) == 0 )
		return;

	if ( dm.dmPelsWidth == 0 || dm.dmPelsHeight == 0 )
		return;

	// Normally, we should check dm.dmFields for the following flags:
	// DM_POSITION DM_PELSWIDTH DM_PELSHEIGHT
	// EnumDisplaySettingsExA doesn't always set up the flags properly.

	RECT& rect = g_wv.monitorRects[g_wv.monitor];
	rect.left = dm.dmPosition.x;
	rect.top = dm.dmPosition.y;
	rect.right = dm.dmPosition.x + dm.dmPelsWidth;
	rect.bottom = dm.dmPosition.y + dm.dmPelsHeight;
}


static qbool GLW_SetDisplaySettings( DEVMODE& dm )
{
	const char* deviceName = GLW_GetCurrentDisplayDeviceName();
	const int ec = ChangeDisplaySettingsExA( deviceName, &dm, NULL, CDS_FULLSCREEN, NULL );
	if ( ec == DISP_CHANGE_SUCCESSFUL )
	{
		glw_state.cdsDevMode = dm;
		glw_state.cdsDevModeValid = qtrue;
		GLW_UpdateMonitorRect( deviceName );
		return qtrue;
	}

	glw_state.cdsDevModeValid = qfalse;

	ri.Printf( PRINT_ALL, "...CDS: %ix%i (C%i) failed: ", (int)dm.dmPelsWidth, (int)dm.dmPelsHeight, (int)dm.dmBitsPerPel );

#define CDS_ERROR(x) case x: ri.Printf( PRINT_ALL, #x##"\n" ); break;
	switch (ec) {
		default:
			ri.Printf( PRINT_ALL, "unknown error %d\n", ec );
			break;
		CDS_ERROR( DISP_CHANGE_RESTART );
		CDS_ERROR( DISP_CHANGE_BADPARAM );
		CDS_ERROR( DISP_CHANGE_BADFLAGS );
		CDS_ERROR( DISP_CHANGE_FAILED );
		CDS_ERROR( DISP_CHANGE_BADMODE );
		CDS_ERROR( DISP_CHANGE_NOTUPDATED );
	}
#undef CDS_ERROR

	return qfalse;
}


static void GLW_ResetDisplaySettings( qbool invalidate )
{
	const char* deviceName = GLW_GetCurrentDisplayDeviceName();
	ChangeDisplaySettingsEx( deviceName, NULL, NULL, 0, NULL );
	GLW_UpdateMonitorRect( deviceName );
	if ( invalidate )
		glw_state.cdsDevModeValid = qfalse;
}


void WIN_SetGameDisplaySettings()
{
	if ( glw_state.cdsDevModeValid )
		GLW_SetDisplaySettings( glw_state.cdsDevMode );
}


void WIN_SetDesktopDisplaySettings()
{
	// We don't invalidate glw_state.cdsDevModeValid so we can
	// return to the previous mode later.
	GLW_ResetDisplaySettings( qfalse );
}


static qbool GLW_SetMode()
{
	WIN_InitMonitorList();
	WIN_UpdateMonitorIndexFromCvar();

	const RECT& monRect = g_wv.monitorRects[g_wv.monitor];
	const int desktopWidth = (int)(monRect.right - monRect.left);
	const int desktopHeight = (int)(monRect.bottom - monRect.top);
	R_ConfigureVideoMode( desktopWidth, desktopHeight );

	DEVMODE dm;
	ZeroMemory( &dm, sizeof( dm ) );
	dm.dmSize = sizeof( dm );

	if (glInfo.vidFullscreen != glw_state.cdsDevModeValid) {
		if (glInfo.vidFullscreen) {
			dm.dmPelsWidth  = glConfig.vidWidth;
			dm.dmPelsHeight = glConfig.vidHeight;
			dm.dmFields     = DM_PELSWIDTH | DM_PELSHEIGHT;

			if ( r_displayRefresh->integer ) {
				dm.dmDisplayFrequency = r_displayRefresh->integer;
				dm.dmFields |= DM_DISPLAYFREQUENCY;
			}

			dm.dmBitsPerPel = 32;
			dm.dmFields |= DM_BITSPERPEL;

			dm.dmPosition.x = monRect.left;
			dm.dmPosition.y = monRect.top;
			dm.dmFields |= DM_POSITION;

			glInfo.vidFullscreen = GLW_SetDisplaySettings( dm );
		}
		else
		{
			GLW_ResetDisplaySettings( qtrue );
		}
	}

	if (!GLW_CreateWindow())
		return qfalse;

	if (EnumDisplaySettingsA( GLW_GetCurrentDisplayDeviceName(), ENUM_CURRENT_SETTINGS, &dm ))
		glInfo.displayFrequency = dm.dmDisplayFrequency;

	return qtrue;
}


void Sys_V_EndFrame()
{
}


void Sys_V_Init()
{
	if ( !GLW_SetMode() )
		ri.Error( ERR_FATAL, "Sys_V_Init - could not load video subsystem\n" );
}


void Sys_V_Shutdown()
{
	Sys_ShutdownInput();

	// destroy window
	if ( g_wv.hWnd )
	{
		ri.Printf( PRINT_DEVELOPER, "...destroying window\n" );
		ShowWindow( g_wv.hWnd, SW_HIDE );
		DestroyWindow( g_wv.hWnd );
		g_wv.hWnd = NULL;
	}

	// reset display settings
	if ( glw_state.cdsDevModeValid )
	{
		ri.Printf( PRINT_DEVELOPER, "...resetting display\n" );
		GLW_ResetDisplaySettings( qtrue );
	}
}


qbool Sys_V_IsVSynced()
{
	// with Direct3D 12, our swap interval is (normally) respected
	return r_swapInterval->integer != 0;
}


qbool Sys_IsMinimized()
{
	return ( g_wv.hWnd != NULL ) && !!IsIconic( g_wv.hWnd );
}


void WIN_UpdateResolution( int width, int height )
{
	glInfo.winWidth = width;
	glInfo.winHeight = height;
	if ( r_fullscreen->integer == 0 )
	{
		glConfig.vidWidth = width;
		glConfig.vidHeight = height;
	}
}

