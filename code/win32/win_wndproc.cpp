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

WinVars_t	g_wv;


// Console variables that we need to access from this module
cvar_t		*vid_xpos;			// X coordinate of window position
cvar_t		*vid_ypos;			// Y coordinate of window position
cvar_t		*r_fullscreen;

static qbool s_alttab_disabled;


#pragma warning(disable : 4702) // unreachable code

static void WIN_DisableAltTab()
{
	if ( s_alttab_disabled )
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
	if ( s_alttab_disabled )
	{
		if ( !Q_stricmp( Cvar_VariableString( "arch" ), "winnt" ) )
		{
			UnregisterHotKey( 0, 0 );
		}
		else
		{
			BOOL old;
			SystemParametersInfo( SPI_SCREENSAVERRUNNING, 0, &old, 0 );
		}
	}

	s_alttab_disabled = qfalse;
}


static void VID_AppActivate( BOOL fActive, BOOL minimize )
{
	if (r_fullscreen->integer)
		SetWindowPos( g_wv.hWnd, fActive ? HWND_TOPMOST : HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );

	g_wv.isMinimized = (minimize == TRUE);

	Com_DPrintf( "VID_AppActivate: %i\n", fActive );

	Key_ClearStates();	// FIXME!!!

	// we don't want to act like we're active if we're minimized
	g_wv.activeApp = (fActive && !g_wv.isMinimized);

	// minimize/restore mouse-capture on demand
	if (!g_wv.activeApp)
	    IN_Activate( qfalse );
}


///////////////////////////////////////////////////////////////


static const byte s_scantokey[128] =
{
//	0         1         2         3         4         5         6         7
//	8         9         A         B         C         D         E         F
	0,        27,       '1',      '2',      '3',      '4',      '5',      '6',
	'7',      '8',      '9',      '0',      '-',      '=',      K_BACKSPACE, 9, // 0
	'q',      'w',      'e',      'r',      't',      'y',      'u',      'i',
	'o',      'p',      '[',      ']',      13 ,      K_CTRL,   'a',      's',  // 1
	'd',      'f',      'g',      'h',      'j',      'k',      'l',      ';',
	'\'',     '`',      K_SHIFT,  '\\',     'z',      'x',      'c',      'v',  // 2
	'b',      'n',      'm',      ',',      '.',      '/',      K_SHIFT,  '*',
	K_ALT,    ' ',      K_CAPSLOCK, K_F1,   K_F2,     K_F3,     K_F4,     K_F5, // 3
	K_F6,     K_F7,     K_F8,     K_F9,     K_F10,    K_PAUSE,  0,        K_HOME,
	K_UPARROW,K_PGUP,   K_KP_MINUS,K_LEFTARROW,K_KP_5,K_RIGHTARROW,K_KP_PLUS,K_END, //4
	K_DOWNARROW,K_PGDN, K_INS,    K_DEL,    0,        0,        0,        K_F11,
	K_F12,0  ,    0  ,    0  ,    0  ,    K_MENU  ,    0  ,    0,        // 5
	0,        0,        0,        0,        0,        0,        0,        0,
	0,        0,        0,        0,        0,        0,        0,        0,    // 6
	0,        0,        0,        0,        0,        0,        0,        0,
	0,        0,        0,        0,        0,        0,        0,        0,    // 7
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

//	Com_Printf( "0x%x\n", key);

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
		default:
			return result;
		}
	}
	else
	{
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
	if( result >= ' ' && result < 128 ) { // drakkar - do not use extended keys as normal chars
		return 0;
	}
}

// drakkar
// http://support.microsoft.com/kb/226359
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	int vkCode, isDown;

	if( nCode == HC_ACTION )
	{
		vkCode = ((KBDLLHOOKSTRUCT *)lParam)->vkCode;
		isDown = ( wParam == WM_SYSKEYDOWN || wParam == WM_KEYDOWN );

		switch( vkCode )
		{
			// Disable Windows key
		case VK_LWIN:
		case VK_RWIN:
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_WIN, isDown, 0, NULL );
			return 1;
			break;

			// Disable XXX+TAB : ALT+TAB, SHIFT+TAB
		case VK_TAB:
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_TAB, isDown, 0, NULL );
			return 1;
			break;

			// Disable ALT+XXX : ALT+F4, ALT+TAB
		case VK_LMENU:
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_ALT, isDown, 0, NULL );
			return 1;
			break;

			// Disable XXX+ESC : CTRL+ESC, ALT+ESC, SHIFT+ESC
		case VK_ESCAPE:
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_ESCAPE, isDown, 0, NULL );
			return 1;
			break;
		}
	}

	return CallNextHookEx( 0, nCode, wParam, lParam );
}
// !drakkar


// drakkar
static HHOOK hHook = NULL;
void EnableLowLevelKeyboard(qbool enable)
{
	if( enable )
	{
		if( !hHook )
		{
			// WH_KEYBOARD_LL -> Windows NT/2000/XP
			hHook = SetWindowsHookEx( WH_KEYBOARD_LL, LowLevelKeyboardProc, g_wv.hInstance, 0 );
			if( !hHook )
			{
				Com_Printf("Could not create key hook ( LowLevelKeyboard )\n");
			}
		}
	}
	else
	{
		if( hHook )
		{
			UnhookWindowsHookEx( hHook );
			hHook = NULL;
		}
	}
}
// !drakkar


// drakkar
void EnableKeyboardShortcuts(qbool enable)
{
	if( !g_wv.hWnd || !g_wv.activeApp ) 
		enable = qtrue; // prevent possible bug

	if( enable )
	{
		EnableLowLevelKeyboard( qfalse );
		WIN_EnableAltTab();
	}
	else
	{
		EnableLowLevelKeyboard( qtrue );
		WIN_DisableAltTab();
	}
}
// !drakkar



LONG WINAPI MainWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	switch (uMsg)
	{
	case WM_CREATE:
		g_wv.hWnd = hWnd;
		vid_xpos = Cvar_Get( "vid_xpos", "3", CVAR_ARCHIVE );
		vid_ypos = Cvar_Get( "vid_ypos", "22", CVAR_ARCHIVE );
		r_fullscreen = Cvar_Get( "r_fullscreen", "1", CVAR_ARCHIVE | CVAR_LATCH );
		
		if ( r_fullscreen->integer )
			WIN_DisableAltTab();
		else
			WIN_EnableAltTab();
		break;

	case WM_DESTROY:
		// let sound and input know about this?
		g_wv.hWnd = NULL;
		// drakkar
		EnableKeyboardShortcuts( qtrue );
		// !drakkar
		if ( r_fullscreen->integer )
			WIN_EnableAltTab();
		break;

	// drakkar
	case WM_SETFOCUS:
		Key_ClearStates();
		re.WindowFocus( qtrue );
		SNDDMA_Activate();
		break;

	case WM_KILLFOCUS:
		Key_ClearStates();
		re.WindowFocus( qfalse );
		SNDDMA_Activate();
		break;

	case WM_SIZE:
		if( wParam == SIZE_MINIMIZED ) Cmd_ExecuteString( "windowMode minimized\n" );
		if( wParam == SIZE_MAXIMIZED ) Cmd_ExecuteString( "windowMode fullscreen\n" );
		if( wParam == SIZE_RESTORED  ) Cmd_ExecuteString( "windowMode windowed\n" );		
		break;
	// !drakkar

	case WM_ACTIVATE:
		VID_AppActivate( (LOWORD(wParam) != WA_INACTIVE), (BOOL)HIWORD(wParam) );
		SNDDMA_Activate();
		break;

    case WM_CLOSE:
        Cbuf_AddText( "quit\n" );
        //break;
	case WM_MOVE:
		if (!r_fullscreen->integer)
		{
			RECT rc;
			SetRectEmpty( &rc );
			AdjustWindowRect( &rc, GetWindowLong( hWnd, GWL_STYLE ), FALSE );
			Cvar_SetValue( "vid_xpos", (short)LOWORD(lParam) + rc.left );
			Cvar_SetValue( "vid_ypos", (short)HIWORD(lParam) + rc.top );
			vid_xpos->modified = qfalse;
			vid_ypos->modified = qfalse;
			IN_Move();
		}
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
		Sys_QueEvent( g_wv.sysMsgTime, SE_CHAR, wParam, 0, 0, NULL );
		break;

	default:
	    if (uMsg == WM_INPUT || (uMsg>=WM_MOUSEFIRST && uMsg<=WM_MOUSELAST))
            IN_Activate( g_wv.activeApp );

		if (IN_ProcessMessage( uMsg, wParam, lParam ))
			return 0;
		break;
	}

	return DefWindowProc( hWnd, uMsg, wParam, lParam );
}

