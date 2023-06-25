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
#include "../qcommon/qcommon.h"
#include "win_local.h"
#include "win_help.h"
#include "resource.h"
#include <errno.h>
#include <float.h>
#include <fcntl.h>
#include <stdio.h>
#include <direct.h>
#include <io.h>
#include <conio.h>
#include <malloc.h>
#include <VersionHelpers.h>
#include "../renderdoc/renderdoc_app.h"


WinVars_t g_wv;


static qbool win_timePeriodActive = qfalse;


static void WIN_BeginTimePeriod()
{
	if ( win_timePeriodActive )
		return;

	timeBeginPeriod( 1 );
	win_timePeriodActive = qtrue;
}


void WIN_EndTimePeriod()
{
	if ( !win_timePeriodActive )
		return;

	timeEndPeriod( 1 );
	win_timePeriodActive = qfalse;
}


#define MEM_THRESHOLD 96*1024*1024

qbool Sys_LowPhysicalMemory()
{
	MEMORYSTATUS stat;
	GlobalMemoryStatus( &stat );
	return (stat.dwTotalPhys <= MEM_THRESHOLD);
}


// show the early console as an error dialog

void QDECL Sys_Error( PRINTF_FORMAT_STRING const char *error, ... )
{
	va_list		argptr;
	char		text[4096];

	va_start (argptr, error);
	vsprintf (text, error, argptr);
	va_end (argptr);

	WIN_AppendConsoleText( text );
	WIN_AppendConsoleText( "\n" );

	Sys_SetErrorText( text );
	Sys_ShowConsole( 3, qtrue );

	WIN_EndTimePeriod();

#ifndef DEDICATED
	Sys_ShutdownInput();
#endif

	// wait for the user to quit
	while (1) {
		MSG msg;
		if (!GetMessage(&msg, NULL, 0, 0))
			Com_Quit( 1 );
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	WIN_DestroyConsole();

	exit(1);
}


void Sys_Quit( int status )
{
	WIN_EndTimePeriod();
#ifndef DEDICATED
	Sys_ShutdownInput();
#endif
	WIN_DestroyConsole();
	exit( status );
}


void Sys_Print( const char *msg )
{
	WIN_AppendConsoleText( msg );
}


void Sys_Mkdir( const char* path )
{
	_mkdir( path );
}


const char* Sys_Cwd()
{
	static char cwd[MAX_OSPATH];

	_getcwd( cwd, sizeof( cwd ) - 1 );
	cwd[MAX_OSPATH-1] = 0;

	return cwd;
}


/*
==============================================================

DIRECTORY SCANNING

==============================================================
*/

#define	MAX_FOUND_FILES	0x1000

static void Sys_ListFilteredFiles( const char *basedir, const char *subdirs, const char *filter, char **list, int *numfiles )
{
	if ( *numfiles >= MAX_FOUND_FILES - 1 ) {
		return;
	}

	char		search[MAX_OSPATH];

	if (subdirs[0]) {
		Com_sprintf( search, sizeof(search), "%s\\%s\\*", basedir, subdirs );
	}
	else {
		Com_sprintf( search, sizeof(search), "%s\\*", basedir );
	}

	struct _finddata_t findinfo;
	const intptr_t findhandle = _findfirst (search, &findinfo);
	if (findhandle == -1) {
		return;
	}

	char newsubdirs[MAX_OSPATH];
	char filename[MAX_OSPATH];
	do {
		if (findinfo.attrib & _A_SUBDIR) {
			if (Q_stricmp(findinfo.name, ".") && Q_stricmp(findinfo.name, "..")) {
				if (subdirs[0]) {
					Com_sprintf( newsubdirs, sizeof(newsubdirs), "%s\\%s", subdirs, findinfo.name);
				}
				else {
					Com_sprintf( newsubdirs, sizeof(newsubdirs), "%s", findinfo.name);
				}
				Sys_ListFilteredFiles( basedir, newsubdirs, filter, list, numfiles );
			}
		}
		if ( *numfiles >= MAX_FOUND_FILES - 1 ) {
			break;
		}
		Com_sprintf( filename, sizeof(filename), "%s\\%s", subdirs, findinfo.name );
		if (!Com_FilterPath( filter, filename ))
			continue;
		list[ *numfiles ] = CopyString( filename );
		(*numfiles)++;
	} while ( _findnext (findhandle, &findinfo) != -1 );

	_findclose (findhandle);
}


char **Sys_ListFiles( const char *directory, const char *extension, const char *filter, int *numfiles, qbool wantsubs )
{
	int			nfiles;
	char		**listCopy;
	char		*list[MAX_FOUND_FILES];
	struct _finddata_t findinfo;
	int			flag;
	int			i;

	if (filter) {

		nfiles = 0;
		Sys_ListFilteredFiles( directory, "", filter, list, &nfiles );

		list[ nfiles ] = 0;
		*numfiles = nfiles;

		if (!nfiles)
			return NULL;

		listCopy = (char**)Z_Malloc( ( nfiles + 1 ) * sizeof( *listCopy ) );
		for ( i = 0 ; i < nfiles ; i++ ) {
			listCopy[i] = list[i];
		}
		listCopy[i] = NULL;

		return listCopy;
	}

	if ( !extension) {
		extension = "";
	}

	// passing a slash as extension will find directories
	if ( extension[0] == '/' && extension[1] == 0 ) {
		extension = "";
		flag = 0;
	} else {
		flag = _A_SUBDIR;
	}

	char search[MAX_OSPATH];
	Com_sprintf( search, sizeof(search), "%s\\*%s", directory, extension );

	// search
	nfiles = 0;

	const intptr_t findhandle = _findfirst (search, &findinfo);
	if (findhandle == -1) {
		*numfiles = 0;
		return NULL;
	}

	do {
		if ( (!wantsubs && flag ^ ( findinfo.attrib & _A_SUBDIR )) || (wantsubs && findinfo.attrib & _A_SUBDIR) ) {
			if ( nfiles == MAX_FOUND_FILES - 1 ) {
				break;
			}
			list[ nfiles ] = CopyString( findinfo.name );
			nfiles++;
		}
	} while ( _findnext (findhandle, &findinfo) != -1 );

	list[ nfiles ] = 0;

	_findclose (findhandle);

	// return a copy of the list
	*numfiles = nfiles;

	if ( !nfiles ) {
		return NULL;
	}

	listCopy = (char**)Z_Malloc( ( nfiles + 1 ) * sizeof( *listCopy ) );
	for ( i = 0 ; i < nfiles ; i++ ) {
		listCopy[i] = list[i];
	}
	listCopy[i] = NULL;

	do {
		flag = 0;
		for(i=1; i<nfiles; i++) {
			if ( strcmp( listCopy[i-1], listCopy[i] ) == 1 ) {
				char *temp = listCopy[i];
				listCopy[i] = listCopy[i-1];
				listCopy[i-1] = temp;
				flag = 1;
			}
		}
	} while(flag);

	return listCopy;
}


void Sys_FreeFileList( char **list )
{
	int		i;

	if ( !list ) {
		return;
	}

	for ( i = 0 ; list[i] ; i++ ) {
		Z_Free( list[i] );
	}

	Z_Free( list );
}


char *Sys_GetClipboardData( void )
{
	char *data = NULL;
	char *cliptext;

	if ( OpenClipboard( NULL ) ) {
		HANDLE hClipboardData;

		if ( ( hClipboardData = GetClipboardData( CF_TEXT ) ) != 0 ) {
			if ( ( cliptext = (char*)GlobalLock( hClipboardData ) ) != 0 ) {
				data = (char*)Z_Malloc( GlobalSize( hClipboardData ) + 1 );
				Q_strncpyz( data, cliptext, GlobalSize( hClipboardData ) );
				GlobalUnlock( hClipboardData );
				//strtok( data, "\n\r\b" ); // keep all the lines...
			}
		}

		CloseClipboard();
	}

	return data;
}


void Sys_SetClipboardData( const char* text )
{
	if ( OpenClipboard( NULL ) ) {
		const int l = strlen( text );
		const HGLOBAL hMemory = GlobalAlloc( GMEM_MOVEABLE, l + 1 );
		if ( hMemory ) {
			void* const dstMemory = GlobalLock( hMemory );
			if ( dstMemory ) {
				strcpy( (char*)dstMemory, text );
				GlobalUnlock( hMemory );
				EmptyClipboard();
				SetClipboardData( CF_TEXT, hMemory );
			}
			GlobalFree( hMemory );
		}
		CloseClipboard();
	}
}


void Sys_GetCursorPosition( int* x, int* y )
{
	POINT point;
	GetCursorPos( &point );
	ScreenToClient( g_wv.hWnd, &point );
	*x = point.x;
	*y = point.y;
}


/*
========================================================================

LOAD/UNLOAD DLL

========================================================================
*/


void Sys_UnloadDll( void *dllHandle )
{
	if ( !dllHandle ) {
		return;
	}
	if ( !FreeLibrary( (HMODULE)dllHandle ) ) {
		Com_Error (ERR_FATAL, "Sys_UnloadDll FreeLibrary failed");
	}
}


// used to load a development dll instead of a virtual machine

void* QDECL Sys_LoadDll( const char* name, dllSyscall_t *entryPoint, dllSyscall_t systemcalls )
{
	char filename[MAX_QPATH];
	Com_sprintf( filename, sizeof( filename ), "%sx86.dll", name );

	const char* basepath = Cvar_VariableString( "fs_basepath" );
	const char* gamedir = Cvar_VariableString( "fs_game" );
	const char* fn = FS_BuildOSPath( basepath, gamedir, filename );

#ifdef NDEBUG
	static int lastWarning = 0;
	int timestamp = Sys_Milliseconds();
	if( ((timestamp - lastWarning) > (5 * 60000)) && !Cvar_VariableIntegerValue( "dedicated" )
		&& !Cvar_VariableIntegerValue( "com_blindlyLoadDLLs" ) ) {
		if (FS_FileExists(filename)) {
			lastWarning = timestamp;
			if (IDOK != MessageBoxEx( NULL, "You are about to load a .DLL executable that\n"
					"has not been verified for use with Quake III Arena.\n"
					"This type of file can compromise the security of\n"
					"your computer.\n\n"
					"Select 'OK' if you choose to load it anyway.",
					"Security Warning", MB_OKCANCEL | MB_ICONEXCLAMATION | MB_DEFBUTTON2 | MB_TOPMOST | MB_SETFOREGROUND,
					MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ) ) )
				return NULL;
		}
	}
#endif

	HINSTANCE libHandle = LoadLibrary( fn );

#ifndef NDEBUG
	Com_Printf( "LoadLibrary '%s' %s\n", fn, libHandle ? "ok" : "failed" );
#endif

	if ( !libHandle )
		return NULL;

	dllEntry_t dllEntry = ( dllEntry_t ) GetProcAddress( libHandle, "dllEntry" );
	*entryPoint = ( dllSyscall_t ) GetProcAddress( libHandle, "vmMain" );
	if ( !*entryPoint || !dllEntry ) {
		FreeLibrary( libHandle );
		return NULL;
	}
	dllEntry( systemcalls );

	return libHandle;
}


/*
========================================================================

EVENT LOOP

========================================================================
*/

#define	MAX_QUED_EVENTS		512
#define	MASK_QUED_EVENTS	( MAX_QUED_EVENTS - 1 )

static sysEvent_t	eventQue[MAX_QUED_EVENTS];
static int			eventHead, eventTail;


// a time of 0 will get the current time
// ptr should either be null, or point to a block of data that can be freed by the game later

void WIN_QueEvent( int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr )
{
	sysEvent_t* ev = &eventQue[ eventHead & MASK_QUED_EVENTS ];

	if ( eventHead - eventTail >= MAX_QUED_EVENTS ) {
		Com_Printf("Sys_QueEvent: overflow\n");
		// we are discarding an event, but don't leak memory
		if ( ev->evPtr ) {
			Z_Free( ev->evPtr );
		}
		eventTail++;
	}

	eventHead++;

	if ( time == 0 ) {
		time = Sys_Milliseconds();
	}

	ev->evTime = time;
	ev->evType = type;
	ev->evValue = value;
	ev->evValue2 = value2;
	ev->evPtrLength = ptrLength;
	ev->evPtr = ptr;
}


sysEvent_t Sys_GetEvent()
{
	// return if we have data
	if ( eventHead > eventTail ) {
		eventTail++;
		return eventQue[ ( eventTail - 1 ) & MASK_QUED_EVENTS ];
	}

	// pump the message loop
	MSG msg;
	while (PeekMessage( &msg, NULL, 0, 0, PM_NOREMOVE )) {
		if (!GetMessage( &msg, NULL, 0, 0 )) {
			Com_Quit( 0 );
		}

		// save the msg time, because wndprocs don't have access to the timestamp
		// msg.time seems to use values from GetTickCount
		g_wv.sysMsgTime = Sys_Milliseconds();

		TranslateMessage( &msg );
		DispatchMessage( &msg );
	}

	// check for console commands
	const char* s = WIN_ConsoleInput();
	if ( s ) {
		int len = strlen( s ) + 1;
		char* b = (char*)Z_Malloc( len );
		Q_strncpyz( b, s, len-1 );
		WIN_QueEvent( 0, SE_CONSOLE, 0, 0, len, b );
	}

	// check for network packets
	msg_t		netmsg;
	netadr_t	adr;
	static byte sys_packetReceived[MAX_MSGLEN]; // static or it'll blow half the stack
	MSG_Init( &netmsg, sys_packetReceived, sizeof( sys_packetReceived ) );
	if ( Sys_GetPacket( &adr, &netmsg ) ) {
		// copy out to a seperate buffer for qeueing
		// the readcount stepahead is for SOCKS support
		int len = sizeof( netadr_t ) + netmsg.cursize - netmsg.readcount;
		netadr_t* buf = (netadr_t*)Z_Malloc( len );
		*buf = adr;
		memcpy( buf+1, &netmsg.data[netmsg.readcount], netmsg.cursize - netmsg.readcount );
		WIN_QueEvent( 0, SE_PACKET, 0, 0, len, buf );
	}

	// return if we have data
	if ( eventHead > eventTail ) {
		eventTail++;
		return eventQue[ ( eventTail - 1 ) & MASK_QUED_EVENTS ];
	}

	// create an empty event to return
	sysEvent_t ev;
	memset( &ev, 0, sizeof( ev ) );
	ev.evTime = Sys_Milliseconds();
	return ev;
}


///////////////////////////////////////////////////////////////


static void Sys_Net_Restart_f( void )
{
	NET_Restart();
}


// called after the common systems (cvars, files, etc) are initialized
void Sys_Init()
{
	// make sure the timer is high precision, otherwise NT gets 18ms resolution
	WIN_BeginTimePeriod();

	Cmd_AddCommand( "net_restart", Sys_Net_Restart_f );
	Cmd_SetHelp( "net_restart", "restarts the network system" );

	if ( !IsWindowsVistaOrGreater() )
		Sys_Error( "%s requires Windows Vista or later", Q3_VERSION );

	Cvar_Set( "arch", "winnt" );
}


///////////////////////////////////////////////////////////////


#ifndef DEDICATED


static void WIN_StartTaskBarFlashing()
{
	FLASHWINFO info;
	ZeroMemory( &info, sizeof( info ) );
	info.cbSize = sizeof( info );
	info.dwFlags = FLASHW_TRAY | FLASHW_TIMERNOFG;
	info.dwTimeout = 0;	// use the default value
	info.hwnd = g_wv.hWnd;
	info.uCount = 0;	// it's continuous because of FLASHW_TIMERNOFG
	FlashWindowEx( &info );
}


static void WIN_StopTaskBarFlashing()
{
	FLASHWINFO info;
	ZeroMemory( &info, sizeof( info ) );
	info.cbSize = sizeof( info );
	info.dwFlags = FLASHW_STOP;
	info.hwnd = g_wv.hWnd;
	FlashWindowEx( &info );
}


static void WIN_MatchStartAlert()
{
	const int alerts = cl_matchAlerts->integer;
	const qbool unfocusedBit = ( alerts & MAF_UNFOCUSED ) != 0;
	const qbool minimized = !!IsIconic( g_wv.hWnd );
	const qbool hasFocus = GetFocus() == g_wv.hWnd;
	const qbool enable = minimized || ( unfocusedBit && !hasFocus );
	if ( !enable )
		return;

	const qbool flashBit = ( alerts & MAF_FLASH ) != 0;
	const qbool beepBit = ( alerts & MAF_BEEP ) != 0;
	const qbool unmuteBit = ( alerts & MAF_UNMUTE ) != 0;

	if ( flashBit )
		WIN_StartTaskBarFlashing();

	if ( beepBit )
		MessageBeep( MB_OK );

	if ( unmuteBit )
		g_wv.forceUnmute = qtrue;
}


static void WIN_MatchEndAlert()
{
	g_wv.forceUnmute = qfalse;

	WIN_StopTaskBarFlashing();
}


void Sys_MatchAlert( sysMatchAlertEvent_t event )
{
	if ( event == SMAE_MATCH_START )
		WIN_MatchStartAlert();
	else if ( event == SMAE_MATCH_END )
		WIN_MatchEndAlert();
}


static void S_Frame()
{
	if ( g_wv.forceUnmute ) {
		WIN_S_Mute( qfalse );
		return;
	}	

	qbool mute = qfalse;
	if ( s_autoMute->integer == AMM_UNFOCUSED ) {
		const qbool hasFocus = GetFocus() == g_wv.hWnd;
		mute = !hasFocus;
	} else if ( s_autoMute->integer == AMM_MINIMIZED ) {
		const qbool minimized = !!IsIconic( g_wv.hWnd );
		mute = minimized;
	}
	WIN_S_Mute( mute );
}


static void WIN_LoadRenderDoc()
{
	renderDocAPI = NULL;

	const HMODULE module = GetModuleHandleA( "renderdoc.dll" );
	if ( module != NULL ) {
		const pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress( module, "RENDERDOC_GetAPI" );
		if ( RENDERDOC_GetAPI( CNQ3_RENDERDOC_API_VERSION, (void**)&renderDocAPI ) != 1 ) {
			renderDocAPI = NULL;
		}
	}

	if ( renderDocAPI ) {
		renderDocAPI->UnloadCrashHandler();
	}
}


#endif


///////////////////////////////////////////////////////////////


static BOOL CALLBACK WIN_MonitorEnumCallback( HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData )
{
	if ( lprcMonitor ) {
		g_wv.monitorRects[g_wv.monitorCount] = *lprcMonitor;
		g_wv.hMonitors[g_wv.monitorCount] = hMonitor;
		g_wv.monitorCount++;
	}

	if ( g_wv.monitorCount >= MAX_MONITOR_COUNT )
		return FALSE;
	
	return TRUE;
}


void WIN_InitMonitorList()
{
	g_wv.monitor = 0;
	g_wv.primaryMonitor = 0;
	g_wv.monitorCount = 0;

	EnumDisplayMonitors( NULL, NULL, &WIN_MonitorEnumCallback, 0 );

	const POINT zero = { 0, 0 };
	const HMONITOR hMonitor = MonitorFromPoint( zero, MONITOR_DEFAULTTOPRIMARY );
	for ( int i = 0; i < g_wv.monitorCount; i++ ) {
		if ( hMonitor ==  g_wv.hMonitors[i] ) {
			g_wv.primaryMonitor = i;
			g_wv.monitor = i;
			break;
		}
	}
}


void WIN_UpdateMonitorIndexFromCvar()
{
	// r_monitor is the 1-based monitor index, 0 means primary monitor
	// use Cvar_Get to enforce the latched change, if any
	const int monitor = Cvar_Get( "r_monitor", "0", CVAR_ARCHIVE | CVAR_LATCH )->integer;
	Cvar_SetRange( "r_monitor", CVART_INTEGER, "0", va("%d", g_wv.monitorCount) );
	Cvar_SetHelp( "r_monitor", help_r_monitor );
	if ( monitor <= 0 || monitor > g_wv.monitorCount ) {
		g_wv.monitor = g_wv.primaryMonitor;
		return;
	}

	g_wv.monitor = Com_ClampInt( 0, g_wv.monitorCount - 1, monitor - 1 );
}


void WIN_UpdateMonitorIndexFromMainWindow()
{
	const HMONITOR hMonitor = MonitorFromWindow( g_wv.hWnd, MONITOR_DEFAULTTONEAREST );
	for ( int i = 0; i < g_wv.monitorCount; i++ ) {
		if ( hMonitor == g_wv.hMonitors[i] ) {
			g_wv.monitor = i;
			break;
		}
	}

	// if r_monitor is 0 and we're already on the primary monitor,
	// don't change the cvar to a non-zero number
	if ( Cvar_VariableIntegerValue( "r_monitor" ) == 0 &&
		 g_wv.monitor == g_wv.primaryMonitor ) {
		return;
	}

	// use the function to apply the change properly
	Cvar_Set( "r_monitor", va( "%d", g_wv.monitor + 1 ) );
}


static void WIN_MonitorList_f()
{
	WIN_InitMonitorList();
	WIN_UpdateMonitorIndexFromCvar();

	if ( g_wv.monitorCount <= 0 ) {
		Com_Printf( "No monitor detected.\n" );
		return;
	}

	Com_Printf( "Monitors detected (left is " S_COLOR_CVAR "r_monitor ^7value):\n" );
	for ( int i = 0; i < g_wv.monitorCount; i++ ) {
		const RECT r = g_wv.monitorRects[i];
		const int w = (int)( r.right - r.left );
		const int h = (int)( r.bottom - r.top );
		const char* const p = i == g_wv.primaryMonitor ? " (primary)" : "";
		Com_Printf( S_COLOR_VAL "%d ^7%dx%d at %d,%d%s\n",
				   i + 1, w, h, (int)r.left, (int)r.top, p );
	}
}


static void WIN_RegisterMonitorCommands()
{
	Cmd_AddCommand( "monitorlist", &WIN_MonitorList_f );
	Cmd_SetModule( "monitorlist", MODULE_CLIENT );
	Cmd_SetHelp( "monitorlist", "refreshes and prints the monitor list" );
}


static void WIN_SetThreadName( PCWSTR name )
{
	// SetThreadDescription is only available since Windows 10 version 1607

	typedef HRESULT (WINAPI *SetThreadDescription_t)( HANDLE, PCWSTR );

	HINSTANCE module = LoadLibraryA( "KernelBase.dll" );
	if ( module == NULL )
		return;

	SetThreadDescription_t pSetThreadDescription = (SetThreadDescription_t)GetProcAddress( module, "SetThreadDescription" );
	if ( pSetThreadDescription != NULL )
		(*pSetThreadDescription)( GetCurrentThread(), name );

	FreeLibrary( module );
}


///////////////////////////////////////////////////////////////


int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow )
{
	// should never get a previous instance in Win32
	if ( hPrevInstance )
		return 0;

	g_wv.hInstance = hInstance;

#ifndef DEDICATED
	WIN_LoadRenderDoc(); // load first to avoid messing with our exception handlers
#endif

	WIN_InstallExceptionHandlers();

	WIN_SetThreadName( L"main thread" );

	// done here so the early console can be shown on the primary monitor
	WIN_InitMonitorList();

	// done before Com/Sys_Init since we need this for error output
	WIN_CreateConsole();

	// get the initial time base
	Sys_Milliseconds();

	char sys_cmdline[MAX_STRING_CHARS];
	Q_strncpyz( sys_cmdline, lpCmdLine, sizeof( sys_cmdline ) );
	Com_Init( sys_cmdline );
	WIN_RegisterExceptionCommands();
	WIN_RegisterMonitorCommands();

	NET_Init();

	char cwd[MAX_OSPATH];
	_getcwd( cwd, sizeof(cwd) );
	Com_Printf( "Working directory: %s\n", cwd );

	// hide the early console since we've reached the point where we
	// have a working graphics subsystem
	if ( !com_dedicated->integer && !com_viewlog->integer ) {
		Sys_ShowConsole( 0, qfalse );
	}

#ifndef DEDICATED
	if ( !com_dedicated->integer )
		Sys_InitInput();
#endif

	// main game loop
	for (;;) {
#ifndef DEDICATED
		// make sure mouse and joystick are only called once a frame
		IN_Frame();
		S_Frame();
#endif

		// run the game
#ifdef DEDICATED
		Com_Frame( qfalse );
#else
		Com_Frame( clc.demoplaying );
#endif
	}

	// never gets here
}
