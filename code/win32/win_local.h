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

#define _WIN32_WINNT 0x0501

#include <windows.h>


void Sys_QueEvent( int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr );

void	Sys_CreateConsole( void );
void	Sys_DestroyConsole( void );
const char* Sys_ConsoleInput();
void Conbuf_AppendText( const char *msg );


void	IN_Init();
void	IN_Activate( qbool active );
void	IN_Frame();
void	IN_Shutdown();
void	IN_Move();
qbool	IN_ProcessMessage( UINT msg, WPARAM wParam, LPARAM lParam );


void SNDDMA_Activate();
void EnableKeyboardShortcuts(qbool enable);

LONG WINAPI MainWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );

typedef struct
{
	OSVERSIONINFO osversion;

	HWND		hWnd;
	HINSTANCE	hInstance;
	qbool		activeApp;
	qbool		isMinimized;

	// when we get a windows message, we store off the time
	// so keyboard processing can know the exact time of an event
	unsigned	sysMsgTime;
} WinVars_t;

extern WinVars_t	g_wv;
