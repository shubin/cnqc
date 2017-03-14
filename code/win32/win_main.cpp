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


WinVars_t	g_wv;


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

void QDECL Sys_Error( const char *error, ... )
{
	va_list		argptr;
	char		text[4096];

	va_start (argptr, error);
	vsprintf (text, error, argptr);
	va_end (argptr);

	Conbuf_AppendText( text );
	Conbuf_AppendText( "\n" );

	Sys_SetErrorText( text );
	Sys_ShowConsole( 1, qtrue );

	WIN_EndTimePeriod();

#ifndef DEDICATED
	IN_Shutdown();
#endif

	// wait for the user to quit
	while (1) {
		MSG msg;
		if (!GetMessage(&msg, NULL, 0, 0))
			Com_Quit_f();
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	Sys_DestroyConsole();

	exit(1);
}


void Sys_Quit()
{
	WIN_EndTimePeriod();
#ifndef DEDICATED
	IN_Shutdown();
#endif
	Sys_DestroyConsole();
	exit(0);
}


void Sys_Print( const char *msg )
{
	Conbuf_AppendText( msg );
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
				strtok( data, "\n\r\b" );
			}
		}

		CloseClipboard();
	}

	return data;
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

void Sys_QueEvent( int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr )
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
			Com_Quit_f();
		}

		// save the msg time, because wndprocs don't have access to the timestamp
		g_wv.sysMsgTime = msg.time;

		TranslateMessage( &msg );
		DispatchMessage( &msg );
	}

	// check for console commands
	const char* s = Sys_ConsoleInput();
	if ( s ) {
		int len = strlen( s ) + 1;
		char* b = (char*)Z_Malloc( len );
		Q_strncpyz( b, s, len-1 );
		Sys_QueEvent( 0, SE_CONSOLE, 0, 0, len, b );
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
		Sys_QueEvent( 0, SE_PACKET, 0, 0, len, buf );
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


#ifndef DEDICATED
static void Sys_In_Restart_f( void )
{
	IN_Shutdown();
	IN_Init();
}
#endif


static void Sys_Net_Restart_f( void )
{
	NET_Restart();
}


// called after the common systems (cvars, files, etc) are initialized
void Sys_Init()
{
	// make sure the timer is high precision, otherwise NT gets 18ms resolution
	WIN_BeginTimePeriod();

#ifndef DEDICATED
	Cmd_AddCommand( "in_restart", Sys_In_Restart_f );
#endif
	Cmd_AddCommand( "net_restart", Sys_Net_Restart_f );

	if ( !IsWindowsVistaOrGreater() )
		Sys_Error( "%s requires Windows Vista or later", Q3_VERSION );

	Cvar_Set( "arch", "winnt" );

	// save out a couple things in rom cvars for the renderer to access
	Cvar_Get( "win_hinstance", va("%i", (int)g_wv.hInstance), CVAR_ROM );

	//Cvar_Set( "username", Sys_GetCurrentUser() );
}


///////////////////////////////////////////////////////////////


static BOOL CALLBACK WIN_MonitorEnumCallback( HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData )
{
	if ( lprcMonitor )
	{
		g_wv.monitorRects[g_wv.monitorCount] = *lprcMonitor;
		g_wv.hMonitors[g_wv.monitorCount] = hMonitor;
		g_wv.monitorCount++;
	}

	if ( g_wv.monitorCount >= MAX_MONITOR_COUNT )
	{
		return FALSE;
	}
	
	return TRUE;
}


static void WIN_GetMonitorList()
{
	EnumDisplayMonitors( NULL, NULL, &WIN_MonitorEnumCallback, 0 );

	const POINT zero = { 0, 0 };
	const HMONITOR hMonitor = MonitorFromPoint( zero, MONITOR_DEFAULTTOPRIMARY );
	for ( int i = 0; i < g_wv.monitorCount; i++ )
	{
		if ( hMonitor ==  g_wv.hMonitors[i] )
		{
			g_wv.primaryMonitor = i;
			g_wv.monitor = i;
			break;
		}
	}
}


void WIN_GetStartUpMonitorIndex()
{
	static qbool called = qfalse;

	if ( called )
		return;

	called = qtrue;

	// r_monitor is the 1-based monitor index the user asks for
	const int monitor = Cvar_Get( "r_monitor", "0", CVAR_ARCHIVE )->integer;
	if ( monitor <= 0 || monitor > g_wv.monitorCount )
	{
		g_wv.monitor = g_wv.primaryMonitor;
		return;
	}

	g_wv.monitor = monitor - 1;
}


void WIN_GetMonitorIndexFromMainWindow()
{
	const HMONITOR hMonitor = MonitorFromWindow( g_wv.hWnd, MONITOR_DEFAULTTONEAREST );
	for ( int i = 0; i < g_wv.monitorCount; i++ )
	{
		if ( hMonitor == g_wv.hMonitors[i] )
		{
			g_wv.monitor = i;
			break;
		}
	}
}


///////////////////////////////////////////////////////////////


int WINAPI WinMainImpl( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow )
{
	// should never get a previous instance in Win32
	if ( hPrevInstance )
		return 0;

	g_wv.hInstance = hInstance;

	WIN_GetMonitorList();

	// done before Com/Sys_Init since we need this for error output
	Sys_CreateConsole();

	// get the initial time base
	Sys_Milliseconds();

	char sys_cmdline[MAX_STRING_CHARS];
	Q_strncpyz( sys_cmdline, lpCmdLine, sizeof( sys_cmdline ) );
	Com_Init( sys_cmdline );

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
	if (!com_dedicated->integer)
		IN_Init();
#endif

	int totalMsec = 0, countMsec = 0;

	// main game loop
	while (qtrue) {
		// if running as a client but not focused, sleep a bit
		// (servers have their own sleep path)
		if ( !g_wv.activeApp && com_dedicated && !com_dedicated->integer )
			Sleep( 5 );

		int startTime = Sys_Milliseconds();

#ifndef DEDICATED
		// make sure mouse and joystick are only called once a frame
		IN_Frame();
#endif

		// run the game
		Com_Frame();

		totalMsec += Sys_Milliseconds() - startTime;
		countMsec++;
	}

	// never gets here
}


int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow )
{
	// Register the exception handler for all threads present and future in this process.
	// 1 means we're inserting the handler at the front of the queue.
	// The debugger does still get first-chance access though.
	// The handler is always called in the context of the thread raising the exception.
	AddVectoredExceptionHandler( 1, WIN_HandleException );

	// Make sure we reset system settings even when someone calls exit.
	atexit( WIN_HandleExit );

	// SetErrorMode(0) gets the current flags
	// SEM_FAILCRITICALERRORS -> no abort/retry/fail errors
	// SEM_NOGPFAULTERRORBOX  -> the Windows Error Reporting dialog will not be shown
	SetErrorMode( SetErrorMode(0) | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX );

	return WinMainImpl( hInstance, hPrevInstance, lpCmdLine, nCmdShow );
}
