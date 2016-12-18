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
// common.c -- misc functions used in client and server

#include "q_shared.h"
#include "qcommon.h"
#include <setjmp.h>

#ifndef _WIN32
#include <netinet/in.h>
#include <sys/stat.h> // umask
#endif


#define MIN_DEDICATED_COMHUNKMEGS 8
#define MIN_COMHUNKMEGS		56
#define DEF_COMHUNKMEGS		64
#define DEF_COMZONEMEGS		24
#define XSTRING(x)			STRING(x)
#define STRING(x)			#x
#define DEF_COMHUNKMEGS_S	XSTRING(DEF_COMHUNKMEGS)
#define DEF_COMZONEMEGS_S	XSTRING(DEF_COMZONEMEGS)


static jmp_buf abortframe;		// an ERR_DROP occured, exit the entire frame


static fileHandle_t logfile = 0;
static fileHandle_t com_journalFile = 0;		// events are written here
fileHandle_t	com_journalDataFile = 0;		// config files are written here

cvar_t	*com_viewlog = 0;
cvar_t	*com_speeds = 0;
cvar_t	*com_developer = 0;
cvar_t	*com_dedicated = 0;
cvar_t	*com_timescale = 0;
cvar_t	*com_fixedtime = 0;
cvar_t	*com_journal = 0;
cvar_t	*com_maxfps = 0;
cvar_t	*com_timedemo = 0;
cvar_t	*com_sv_running = 0;
cvar_t	*com_cl_running = 0;
cvar_t	*com_logfile = 0;		// 1 = buffer log, 2 = flush after each print
cvar_t	*com_showtrace = 0;
cvar_t	*com_version = 0;
cvar_t	*com_buildScript = 0;	// for automated data building scripts
cvar_t	*com_introPlayed = 0;
cvar_t	*cl_paused = 0;
cvar_t	*sv_paused = 0;
cvar_t	*cl_packetdelay = 0;
cvar_t	*sv_packetdelay = 0;
#if defined(_WIN32) && defined(_DEBUG)
cvar_t	*com_noErrorInterrupt;
#endif

// com_speeds times
int		time_game;
int		time_frontend;		// renderer frontend time
int		time_backend;		// renderer backend time

int		com_frameTime;
int		com_frameNumber;

qbool	com_errorEntered;
qbool	com_fullyInitialized;

static char com_errorMessage[MAXPRINTMSG];

static void Com_WriteConfig_f();
extern void CIN_CloseAllVideos( void );

//============================================================================

static char	*rd_buffer;
static int	rd_buffersize;
static void	(*rd_flush)( char *buffer );

void Com_BeginRedirect (char *buffer, int buffersize, void (*flush)( char *) )
{
	if (!buffer || !buffersize || !flush)
		return;
	rd_buffer = buffer;
	rd_buffersize = buffersize;
	rd_flush = flush;

	*rd_buffer = 0;
}

void Com_EndRedirect (void)
{
	if ( rd_flush ) {
		rd_flush(rd_buffer);
	}

	rd_buffer = NULL;
	rd_buffersize = 0;
	rd_flush = NULL;
}


///////////////////////////////////////////////////////////////

// client and server can both use these, and will do the appropriate things


// a raw string should NEVER be passed as fmt, because of "%f" type crashers

void QDECL Com_Printf( const char *fmt, ... )
{
	char msg[MAXPRINTMSG];

	va_list argptr;
	va_start( argptr, fmt );
	Q_vsnprintf( msg, sizeof(msg), fmt, argptr );
	va_end( argptr );

	if ( rd_buffer ) {
		if ((strlen (msg) + strlen(rd_buffer)) > (rd_buffersize - 1)) {
			rd_flush(rd_buffer);
			*rd_buffer = 0;
		}
		Q_strcat(rd_buffer, rd_buffersize, msg);
		return;
	}

#ifndef DEDICATED
	// echo to console if we're not a dedicated server
	if ( com_dedicated && !com_dedicated->integer ) {
		CL_ConsolePrint( msg );
	}
#endif

	// echo to dedicated console and early console
	Sys_Print( msg );

	if ( !com_logfile || !com_logfile->integer )
		return;

	static qbool opening_qconsole = qfalse;

	// TTimo: only open the qconsole.log if the filesystem is in an initialized state
	//   also, avoid recursing in the qconsole.log opening (i.e. if fs_debug is on)
	if ( !logfile && FS_Initialized() && !opening_qconsole) {
		opening_qconsole = qtrue;

		time_t aclock;
		time( &aclock );
		struct tm* newtime = localtime( &aclock );

		logfile = FS_FOpenFileWrite( "qconsole.log" );
		if (logfile)
		{
			Com_Printf( "logfile opened on %s\n", asctime( newtime ) );
			if ( com_logfile->integer > 1 )
			{
				// force it to not buffer so we get valid
				// data even if we are crashing
				FS_ForceFlush(logfile);
			}
		}
		else
		{
			Com_Printf("Opening qconsole.log failed!\n");
			Cvar_SetValue("logfile", 0);
		}

		opening_qconsole = qfalse;
	}

	if ( logfile && FS_Initialized() ) {
		FS_Write( msg, strlen(msg), logfile );
	}
}


// a Com_Printf that only shows up if the "developer" cvar is set

void QDECL Com_DPrintf( const char *fmt, ...)
{
	// don't confuse non-developers with techie stuff...
	if ( !com_developer || !com_developer->integer )
		return;

	char msg[MAXPRINTMSG];

	va_list argptr;
	va_start( argptr, fmt );
	Q_vsnprintf( msg, sizeof(msg), fmt, argptr );
	Com_Printf( "%s", msg );
	va_end( argptr );
}


void QDECL Com_Error( int code, const char *fmt, ... )
{
	static int	lastErrorTime;
	static int	errorCount;

#if defined(_WIN32) && defined(_DEBUG)
	if ( code != ERR_DISCONNECT && code != ERR_NEED_CD ) {
		if (!com_noErrorInterrupt->integer) {
			__asm {
				int 0x03
			}
		}
	}
#endif

	// when we are running automated scripts, make sure we
	// know if anything failed
	if ( com_buildScript && com_buildScript->integer ) {
		code = ERR_FATAL;
	}

	// make sure we can get at our local stuff
	FS_PureServerSetLoadedPaks( "", "" );

	// if we are getting a solid stream of ERR_DROP, do an ERR_FATAL
	int currentTime = Sys_Milliseconds();
	if ( currentTime - lastErrorTime < 100 ) {
		if ( ++errorCount > 3 ) {
			code = ERR_FATAL;
		}
	} else {
		errorCount = 0;
	}
	lastErrorTime = currentTime;

	if ( com_errorEntered ) {
		Sys_Error( "recursive error after: %s", com_errorMessage );
	}
	com_errorEntered = qtrue;

	va_list argptr;
	va_start( argptr, fmt );
	vsprintf( com_errorMessage, fmt, argptr );
	va_end( argptr );

	if ( code != ERR_DISCONNECT && code != ERR_NEED_CD ) {
		Cvar_Set("com_errorMessage", com_errorMessage);
	}

	if ( code == ERR_SERVERDISCONNECT ) {
		CL_Disconnect( qtrue );
		CL_FlushMemory();
		com_errorEntered = qfalse;
		longjmp (abortframe, -1);
	} else if ( code == ERR_DROP || code == ERR_DISCONNECT ) {
		Com_Printf( "********************\nERROR: %s\n********************\n", com_errorMessage );
		SV_Shutdown( va("Server crashed: %s",  com_errorMessage) );
		CL_Disconnect( qtrue );
		CL_FlushMemory();
		com_errorEntered = qfalse;
		longjmp (abortframe, -1);
	} else if ( code == ERR_NEED_CD ) {
		SV_Shutdown( "Server didn't have CD" );
		if ( com_cl_running && com_cl_running->integer ) {
			CL_Disconnect( qtrue );
			CL_FlushMemory();
			com_errorEntered = qfalse;
			CL_CDDialog();
		} else {
			Com_Printf("Server didn't have CD\n" );
		}
		longjmp (abortframe, -1);
	} else {
		CL_Shutdown();
		SV_Shutdown( va("Server fatal crashed: %s", com_errorMessage) );
	}

	Com_Shutdown();

	Sys_Error( "%s", com_errorMessage );
}


void Com_Quit_f( void )
{
	// don't try to shutdown if we are in a recursive error
	if ( !com_errorEntered ) {
		SV_Shutdown( "Server quit" );
		CL_Shutdown();
		Com_Shutdown();
		FS_Shutdown( qtrue );
	}

	Sys_Quit();
}


///////////////////////////////////////////////////////////////


/*
COMMAND LINE FUNCTIONS

+ characters seperate the commandLine string into multiple console
command lines.

All of these are valid:

quake3 +set test blah +map test
quake3 set test blah+map test
quake3 set test blah + map test

*/

#define MAX_CONSOLE_LINES	32
static int com_numConsoleLines;
static char* com_consoleLines[MAX_CONSOLE_LINES];


// break the process args up into multiple console lines

static void Com_ParseCommandLine( char* commandLine )
{
	int inq = 0;
	com_consoleLines[0] = commandLine;
	com_numConsoleLines = 1;

	while ( *commandLine ) {
		if (*commandLine == '"') {
			inq = !inq;
		}
		// look for a + separating character
		// if commandLine came from a file, we might have real line separators
		if ( (*commandLine == '+' && !inq) || *commandLine == '\n'  || *commandLine == '\r' ) {
			if ( com_numConsoleLines == MAX_CONSOLE_LINES ) {
				return;
			}
			*commandLine++ = 0; // terminate the PREVIOUS command
			com_consoleLines[com_numConsoleLines++] = commandLine;
		}
		++commandLine;
	}
}


// check for "+safe" on the command line, which will skip loading of q3config.cfg

qbool Com_SafeMode()
{
	for (int i = 0; i < com_numConsoleLines; ++i) {
		Cmd_TokenizeString( com_consoleLines[i] );
		if ( !Q_stricmp( Cmd_Argv(0), "safe" ) || !Q_stricmp( Cmd_Argv(0), "cvar_restart" ) ) {
			com_consoleLines[i][0] = 0;
			return qtrue;
		}
	}

	return qfalse;
}


/*
Searches for command line parameters that are set commands.
If match is not NULL, only that cvar will be looked for.
That is necessary because cddir and basedir need to be set
before the filesystem is started, but all other sets should
be after execing the config and default.
*/
void Com_StartupVariable( const char *match )
{
	for (int i = 0; i < com_numConsoleLines; ++i) {
		Cmd_TokenizeString( com_consoleLines[i] );
		if ( strcmp( Cmd_Argv(0), "set" ) )
			continue;

		const char* s = Cmd_Argv(1);
		if ( !match || !strcmp( s, match ) ) {
			Cvar_Set( s, Cmd_Argv(2) );
			cvar_t* cv = Cvar_Get( s, "", 0 );
			cv->flags |= CVAR_USER_CREATED;
		}
	}
}


/*
Adds command line parameters as script statements
Commands are separated by + signs

Returns qtrue if any late commands were added
which will keep the fucking annoying cins from playing
*/
static qbool Com_AddStartupCommands()
{
	qbool added = qfalse;

	// quote every token, so args with semicolons can work
	for (int i = 0; i < com_numConsoleLines; ++i) {
		if ( !com_consoleLines[i] || !com_consoleLines[i][0] ) {
			continue;
		}

		// set commands won't override menu startup
		if ( Q_stricmpn( com_consoleLines[i], "set", 3 ) ) {
			added = qtrue;
		}

		Cbuf_AddText( com_consoleLines[i] );
		Cbuf_AddText( "\n" );
	}

	return added;
}


//============================================================================

void Info_Print( const char *s ) {
	char	key[512];
	char	value[512];
	char	*o;
	int		l;

	if (*s == '\\')
		s++;
	while (*s)
	{
		o = key;
		while (*s && *s != '\\')
			*o++ = *s++;

		l = o - key;
		if (l < 20)
		{
			Com_Memset (o, ' ', 20-l);
			key[20] = 0;
		}
		else
			*o = 0;
		Com_Printf ("%s", key);

		if (!*s)
		{
			Com_Printf ("MISSING VALUE\n");
			return;
		}

		o = value;
		s++;
		while (*s && *s != '\\')
			*o++ = *s++;
		*o = 0;

		if (*s)
			s++;
		Com_Printf ("%s\n", value);
	}
}


static const char* Com_StringContains( const char* str1, const char* str2)
{
	int i, j;

	int len = strlen(str1) - strlen(str2);
	for (i = 0; i <= len; i++, str1++) {
		for (j = 0; str2[j]; j++) {
			if (toupper(str1[j]) != toupper(str2[j])) {
				break;
			}
		}
		if (!str2[j]) {
			return str1;
		}
	}
	return NULL;
}


int Com_Filter( const char* filter, const char* name )
{
	char buf[MAX_TOKEN_CHARS];
	const char* ptr;
	int i, found;

	while (*filter) {
		if (*filter == '*') {
			filter++;
			for (i = 0; *filter; i++) {
				if (*filter == '*' || *filter == '?') break;
				buf[i] = *filter;
				filter++;
			}
			buf[i] = '\0';
			if (buf[0]) {
				ptr = Com_StringContains(name, buf);
				if (!ptr) return qfalse;
				name = ptr + strlen(buf);
			}
		}
		else if (*filter == '?') {
			filter++;
			name++;
		}
		else if (*filter == '[' && *(filter+1) == '[') {
			filter++;
		}
		else if (*filter == '[') {
			filter++;
			found = qfalse;
			while(*filter && !found) {
				if (*filter == ']' && *(filter+1) != ']') break;
				if (*(filter+1) == '-' && *(filter+2) && (*(filter+2) != ']' || *(filter+3) == ']')) {
					if (toupper(*name) >= toupper(*filter) && toupper(*name) <= toupper(*(filter+2)))
						found = qtrue;
					filter += 3;
				}
				else {
					if (toupper(*filter) == toupper(*name))
						found = qtrue;
					filter++;
				}
			}
			if (!found) return qfalse;
			while(*filter) {
				if (*filter == ']' && *(filter+1) != ']') break;
				filter++;
			}
			filter++;
			name++;
		}
		else {
			if (toupper(*filter) != toupper(*name))
				return qfalse;
			filter++;
			name++;
		}
	}

	return qtrue;
}


int Com_FilterPath( const char* filter, const char* name )
{
	int i;
	char new_filter[MAX_QPATH];
	char new_name[MAX_QPATH];

	for (i = 0; i < MAX_QPATH-1 && filter[i]; i++) {
		if ( filter[i] == '\\' || filter[i] == ':' ) {
			new_filter[i] = '/';
		}
		else {
			new_filter[i] = filter[i];
		}
	}
	new_filter[i] = '\0';

	for (i = 0; i < MAX_QPATH-1 && name[i]; i++) {
		if ( name[i] == '\\' || name[i] == ':' ) {
			new_name[i] = '/';
		}
		else {
			new_name[i] = name[i];
		}
	}
	new_name[i] = '\0';

	return Com_Filter( new_filter, new_name );
}

/*
============
Com_HashKey
============
*/
int Com_HashKey(char *string, int maxlen) {
	int register hash, i;

	hash = 0;
	for (i = 0; i < maxlen && string[i] != '\0'; i++) {
		hash += string[i] * (119 + i);
	}
	hash = (hash ^ (hash >> 10) ^ (hash >> 20));
	return hash;
}


int Com_RealTime( qtime_t* qtime )
{
	time_t t = time(NULL);
	if (!qtime)
		return t;

	const struct tm* tms = localtime(&t);
	if (tms) {
		qtime->tm_sec = tms->tm_sec;
		qtime->tm_min = tms->tm_min;
		qtime->tm_hour = tms->tm_hour;
		qtime->tm_mday = tms->tm_mday;
		qtime->tm_mon = tms->tm_mon;
		qtime->tm_year = tms->tm_year;
		qtime->tm_wday = tms->tm_wday;
		qtime->tm_yday = tms->tm_yday;
		qtime->tm_isdst = tms->tm_isdst;
	}

	return t;
}


/*
==============================================================================

						ZONE MEMORY ALLOCATION

There is never any space between memblocks, and there will never be two
contiguous free memblocks.

The rover can be left pointing at a non-empty block

The zone calls are pretty much only used for small strings and structures,
all big things are allocated on the hunk.
==============================================================================
*/

#define	ZONEID	0x1d4a11
#define MINFRAGMENT	64

typedef struct zonedebug_s {
	char *label;
	char *file;
	int line;
	int allocSize;
} zonedebug_t;

typedef struct memblock_s {
	int		size;           // including the header and possibly tiny fragments
	int     tag;            // a tag of 0 is a free block
	struct memblock_s       *next, *prev;
	int     id;        		// should be ZONEID
#ifdef ZONE_DEBUG
	zonedebug_t d;
#endif
} memblock_t;

typedef struct {
	int		size;			// total bytes malloced, including header
	int		used;			// total bytes used
	memblock_t	blocklist;	// start / end cap for linked list
	memblock_t	*rover;
} memzone_t;

// main zone for all "dynamic" memory allocation
static memzone_t* mainzone = NULL;
// we also have a small zone for small allocations that would only
// fragment the main zone (think of cvar and cmd strings)
static memzone_t* smallzone = NULL;


static void Z_ClearZone( memzone_t* zone, int size )
{
	memblock_t* block;

	// set the entire zone to one free block

	zone->blocklist.next = zone->blocklist.prev = block =
		(memblock_t *)( (byte *)zone + sizeof(memzone_t) );
	zone->blocklist.tag = 1;	// in use block
	zone->blocklist.id = 0;
	zone->blocklist.size = 0;
	zone->rover = block;
	zone->size = size;
	zone->used = 0;

	block->prev = block->next = &zone->blocklist;
	block->tag = 0;			// free block
	block->id = ZONEID;
	block->size = size - sizeof(memzone_t);
}


static int Z_AvailableZoneMemory( const memzone_t* zone )
{
	return zone->size - zone->used;
}


int Z_AvailableMemory( void )
{
	return Z_AvailableZoneMemory( mainzone );
}


void Z_Free( void* ptr )
{
	if (!ptr) {
		Com_Error( ERR_DROP, "Z_Free: NULL pointer" );
	}

	memblock_t* block = (memblock_t*)((byte*)ptr - sizeof(memblock_t));
	if (block->id != ZONEID) {
		Com_Error( ERR_FATAL, "Z_Free: freed a pointer without ZONEID" );
	}
	if (block->tag == 0) {
		Com_Error( ERR_FATAL, "Z_Free: freed a freed pointer" );
	}
	// if static memory
	if (block->tag == TAG_STATIC) {
		return;
	}

	// check the memory trash tester
	if ( *(int *)((byte *)block + block->size - 4 ) != ZONEID ) {
		Com_Error( ERR_FATAL, "Z_Free: memory block wrote past end" );
	}

	memzone_t* zone = (block->tag == TAG_SMALL) ? smallzone : mainzone;
	zone->used -= block->size;
	// set the block to something that should cause problems
	// if it is referenced...
	Com_Memset( ptr, 0xaa, block->size - sizeof( *block ) );

	block->tag = 0;		// mark as free

	memblock_t* other = block->prev;
	if (!other->tag) {
		// merge with previous free block
		other->size += block->size;
		other->next = block->next;
		other->next->prev = other;
		if (block == zone->rover) {
			zone->rover = other;
		}
		block = other;
	}

	zone->rover = block;

	other = block->next;
	if ( !other->tag ) {
		// merge the next free block onto the end
		block->size += other->size;
		block->next = other->next;
		block->next->prev = block;
		if (other == zone->rover) {
			zone->rover = block;
		}
	}
}


#ifdef ZONE_DEBUG

static void Z_LogZoneHeap( const memzone_t* zone, const char* name )
{
	char dump[32], *ptr;
	int  i, j;
	memblock_t	*block;
	char		buf[4096];
	int size, allocSize, numBlocks;

	if (!logfile || !FS_Initialized())
		return;
	size = allocSize = numBlocks = 0;
	Com_sprintf(buf, sizeof(buf), "\r\n================\r\n%s log\r\n================\r\n", name);
	FS_Write(buf, strlen(buf), logfile);
	for (block = zone->blocklist.next ; block->next != &zone->blocklist; block = block->next) {
		if (block->tag) {
			ptr = ((char *) block) + sizeof(memblock_t);
			j = 0;
			for (i = 0; i < 20 && i < block->d.allocSize; i++) {
				if (ptr[i] >= 32 && ptr[i] < 127) {
					dump[j++] = ptr[i];
				}
				else {
					dump[j++] = '_';
				}
			}
			dump[j] = '\0';
			Com_sprintf(buf, sizeof(buf), "size = %8d: %s, line: %d (%s) [%s]\r\n", block->d.allocSize, block->d.file, block->d.line, block->d.label, dump);
			FS_Write(buf, strlen(buf), logfile);
			allocSize += block->d.allocSize;
			size += block->size;
			numBlocks++;
		}
	}

	// subtract debug memory
	size -= numBlocks * sizeof(zonedebug_t);
	allocSize = numBlocks * sizeof(memblock_t); // + 32 bit alignment

	Com_sprintf(buf, sizeof(buf), "%d %s memory in %d blocks\r\n", size, name, numBlocks);
	FS_Write(buf, strlen(buf), logfile);
	Com_sprintf(buf, sizeof(buf), "%d %s memory overhead\r\n", size - allocSize, name);
	FS_Write(buf, strlen(buf), logfile);
}


static void Z_LogHeap()
{
	Z_LogZoneHeap( mainzone, "MAIN" );
	Z_LogZoneHeap( smallzone, "SMALL" );
}

#endif


/*
================
Z_TagMalloc
================
*/
#ifdef ZONE_DEBUG
void *Z_TagMallocDebug( int size, int tag, char *label, char *file, int line ) {
#else
void *Z_TagMalloc( int size, int tag ) {
#endif
	int		extra, allocSize;
	memblock_t	*start, *rover, *base;
	memzone_t *zone;

	if (!tag) {
		Com_Error( ERR_FATAL, "Z_TagMalloc: tried to use a 0 tag" );
	}

	if ( tag == TAG_SMALL ) {
		zone = smallzone;
	}
	else {
		zone = mainzone;
	}

	allocSize = size;
	//
	// scan through the block list looking for the first free block
	// of sufficient size
	//
	size += sizeof(memblock_t);	// account for size of block header
	size += 4;					// space for memory trash tester
	size = PAD(size, sizeof(intptr_t));		// align to 32/64 bit boundary

	base = rover = zone->rover;
	start = base->prev;

	do {
		if (rover == start) {
#ifdef ZONE_DEBUG
			Z_LogHeap();
#endif
			// scaned all the way around the list
			Com_Error( ERR_FATAL, "Z_Malloc: failed on allocation of %i bytes from the %s zone",
								size, zone == smallzone ? "small" : "main");
			return NULL;
		}
		if (rover->tag) {
			base = rover = rover->next;
		} else {
			rover = rover->next;
		}
	} while (base->tag || base->size < size);

	//
	// found a block big enough
	//
	extra = base->size - size;
	if (extra > MINFRAGMENT) {
		// there will be a free fragment after the allocated block
		memblock_t* p = (memblock_t *) ((byte *)base + size );
		p->size = extra;
		p->tag = 0;			// free block
		p->prev = base;
		p->id = ZONEID;
		p->next = base->next;
		p->next->prev = p;
		base->next = p;
		base->size = size;
	}

	base->tag = tag;			// no longer a free block

	zone->rover = base->next;	// next allocation will start looking here
	zone->used += base->size;	//
	
	base->id = ZONEID;

#ifdef ZONE_DEBUG
	base->d.label = label;
	base->d.file = file;
	base->d.line = line;
	base->d.allocSize = allocSize;
#endif

	// marker for memory trash testing
	*(int *)((byte *)base + base->size - 4) = ZONEID;

	return (void *) ((byte *)base + sizeof(memblock_t));
}


static void Z_CheckHeap()
{
	const memblock_t* block;

	for (block = mainzone->blocklist.next ; ; block = block->next) {
		if (block->next == &mainzone->blocklist) {
			break;			// all blocks have been hit
		}
		if ( (byte *)block + block->size != (byte *)block->next)
			Com_Error( ERR_FATAL, "Z_CheckHeap: block size does not touch the next block\n" );
		if ( block->next->prev != block) {
			Com_Error( ERR_FATAL, "Z_CheckHeap: next block doesn't have proper back link\n" );
		}
		if ( !block->tag && !block->next->tag ) {
			Com_Error( ERR_FATAL, "Z_CheckHeap: two consecutive free blocks\n" );
		}
	}
}


#ifdef ZONE_DEBUG
void *Z_MallocDebug( int size, char *label, char *file, int line )
{
	Z_CheckHeap();
	void* buf = Z_TagMallocDebug( size, TAG_GENERAL, label, file, line );
#else
void *Z_Malloc( int size )
{
	void* buf = Z_TagMalloc( size, TAG_GENERAL );
#endif
	Com_Memset( buf, 0, size );

	return buf;
}


#ifdef ZONE_DEBUG
void *S_MallocDebug( int size, char *label, char *file, int line )
{
	return Z_TagMallocDebug( size, TAG_SMALL, label, file, line );
}
#else
void *S_Malloc( int size )
{
	return Z_TagMalloc( size, TAG_SMALL );
}
#endif


// static mem blocks to reduce a lot of small zone overhead
typedef struct memstatic_s {
	memblock_t b;
	byte mem[2];
} memstatic_t;

static memstatic_t emptystring =
	{ {(sizeof(memblock_t)+2 + 3) & ~3, TAG_STATIC, NULL, NULL, ZONEID}, {'\0', '\0'} };
static memstatic_t numberstring[] = {
	{ {(sizeof(memstatic_t) + 3) & ~3, TAG_STATIC, NULL, NULL, ZONEID}, {'0', '\0'} },
	{ {(sizeof(memstatic_t) + 3) & ~3, TAG_STATIC, NULL, NULL, ZONEID}, {'1', '\0'} },
	{ {(sizeof(memstatic_t) + 3) & ~3, TAG_STATIC, NULL, NULL, ZONEID}, {'2', '\0'} },
	{ {(sizeof(memstatic_t) + 3) & ~3, TAG_STATIC, NULL, NULL, ZONEID}, {'3', '\0'} },
	{ {(sizeof(memstatic_t) + 3) & ~3, TAG_STATIC, NULL, NULL, ZONEID}, {'4', '\0'} },
	{ {(sizeof(memstatic_t) + 3) & ~3, TAG_STATIC, NULL, NULL, ZONEID}, {'5', '\0'} },
	{ {(sizeof(memstatic_t) + 3) & ~3, TAG_STATIC, NULL, NULL, ZONEID}, {'6', '\0'} },
	{ {(sizeof(memstatic_t) + 3) & ~3, TAG_STATIC, NULL, NULL, ZONEID}, {'7', '\0'} },
	{ {(sizeof(memstatic_t) + 3) & ~3, TAG_STATIC, NULL, NULL, ZONEID}, {'8', '\0'} },
	{ {(sizeof(memstatic_t) + 3) & ~3, TAG_STATIC, NULL, NULL, ZONEID}, {'9', '\0'} }
};

/*
========================
CopyString

 NOTE:	never write over the memory CopyString returns because
		memory from a memstatic_t might be returned
========================
*/
char* CopyString( const char *in )
{
	if (!in[0]) {
		return ((char *)&emptystring) + sizeof(memblock_t);
	}
	else if (!in[1]) {
		if (in[0] >= '0' && in[0] <= '9') {
			return ((char *)&numberstring[in[0]-'0']) + sizeof(memblock_t);
		}
	}
	char* out = (char*)S_Malloc(strlen(in)+1);
	strcpy(out, in);
	return out;
}

/*
==============================================================================

Goals:
  reproducable without history effects -- no out of memory errors on weird map to map changes
  allow restarting of the client without fragmentation
  minimize total pages in use at run time
  minimize total pages needed during load time

  Single block of memory with stack allocators coming from both ends towards the middle.

  One side is designated the temporary memory allocator.

  Temporary memory can be allocated and freed in any order.

  A highwater mark is kept of the most in use at any time.

  When there is no temporary memory allocated, the permanent and temp sides
  can be switched, allowing the already touched temp memory to be used for
  permanent storage.

  Temp memory must never be allocated on two ends at once, or fragmentation
  could occur.

  If we have any in-use temp memory, additional temp allocations must come from
  that side.

  If not, we can choose to make either side the new temp side and push future
  permanent allocations to the other side.  Permanent allocations should be
  kept on the side that has the current greatest wasted highwater mark.

==============================================================================
*/


typedef unsigned int hmagic;
static const hmagic HUNK_MAGIC_INUSE = 0x89537892;
static const hmagic HUNK_MAGIC_FREED = 0x89537893;

typedef struct {
	hmagic	magic;
	int		size;
} hunkHeader_t;

typedef struct {
	int		mark;
	int		permanent;
	int		temp;
	int		tempHighwater;
} hunkUsed_t;

static	hunkUsed_t	hunk_low = {0}, hunk_high = {0};
static	hunkUsed_t	*hunk_permanent = &hunk_low, *hunk_temp = &hunk_high;

static	byte	*s_hunkData = NULL;
static	int		s_hunkTotal = 0;

static	int		s_zoneTotal = 0;

#ifdef HUNK_DEBUG

typedef struct hunkblock_s {
	int size;
	byte printed;
	struct hunkblock_s *next;
	char *label;
	char *file;
	int line;
} hunkblock_t;

static hunkblock_t* hunkblocks = NULL;

#endif


static void Com_Meminfo_f( void )
{
	const memblock_t* block;
	int zoneBytes = 0;
	int zoneBlocks = 0;
	int botlibBytes = 0;
	int rendererBytes = 0;

	for (block = mainzone->blocklist.next ; ; block = block->next) {
		if ( Cmd_Argc() != 1 ) {
			Com_Printf ("block:%p    size:%7i    tag:%3i\n",
				block, block->size, block->tag);
		}
		if ( block->tag ) {
			zoneBytes += block->size;
			zoneBlocks++;
			if ( block->tag == TAG_BOTLIB ) {
				botlibBytes += block->size;
			} else if ( block->tag == TAG_RENDERER ) {
				rendererBytes += block->size;
			}
		}

		if (block->next == &mainzone->blocklist) {
			break;			// all blocks have been hit
		}
		if ( (byte *)block + block->size != (byte *)block->next) {
			Com_Printf ("ERROR: block size does not touch the next block\n");
		}
		if ( block->next->prev != block) {
			Com_Printf ("ERROR: next block doesn't have proper back link\n");
		}
		if ( !block->tag && !block->next->tag ) {
			Com_Printf ("ERROR: two consecutive free blocks\n");
		}
	}

	int smallZoneBytes = 0;
	int smallZoneBlocks = 0;
	for (block = smallzone->blocklist.next ; ; block = block->next) {
		if ( block->tag ) {
			smallZoneBytes += block->size;
			smallZoneBlocks++;
		}

		if (block->next == &smallzone->blocklist) {
			break;			// all blocks have been hit
		}
	}

	Com_Printf( "%8i bytes total hunk\n", s_hunkTotal );
	Com_Printf( "%8i bytes total zone\n", s_zoneTotal );
	Com_Printf( "\n" );
	Com_Printf( "%8i low mark\n", hunk_low.mark );
	Com_Printf( "%8i low permanent\n", hunk_low.permanent );
	if ( hunk_low.temp != hunk_low.permanent ) {
		Com_Printf( "%8i low temp\n", hunk_low.temp );
	}
	Com_Printf( "%8i low tempHighwater\n", hunk_low.tempHighwater );
	Com_Printf( "\n" );
	Com_Printf( "%8i high mark\n", hunk_high.mark );
	Com_Printf( "%8i high permanent\n", hunk_high.permanent );
	if ( hunk_high.temp != hunk_high.permanent ) {
		Com_Printf( "%8i high temp\n", hunk_high.temp );
	}
	Com_Printf( "%8i high tempHighwater\n", hunk_high.tempHighwater );
	Com_Printf( "\n" );
	Com_Printf( "%8i total hunk in use\n", hunk_low.permanent + hunk_high.permanent );

	int unused = 0;
	if ( hunk_low.tempHighwater > hunk_low.permanent ) {
		unused += hunk_low.tempHighwater - hunk_low.permanent;
	}
	if ( hunk_high.tempHighwater > hunk_high.permanent ) {
		unused += hunk_high.tempHighwater - hunk_high.permanent;
	}
	Com_Printf( "%8i unused highwater\n", unused );
	Com_Printf( "\n" );

	Com_Printf( "%8i bytes in %i zone blocks\n", zoneBytes, zoneBlocks );
	Com_Printf( "   %8i bytes in dynamic botlib\n", botlibBytes );
	Com_Printf( "   %8i bytes in dynamic renderer\n", rendererBytes );
	Com_Printf( "   %8i bytes in dynamic other\n", zoneBytes - ( botlibBytes + rendererBytes ) );
	Com_Printf( "   %8i bytes in small Zone memory\n", smallZoneBytes );
}


// touch all known used data to make sure it is paged in

void Com_TouchMemory()
{
	int		i, j;
	int sum = 0;
	const memblock_t* block;

	Z_CheckHeap();

	int start = Sys_Milliseconds();

	j = hunk_low.permanent >> 2;
	for ( i = 0 ; i < j ; i+=64 ) {			// only need to touch each page
		sum += ((int *)s_hunkData)[i];
	}

	i = ( s_hunkTotal - hunk_high.permanent ) >> 2;
	j = hunk_high.permanent >> 2;
	for (  ; i < j ; i+=64 ) {			// only need to touch each page
		sum += ((int *)s_hunkData)[i];
	}

	for (block = mainzone->blocklist.next ; ; block = block->next) {
		if ( block->tag ) {
			j = block->size >> 2;
			for ( i = 0 ; i < j ; i+=64 ) {				// only need to touch each page
				sum += ((int *)block)[i];
			}
		}
		if ( block->next == &mainzone->blocklist ) {
			break;			// all blocks have been hit
		}
	}

	int end = Sys_Milliseconds();

	Com_Printf( "Com_TouchMemory: %i msec\n", end - start );
}


static void Com_InitSmallZoneMemory()
{
	const int s_smallZoneTotal = 512 * 1024;

	smallzone = (memzone_t*)calloc( s_smallZoneTotal, 1 );
	if ( !smallzone )
		Com_Error( ERR_FATAL, "Small zone data failed to allocate %1.1f megs", (float)s_smallZoneTotal / (1024*1024) );

	Z_ClearZone( smallzone, s_smallZoneTotal );
}


static void Com_InitZoneMemory()
{
	//FIXME: 05/01/06 com_zoneMegs is useless right now as neither q3config.cfg nor
	// Com_StartupVariable have been executed by this point. The net result is that
	// s_zoneTotal will always be set to the default value.

	// allocate the random block zone
	const cvar_t* cv = Cvar_Get( "com_zoneMegs", DEF_COMZONEMEGS_S, CVAR_LATCH | CVAR_ARCHIVE );
	s_zoneTotal = 1024 * 1024 * max( DEF_COMZONEMEGS, cv->integer );

	mainzone = (memzone_t*)calloc( s_zoneTotal, 1 );
	if ( !mainzone )
		Com_Error( ERR_FATAL, "Zone data failed to allocate %i megs", s_zoneTotal / (1024*1024) );

	Z_ClearZone( mainzone, s_zoneTotal );
}


#ifdef HUNK_DEBUG

static void Hunk_Log( void )
{
	char buf[4096];
	const hunkblock_t* block;
	int size = 0, numBlocks = 0;

	if (!logfile || !FS_Initialized())
		return;

	Com_sprintf(buf, sizeof(buf), "\r\n================\r\nHunk log\r\n================\r\n");
	FS_Write(buf, strlen(buf), logfile);

	for (block = hunkblocks ; block; block = block->next) {
		Com_sprintf(buf, sizeof(buf), "size = %8d: %s, line: %d (%s)\r\n", block->size, block->file, block->line, block->label);
		FS_Write(buf, strlen(buf), logfile);
		size += block->size;
		numBlocks++;
	}

	Com_sprintf(buf, sizeof(buf), "%d Hunk memory\r\n", size);
	FS_Write(buf, strlen(buf), logfile);
	Com_sprintf(buf, sizeof(buf), "%d hunk blocks\r\n", numBlocks);
	FS_Write(buf, strlen(buf), logfile);
}

void Hunk_SmallLog( void )
{
	hunkblock_t	*block, *block2;
	char		buf[4096];
	int size, locsize, numBlocks;

	if (!logfile || !FS_Initialized())
		return;
	for (block = hunkblocks ; block; block = block->next) {
		block->printed = qfalse;
	}
	size = 0;
	numBlocks = 0;
	Com_sprintf(buf, sizeof(buf), "\r\n================\r\nHunk Small log\r\n================\r\n");
	FS_Write(buf, strlen(buf), logfile);
	for (block = hunkblocks; block; block = block->next) {
		if (block->printed) {
			continue;
		}
		locsize = block->size;
		for (block2 = block->next; block2; block2 = block2->next) {
			if (block->line != block2->line) {
				continue;
			}
			if (Q_stricmp(block->file, block2->file)) {
				continue;
			}
			size += block2->size;
			locsize += block2->size;
			block2->printed = qtrue;
		}

		Com_sprintf(buf, sizeof(buf), "size = %8d: %s, line: %d (%s)\r\n", locsize, block->file, block->line, block->label);
		FS_Write(buf, strlen(buf), logfile);

		size += block->size;
		numBlocks++;
	}

	Com_sprintf(buf, sizeof(buf), "%d Hunk memory\r\n", size);
	FS_Write(buf, strlen(buf), logfile);
	Com_sprintf(buf, sizeof(buf), "%d hunk blocks\r\n", numBlocks);
	FS_Write(buf, strlen(buf), logfile);
}

#endif


static void Com_InitHunkMemory()
{
	// make sure the file system has allocated and "not" freed any temp blocks
	// this allows the config and product id files ( journal files too ) to be loaded
	// by the file system without redundant routines in the FS utilizing different memory systems
	if (FS_LoadStack() != 0) {
		Com_Error( ERR_FATAL, "Hunk initialization failed. File system load stack not zero" );
	}

	// allocate the stack based hunk allocator
	const cvar_t* cv = Cvar_Get( "com_hunkMegs", DEF_COMHUNKMEGS_S, CVAR_LATCH | CVAR_ARCHIVE );

	// if we are not dedicated min allocation is 56, otherwise min is 1
	int nMinAlloc;
	const char* s;
	if (com_dedicated && com_dedicated->integer) {
		nMinAlloc = MIN_DEDICATED_COMHUNKMEGS;
		s = "Minimum com_hunkMegs for a dedicated server is %i, allocating %i megs.\n";
	}
	else {
		nMinAlloc = MIN_COMHUNKMEGS;
		s = "Minimum com_hunkMegs is %i, allocating %i megs.\n";
	}

	if ( cv->integer < nMinAlloc ) {
		s_hunkTotal = 1024 * 1024 * nMinAlloc;
		Com_Printf( s, nMinAlloc, s_hunkTotal / (1024 * 1024) );
	} else {
		s_hunkTotal = cv->integer * 1024 * 1024;
	}

	s_hunkData = (byte*)calloc( s_hunkTotal + 31, 1 );
	if ( !s_hunkData ) {
		Com_Error( ERR_FATAL, "Hunk data failed to allocate %i megs", s_hunkTotal / (1024*1024) );
	}
	// cacheline align
	s_hunkData = (byte *) ( ( (intptr_t)s_hunkData + 31 ) & ~31 );
	Hunk_Clear();

	Cmd_AddCommand( "meminfo", Com_Meminfo_f );
#ifdef ZONE_DEBUG
	Cmd_AddCommand( "zonelog", Z_LogHeap );
#endif
#ifdef HUNK_DEBUG
	Cmd_AddCommand( "hunklog", Hunk_Log );
	Cmd_AddCommand( "hunksmalllog", Hunk_SmallLog );
#endif
}


int Hunk_MemoryRemaining()
{
	int low = hunk_low.permanent > hunk_low.temp ? hunk_low.permanent : hunk_low.temp;
	int high = hunk_high.permanent > hunk_high.temp ? hunk_high.permanent : hunk_high.temp;

	return s_hunkTotal - ( low + high );
}


// the server calls this after the level and game VM have been loaded

void Hunk_SetMark()
{
	hunk_low.mark = hunk_low.permanent;
	hunk_high.mark = hunk_high.permanent;
}


// the client calls this before starting a vid_restart or snd_restart

void Hunk_ClearToMark()
{
	hunk_low.permanent = hunk_low.temp = hunk_low.mark;
	hunk_high.permanent = hunk_high.temp = hunk_high.mark;
}


// the bot code uses this for no good reason FAICT

qbool Hunk_CheckMark()
{
	return ( hunk_low.mark || hunk_high.mark );
}


// the server calls this before shutting down or loading a new map

void Hunk_Clear()
{
	extern void CL_ShutdownCGame();
	extern void CL_ShutdownUI();
	extern void SV_ShutdownGameProgs();

#ifndef DEDICATED
	CL_ShutdownCGame();
	CL_ShutdownUI();
#endif

	SV_ShutdownGameProgs();

#ifndef DEDICATED
	CIN_CloseAllVideos();
#endif

	hunk_low.mark = 0;
	hunk_low.permanent = 0;
	hunk_low.temp = 0;
	hunk_low.tempHighwater = 0;

	hunk_high.mark = 0;
	hunk_high.permanent = 0;
	hunk_high.temp = 0;
	hunk_high.tempHighwater = 0;

	hunk_permanent = &hunk_low;
	hunk_temp = &hunk_high;

	VM_Clear();

#ifdef HUNK_DEBUG
	hunkblocks = NULL;
#endif
}


static void Hunk_SwapBanks()
{
	// can't swap banks if there is any temp already allocated
	if ( hunk_temp->temp != hunk_temp->permanent ) {
		return;
	}

	// if we have a larger highwater mark on this side, start making
	// our permanent allocations here and use the other side for temp
	if ( hunk_temp->tempHighwater - hunk_temp->permanent >
			hunk_permanent->tempHighwater - hunk_permanent->permanent ) {
		hunkUsed_t* swap = hunk_temp;
		hunk_temp = hunk_permanent;
		hunk_permanent = swap;
	}
}


// allocate permanent (until the hunk is cleared) memory

#ifdef HUNK_DEBUG
void *Hunk_AllocDebug( int size, ha_pref preference, char *label, char *file, int line )
#else
void *Hunk_Alloc( int size, ha_pref preference )
#endif
{
	if (!s_hunkData) {
		Com_Error( ERR_FATAL, "Hunk_Alloc: Hunk memory system not initialized" );
	}

	// can't do preference if there is any temp allocated
	if (preference == h_dontcare || hunk_temp->temp != hunk_temp->permanent) {
		Hunk_SwapBanks();
	} else {
		if (preference == h_low && hunk_permanent != &hunk_low) {
			Hunk_SwapBanks();
		} else if (preference == h_high && hunk_permanent != &hunk_high) {
			Hunk_SwapBanks();
		}
	}

#ifdef HUNK_DEBUG
	size += sizeof(hunkblock_t);
#endif

	// round to cacheline
	size = (size+31)&~31;

	if ( hunk_low.temp + hunk_high.temp + size > s_hunkTotal ) {
#ifdef HUNK_DEBUG
		Hunk_Log();
		Hunk_SmallLog();
#endif
		Com_Error( ERR_DROP, "Hunk_Alloc failed on %i", size );
	}

	void* buf;
	if ( hunk_permanent == &hunk_low ) {
		buf = (void *)(s_hunkData + hunk_permanent->permanent);
		hunk_permanent->permanent += size;
	} else {
		hunk_permanent->permanent += size;
		buf = (void *)(s_hunkData + s_hunkTotal - hunk_permanent->permanent );
	}

	hunk_permanent->temp = hunk_permanent->permanent;

	Com_Memset( buf, 0, size );

#ifdef HUNK_DEBUG
	{
		hunkblock_t* block = (hunkblock_t*)buf;
		block->size = size - sizeof(hunkblock_t);
		block->file = file;
		block->label = label;
		block->line = line;
		block->next = hunkblocks;
		hunkblocks = block;
		buf = ((byte *) buf) + sizeof(hunkblock_t);
	}
#endif

	return buf;
}


/*
This is used by the file loading system.
Multiple files can be loaded in temporary memory.
When the files-in-use count reaches zero, all temp memory will be deleted
*/
void* Hunk_AllocateTempMemory( int size )
{
	// return a Z_Malloc'd block if the hunk has not been initialized
	// this allows the config and product id files ( journal files too ) to be loaded
	// by the file system without redundant routines in the file system utilizing different
	// memory systems
	if (!s_hunkData) {
		return Z_Malloc(size);
	}

	Hunk_SwapBanks();

	size = PAD(size, sizeof(intptr_t)) + sizeof( hunkHeader_t );

	if ( hunk_temp->temp + hunk_permanent->permanent + size > s_hunkTotal ) {
		Com_Error( ERR_DROP, "Hunk_AllocateTempMemory: failed on %i", size );
	}

	void* buf;
	if ( hunk_temp == &hunk_low ) {
		buf = (void *)(s_hunkData + hunk_temp->temp);
		hunk_temp->temp += size;
	} else {
		hunk_temp->temp += size;
		buf = (void *)(s_hunkData + s_hunkTotal - hunk_temp->temp );
	}

	if ( hunk_temp->temp > hunk_temp->tempHighwater ) {
		hunk_temp->tempHighwater = hunk_temp->temp;
	}

	hunkHeader_t* hdr = (hunkHeader_t*)buf;
	buf = (void*)(hdr+1);

	hdr->magic = HUNK_MAGIC_INUSE;
	hdr->size = size;

	// don't bother clearing, because we are going to load a file over it
	return buf;
}


void Hunk_FreeTempMemory( void* buf )
{
	// free with Z_Free if the hunk has not been initialized
	// this allows the config and product id files ( journal files too ) to be loaded
	// by the file system without redunant routines in the file system utilizing different 
	// memory systems
	if (!s_hunkData) {
		Z_Free(buf);
		return;
	}

	hunkHeader_t* hdr = ( (hunkHeader_t *)buf ) - 1;
	if ( hdr->magic != HUNK_MAGIC_INUSE ) {
		Com_Error( ERR_FATAL, "Hunk_FreeTempMemory: bad magic" );
	}

	hdr->magic = HUNK_MAGIC_FREED;

	// this only works if the files are freed in stack order,
	// otherwise the memory will stay around until Hunk_ClearTempMemory
	if ( hunk_temp == &hunk_low ) {
		if ( hdr == (void *)(s_hunkData + hunk_temp->temp - hdr->size ) ) {
			hunk_temp->temp -= hdr->size;
		} else {
			Com_Printf( "Hunk_FreeTempMemory: not the final block\n" );
		}
	} else {
		if ( hdr == (void *)(s_hunkData + s_hunkTotal - hunk_temp->temp ) ) {
			hunk_temp->temp -= hdr->size;
		} else {
			Com_Printf( "Hunk_FreeTempMemory: not the final block\n" );
		}
	}
}


/*
The temp space is no longer needed.
If we have left more touched but unused memory on this side,
have future permanent allocs use this side.
*/
void Hunk_ClearTempMemory()
{
	if ( s_hunkData ) {
		hunk_temp->temp = hunk_temp->permanent;
	}
}


/*
===================================================================

EVENTS AND JOURNALING

In addition to these events, .cfg files are also copied to the
journaled file
===================================================================
*/

// FIXME TTimo blunt upping from 256 to 1024
// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=5
#define MAX_PUSHED_EVENTS	1024
static sysEvent_t com_pushedEvents[MAX_PUSHED_EVENTS];
static int com_pushedEventsHead;
static int com_pushedEventsTail;


static void Com_InitJournaling()
{
	Com_StartupVariable( "journal" );
	com_journal = Cvar_Get ("journal", "0", CVAR_INIT);
	if ( !com_journal->integer ) {
		return;
	}

	if ( com_journal->integer == 1 ) {
		Com_Printf( "Journaling events\n");
		com_journalFile = FS_FOpenFileWrite( "journal.dat" );
		com_journalDataFile = FS_FOpenFileWrite( "journaldata.dat" );
	} else if ( com_journal->integer == 2 ) {
		Com_Printf( "Replaying journaled events\n");
		FS_FOpenFileRead( "journal.dat", &com_journalFile, qtrue );
		FS_FOpenFileRead( "journaldata.dat", &com_journalDataFile, qtrue );
	}

	if ( !com_journalFile || !com_journalDataFile ) {
		Cvar_Set( "com_journal", "0" );
		com_journalFile = 0;
		com_journalDataFile = 0;
		Com_Printf( "Couldn't open journal files\n" );
	}
}


static sysEvent_t Com_GetRealEvent()
{
	int			r;
	sysEvent_t	ev;

	// get an event from either the system or the journal file
	if ( com_journal->integer == 2 ) {
		r = FS_Read( &ev, sizeof(ev), com_journalFile );
		if ( r != sizeof(ev) ) {
			Com_Error( ERR_FATAL, "Error reading from journal file" );
		}
		if ( ev.evPtrLength ) {
			ev.evPtr = Z_Malloc( ev.evPtrLength );
			r = FS_Read( ev.evPtr, ev.evPtrLength, com_journalFile );
			if ( r != ev.evPtrLength ) {
				Com_Error( ERR_FATAL, "Error reading from journal file" );
			}
		}
	} else {
		ev = Sys_GetEvent();

		// write the journal value out if needed
		if ( com_journal->integer == 1 ) {
			r = FS_Write( &ev, sizeof(ev), com_journalFile );
			if ( r != sizeof(ev) ) {
				Com_Error( ERR_FATAL, "Error writing to journal file" );
			}
			if ( ev.evPtrLength ) {
				r = FS_Write( ev.evPtr, ev.evPtrLength, com_journalFile );
				if ( r != ev.evPtrLength ) {
					Com_Error( ERR_FATAL, "Error writing to journal file" );
				}
			}
		}
	}

	return ev;
}


static void Com_PushEvent( const sysEvent_t* event )
{
	static qbool printedWarning = qfalse;

	sysEvent_t* ev = &com_pushedEvents[ com_pushedEventsHead & (MAX_PUSHED_EVENTS-1) ];

	if ( com_pushedEventsHead - com_pushedEventsTail >= MAX_PUSHED_EVENTS ) {

		// don't print the warning constantly, or it can give time for more...
		if ( !printedWarning ) {
			printedWarning = qtrue;
			Com_Printf( "WARNING: Com_PushEvent overflow\n" );
		}

		if ( ev->evPtr ) {
			Z_Free( ev->evPtr );
		}
		com_pushedEventsTail++;
	} else {
		printedWarning = qfalse;
	}

	*ev = *event;
	com_pushedEventsHead++;
}


static sysEvent_t Com_GetEvent()
{
	if ( com_pushedEventsHead > com_pushedEventsTail ) {
		com_pushedEventsTail++;
		return com_pushedEvents[ (com_pushedEventsTail-1) & (MAX_PUSHED_EVENTS-1) ];
	}
	return Com_GetRealEvent();
}


static void Com_RunAndTimeServerPacket( const netadr_t& from, msg_t* msg )
{
	int t1 = (com_speeds->integer == 3) ? Sys_Milliseconds() : 0;

	SV_PacketEvent( from, msg );

	if (com_speeds->integer == 3) {
		int ms = Sys_Milliseconds() - t1;
		Com_Printf( "SV_PacketEvent time: %i\n", ms );
	}
}


// returns last event time

int Com_EventLoop()
{
	sysEvent_t	ev;
	netadr_t	evFrom;
	byte		bufData[MAX_MSGLEN];
	msg_t		buf;

	MSG_Init( &buf, bufData, sizeof( bufData ) );

	while ( 1 ) {
		NET_FlushPacketQueue();
		ev = Com_GetEvent();

		// if no more events are available
		if ( ev.evType == SE_NONE ) {
			// manually send packet events for the loopback channel
			while ( NET_GetLoopPacket( NS_CLIENT, &evFrom, &buf ) ) {
				CL_PacketEvent( evFrom, &buf );
			}

			while ( NET_GetLoopPacket( NS_SERVER, &evFrom, &buf ) ) {
				// if the server just shut down, flush the events
				if ( com_sv_running->integer ) {
					Com_RunAndTimeServerPacket( evFrom, &buf );
				}
			}

			return ev.evTime;
		}


		switch ( ev.evType ) {
		default:
			Com_Error( ERR_FATAL, "Com_EventLoop: bad event type %i", ev.evType );
			break;
		case SE_NONE:
			break;
		case SE_KEY:
			CL_KeyEvent( ev.evValue, (ev.evValue2 != 0), ev.evTime );
			break;
		case SE_CHAR:
			CL_CharEvent( ev.evValue );
			break;
		case SE_MOUSE:
			CL_MouseEvent( ev.evValue, ev.evValue2, ev.evTime );
			break;
		case SE_JOYSTICK_AXIS:
			CL_JoystickEvent( ev.evValue, ev.evValue2, ev.evTime );
			break;
		case SE_CONSOLE:
			Cbuf_AddText( (char *)ev.evPtr );
			Cbuf_AddText( "\n" );
			break;
		case SE_PACKET:
			evFrom = *(netadr_t *)ev.evPtr;
			buf.cursize = ev.evPtrLength - sizeof( evFrom );

			// we must copy the contents of the message out, because
			// the event buffers are only large enough to hold the
			// exact payload, but channel messages need to be large
			// enough to hold fragment reassembly
			if ( (unsigned)buf.cursize > buf.maxsize ) {
				Com_Printf("Com_EventLoop: oversize packet\n");
				continue;
			}
			Com_Memcpy( buf.data, (byte *)((netadr_t *)ev.evPtr + 1), buf.cursize );
			if ( com_sv_running->integer ) {
				Com_RunAndTimeServerPacket( evFrom, &buf );
			} else {
				CL_PacketEvent( evFrom, &buf );
			}
			break;
		}

		// free any block data
		if ( ev.evPtr ) {
			Z_Free( ev.evPtr );
		}
	}

	return 0;	// never reached
}


// can be used for profiling, but will be journaled accurately

int Com_Milliseconds()
{
	sysEvent_t ev;

	// get events and push them until we get a null event with the current time
	do {
		ev = Com_GetRealEvent();
		if ( ev.evType != SE_NONE ) {
			Com_PushEvent( &ev );
		}
	} while ( ev.evType != SE_NONE );

	return ev.evTime;
}


///////////////////////////////////////////////////////////////


// throw a fatal error to test error shutdown procedures

static void Com_Error_f( void )
{
	if ( Cmd_Argc() > 1 ) {
		Com_Error( ERR_DROP, "Testing drop error" );
	} else {
		Com_Error( ERR_FATAL, "Testing fatal error" );
	}
}


// freeze in place for a given number of seconds to test error recovery

static void Com_Freeze_f( void )
{
	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "freeze <seconds>\n" );
		return;
	}

	float s = atof( Cmd_Argv(1) );

	int start = Com_Milliseconds();
	while ( ( Com_Milliseconds() - start ) * 0.001 < s )
		;
}


// force a bus error for development reasons

static void Com_Crash_f( void )
{
	*(int*)0 = 0x12345678;
}


// TTimo: centralizing the cl_cdkey stuff after I discovered a buffer overflow problem with the dedicated server version
//   not sure it's necessary to have different defaults for regular and dedicated, but I don't want to risk it
//   https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=470
#ifndef DEDICATED
char	cl_cdkey[34] = "                                ";
#else
char	cl_cdkey[34] = "123456789";
#endif

/*
=================
Com_ReadCDKey
=================
*/
qbool CL_CDKeyValidate( const char *key, const char *checksum );
void Com_ReadCDKey( const char *filename ) {
	fileHandle_t	f;
	char			buffer[33];
	char			fbuffer[MAX_OSPATH];

	sprintf(fbuffer, "%s/q3key", filename);

	FS_SV_FOpenFileRead( fbuffer, &f );
	if ( !f ) {
		Q_strncpyz( cl_cdkey, "                ", 17 );
		return;
	}

	Com_Memset( buffer, 0, sizeof(buffer) );

	FS_Read( buffer, 16, f );
	FS_FCloseFile( f );

	if (CL_CDKeyValidate(buffer, NULL)) {
		Q_strncpyz( cl_cdkey, buffer, 17 );
	} else {
		Q_strncpyz( cl_cdkey, "                ", 17 );
	}
}

/*
=================
Com_AppendCDKey
=================
*/
void Com_AppendCDKey( const char *filename ) {
	fileHandle_t	f;
	char			buffer[33];
	char			fbuffer[MAX_OSPATH];

	sprintf(fbuffer, "%s/q3key", filename);

	FS_SV_FOpenFileRead( fbuffer, &f );
	if (!f) {
		Q_strncpyz( &cl_cdkey[16], "                ", 17 );
		return;
	}

	Com_Memset( buffer, 0, sizeof(buffer) );

	FS_Read( buffer, 16, f );
	FS_FCloseFile( f );

	if (CL_CDKeyValidate(buffer, NULL)) {
		strcat( &cl_cdkey[16], buffer );
	} else {
		Q_strncpyz( &cl_cdkey[16], "                ", 17 );
	}
}


/*
#ifndef DEDICATED
static void Com_WriteCDKey( const char *filename, const char *ikey )
{
	fileHandle_t	f;
	char			fbuffer[MAX_OSPATH];
	char			key[17];
#ifndef _WIN32
	mode_t			savedumask;
#endif

	sprintf(fbuffer, "%s/q3key", filename);

	Q_strncpyz( key, ikey, 17 );

	if(!CL_CDKeyValidate(key, NULL) ) {
		return;
	}

#ifndef _WIN32
	savedumask = umask(0077);
#endif
	f = FS_SV_FOpenFileWrite( fbuffer );
	if ( !f ) {
		Com_Printf ("Couldn't write CD key to %s.\n", fbuffer );
		goto out;
	}

	FS_Write( key, 16, f );

	FS_Printf( f, "\n// generated by quake, do not modify\r\n" );
	FS_Printf( f, "// Do not give this file to ANYONE.\r\n" );
	FS_Printf( f, "// id Software and Activision will NOT ask you to send this file to them.\r\n");

	FS_FCloseFile( f );
out:
#ifndef _WIN32
	umask(savedumask);
#endif
	return;
}
#endif
*/


#if defined(_MSC_VER)
#pragma warning (disable: 4611) // setjmp + destructors = bad. which it is, but...
#endif

void Com_Init( char *commandLine )
{
	Com_Printf( "%s %s %s\n", Q3_VERSION, PLATFORM_STRING, __DATE__ );

	if ( setjmp(abortframe) ) {
		Sys_Error ("Error during initialization");
	}

	memset( com_pushedEvents, 0, sizeof(com_pushedEvents) );
	com_pushedEventsHead = 0;
	com_pushedEventsTail = 0;

	Com_InitSmallZoneMemory();
	Cvar_Init();

	// prepare enough of the subsystems to handle
	// cvar and command buffer management
	Com_ParseCommandLine( commandLine );

	Cbuf_Init();

	Com_InitZoneMemory();
	Cmd_Init();

	// override anything from the config files with command line args
	Com_StartupVariable( NULL );

	// get the developer cvar set as early as possible
	Com_StartupVariable( "developer" );

	// done early so bind command exists
	CL_InitKeyCommands();

	FS_InitFilesystem();

	Com_InitJournaling();

	Cbuf_AddText("exec default.cfg\n");

	// skip the q3config.cfg if "safe" is on the command line
	if ( !Com_SafeMode() )
		Cbuf_AddText("exec q3config.cfg\n");

	Cbuf_AddText("exec autoexec.cfg\n");

	Cbuf_Execute();

	// override anything from the config files with command line args
	Com_StartupVariable( NULL );

	// get dedicated here for proper hunk megs initialization
#ifdef DEDICATED
	com_dedicated = Cvar_Get("dedicated", "1", CVAR_ROM);
#else
	com_dedicated = Cvar_Get("dedicated", "0", CVAR_LATCH);
#endif
	// allocate the stack based hunk allocator
	Com_InitHunkMemory();

	// if any archived cvars are modified after this, we will trigger a writing
	// of the config file
	cvar_modifiedFlags &= ~CVAR_ARCHIVE;

	//
	// init commands and vars
	//
	com_maxfps = Cvar_Get ("com_maxfps", "85", CVAR_ARCHIVE);

	com_developer = Cvar_Get ("developer", "0", CVAR_TEMP );
	com_logfile = Cvar_Get ("logfile", "0", CVAR_TEMP );

	com_timescale = Cvar_Get ("timescale", "1", CVAR_CHEAT | CVAR_SYSTEMINFO );
	com_fixedtime = Cvar_Get ("fixedtime", "0", CVAR_CHEAT);
	com_showtrace = Cvar_Get ("com_showtrace", "0", CVAR_CHEAT);
	com_viewlog = Cvar_Get( "viewlog", "0", CVAR_CHEAT );
	com_speeds = Cvar_Get ("com_speeds", "0", 0);
	com_timedemo = Cvar_Get ("timedemo", "0", CVAR_CHEAT);

	cl_paused = Cvar_Get ("cl_paused", "0", CVAR_ROM);
	sv_paused = Cvar_Get ("sv_paused", "0", CVAR_ROM);
	cl_packetdelay = Cvar_Get ("cl_packetdelay", "0", CVAR_CHEAT);
	sv_packetdelay = Cvar_Get ("sv_packetdelay", "0", CVAR_CHEAT);
	com_sv_running = Cvar_Get ("sv_running", "0", CVAR_ROM);
	com_cl_running = Cvar_Get ("cl_running", "0", CVAR_ROM);
	com_buildScript = Cvar_Get( "com_buildScript", "0", 0 );

	com_introPlayed = Cvar_Get( "com_introplayed", "0", CVAR_ARCHIVE );

#if defined(_WIN32) && defined(_DEBUG)
	com_noErrorInterrupt = Cvar_Get( "com_noErrorInterrupt", "0", 0 );
#endif

	if ( com_dedicated->integer ) {
		if ( !com_viewlog->integer ) {
			Cvar_Set( "viewlog", "1" );
		}
	}

	if ( com_developer && com_developer->integer ) {
		Cmd_AddCommand( "error", Com_Error_f );
		Cmd_AddCommand( "crash", Com_Crash_f );
		Cmd_AddCommand( "freeze", Com_Freeze_f );
		//Cmd_AddCommand( "changeVectors", MSG_ReportChangeVectors_f );
	}

	Cmd_AddCommand( "quit", Com_Quit_f );
	Cmd_AddCommand( "writeconfig", Com_WriteConfig_f );

	const char* s = Q3_VERSION" "PLATFORM_STRING" "__DATE__;
	com_version = Cvar_Get( "version", s, CVAR_ROM | CVAR_SERVERINFO );

	Sys_Init();
	Netchan_Init( Com_Milliseconds() & 0xffff );	// pick a port value that should be nice and random
	VM_Init();
	SV_Init();

	com_dedicated->modified = qfalse;
	if ( !com_dedicated->integer ) {
		CL_Init();
		Sys_ShowConsole( com_viewlog->integer, qfalse );
	}

	// set com_frameTime so that if a map is started on the
	// command line it will still be able to count on com_frameTime
	// being random enough for a serverid
	com_frameTime = Com_Milliseconds();

	// add + commands from command line
	if ( !Com_AddStartupCommands() ) {
		// if the user didn't give any commands, run default action
		if ( !com_dedicated->integer ) {
			if ( !com_introPlayed->integer ) {
				Cvar_Set( com_introPlayed->name, "1" );
				Cbuf_AddText( "cinematic idlogo.RoQ\n" );
				Cvar_Set( "nextmap", "cinematic intro.RoQ" );
			}
		}
	}

	// start in full screen ui mode
	Cvar_Set( "r_uiFullScreen", "1" );

	CL_StartHunkUsers();

	// make sure single player is off by default
	Cvar_Set( "sv_singlePlayer", "0" );

	com_fullyInitialized = qtrue;

	Com_Printf ("--- Common Initialization Complete ---\n");
}


//==================================================================


static void Com_WriteConfigToFile( const char* filename )
{
	fileHandle_t f = FS_FOpenFileWrite( filename );
	if ( !f ) {
		Com_Printf ("Couldn't write %s.\n", filename );
		return;
	}

	FS_Printf(f, "// generated by quake, do not modify\n");
	Key_WriteBindings(f);
	Cvar_WriteVariables(f);
	FS_FCloseFile( f );
}


// write the config file to a specific name

static void Com_WriteConfig_f()
{
	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "Usage: writeconfig <filename>\n" );
		return;
	}

	char filename[MAX_QPATH];
	Q_strncpyz( filename, Cmd_Argv(1), sizeof( filename ) );
	COM_DefaultExtension( filename, sizeof( filename ), ".cfg" );
	Com_Printf( "Writing %s.\n", filename );
	Com_WriteConfigToFile( filename );
}


static int Com_ModifyMsec( int msec )
{
	// modify time for debugging values
	//
	if ( com_fixedtime->integer ) {
		msec = com_fixedtime->integer;
	} else if ( com_timescale->value ) {
		msec *= com_timescale->value;
	}

	// don't let it scale below 1 msec
	if ( msec < 1 && com_timescale->value) {
		msec = 1;
	}

	int clampTime;
	if ( com_dedicated->integer ) {
		// dedicated servers don't want to clamp for too long a period
		// because it would mess up all the client's views of time
		if ( msec > 500 ) {
			Com_Printf( "Hitch warning: %i msec frame time\n", msec );
		}
		clampTime = 5000;
	}
	else if ( !com_sv_running->integer ) {
		// clients of remote servers do not want to clamp time, because
		// it would skew their view of the server's time temporarily
		clampTime = 5000;
	}
	else {
		// for local single player gaming
		// we may want to clamp the time to prevent players from
		// flying off edges when something hitches.
		clampTime = 200;
	}

	if ( msec > clampTime ) {
		msec = clampTime;
	}

	return msec;
}


void Com_Frame()
{
	if ( setjmp(abortframe) ) {
		return;			// an ERR_DROP was thrown
	}

	// bk001204 - init to zero.
	//  also:  might be clobbered by `longjmp' or `vfork'
	int timeBeforeFirstEvents =0;
	int timeBeforeServer =0;
	int timeBeforeEvents =0;
	int timeBeforeClient = 0;
	int timeAfter = 0;

	// if "viewlog" has been modified, show or hide the log console
	if ( com_viewlog->modified ) {
		if ( !com_dedicated->value ) {
			Sys_ShowConsole( com_viewlog->integer, qfalse );
		}
		com_viewlog->modified = qfalse;
	}

	//
	// main event loop
	//
	if ( com_speeds->integer ) {
		timeBeforeFirstEvents = Sys_Milliseconds();
	}

	// we may want to spin here if things are going too fast
	int minMsec = 1;
	if ( !com_dedicated->integer && com_maxfps->integer > 0 && !com_timedemo->integer )
		minMsec = 1000 / com_maxfps->integer;

	static int lastTime = 0;
	int msec;
	do {
		com_frameTime = Com_EventLoop();
		msec = com_frameTime - lastTime;
	} while ( msec < minMsec );
	lastTime = com_frameTime;

	Cbuf_Execute();

	// mess with msec if needed
	msec = Com_ModifyMsec( msec );

	//
	// server side
	//
	if ( com_speeds->integer ) {
		timeBeforeServer = Sys_Milliseconds();
	}

	SV_Frame( msec );

	// if "dedicated" has been modified, start up
	// or shut down the client system.
	// Do this after the server may have started,
	// but before the client tries to auto-connect
	if ( com_dedicated->modified ) {
		// get the latched value
		Cvar_Get( "dedicated", "0", 0 );
		com_dedicated->modified = qfalse;
		if ( !com_dedicated->integer ) {
			CL_Init();
			Sys_ShowConsole( com_viewlog->integer, qfalse );
		} else {
			CL_Shutdown();
			Sys_ShowConsole( 1, qtrue );
		}
	}

	//
	// client system
	//
	if ( !com_dedicated->integer ) {
		//
		// run event loop a second time to get server to client packets
		// without a frame of latency
		//
		if ( com_speeds->integer ) {
			timeBeforeEvents = Sys_Milliseconds();
		}
		Com_EventLoop();
		Cbuf_Execute();

		//
		// client side
		//
		if ( com_speeds->integer ) {
			timeBeforeClient = Sys_Milliseconds();
		}

		CL_Frame( msec );

		if ( com_speeds->integer ) {
			timeAfter = Sys_Milliseconds();
		}
	}

	//
	// report timing information
	//
	if ( com_speeds->integer ) {
		int all = timeAfter - timeBeforeServer;
		int sv = timeBeforeEvents - timeBeforeServer - time_game;
		int ev = timeBeforeServer - timeBeforeFirstEvents + timeBeforeClient - timeBeforeEvents;
		int cl = timeAfter - timeBeforeClient - (time_frontend + time_backend);
		Com_Printf( "frame:%i all:%3i sv:%3i ev:%3i cl:%3i gm:%3i rf:%3i bk:%3i\n",
				com_frameNumber, all, sv, ev, cl, time_game, time_frontend, time_backend );
	}

	//
	// trace optimization tracking
	//
	if ( com_showtrace->integer ) {
		extern int c_traces, c_brush_traces, c_patch_traces, c_pointcontents;
		Com_Printf( "%4i traces  (%ib %ip) %4i points\n",
				c_traces, c_brush_traces, c_patch_traces, c_pointcontents );
		c_traces = 0;
		c_brush_traces = 0;
		c_patch_traces = 0;
		c_pointcontents = 0;
	}

	com_frameNumber++;
}


void Com_Shutdown()
{
	if (logfile) {
		FS_FCloseFile( logfile );
		logfile = 0;
	}

	if ( com_journalFile ) {
		FS_FCloseFile( com_journalFile );
		com_journalFile = 0;
	}
}


///////////////////////////////////////////////////////////////


/*
=====================
KHB !!!  see if this is still true - even vc might actually have a bug fixed in 8 years  :P

the msvc acos doesn't always return a value between -PI and PI:

int i;
i = 1065353246;
acos(*(float*) &i) == -1.#IND0

	This should go in q_math but it is too late to add new traps
	to game and ui
=====================
*/
float Q_acos(float c)
{
	float angle = acos(c);

	if (angle > M_PI) {
		return (float)M_PI;
	}
	if (angle < -M_PI) {
		return (float)M_PI;
	}
	return angle;
}


/*
===========================================
command line completion
===========================================
*/


void Field_Clear( field_t* edit )
{
	memset(edit->buffer, 0, MAX_EDIT_LINE);
	edit->cursor = 0;
	edit->scroll = 0;
}

static const char* completionString;
static char shortestMatch[MAX_TOKEN_CHARS];
static int	matchCount;
// field we are working on, passed to Field_AutoComplete(&g_consoleCommand for instance)
static field_t* completionField;

static void FindMatches( const char *s )
{
	int		i;

	if ( Q_stricmpn( s, completionString, strlen( completionString ) ) ) {
		return;
	}
	matchCount++;
	if ( matchCount == 1 ) {
		Q_strncpyz( shortestMatch, s, sizeof( shortestMatch ) );
		return;
	}

	// cut shortestMatch to the amount common with s
	for ( i = 0 ; shortestMatch[i] ; i++ ) {
		if ( i >= strlen( s ) ) {
			shortestMatch[i] = 0;
			break;
		}

		if ( tolower(shortestMatch[i]) != tolower(s[i]) ) {
			shortestMatch[i] = 0;
		}
	}
}


static void PrintMatches( const char *s )
{
	if ( !Q_stricmpn( s, shortestMatch, strlen( shortestMatch ) ) ) {
		Com_Printf( "    %s\n", s );
	}
}


static void PrintCvarMatches( const char *s )
{
	if ( !Q_stricmpn( s, shortestMatch, strlen( shortestMatch ) ) ) {
		Com_Printf( "    %s = \"%s\"\n", s, Cvar_VariableString( s ) );
	}
}


#if I_EVER_NAG_TIMBO_INTO_FIXING_THIS


/*
==================
Com_CharIsOneOfCharset
==================
*/
static qbool Com_CharIsOneOfCharset( char c, char *set )
{
	int i;

	for( i = 0; i < strlen( set ); i++ )
	{
		if( set[ i ] == c )
			return qtrue;
	}

	return qfalse;
}

/*
==================
Com_SkipCharset
==================
*/
char *Com_SkipCharset( char *s, char *sep )
{
	char	*p = s;

	while( p )
	{
		if( Com_CharIsOneOfCharset( *p, sep ) )
			p++;
		else
			break;
	}

	return p;
}

/*
==================
Com_SkipTokens
==================
*/
char *Com_SkipTokens( char *s, int numTokens, char *sep )
{
	int		sepCount = 0;
	char	*p = s;

	while( sepCount < numTokens )
	{
		if( Com_CharIsOneOfCharset( *p++, sep ) )
		{
			sepCount++;
			while( Com_CharIsOneOfCharset( *p, sep ) )
				p++;
		}
		else if( *p == '\0' )
			break;
	}

	if( sepCount == numTokens )
		return p;
	else
		return s;
}

/*
===============
Field_FindFirstSeparator
===============
*/
static char *Field_FindFirstSeparator( char *s )
{
	int i;

	for( i = 0; i < strlen( s ); i++ )
	{
		if( s[ i ] == ';' )
			return &s[ i ];
	}

	return NULL;
}

/*
===============
Field_CompleteFilename
===============
*/
static void Field_CompleteFilename( const char *dir,
		const char *ext, qbool stripExt )
{
	matchCount = 0;
	shortestMatch[ 0 ] = 0;

	FS_FilenameCompletion( dir, ext, stripExt, FindMatches );

	if( matchCount == 0 )
		return;

	Q_strcat( completionField->buffer, sizeof( completionField->buffer ),
			shortestMatch + strlen( completionString ) );
	completionField->cursor = strlen( completionField->buffer );

	if( matchCount == 1 )
	{
		Q_strcat( completionField->buffer, sizeof( completionField->buffer ), " " );
		completionField->cursor++;
		return;
	}

	Com_Printf( "]%s\n", completionField->buffer );
	
	FS_FilenameCompletion( dir, ext, stripExt, PrintMatches );
}

/*
===============
Field_CompleteCommand
===============
*/
static void Field_CompleteCommand( char *cmd,
		qbool doCommands, qbool doCvars )
{
	int		completionArgument = 0;
	char	*p;

	// Skip leading whitespace and quotes
	cmd = Com_SkipCharset( cmd, " \"" );

	Cmd_TokenizeStringIgnoreQuotes( cmd );
	completionArgument = Cmd_Argc( );

	// If there is trailing whitespace on the cmd
	if( *( cmd + strlen( cmd ) - 1 ) == ' ' )
	{
		completionString = "";
		completionArgument++;
	}
	else
		completionString = Cmd_Argv( completionArgument - 1 );

	if( completionArgument > 1 )
	{
		const char *baseCmd = Cmd_Argv( 0 );

#ifndef DEDICATED
		// If the very first token does not have a leading \ or /,
		// refuse to autocomplete
		if( cmd == completionField->buffer )
		{
			if( baseCmd[ 0 ] != '\\' && baseCmd[ 0 ] != '/' )
				return;
			baseCmd++;
		}
#endif

		if( ( p = Field_FindFirstSeparator( cmd ) ) )
		{
			// Compound command
			Field_CompleteCommand( p + 1, qtrue, qtrue );
		}
		else
		{
			// FIXME: all this junk should really be associated with the respective
			// commands, instead of being hard coded here
			if( ( !Q_stricmp( baseCmd, "map" ) ||
						!Q_stricmp( baseCmd, "devmap" ) ||
						!Q_stricmp( baseCmd, "spmap" ) ||
						!Q_stricmp( baseCmd, "spdevmap" ) ) &&
					completionArgument == 2 )
			{
				Field_CompleteFilename( "maps", "bsp", qtrue );
			}
			else if( ( !Q_stricmp( baseCmd, "exec" ) ||
						!Q_stricmp( baseCmd, "writeconfig" ) ) &&
					completionArgument == 2 )
			{
				Field_CompleteFilename( "", "cfg", qfalse );
			}
			else if( !Q_stricmp( baseCmd, "condump" ) &&
					completionArgument == 2 )
			{
				Field_CompleteFilename( "", "txt", qfalse );
			}
			else if( !Q_stricmp( baseCmd, "demo" ) && completionArgument == 2 )
			{
				char demoExt[ 16 ];

				Com_sprintf( demoExt, sizeof( demoExt ), ".dm_%d", PROTOCOL_VERSION );
				Field_CompleteFilename( "demos", demoExt, qtrue );
			}
			else if( ( !Q_stricmp( baseCmd, "toggle" ) ||
						!Q_stricmp( baseCmd, "vstr" ) ||
						!Q_stricmp( baseCmd, "set" ) ||
						!Q_stricmp( baseCmd, "seta" ) ||
						!Q_stricmp( baseCmd, "setu" ) ||
						!Q_stricmp( baseCmd, "sets" ) ) &&
					completionArgument == 2 )
			{
				// Skip "<cmd> "
				p = Com_SkipTokens( cmd, 1, " " );

				if( p > cmd )
					Field_CompleteCommand( p, qfalse, qtrue );
			}
			else if( !Q_stricmp( baseCmd, "rcon" ) && completionArgument == 2 )
			{
				// Skip "rcon "
				p = Com_SkipTokens( cmd, 1, " " );

				if( p > cmd )
					Field_CompleteCommand( p, qtrue, qtrue );
			}
			else if( !Q_stricmp( baseCmd, "bind" ) && completionArgument >= 3 )
			{
				// Skip "bind <key> "
				p = Com_SkipTokens( cmd, 2, " " );

				if( p > cmd )
					Field_CompleteCommand( p, qtrue, qtrue );
			}
		}
	}
	else
	{
		if( completionString[0] == '\\' || completionString[0] == '/' )
			completionString++;

		matchCount = 0;
		shortestMatch[ 0 ] = 0;

		if( strlen( completionString ) == 0 )
			return;

		if( doCommands )
			Cmd_CommandCompletion( FindMatches );

		if( doCvars )
			Cvar_CommandCompletion( FindMatches );

		if( matchCount == 0 )
			return;	// no matches

		if( cmd == completionField->buffer )
		{
#ifndef DEDICATED
			Com_sprintf( completionField->buffer,
					sizeof( completionField->buffer ), "\\%s", shortestMatch );
#else
			Com_sprintf( completionField->buffer,
					sizeof( completionField->buffer ), "%s", shortestMatch );
#endif
		}
		else
		{
			Q_strcat( completionField->buffer, sizeof( completionField->buffer ),
					shortestMatch + strlen( completionString ) );
		}

		completionField->cursor = strlen( completionField->buffer );

		if( matchCount == 1 )
		{
			Q_strcat( completionField->buffer, sizeof( completionField->buffer ), " " );
			completionField->cursor++;
			return;
		}

		Com_Printf( "]%s\n", completionField->buffer );

		// run through again, printing matches
		if( doCommands )
			Cmd_CommandCompletion( PrintMatches );

		if( doCvars )
			Cvar_CommandCompletion( PrintCvarMatches );
	}
}

/*
===============
Field_AutoComplete

Perform Tab expansion
===============
*/
void Field_AutoComplete( field_t *field )
{
	completionField = field;

	Field_CompleteCommand( completionField->buffer, qtrue, qtrue );
}


#else
// use the id tab-completion code, which doesn't have all the nice stuff timbo did
// but has a "familiar" set of bugs rather than a new and thus even-more-hated set

static void keyConcatArgs()
{
	int		i;
	const char* arg;

	for ( i = 1 ; i < Cmd_Argc() ; i++ ) {
		Q_strcat( completionField->buffer, sizeof( completionField->buffer ), " " );
		arg = Cmd_Argv( i );
		while (*arg) {
			if (*arg == ' ') {
				Q_strcat( completionField->buffer, sizeof( completionField->buffer ),  "\"");
				break;
			}
			arg++;
		}
		Q_strcat( completionField->buffer, sizeof( completionField->buffer ),  Cmd_Argv( i ) );
		if (*arg == ' ') {
			Q_strcat( completionField->buffer, sizeof( completionField->buffer ),  "\"");
		}
	}
}

static void ConcatRemaining( const char *src, const char *start )
{
	const char* str = strstr( src, start );

	if (!str) {
		keyConcatArgs();
		return;
	}

	str += strlen( start );
	Q_strcat( completionField->buffer, sizeof( completionField->buffer ), str );
}

/*
perform Tab expansion
NOTE TTimo this was originally client code only
  moved to common code when writing tty console for *nix dedicated server
*/
static void Field_CompleteCommand( field_t *field )
{
	completionField = field;

	// only look at the first token for completion purposes
	Cmd_TokenizeString( completionField->buffer );

	completionString = Cmd_Argv(0);
#ifndef DEDICATED
	if ( completionString[0] == '\\' || completionString[0] == '/' ) {
		completionString++;
	}
#endif
	matchCount = 0;
	shortestMatch[0] = 0;

	if ( *completionString == '\0' ) {
		return;
	}

	Cmd_CommandCompletion( FindMatches );
	Cvar_CommandCompletion( FindMatches );

	if ( matchCount == 0 ) {
		return;	// no matches
	}

	field_t temp;
	Com_Memcpy( &temp, completionField, sizeof(field_t) );

	if ( matchCount == 1 ) {
		Com_sprintf( completionField->buffer, sizeof( completionField->buffer ),
#ifndef DEDICATED
				"\\%s", shortestMatch );
#else
				"%s", shortestMatch );
#endif
		if ( Cmd_Argc() == 1 ) {
			Q_strcat( completionField->buffer, sizeof( completionField->buffer ), " " );
		} else {
			ConcatRemaining( temp.buffer, completionString );
		}
		completionField->cursor = strlen( completionField->buffer );
		return;
	}

	// multiple matches, complete to shortest
	Com_sprintf( completionField->buffer, sizeof( completionField->buffer ),
#ifndef DEDICATED
			"\\%s", shortestMatch );
#else
			"%s", shortestMatch );
#endif
	completionField->cursor = strlen( completionField->buffer );
	ConcatRemaining( temp.buffer, completionString );

	Com_Printf( "]%s\n", completionField->buffer );

	// run through again, printing matches
	Cmd_CommandCompletion( PrintMatches );
	Cvar_CommandCompletion( PrintCvarMatches );
}

void Field_AutoComplete( field_t *field )
{
	Field_CompleteCommand( field );
}

#endif
