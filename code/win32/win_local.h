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
// win_local.h: Win32-specific Quake3 header file

#include "windows.h"


void Sys_QueEvent( int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr );

void	Sys_CreateConsole( void );
void	Sys_DestroyConsole( void );
const char* Sys_ConsoleInput();
void Conbuf_AppendText( const char *msg );


void	IN_Init();
void	IN_SetCursorSettings( qbool active );
void	IN_Activate( qbool active );
qbool	IN_ShouldBeActive();
qbool	IN_ProcessMessage( UINT msg, WPARAM wParam, LPARAM lParam ); // returns true if the event was handled
void	IN_Frame();
void	IN_Shutdown();

void WIN_UpdateMonitorIndexFromCvar();
void WIN_UpdateMonitorIndexFromMainWindow();
void WIN_UpdateResolution( int width, int height );
void WIN_RegisterLastValidHotKey();
void WIN_UnregisterHotKey();
void WIN_SetGameDisplaySettings();
void WIN_SetDesktopDisplaySettings();

void SNDDMA_Activate();

LRESULT CALLBACK MainWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );

// crash handling
LONG CALLBACK WIN_HandleException( EXCEPTION_POINTERS* ep );
void WIN_HandleExit();
void WIN_EndTimePeriod();

#define MAX_MONITOR_COUNT 16

typedef struct
{
	HWND		hWnd;
	HINSTANCE	hInstance;
	qbool		activeApp;

	// when we get a windows message, we store the time off
	// using Sys_Milliseconds
	int			sysMsgTime;

	RECT		monitorRects[MAX_MONITOR_COUNT];
	HMONITOR	hMonitors[MAX_MONITOR_COUNT];
	int			monitor;		// 0-based index of the monitor currently used for display
	int			primaryMonitor;	// 0-based index of the primary monitor
	int			monitorCount;

	int			minimizeHotKeyId;
	qbool		minimizeHotKeyValid;
	UINT		minimizeHotKeyKey;
	UINT		minimizeHotKeyMods;
} WinVars_t;

extern WinVars_t g_wv;
