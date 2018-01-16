#include "client.h"
#include <stdio.h>
#include <stdarg.h>
#if defined(_WIN32)
#include <io.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#if defined(_WIN32)
#include <winsock2.h>
#include <Windows.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#endif

#if defined(_WIN32)
typedef ADDRINFOA		addrInfo_t;
#define Q_closesocket	closesocket
#define Q_unlink		_unlink
#else
typedef addrinfo		addrInfo_t;
typedef int				SOCKET;
#define INVALID_SOCKET	(-1)
#define SOCKET_ERROR	(-1)
#define MAX_PATH		(256)
#define Q_closesocket	close
#define Q_unlink		unlink
#endif


#define MAX_TIMEOUT_MS	2000


struct mapDownload_t {
	char recBuffer[1 << 20];		// for both download data and checksumming
	char tempMessage[MAXPRINTMSG];	// for PrintError
	char tempMessage2[MAXPRINTMSG];	// for PrintSocketError
	char errorMessage[MAXPRINTMSG];
	char mapName[MAX_PATH];		// only used if the server doesn't give us a .pk3 name
	char tempPath[MAX_PATH];	// full path of the temp file being written to
	char finalName[MAX_PATH];	// file name with extension suggested by the server
	char finalPath[MAX_PATH];	// full path of the new .pk3 file
	char httpHeaderValue[128];	// only set when BadResponse is qtrue
	SOCKET socket;
	FILE* file;
	int startTimeMS;
	unsigned int bytesHeader;		// header only
	unsigned int bytesContent;		// file content only
	unsigned int bytesTotal;		// message header + file content
	unsigned int bytesDownloaded;	// total downloaded, including the header
	unsigned int crc32;
	qbool fromCommand;		// qtrue if started by a console command
	qbool headerParsed;		// qtrue if we're done parsing the header
	qbool badResponse;		// qtrue if we need to read more packets for the custom error message
	int timeOutStartTimeMS;	// when the recv timeout started
	qbool lastErrorTimeOut;	// qtrue if the last recv error was a timeout
	int sourceIndex;		// index into the cl_mapDLSources array
	qbool exactMatch;		// qtrue if an exact match is required
	qbool cleared;			// qtrue if Download_Clear was called at least once
	qbool realMapName;		// qtrue if the name of a .bsp - otherwise, might be invalid or auto-generated
};


enum mapDownloadStatus_t {
	MDLS_NOTHING,
	MDLS_ERROR,		// just finished unsuccessfully
	MDLS_SUCCESS,	// just finished successfully
	MDLS_IN_PROGRESS,
	MDLS_COUNT
};


typedef void (*mapdlQueryFormatter_t)( char* query, int querySize, const char* mapName );


// map download source for queries by map name only
struct mapDownloadSource_t {
	const char* name;
	const char* hostName; // don't put in the scheme (e.g. "http://")
	int port;
	mapdlQueryFormatter_t formatQuery;
};


static mapDownload_t cl_mapDL;


static void FormatQueryCNQ3( char* query, int querySize, const char* mapName );
static void FormatQueryWS( char* query, int querySize, const char* mapName );


static const mapDownloadSource_t cl_mapDLSources[2] = {
	{ "CNQ3",		"maps.playmorepromode.org",	8000,	&FormatQueryCNQ3 },
	{ "WorldSpawn",	"ws.q3df.org",				80,		&FormatQueryWS }
};


static void FormatQueryCNQ3( char* query, int querySize, const char* mapName )
{
	Com_sprintf(query, querySize, "map?n=%s", mapName);
}


static void FormatQueryWS( char* query, int querySize, const char* mapName )
{
	Com_sprintf(query, querySize, "getpk3bymapname.php/%s", mapName);
}


static void PrintError( mapDownload_t* dl, const char* format, ... )
{
	va_list ap;
	va_start(ap, format);
	Q_vsnprintf(dl->tempMessage, sizeof(dl->tempMessage), format, ap);
	va_end(ap);

	if (dl->realMapName && dl->mapName[0] != '\0')
		Com_sprintf(dl->errorMessage, sizeof(dl->errorMessage), "^bMap DL failed: ^7map '%s' - %s", dl->mapName, dl->tempMessage);
	else
		Com_sprintf(dl->errorMessage, sizeof(dl->errorMessage), "^bMap DL failed: ^7%s", dl->tempMessage);

	// only append a line return if there wasn't one already
	const int l = strlen(dl->errorMessage);
	if (l > 0 && dl->errorMessage[l - 1] != '\n')
		Q_strcat(dl->errorMessage, sizeof(dl->errorMessage), "\n");

	Com_Printf(dl->errorMessage);
}


#if defined(_WIN32)
static void PrintSocketError( mapDownload_t* dl, const char* functionName, int ec )
{
	const int bufferSize = sizeof(dl->tempMessage2);
	const int bw = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
								  NULL, (DWORD)ec, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
								  dl->tempMessage2, (DWORD)bufferSize, NULL);
	if (bw <= 0) {
		*dl->tempMessage2 = '\0';
		PrintError(dl, "%s failed: %d", functionName, ec);
		return;
	}

	int lastByte = bw;
	if (lastByte >= bufferSize)
		lastByte = bufferSize - 1;
	dl->tempMessage2[lastByte] = '\0';

	PrintError(dl, "%s failed: %d -> %s", functionName, ec, dl->tempMessage2);
}
#else
static void PrintSocketError( mapDownload_t* dl, const char* functionName, int ec )
{
#if (_POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600) && !_GNU_SOURCE
	// XSI strerror_r
	const int serec = strerror_r(ec, dl->tempMessage2, sizeof(dl->tempMessage2));
	const char* const errorMsg = dl->tempMessage2;
	if (serec != 0) {
#else
	// GNU strerror_r
	const char* const errorMsg = strerror_r(ec, dl->tempMessage2, sizeof(dl->tempMessage2));
	if (errorMsg == NULL) {
#endif
		PrintError(dl, "%s failed with error %d", functionName, ec);
		return;
	}
	
	PrintError(dl, "%s failed with error %d (%s)", functionName, ec, errorMsg);
}
#endif


static void PrintSocketError( mapDownload_t* dl, const char* functionName )
{
#if defined(_WIN32)
	PrintSocketError(dl, functionName, WSAGetLastError());
#else
	PrintSocketError(dl, functionName, errno);
#endif
}


static qbool IsSocketTimeoutError()
{
#if defined(_WIN32)
	const int ec = WSAGetLastError();
	return ec == WSAEWOULDBLOCK || ec == WSAETIMEDOUT;
#else
	const int ec = errno;
	return ec == EAGAIN || ec == EWOULDBLOCK;
#endif
}


static void Download_Clear( mapDownload_t* dl )
{
	// NOTE: we must not reset mapName, realMapName
	*dl->tempPath = '\0';
	*dl->finalName = '\0';
	*dl->errorMessage = '\0';
	dl->socket = INVALID_SOCKET;
	dl->file = NULL;
	dl->startTimeMS = INT_MIN;
	dl->crc32 = 0;
	dl->bytesHeader = 0;
	dl->bytesTotal = 0;
	dl->bytesDownloaded = 0;
	dl->headerParsed = qfalse;
	dl->badResponse = qfalse;
	dl->timeOutStartTimeMS = INT_MIN;
	dl->lastErrorTimeOut = qfalse;
	dl->sourceIndex = 0;
	dl->exactMatch = qfalse;
	dl->cleared = qtrue;
}


static qbool Download_IsFileValid( mapDownload_t* dl )
{
	const unsigned int fileSize = dl->bytesTotal - dl->bytesHeader;
	if (fileSize == 0)
		return qfalse;

	FILE* const file = fopen(dl->tempPath, "rb");
	if (file == NULL)
		return qfalse;

	const unsigned int maxBlockSize = (unsigned int)sizeof(dl->recBuffer);
	const unsigned int fullBlockCount = fileSize / maxBlockSize;
	const unsigned int lastBlockSize = fileSize - fullBlockCount * maxBlockSize;

	unsigned int crc32;
	CRC32_Begin(&crc32);
	for (unsigned int i = 0; i < fullBlockCount; ++i) {
		if (fread(dl->recBuffer, maxBlockSize, 1, file) != 1) {
			fclose(file);
			return qfalse;
		}
		CRC32_ProcessBlock(&crc32, dl->recBuffer, maxBlockSize);
	}
	if (lastBlockSize > 0) {
		if (fread(dl->recBuffer, lastBlockSize, 1, file) != 1) {
			fclose(file);
			return qfalse;
		}
		CRC32_ProcessBlock(&crc32, dl->recBuffer, lastBlockSize);
	}
	CRC32_End(&crc32);

	fclose(file);

	if (crc32 != dl->crc32) {
		PrintError(dl, "The CRC32 for %s was %08X instead of %08X", dl->finalName, crc32, dl->crc32);
		return qfalse;
	}

	return qtrue;
}


// fails if the dest path already exists
static qbool RenameFile( const char* source, const char* dest )
{
	// note: the rename function behaves differently on Windows and Linux
	// Windows: dest path cannot exist
	// Linux: if dest path exists, old file gets erased
#if defined(_WIN32)
	return MoveFileA(source, dest) != 0;
#else
	struct stat destInfo;  
	if (stat(dest, &destInfo) == 0)
		return qfalse;

	return rename(source, dest) == 0;
#endif
}


static qbool Download_Rename( mapDownload_t* dl )
{
	char dir[MAX_PATH];
#if defined(_WIN32)
	Q_strncpyz(dir, "baseq3\\", sizeof(dir));
#else
	Q_strncpyz(dir, "baseq3/", sizeof(dir));
#endif

	char name[MAX_PATH];
	if (*dl->finalName == '\0') {
		Q_strncpyz(name, dl->mapName, sizeof(name));
	} else {
		Q_strncpyz(name, dl->finalName, sizeof(name));
		const int l = strlen(dl->finalName);
		if (Q_stricmp(name + l - 4, ".pk3") == 0)
			name[l - 4] = '\0';
	}

	// try with the desired name
	const char* const fs_basepath = Cvar_VariableString("fs_basepath");
	const char* finalPath = FS_BuildOSPath(fs_basepath, "baseq3", va("%s.pk3", name));
	Q_strncpyz(dl->finalPath, finalPath, sizeof(dl->finalPath));
	if (RenameFile(dl->tempPath, dl->finalPath))
		return qtrue;

	// try a few more times with random name suffixes
	for (int i = 0; i < 4; ++i) {
		// the suffix is 24 bits long, i.e. 6 characters
		const unsigned int suffix0 = (unsigned int)rand() % (1 << 12);
		const unsigned int suffix1 = (unsigned int)rand() % (1 << 12);
		const unsigned int suffix = suffix0 | (suffix1 << 12);
		finalPath = FS_BuildOSPath(fs_basepath, "baseq3", va("%s_%06x.pk3", name, suffix));
		Q_strncpyz(dl->finalPath, finalPath, sizeof(dl->finalPath));
		if (RenameFile(dl->tempPath, dl->finalPath))
			return qtrue;
	}

	PrintError(dl, "Failed to rename '%s' to '%s' or a similar name", dl->tempPath, name);

	return qfalse;
}


static qbool Download_CleanUp( mapDownload_t* dl, qbool rename )
{
	if (!cl_mapDL.cleared)
		return qfalse;

	if (dl->socket != INVALID_SOCKET) {
		Q_closesocket(dl->socket);
		dl->socket = INVALID_SOCKET;
	}

	if (dl->file != NULL) {
		fclose(dl->file);
		dl->file = NULL;
	}

	qbool success = qtrue;
	if (rename) {
		if (dl->crc32 != 0)
			success = Download_IsFileValid(dl);

		if (success)
			success = Download_Rename(dl);
	}

	if (Q_unlink(dl->tempPath) != 0 && errno != ENOENT) {
		// ENOENT means the file wasn't found, which is good because
		// it means our previous code was successful
		PrintError(dl, "Failed to delete file '%s'", dl->tempPath);
	}

	return success;
}


static qbool Download_Begin( mapDownload_t* dl, int port, const char* server, const char* file )
{
	Download_CleanUp(dl, qfalse);
	Download_Clear(dl);

	char portString[16];
	Com_sprintf(portString, sizeof(portString), "%d", port);

	addrInfo_t* address;
	const int ec = getaddrinfo(server, portString, NULL, &address);
	if (ec != 0) {
		// EAI* errors map to WSA* errors, so it's ok to call this
		PrintSocketError(dl, "getaddrinfo", ec);
		return qfalse;
	}

	qbool connected = qfalse;
	for (addrInfo_t* a = address; a != NULL; a = a->ai_next) {
		dl->socket = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
		if (dl->socket == INVALID_SOCKET) {
			PrintSocketError(dl, "socket");
			continue;
		}

		if (connect(dl->socket, address->ai_addr, address->ai_addrlen) == SOCKET_ERROR) {
			PrintSocketError(dl, "connect");
			if (dl->socket != INVALID_SOCKET) {
				Q_closesocket(dl->socket);
				dl->socket = INVALID_SOCKET;
			}
			continue;
		}

		connected = qtrue;
		break;
	}

	freeaddrinfo(address);

	if (!connected) {
		PrintError(dl, "Failed to connect to the host");
		return qfalse;
	}

#if defined(_WIN32)
	const DWORD timeoutMs = 1;
	if (setsockopt(dl->socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs)) == SOCKET_ERROR) {
		PrintSocketError(dl, "setsockopt");
		return qfalse;
	}
#else
	timeval timeout;      
	timeout.tv_sec = 0;
	timeout.tv_usec = 1000;
	if (setsockopt(dl->socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) == SOCKET_ERROR) {
		PrintSocketError(dl, "setsockopt");
		return qfalse;
	}
#endif

	char request[256];
	Com_sprintf(request, sizeof(request), "GET /%s HTTP/1.0\r\nHost: %s:%d\r\n\r\n", file, server, port);
	const int requestLength = strlen(request);
	if (send(dl->socket, request, requestLength, 0) != requestLength) {
		PrintSocketError(dl, "send");
		return qfalse;
	}

	const char* const tempPathBase = FS_BuildOSPath(Cvar_VariableString("fs_basepath"), "baseq3", "");
#if defined(_WIN32)
	if (GetTempFileNameA(tempPathBase, "", 0, dl->tempPath) == 0) {
		PrintError(dl, "Couldn't create a file name");
		return qfalse;
	}
	dl->file = fopen(dl->tempPath, "wb");
#else
	Com_sprintf(dl->tempPath, sizeof(dl->tempPath), "%s/XXXXXX.tmp", tempPathBase);
	const int fd = mkstemps(dl->tempPath, 4);
	dl->file = fd != -1 ? fdopen(fd, "wb") : NULL;
#endif
	if (dl->file == NULL) {
		const int error = errno;
		char* const errorString = strerror(error);
		PrintError(dl, "Failed to open file '%s': %s (%d)", dl->tempPath, errorString ? errorString : "?", error);
		return qfalse;
	}

	dl->startTimeMS = Sys_Milliseconds();

	return qtrue;
}


static qbool IsWhiteSpace( char c )
{
	return c == '\r' || c == '\n' || c == '\t' || c == ' ';
}


static void RemoveTrailingWhiteSpace( char* s )
{
	const int l = strlen(s);
	int i = l;
	while (i--) {
		if (!IsWhiteSpace(s[i])) {
			if (i + 1 < l)
				s[i + 1] = '\0';
			break;
		}
	}
}


static qbool ParseHeader( unsigned int* headerLength, mapDownload_t* dl )
{
	if (dl->badResponse) {
		RemoveTrailingWhiteSpace(dl->recBuffer);
		if (*dl->recBuffer != '\0')
			PrintError(dl, "HTTP status %s - %s", dl->httpHeaderValue, dl->recBuffer);
		else
			PrintError(dl, "HTTP status %s", dl->httpHeaderValue);
		return qfalse;
	}

// note: sscanf %s with the width specifier will null terminate in overflow cases too
#define		MAXSTRLEN	512
#define		WSPEC		XSTRING(MAXSTRLEN)

	static char httpHeaderValue[MAXSTRLEN];
	static char header[MAXSTRLEN];
	static char value[MAXSTRLEN];
	static char fileName[MAXSTRLEN];

	qbool badResponse = qfalse;
	const char* s = dl->recBuffer;
	for (;;) {
		qbool httpHeader = qfalse;
		int bytesRead = 0;
		if (sscanf(s, "HTTP/1.1 %" WSPEC "[^\r]\r\n%n", httpHeaderValue, &bytesRead) == 1)
			httpHeader = qtrue;
		else if (sscanf(s, "%" WSPEC "[^:]: %" WSPEC "[^\r]\r\n%n", header, value, &bytesRead) != 2)
			break;

		s += bytesRead;

		if (badResponse)
			continue;

		if (httpHeader) {
			int code;
			if (sscanf(httpHeaderValue, "%d", &code) == 1 && code != 200)
				badResponse = qtrue;
		} else if (Q_stricmp(header, "Content-Length") == 0) {
			int temp;
			if (sscanf(value, "%d", &temp) == 1)
				dl->bytesContent = temp;
		} else if (Q_stricmp(header, "Content-Disposition") == 0) {
			const size_t l = strlen("filename=");
			const char* valueFileName = strstr(value, "filename=");
			if (valueFileName != NULL) {
				valueFileName += l;
				if (*valueFileName == '\"')
					valueFileName++;

				if (sscanf(valueFileName, "%" WSPEC "[^\";\r]", fileName) == 1)
					Q_strncpyz(dl->finalName, fileName, sizeof(dl->finalName));
			}
		} else if (Q_stricmp(header, "X-CNQ3-CRC32") == 0) {
			unsigned int temp;
			if (sscanf(value, "%x", &temp) == 1)
				dl->crc32 = temp;
		}
	}

	if (badResponse) {
		if (Q_stricmpn(s - 4, "\r\n\r\n", 4) == 0) {
			RemoveTrailingWhiteSpace((char*)s);
			if (*s != '\0') {
				PrintError(dl, "%s - %s", httpHeaderValue, s);
				return qfalse;
			} else {
				dl->badResponse = qtrue;
				Q_strncpyz(dl->httpHeaderValue, httpHeaderValue, sizeof(dl->httpHeaderValue));
				return qtrue;
			}
		}
		return qfalse;
	}

	if (Q_stricmpn(s - 4, "\r\n\r\n", 4) == 0) {
		if (dl->bytesContent == 0) {
			PrintError(dl, "Content-Length wasn't defined or was invalid");
			return qfalse;
		}

		if (dl->finalName[0] == '\0') {
			PrintError(dl, "Content-Disposition wasn't defined or was missing the filename field");
			return qfalse;
		}

		dl->headerParsed = qtrue;
	}

	const unsigned int headerBytes = s - dl->recBuffer;
	dl->bytesHeader += headerBytes;
	if (dl->headerParsed) {
		dl->bytesTotal = dl->bytesHeader + dl->bytesContent;
		*headerLength = headerBytes;
	}

	return qtrue;

#undef	WSPEC
#undef	MAXSTRLEN
}


int Download_Continue( mapDownload_t* dl )
{
	if (dl->socket == INVALID_SOCKET)
		return MDLS_NOTHING;

	// the -1 is necessary because we need to be able to safely null-terminate
	// the buffer for ParseHeader without stomping another buffer's memory
	const int ec = recv(dl->socket, dl->recBuffer, sizeof(dl->recBuffer) - 1, 0);
	if (ec < 0) {
		if (IsSocketTimeoutError()) {
			const int now = Sys_Milliseconds();
			if (!dl->lastErrorTimeOut)
				dl->timeOutStartTimeMS = now;
			if (now - dl->timeOutStartTimeMS >= MAX_TIMEOUT_MS) {
				PrintError(dl, "Timed out for more than " XSTRING(MAX_TIMEOUT_MS) "ms");
				Download_CleanUp(dl, qfalse);
				return MDLS_ERROR;
			}
			dl->lastErrorTimeOut = qtrue;
			return MDLS_IN_PROGRESS;
		}
		PrintSocketError(dl, "recv");
		Download_CleanUp(dl, qfalse);
		return MDLS_ERROR;
	}

	if (ec == 0) {
		if (dl->bytesDownloaded == dl->bytesTotal)
			return Download_CleanUp(dl, qtrue) ? MDLS_SUCCESS : MDLS_ERROR;

		if (dl->badResponse)
			PrintError(dl, "HTTP status %s", dl->httpHeaderValue);
		else
			PrintError(dl, "Connection closed too early");
		Download_CleanUp(dl, qfalse);
		return MDLS_ERROR;
	}

	dl->bytesDownloaded += ec;

	unsigned int offset = 0;
	if (!dl->headerParsed) {
		// make sure ParseHeader can read the buffer as a C string
		dl->recBuffer[ec] = '\0';
		if (!ParseHeader(&offset, dl)) {
			Download_CleanUp(dl, qfalse);
			return MDLS_ERROR;
		}
	}

	const unsigned int bytesToWrite = ec - offset;
	if (dl->headerParsed && bytesToWrite > 0) {
		if (fwrite(dl->recBuffer + offset, bytesToWrite, 1, dl->file) != 1) {
			const int error = errno;
			char* const errorString = strerror(error);
			PrintError(dl, "Failed to write %d bytes to '%s': %s (%d)",
					   (int)(ec - offset), dl->tempPath, errorString ? errorString : "?", error);
			Download_CleanUp(dl, qfalse);
			return MDLS_ERROR;
		}
	}

	if (dl->bytesDownloaded == dl->bytesTotal)
		return Download_CleanUp(dl, qtrue) ? MDLS_SUCCESS : MDLS_ERROR;

	dl->timeOutStartTimeMS = INT_MIN;
	dl->lastErrorTimeOut = qfalse;

	return MDLS_IN_PROGRESS;
}


static qbool CL_MapDownload_StartImpl( const char* mapName, int source, const char* query, qbool fromCommand, qbool exactMatch, qbool realMapName )
{
	Com_Printf("Attempting download from the %s map server...\n", cl_mapDLSources[source].name);

	Q_strncpyz(cl_mapDL.mapName, mapName, sizeof(cl_mapDL.mapName));
	cl_mapDL.realMapName = realMapName;

	const qbool success = Download_Begin(&cl_mapDL, cl_mapDLSources[source].port, cl_mapDLSources[source].hostName, query);	
	if (!success) {
		if (!fromCommand)
			Com_Error(ERR_DROP, cl_mapDL.errorMessage);
		return qfalse;
	}

	// We set these after the Download_Begin call since it clears cl_mapDL.
	cl_mapDL.fromCommand = fromCommand;
	cl_mapDL.sourceIndex = source;
	cl_mapDL.exactMatch = exactMatch;

	if (!fromCommand) {
		Cvar_Set("cl_downloadName", mapName);
		Cvar_Set("cl_downloadSize", "0");
		Cvar_Set("cl_downloadCount", "0");
		Cvar_SetValue("cl_downloadTime", cls.realtime);
	}

	return qtrue;
}


static qbool CL_MapDownload_CheckActive()
{
	if (!CL_MapDownload_Active())
		return qfalse;

	PrintError(&cl_mapDL, "Download already in progress for map %s", cl_mapDL.mapName);
	return qtrue;
}


qbool CL_MapDownload_Start( const char* mapName, qbool fromCommand )
{
	if (CL_MapDownload_CheckActive())
		return qfalse;

	char query[256];
	(*cl_mapDLSources[0].formatQuery)(query, sizeof(query), mapName);
	if (CL_MapDownload_StartImpl(mapName, 0, query, fromCommand, qfalse, qtrue))
		return qtrue;

	(*cl_mapDLSources[1].formatQuery)(query, sizeof(query), mapName);
	return CL_MapDownload_StartImpl(mapName, 1, query, fromCommand, qfalse, qtrue);
}


qbool CL_MapDownload_Start_MapChecksum( const char* mapName, unsigned int mapCrc32, qbool exactMatch )
{
	if (mapCrc32 == 0 || CL_MapDownload_CheckActive())
		return qfalse;

	char query[256];
	Com_sprintf(query, sizeof(query), "map?n=%s&m=%x", mapName, mapCrc32);
	if (!exactMatch)
		Q_strcat(query, sizeof(query), "&e=0");

	return CL_MapDownload_StartImpl(mapName, 0, query, qfalse, exactMatch, qtrue);
}


qbool CL_MapDownload_Start_PakChecksums( const char* mapName, unsigned int* pakChecksums, int pakCount, qbool exactMatch )
{
	if (pakCount == 0 || CL_MapDownload_CheckActive())
		return qfalse;

	static char query[1024];
	Com_sprintf(query, sizeof(query), "map?n=%s&p=", mapName);
	Q_strcat(query, sizeof(query), va("%x", pakChecksums[0]));
	for (int i = 1; i < pakCount; ++i) {
		Q_strcat(query, sizeof(query), va(",%x", pakChecksums[i]));
	}
	if (!exactMatch)
		Q_strcat(query, sizeof(query), "&e=0");

	return CL_MapDownload_StartImpl(mapName, 0, query, qfalse, exactMatch, qtrue);
}


qbool CL_PakDownload_Start( unsigned int checksum, qbool fromCommand, const char* mapName )
{
	if (checksum == 0 || CL_MapDownload_CheckActive())
		return qfalse;

	char query[64];
	Com_sprintf(query, sizeof(query), "pak?%x", checksum);

	qbool realMapName = qtrue;
	char name[64];
	if (mapName == NULL || mapName[0] == '\0') {
		Com_sprintf(name, sizeof(name), "%x", checksum);
		mapName = name;
		realMapName = qfalse;
	}

	return CL_MapDownload_StartImpl(mapName, 0, query, fromCommand, qtrue, realMapName);
}


static void CL_MapDownload_ClearCvars()
{
	Cvar_Set("cl_downloadSize", "0");
	Cvar_Set("cl_downloadCount", "0");
	Cvar_Set("cl_downloadName", "");
	Cvar_SetValue("cl_downloadTime", cls.realtime);
}


void CL_MapDownload_Continue()
{
	const int status = Download_Continue(&cl_mapDL);
	if (status == MDLS_SUCCESS) {
		FS_Restart(clc.checksumFeed);
		if (!cl_mapDL.fromCommand) {
			CL_DownloadsComplete();
			CL_MapDownload_ClearCvars();
		}
		Com_Printf("'%s' downloaded successfully to '%s'\n", cl_mapDL.finalName, cl_mapDL.finalPath);
	} else if (status == MDLS_IN_PROGRESS) {
		if (!cl_mapDL.fromCommand) {
			Cvar_Set("cl_downloadSize", va("%d", cl_mapDL.bytesTotal));
			Cvar_Set("cl_downloadCount", va("%d", cl_mapDL.bytesDownloaded));
		}
	} else if (status == MDLS_ERROR) {
		if (!cl_mapDL.fromCommand) {
			if (clc.demoplaying)
				CL_DemoCompleted();
			CL_MapDownload_ClearCvars();
			Com_Error(ERR_DROP, cl_mapDL.errorMessage);
		} else if (cl_mapDL.sourceIndex == 0 && !cl_mapDL.exactMatch) {
			char query[256];
			(*cl_mapDLSources[1].formatQuery)(query, sizeof(query), cl_mapDL.mapName);
			CL_MapDownload_StartImpl(cl_mapDL.mapName, 1, query, cl_mapDL.fromCommand, qfalse, cl_mapDL.realMapName);
		}
	}
}


void CL_MapDownload_Init()
{
	Download_Clear(&cl_mapDL);
}


qbool CL_MapDownload_Active()
{
	return cl_mapDL.cleared && cl_mapDL.socket != INVALID_SOCKET;
}


void CL_MapDownload_Cancel()
{
	if (!CL_MapDownload_Active())
		return;

	Download_CleanUp(&cl_mapDL, qfalse);
	CL_MapDownload_ClearCvars();
}


// negative if nothing's going on
static float CL_MapDownload_Progress()
{
	if (!CL_MapDownload_Active() || !cl_mapDL.fromCommand || 
		cl_mapDL.bytesTotal < 1 || cl_mapDL.bytesDownloaded < 0)
		return -1;

	const uint64_t t = (uint64_t)cl_mapDL.bytesTotal;
	const uint64_t d = (uint64_t)cl_mapDL.bytesDownloaded;
	const double p = ((double)d * 100.0) / (double)t;
	const float progress = min((float)p, 100.0f);
	
	return progress;
}


// bytes/s, negative if nothing's going on
static int CL_MapDownload_Speed()
{
	if (!CL_MapDownload_Active() || !cl_mapDL.fromCommand ||
		cl_mapDL.bytesTotal < 1 || cl_mapDL.bytesDownloaded < 0)
		return -1;

	const int startMS = cl_mapDL.startTimeMS;
	const int nowMS = Sys_Milliseconds();
	const int elapsedMS = nowMS - startMS;
	if (startMS == INT_MIN || elapsedMS <= 0)
		return -1;

	return (int)(((uint64_t)cl_mapDL.bytesDownloaded * 1000) / (uint64_t)elapsedMS);
}


static void FormatSize( char* buffer, int bufferSize, unsigned int bytes )
{
	unsigned int m = (unsigned int)(-1);
	unsigned int x = bytes;
	while (x) {
		x >>= 10;
		m++;
	}

	if (m == 0)
		Com_sprintf(buffer, bufferSize, "%u bytes", bytes);
	else if (m == 1)
		Com_sprintf(buffer, bufferSize, "%u KB", bytes >> 10);
	else if (m == 2)
		Com_sprintf(buffer, bufferSize, "%.1f MB", (float)(bytes >> 10) / 1024.0f);
	else
		Com_sprintf(buffer, bufferSize, "%.2f GB", (float)(bytes >> 20) / 1024.0f);
}


static void FormatTime( char* buffer, int bufferSize, unsigned int seconds )
{
	const int s = seconds % 60;
	const int m = seconds / 60;
	if (m > 0)
		Com_sprintf(buffer, bufferSize, "%dm %2ds", m, s);
	else
		Com_sprintf(buffer, bufferSize, "%2ds", s);
}


void CL_MapDownload_DrawConsole( float cw, float ch )
{
	const float progress = CL_MapDownload_Progress();
	if (progress < 0.0f)
		return;

	const float vw = cls.glconfig.vidWidth;

	char msg0[128];
	const char* fileName = cl_mapDL.finalName[0] != '\0' ? cl_mapDL.finalName : "pk3";
	Com_sprintf(msg0, sizeof(msg0), "Downloading %s: %2d%%", fileName, (int)progress);
	const float wl0 = cw * strlen(msg0);
	re.SetColor(colorWhite);
	SCR_DrawString(vw - wl0 - cw / 2, ch / 2, cw, ch, msg0, qtrue);

	const int speed = CL_MapDownload_Speed();
	if (speed <= 0)
		return;

	char size[64];
	FormatSize(size, sizeof(size), speed);
	char msg1[128];
	Com_sprintf(msg1, sizeof(msg1), "Speed: %s/s", size);
	const float wl1 = cw * strlen(msg1);
	SCR_DrawString(vw - wl1 - cw / 2, 3 * ch / 2, cw, ch, msg1, qtrue);

	if (progress <= 0.0f)
		return;

	const int elapsedMS = Sys_Milliseconds() - cl_mapDL.startTimeMS;
	const int totalMS = (int)((elapsedMS * 100.0f) / progress);
	const int remainingMS = max(totalMS - elapsedMS, 0);
	char time[64];
	FormatTime(time, sizeof(time), remainingMS / 1000);
	char msg2[128];
	Com_sprintf(msg2, sizeof(msg2), "Time left: %s", time);
	const float wl2 = cw * strlen(msg2);
	SCR_DrawString(vw - wl2 - cw / 2, 5 * ch / 2, cw, ch, msg2, qtrue);
}


void CL_MapDownload_CrashCleanUp()
{
	if (cl_mapDL.file != NULL && cl_mapDL.tempPath[0] != '\0') {
		fclose(cl_mapDL.file);
		Q_unlink(cl_mapDL.tempPath);
	}
}
