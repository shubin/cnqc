#include "../client/client.h"
#include "../qcommon/qcommon.h"
#include "win_local.h"
#include <Windows.h>
#include <ShlObj.h>

#define PRODUCT_NAME			"Quake III Champions"
#define STEAMPATH_NAME			"Quake 3 Arena"
#define STEAMPATH_APPID			"2200"
#define GOGPATH_ID				"1441704920"
#define MSSTORE_PATH			"Quake 3"


// Used to store the Steam Quake 3 installation path
static char steamPath[ MAX_OSPATH ] = { 0 };

// Used to store the GOG Quake 3 installation path
static char gogPath[ MAX_OSPATH ] = { 0 };

// Used to store the Microsoft Store Quake 3 installation path
static char microsoftStorePath[MAX_OSPATH] = { 0 };

/*
================
Sys_SteamPath
================
*/
char *Sys_SteamPath( void )
{
#if defined(STEAMPATH_NAME) || defined(STEAMPATH_APPID)
	HKEY steamRegKey;
	DWORD pathLen = MAX_OSPATH;
	qboolean finishPath = qfalse;

#ifdef STEAMPATH_APPID
	// Assuming Steam is a 32-bit app
	if (!steamPath[0] && !RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App " STEAMPATH_APPID, 0, KEY_QUERY_VALUE | KEY_WOW64_32KEY, &steamRegKey))
	{
		pathLen = MAX_OSPATH;
		if (RegQueryValueEx(steamRegKey, "InstallLocation", NULL, NULL, (LPBYTE)steamPath, &pathLen))
			steamPath[0] = '\0';

		RegCloseKey(steamRegKey);
	}
#endif

#ifdef STEAMPATH_NAME
	if (!steamPath[0] && !RegOpenKeyEx(HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, KEY_QUERY_VALUE, &steamRegKey))
	{
		pathLen = MAX_OSPATH;
		if (RegQueryValueEx(steamRegKey, "SteamPath", NULL, NULL, (LPBYTE)steamPath, &pathLen))
			if (RegQueryValueEx(steamRegKey, "InstallPath", NULL, NULL, (LPBYTE)steamPath, &pathLen))
				steamPath[0] = '\0';

		if (steamPath[0])
			finishPath = qtrue;

		RegCloseKey(steamRegKey);
	}
#endif

	if (steamPath[0])
	{
		if (pathLen == MAX_OSPATH)
			pathLen--;

		steamPath[pathLen] = '\0';

		if (finishPath)
			Q_strcat(steamPath, MAX_OSPATH, "\\SteamApps\\common\\" STEAMPATH_NAME );
	}
#endif

	return steamPath;
}

/*
================
Sys_GogPath
================
*/
char *Sys_GogPath( void )
{
#ifdef GOGPATH_ID
	HKEY gogRegKey;
	DWORD pathLen = MAX_OSPATH;

	if (!gogPath[0] && !RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SOFTWARE\\GOG.com\\Games\\" GOGPATH_ID, 0, KEY_QUERY_VALUE | KEY_WOW64_32KEY, &gogRegKey))
	{
		pathLen = MAX_OSPATH;
		if (RegQueryValueEx(gogRegKey, "PATH", NULL, NULL, (LPBYTE)gogPath, &pathLen))
			gogPath[0] = '\0';

		RegCloseKey(gogRegKey);
	}

	if (gogPath[0])
	{
		if (pathLen == MAX_OSPATH)
			pathLen--;

		gogPath[pathLen] = '\0';
	}
#endif

	return gogPath;
}

/*
================
Sys_MicrosoftStorePath
================
*/
char* Sys_MicrosoftStorePath(void)
{
#ifdef MSSTORE_PATH
	typedef int ( __stdcall *SHGetFolderPath_f )( HWND hwnd, int csidl, HANDLE hToken, DWORD  dwFlags, LPSTR  pszPath );
	if (!microsoftStorePath[0]) 
	{
		TCHAR szPath[MAX_PATH];
		SHGetFolderPath_f qSHGetFolderPath;
		HMODULE shfolder = LoadLibrary("shfolder.dll");

		if(shfolder == NULL)
		{
			Com_Printf("Unable to load SHFolder.dll\n");
			return microsoftStorePath;
		}
		qSHGetFolderPath = (SHGetFolderPath_f)GetProcAddress(shfolder, "SHGetFolderPathA");
		if ( qSHGetFolderPath == NULL )
		{
			Com_Printf("Unable to find SHGetFolderPath in SHFolder.dll\n");
			FreeLibrary(shfolder);
			return microsoftStorePath;
		}

		if( !SUCCEEDED( qSHGetFolderPath( NULL, CSIDL_PROGRAM_FILES,
						NULL, 0, szPath ) ) )
		{
			Com_Printf("Unable to detect CSIDL_PROGRAM_FILES\n");
			FreeLibrary(shfolder);
			return microsoftStorePath;
		}

		FreeLibrary(shfolder);

		// default: C:\Program Files\ModifiableWindowsApps\Quake 3\EN
		Com_sprintf(microsoftStorePath, sizeof(microsoftStorePath), "%s%cModifiableWindowsApps%c%s%cEN", szPath, PATH_SEP, PATH_SEP, MSSTORE_PATH, PATH_SEP);
	}
#endif
	return microsoftStorePath;
}


static const char *Sys_GetConfigurationValue( const char *key, const char *defaultValue ) {
	HKEY hKey;
	DWORD pathLen = MAX_OSPATH;
	qboolean finishPath = qfalse;
	static char buffer[MAX_OSPATH];
	static char *result = buffer;

	if ( !RegOpenKeyEx( HKEY_CURRENT_USER, "SOFTWARE\\" PRODUCT_NAME, 0, KEY_QUERY_VALUE, &hKey ) ) {
		pathLen = MAX_OSPATH;
		if ( RegQueryValueEx( hKey, key, NULL, NULL, ( LPBYTE )buffer, &pathLen ) ) {
			result = NULL;
		}
		RegCloseKey( hKey );
	}
	return result ? result : defaultValue;
}

static qboolean Sys_SetConfigurationValue( const char *key, const char *value ) {
	HKEY hKey;
	DWORD count = MAX_OSPATH, dontCare;
	qboolean finishPath = qfalse;
	static char buffer[MAX_OSPATH];
	qboolean result = qtrue;

	if ( RegOpenKeyEx( HKEY_CURRENT_USER, "SOFTWARE\\" PRODUCT_NAME, 0, KEY_SET_VALUE, &hKey ) != ERROR_SUCCESS ) {
		if ( RegCreateKeyEx( HKEY_CURRENT_USER, "SOFTWARE\\" PRODUCT_NAME, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, &dontCare ) != ERROR_SUCCESS ) {
			return qfalse;
		}
	}
	count = value ? strlen( value ) : 0;
	if ( RegSetValueExA( hKey, key, 0, REG_SZ, ( const BYTE * )value, count ) != ERROR_SUCCESS ) {
		result = qfalse;
	}
	RegCloseKey( hKey );
	return result;
}

static const char *s_locateDirInitialFolder = NULL;

static int FAR PASCAL BrowseNotify( HWND hWnd, UINT iMessage, long wParam, LPARAM lParam )
{
	if ( iMessage == BFFM_INITIALIZED ) {
		if ( s_locateDirInitialFolder != NULL ) {
			SendMessage( hWnd, BFFM_SETSELECTION, 1, ( LPARAM )s_locateDirInitialFolder );
		}
		return 1;
	}
    return 0;
}

const char *Sys_LocateDir( const char *title, const char *initialDir ) {
	static char buffer[ MAX_PATH ] = { 0 };
	const char *result = NULL;
	LPITEMIDLIST pIDL;
	BROWSEINFO bi;

	if ( OleInitialize( NULL ) != S_OK ) {
		return NULL;
	}

	s_locateDirInitialFolder = initialDir;
	memset( &bi, 0, sizeof( bi ) );
	bi.ulFlags = BIF_USENEWUI;
	bi.lpszTitle = title;
	bi.lpfn = BrowseNotify;

	pIDL = SHBrowseForFolder( &bi );
	if ( pIDL != NULL ) {
		if ( SHGetPathFromIDList( pIDL, buffer ) != 0 ) {
			result = buffer;
		}
		CoTaskMemFree( pIDL );
	}
	OleUninitialize();
	return result;
}

qboolean Sys_LocateHomePath( void ) {
	int result;
	
	result = MessageBoxA( g_wv.hWnd, 
	"Quake III Arena installation path is not found.\n"
	"You must have Quake III Arena installed to play Quake III Champions.\n"
	"Please Install Quake III Arena from Steam, Galaxy or Microsoft Store.\n"
	"Alternatively, you can provide Quake III Arena installation directory.\n"
	"Would you like to choose Quake III Arena installation directory?",
	"Information", MB_YESNO | MB_ICONEXCLAMATION );
	if ( result == IDYES ) {
		const char *quake3path = Sys_GetConfigurationValue( "QuakeIIIArenaPath", NULL );
		const char *selectedPath = Sys_LocateDir( "Please choose your Quake III Arena installation path", quake3path );
		Sys_SetConfigurationValue( "QuakeIIIArenaPath", selectedPath );
		MessageBoxA( g_wv.hWnd, "Please restart the game", PRODUCT_NAME, MB_ICONINFORMATION | MB_OK );
		return qtrue;
	}
	return qfalse;
}

void Sys_FindHomePath( void ) {
	const char* quake3path;

	quake3path = Sys_GetConfigurationValue( "QuakeIIIArenaPath", "" );
	if ( quake3path[0] ) {
		Cvar_Set( "fs_homepath", quake3path );
		Com_Printf( "Quake III Arena is found at %s\n", quake3path );
		return;
	}
	quake3path = Sys_SteamPath();
	if ( quake3path[0] ) {
		Cvar_Set( "fs_homepath", quake3path );
		Com_Printf( "Steam version of Quake III Arena is found at %s\n", quake3path );
		return;
	}
	quake3path = Sys_GogPath();
	if ( quake3path[0] ) {
		Cvar_Set( "fs_homepath", quake3path );
		Com_Printf( "GOG version of Quake III Arena is found at %s\n", quake3path );
		return;
	}
	quake3path = Sys_MicrosoftStorePath();
	if ( quake3path[0] ) {
		Cvar_Set( "fs_homepath", quake3path );
		Com_Printf( "Microsoft Store version of Quake III Arena is found at %s\n", quake3path );
		return;
	}
}
