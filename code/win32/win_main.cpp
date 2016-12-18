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


#define MEM_THRESHOLD 96*1024*1024

qbool Sys_LowPhysicalMemory()
{
	MEMORYSTATUS stat;
	GlobalMemoryStatus( &stat );
	return (stat.dwTotalPhys <= MEM_THRESHOLD);
}


void Sys_BeginProfiling( void ) {
	// this is just used on the mac build
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

	timeEndPeriod( 1 );

	IN_Shutdown();

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
	timeEndPeriod( 1 );
	IN_Shutdown();
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
	int findhandle = _findfirst (search, &findinfo);
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
	int			findhandle;
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

	findhandle = _findfirst (search, &findinfo);
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
	if ( !list ) {
		return;
	}

	for (int i = 0 ; list[i] ; i++ ) {
		Z_Free( list[i] );
	}

	Z_Free( list );
}


char *Sys_GetClipboardData( void )
{
	char *data = NULL;

	if ( OpenClipboard( NULL ) ) {
		HANDLE hClipboardData;

		if ( ( hClipboardData = GetClipboardData( CF_TEXT ) ) != 0 ) {
		    char *cliptext;
			if ( (cliptext = (char*)GlobalLock( hClipboardData ) ) != 0 ) {
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


void Sys_UnloadDll( void* dllHandle )
{
	if ( !dllHandle ) {
		return;
	}
	if ( !FreeLibrary( (HMODULE)dllHandle ) ) {
		Com_Error( ERR_FATAL, "Sys_UnloadDll FreeLibrary failed" );
	}
}


// used to load a development dll instead of a virtual machine

void* QDECL Sys_LoadDll( const char* name,
		intptr_t (QDECL **entryPoint)(intptr_t, ...), intptr_t (QDECL *systemcalls)(intptr_t, ...) )
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

	void (QDECL *dllEntry)( intptr_t (QDECL *syscallptr)(intptr_t, ...) );
	dllEntry = (void (QDECL *)(intptr_t (QDECL *)( intptr_t, ... ) ) )GetProcAddress( libHandle, "dllEntry" );
	*entryPoint = (intptr_t (QDECL *)(intptr_t,...))GetProcAddress( libHandle, "vmMain" );
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

#define	MAX_QUED_EVENTS		1024
#define	MASK_QUED_EVENTS	( MAX_QUED_EVENTS - 1 )

static sysEvent_t	eventQue[MAX_QUED_EVENTS];
static int			eventHead, eventTail;


// a time of 0 will get the current time
// ptr should either be null, or point to a block of data that can be freed by the game later

void Sys_QueEvent( int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr )
{
	sysEvent_t* ev; 
	
    // try to combine all sequential mouse moves in one event
    if ( type == SE_MOUSE ) {
        // get previous event from queue
        ev = &eventQue[ ( eventHead + MAX_QUED_EVENTS - 1 ) & MASK_QUED_EVENTS ];
        if ( ev->evType == SE_MOUSE ) {

            if ( eventTail == eventHead && eventTail )
            {
                ev->evValue = 0;
                ev->evValue2 = 0;
                eventTail--;
            }
            if ( time == 0 ) {
                time = Sys_Milliseconds();
            }
            ev->evValue += value;
            ev->evValue2 += value2;
            ev->evTime = time;

            return;
        }
    }
    
    ev = &eventQue[ eventHead & MASK_QUED_EVENTS ];
    
	if ( eventHead - eventTail >= MAX_QUED_EVENTS ) {
		Com_Printf("Sys_QueEvent: overflow\n");
		// we are discarding an event, but don't leak memory
		if ( ev->evPtr )
			Z_Free( ev->evPtr );
		++eventTail;
	}

	++eventHead;

	if ( time == 0 )
		time = Sys_Milliseconds();

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
		++eventTail;
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
		// copy out to a separate buffer for queuing
		// the readcount stepahead is for SOCKS support
		int len = sizeof( netadr_t ) + netmsg.cursize - netmsg.readcount;
		netadr_t* buf = (netadr_t*)Z_Malloc( len );
		*buf = adr;
		memcpy( buf+1, &netmsg.data[netmsg.readcount], netmsg.cursize - netmsg.readcount );
		Sys_QueEvent( 0, SE_PACKET, 0, 0, len, buf );
	}

	// return if we have data
	if ( eventHead > eventTail ) {
		++eventTail;
		return eventQue[ ( eventTail - 1 ) & MASK_QUED_EVENTS ];
	}

	// create an empty event to return
	sysEvent_t ev;
	memset( &ev, 0, sizeof( ev ) );
	ev.evTime = Sys_Milliseconds();
	return ev;
}


///////////////////////////////////////////////////////////////


static void Sys_In_Restart_f( void )
{
	IN_Shutdown();
	IN_Init();
}


static void Sys_Net_Restart_f( void )
{
	NET_Restart();
}


// called after the common systems (cvars, files, etc) are initialized

#define OSR2_BUILD_NUMBER 1111
#define WIN98_BUILD_NUMBER 1998

void Sys_Init()
{
	// make sure the timer is high precision, otherwise NT gets 18ms resolution
	timeBeginPeriod( 1 );

	Cmd_AddCommand( "in_restart", Sys_In_Restart_f );
	Cmd_AddCommand( "net_restart", Sys_Net_Restart_f );

	g_wv.osversion.dwOSVersionInfoSize = sizeof( g_wv.osversion );

	if (!GetVersionEx (&g_wv.osversion))
		Sys_Error ("Couldn't get OS info");

	if (g_wv.osversion.dwMajorVersion < 4)
		Sys_Error ("Quake3 requires Windows version 4 or greater");
	if (g_wv.osversion.dwPlatformId == VER_PLATFORM_WIN32s)
		Sys_Error ("Quake3 doesn't run on Win32s");

	if ( g_wv.osversion.dwPlatformId == VER_PLATFORM_WIN32_NT )
	{
		Cvar_Set( "arch", "winnt" );
	}
	else if ( g_wv.osversion.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS )
	{
		if ( LOWORD( g_wv.osversion.dwBuildNumber ) >= WIN98_BUILD_NUMBER )
		{
			Cvar_Set( "arch", "win98" );
		}
		else if ( LOWORD( g_wv.osversion.dwBuildNumber ) >= OSR2_BUILD_NUMBER )
		{
			Cvar_Set( "arch", "win95 osr2.x" );
		}
		else
		{
			Cvar_Set( "arch", "win95" );
		}
	}
	else
	{
		Cvar_Set( "arch", "unknown Windows variant" );
	}

	// save out a couple things in rom cvars for the renderer to access
	Cvar_Get( "win_hinstance", va("%i", (int)g_wv.hInstance), CVAR_ROM );

	//
	// figure out our CPU
	//
	Cvar_Get( "sys_cpustring", "detect", 0 );
	if ( !Q_stricmp( Cvar_VariableString( "sys_cpustring"), "detect" ) )
	{
		int cpuid = Sys_GetProcessorId();
		switch ( cpuid )
		{
		case CPUID_GENERIC:
			break;
		case CPUID_AXP:
			Cvar_Set( "sys_cpustring", "Alpha AXP" );
			break;
		case CPUID_UNSUPPORTED:
			Com_Error( ERR_FATAL, "Unsupported cpu type %s\n", Cvar_VariableString( "sys_cpustring" ) );
			break;
		default:
			Com_Error( ERR_FATAL, "Unknown cpu type %d\n", cpuid );
			break;
		}
	}
	Com_DPrintf( "CPU: %s\n", Cvar_VariableString( "sys_cpustring" ) );

	//Cvar_Set( "username", Sys_GetCurrentUser() );
}

static int CheckPrivs()
{  
    HANDLE hToken;

    // Get a token for this process.
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) 
        return 0;
    
    OSVERSIONINFO vinfo;
    vinfo.dwOSVersionInfoSize = sizeof(vinfo);
    GetVersionEx( &vinfo );

    PRIVILEGE_SET priv;
    
    // Get the LUID for the shutdown privilege.
    if ( vinfo.dwMajorVersion == 5 && vinfo.dwPlatformId == VER_PLATFORM_WIN32_NT ) {
        LookupPrivilegeValue(NULL, SE_INC_BASE_PRIORITY_NAME, &priv.Privilege[0].Luid);    // W2K or XP     
    } 
    else if ( vinfo.dwMajorVersion == 6 && vinfo.dwPlatformId == VER_PLATFORM_WIN32_NT ){
        LookupPrivilegeValue(NULL, SE_INC_WORKING_SET_NAME, &priv.Privilege[0].Luid);      // Vista
    }
    else {
        //TODO: what SE_* for Win7 ?
        return 0;
    }
    
    priv.PrivilegeCount = 1;  // one privilege to set    
    priv.Privilege[0].Attributes = 0; 
    
    BOOL res;
    if (!PrivilegeCheck(hToken, &priv, &res)){
        return 0;
    }

    return priv.Privilege[0].Attributes == SE_PRIVILEGE_USED_FOR_ACCESS;
}

///////////////////////////////////////////////////////////////


int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow )
{
	// should never get a previous instance in Win32
	if ( hPrevInstance )
		return 0;

	g_wv.hInstance = hInstance;

#ifndef USE_R_SMP
    SYSTEM_INFO sysInfo;
    GetSystemInfo( &sysInfo );
    SetProcessAffinityMask( GetCurrentProcess(), sysInfo.dwActiveProcessorMask );
#endif    
    
	// done before Com/Sys_Init since we need this for error output
	Sys_CreateConsole();

	// no abort/retry/fail errors
	SetErrorMode( SEM_FAILCRITICALERRORS );

	// get the initial time base
	Sys_Milliseconds();

	char sys_cmdline[MAX_STRING_CHARS];
	Q_strncpyz( sys_cmdline, lpCmdLine, sizeof( sys_cmdline ) );
	Com_Init( sys_cmdline );

	NET_Init();

	// hide the early console since we've reached the point where we
	// have a working graphics subsystem
	if ( !com_dedicated->integer && !com_viewlog->integer ) {
		Sys_ShowConsole( 0, qfalse );
	}

	if (!com_dedicated->integer)
		IN_Init();

    // in some cases (XP SP2, Vista UAC), this can supposedly help trim the working set / VM usage of Q3
    // but it requires admin rights and makes no difference here, so I'm disabling it by default
    if (CheckPrivs())
    {
        typedef BOOL (WINAPI *SetPWSS)( HANDLE, SIZE_T, SIZE_T );
        SetPWSS spwss = (SetPWSS)GetProcAddress( GetModuleHandle("kernel32"), "SetProcessWorkingSetSize" );
        if (spwss) {
            spwss( GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1 );
        }
    }

	int totalMsec = 0, countMsec = 0;

	// main game loop
	while (qtrue) {
		// if running as a client but not focused, sleep a bit
		// (servers have their own sleep path)
		if ( com_dedicated && !com_dedicated->integer )
		{
			if( g_wv.isMinimized || !g_wv.activeApp ) {
				Sleep( 200 );  // 5 fps, free CPU
			} 
            //SwitchToThread();
		}

		int startTime = Sys_Milliseconds();

		// make sure mouse and joystick are only called once a frame
		IN_Frame();

		// run the game
		Com_Frame();

		totalMsec += Sys_Milliseconds() - startTime;
		++countMsec;
	}

	// never gets here
}
