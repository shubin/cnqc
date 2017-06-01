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

#include "../client/client.h"
#include "win_local.h"
#include "glw_win.h"


// Console variables that we need to access from this module
cvar_t		*vid_xpos;			// X coordinate of window position
cvar_t		*vid_ypos;			// Y coordinate of window position
cvar_t		*r_fullscreen;

static qbool s_alttab_disabled;


#pragma warning(disable : 4702) // unreachable code

static void WIN_DisableAltTab()
{
	//if ( s_alttab_disabled )
		return;

	if ( !Q_stricmp( Cvar_VariableString( "arch" ), "winnt" ) )
	{
		RegisterHotKey( 0, 0, MOD_ALT, VK_TAB );
	}
	else
	{
		BOOL old;
		SystemParametersInfo( SPI_SCREENSAVERRUNNING, 1, &old, 0 );
	}

	s_alttab_disabled = qtrue;
}


static void WIN_EnableAltTab()
{
	if ( !s_alttab_disabled )
		return;

	if ( !Q_stricmp( Cvar_VariableString( "arch" ), "winnt" ) )
	{
		UnregisterHotKey( 0, 0 );
	}
	else
	{
		BOOL old;
		SystemParametersInfo( SPI_SCREENSAVERRUNNING, 0, &old, 0 );
	}

	s_alttab_disabled = qfalse;
}


static void WIN_AppActivate( BOOL fActive, BOOL fMinimized )
{
	const qbool active = fActive && !fMinimized;

	if (r_fullscreen->integer)
		SetWindowPos( g_wv.hWnd, active ? HWND_TOPMOST : HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );

	Com_DPrintf("VID_AppActivate: %i\n", fActive );

	Key_ClearStates();	// FIXME!!!

	// we don't want to act like we're active if we're minimized
	g_wv.activeApp = active;

	IN_Activate( IN_ShouldBeActive() );
}


///////////////////////////////////////////////////////////////


static byte s_scantokey[128] =
{
//  0           1       2       3       4       5       6       7 
//  8           9       A       B       C       D       E       F 
	0  ,    27,     '1',    '2',    '3',    '4',    '5',    '6', 
	'7',    '8',    '9',    '0',    '-',    '=',    K_BACKSPACE, 9, // 0 
	'q',    'w',    'e',    'r',    't',    'y',    'u',    'i', 
	'o',    'p',    '[',    ']',    13 ,    K_CTRL,'a',  's',      // 1 
	'd',    'f',    'g',    'h',    'j',    'k',    'l',    ';', 
	'\'' ,    '`',    K_SHIFT,'\\',  'z',    'x',    'c',    'v',      // 2 
	'b',    'n',    'm',    ',',    '.',    '/',    K_SHIFT,'*', 
	K_ALT,' ',   K_CAPSLOCK  ,    K_F1, K_F2, K_F3, K_F4, K_F5,   // 3 
	K_F6, K_F7, K_F8, K_F9, K_F10,  K_PAUSE,    0  , K_HOME, 
	K_UPARROW,K_PGUP,K_KP_MINUS,K_LEFTARROW,K_KP_5,K_RIGHTARROW, K_KP_PLUS,K_END, //4 
	K_DOWNARROW,K_PGDN,K_INS,K_DEL,0,0,             0,              K_F11, 
	K_F12,0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0,        // 5
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0, 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0,        // 6 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0, 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0         // 7 
};

/*
=======
MapKey

Map from windows to quake keynums
=======
*/
static int MapKey (int key)
{
	int result;
	int modified;
	qbool is_extended;

	//Com_Printf( "0x%X\n", key );

	modified = ( key >> 16 ) & 255;

	if ( modified > 127 )
		return 0;

	if ( key & ( 1 << 24 ) )
	{
		is_extended = qtrue;
	}
	else
	{
		is_extended = qfalse;
	}

	result = s_scantokey[modified];

	if ( !is_extended )
	{
		//Com_Printf( "!extended 0x%X\n", result );
		switch ( result )
		{
		case K_HOME:
			return K_KP_HOME;
		case K_UPARROW:
			return K_KP_UPARROW;
		case K_PGUP:
			return K_KP_PGUP;
		case K_LEFTARROW:
			return K_KP_LEFTARROW;
		case K_RIGHTARROW:
			return K_KP_RIGHTARROW;
		case K_END:
			return K_KP_END;
		case K_DOWNARROW:
			return K_KP_DOWNARROW;
		case K_PGDN:
			return K_KP_PGDN;
		case K_INS:
			return K_KP_INS;
		case K_DEL:
			return K_KP_DEL;
		case '*':
			return K_KP_STAR;
		case 0x00:
			return K_BACKSLASH;
		default:
			return result;
		}
	}
	else
	{
		//Com_Printf( "extended 0x%X\n", result );
		switch ( result )
		{
		case K_PAUSE:
			return K_KP_NUMLOCK;
		case 0x0D:
			return K_KP_ENTER;
		case 0x2F:
			return K_KP_SLASH;
		case 0xAF:
			return K_KP_PLUS;
		}
		return result;
	}
}


/*
====================
MainWndProc

main window procedure
====================
*/

LRESULT CALLBACK MainWndProc (
    HWND    hWnd,
    UINT    uMsg,
    WPARAM  wParam,
    LPARAM  lParam)
{
	switch (uMsg)
	{

	case WM_MOUSEWHEEL:
		{
			int i = (short)HIWORD(wParam);
			// note: apparently the vista mouse driver often returns < WHEEL_DELTA
			// but anyone running vista is a moron anyway, so fkit  :P
			if (i > 0) {
				while (i >= WHEEL_DELTA) {
					Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, qtrue, 0, NULL );
					Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, qfalse, 0, NULL );
					i -= WHEEL_DELTA;
				}
			} else {
				while (i <= -WHEEL_DELTA) {
					Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL );
					Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL );
					i += WHEEL_DELTA;
				}
			}
			// when an application processes the WM_MOUSEWHEEL message, it must return zero
			return 0;
		}
		break;

	case WM_CREATE:

		g_wv.hWnd = hWnd;

		vid_xpos = Cvar_Get( "vid_xpos", "3", CVAR_ARCHIVE );
		vid_ypos = Cvar_Get( "vid_ypos", "22", CVAR_ARCHIVE );
		r_fullscreen = Cvar_Get( "r_fullscreen", "1", CVAR_ARCHIVE | CVAR_LATCH );
		Cvar_Get( "r_monitor", "0", CVAR_ARCHIVE | CVAR_LATCH ); // 1-based monitor index, 0 means primary

		if ( r_fullscreen->integer )
			WIN_DisableAltTab();
		else
			WIN_EnableAltTab();

		WIN_RegisterLastValidHotKey();

		break;

	case WM_DESTROY:
		WIN_UnregisterHotKey();
		g_wv.hWnd = NULL;
		if ( r_fullscreen->integer )
			WIN_EnableAltTab();
		break;

	case WM_CLOSE:
		Cbuf_AddText( "quit\n" );
		break;

	case WM_ACTIVATE:
		WIN_AppActivate( (LOWORD(wParam) != WA_INACTIVE), !!(BOOL)HIWORD(wParam) );
		SNDDMA_Activate();
		break;

	case WM_MOVE:
		{
			if (!r_fullscreen->integer )
			{
				WIN_UpdateMonitorIndexFromMainWindow();

				RECT r;
				r.left   = 0;
				r.top    = 0;
				r.right  = 1;
				r.bottom = 1;
				AdjustWindowRect( &r, GetWindowLong( hWnd, GWL_STYLE ), FALSE );
				
				const RECT& monRect = g_wv.monitorRects[g_wv.monitor];
				const int x = LOWORD( lParam );
				const int y = HIWORD( lParam );
				Cvar_SetValue( "vid_xpos", x + r.left - monRect.left );
				Cvar_SetValue( "vid_ypos", y + r.top - monRect.top );
				vid_xpos->modified = qfalse;
				vid_ypos->modified = qfalse;
			}
		}
		break;

	case WM_SIZE:
		WIN_UpdateResolution( (int)LOWORD(lParam), (int)HIWORD(lParam) );
		break;

	case WM_SYSCOMMAND:
		if ( wParam == SC_SCREENSAVE )
			return 0;
		break;

	case WM_SYSKEYDOWN:
		if ( wParam == 13 )
		{
			if ( r_fullscreen )
			{
				Cvar_SetValue( "r_fullscreen", !r_fullscreen->integer );
				Cbuf_AddText( "vid_restart\n" );
			}
			return 0;
		}
		// fall through
	case WM_KEYDOWN:
		Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, MapKey( lParam ), qtrue, 0, NULL );
		break;

	case WM_SYSKEYUP:
	case WM_KEYUP:
		Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, MapKey( lParam ), qfalse, 0, NULL );
		break;

	case WM_CHAR:
		{
			const char scanCode = (char)( ( lParam >> 16 ) & 0xFF );
			if ( scanCode != 0x29 ) // never send an event for the console key ('~' or '`')
				Sys_QueEvent( g_wv.sysMsgTime, SE_CHAR, wParam, 0, 0, NULL );
		}
		break;

	case WM_HOTKEY:
		if ( g_wv.minimizeHotKeyValid && (int)wParam == g_wv.minimizeHotKeyId )
		{
			if ( g_wv.activeApp && !CL_VideoRecording() )
			{
				ShowWindow( hWnd, SW_MINIMIZE );
			}
			else
			{
				ShowWindow( hWnd, SW_RESTORE );
				SetForegroundWindow( hWnd );
				SetFocus( hWnd );
			}
		}
		break;

	case WM_SETFOCUS:
		if ( glw_state.cdsDevModeValid ) // is there a valid mode to restore?
		{
			WIN_SetGameDisplaySettings();
			if ( glw_state.cdsDevModeValid ) // was the mode successfully restored?
			{
				const RECT& rect = g_wv.monitorRects[g_wv.monitor];
				const DEVMODE& dm = glw_state.cdsDevMode;
				SetWindowPos( hWnd, NULL, (int)rect.left, (int)rect.top, (int)dm.dmPelsWidth, (int)dm.dmPelsHeight, SWP_NOZORDER );
			}
		}
		g_wv.activeApp = (qbool)!IsIconic( hWnd );
		IN_SetCursorSettings( IN_ShouldBeActive() );
			
		break;

	case WM_KILLFOCUS:
		g_wv.activeApp = qfalse;
		IN_SetCursorSettings( qfalse );
		if ( glw_state.cdsDevModeValid )
			WIN_SetDesktopDisplaySettings();
		break;

	default:
		// this is complicated because Win32 seems to pack multiple mouse events into
		// one update sometimes, so we always check all states and look for events
		if ( uMsg == WM_INPUT || (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST) )
			if ( IN_ProcessMessage(uMsg, wParam, lParam) )
				return 0;
		break;
	}

	return DefWindowProc( hWnd, uMsg, wParam, lParam );
}

