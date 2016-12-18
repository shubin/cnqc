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
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <errno.h>

#if (defined(DEDICATED) && defined(USE_SDL_VIDEO))
#undef USE_SDL_VIDEO
#endif

#if USE_SDL_VIDEO
#include "SDL.h"
#include "SDL_loadso.h"
#else
#include <dlfcn.h>
#endif

#if defined(__sun)
  #include <sys/file.h>
#endif

#include <termios.h>

#include "../client/client.h"
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "linux_local.h"


///////////////////////////////////////////////////////////////


static qboolean stdin_active = qtrue;

// enable/disable tty input mode
// NOTE TTimo this is used during startup, cannot be changed during run
static cvar_t *ttycon = NULL;
// general flag to tell about tty console mode
static qboolean ttycon_on = qfalse;
// when printing general stuff to stdout stderr (Sys_Printf)
//   we need to disable the tty console stuff
// this increments so we can recursively disable
static int ttycon_hide = 0;
// some key codes that the terminal may be using
// TTimo NOTE: I'm not sure how relevant this is
static int tty_erase;
static int tty_eof;

static struct termios tty_tc;

static field_t tty_con;

static cvar_t *ttycon_ansicolor = NULL;
static qboolean ttycon_color_on = qfalse;

// history
// NOTE TTimo this is a bit duplicate of the graphical console history
//   but it's safer and faster to write our own here
#define TTY_HISTORY 32
static field_t ttyEditLines[TTY_HISTORY];
static int hist_current = -1, hist_count = 0;



// =============================================================
// tty console routines
// NOTE: if the user is editing a line when something gets printed to the early console then it won't look good
//   so we provide tty_Clear and tty_Show to be called before and after a stdout or stderr output
// =============================================================

// flush stdin, I suspect some terminals are sending a LOT of shit
// FIXME TTimo relevant?
static void tty_FlushIn()
{
  char key;
  while (read(0, &key, 1)!=-1);
}

// do a backspace
// TTimo NOTE: it seems on some terminals just sending '\b' is not enough
//   so for now, in any case we send "\b \b" .. yeah well ..
//   (there may be a way to find out if '\b' alone would work though)
static void tty_Back()
{
  char key;
  key = '\b';
  write(1, &key, 1);
  key = ' ';
  write(1, &key, 1);
  key = '\b';
  write(1, &key, 1);
}

// clear the display of the line currently edited
// bring cursor back to beginning of line
static void tty_Hide()
{
  int i;
  assert(ttycon_on);
  if (ttycon_hide)
  {
    ttycon_hide++;
    return;
  }
  if (tty_con.cursor>0)
  {
    for (i=0; i<tty_con.cursor; i++)
    {
      tty_Back();
    }
  }
  ttycon_hide++;
}

// show the current line
// FIXME TTimo need to position the cursor if needed??
static void tty_Show()
{
  int i;
  assert(ttycon_on);
  assert(ttycon_hide>0);
  ttycon_hide--;
  if (ttycon_hide == 0)
  {
    if (tty_con.cursor)
    {
      for (i=0; i<tty_con.cursor; i++)
      {
        write(1, tty_con.buffer+i, 1);
      }
    }
  }
}

static void tty_Hist_Add(field_t *field)
{
  int i;
  assert(hist_count <= TTY_HISTORY);
  assert(hist_count >= 0);
  assert(hist_current >= -1);
  assert(hist_current <= hist_count);
  // make some room
  for (i=TTY_HISTORY-1; i>0; i--)
  {
    ttyEditLines[i] = ttyEditLines[i-1];
  }
  ttyEditLines[0] = *field;
  if (hist_count<TTY_HISTORY)
  {
    hist_count++;
  }
  hist_current = -1; // re-init
}

static field_t* tty_Hist_Prev()
{
  int hist_prev;
  assert(hist_count <= TTY_HISTORY);
  assert(hist_count >= 0);
  assert(hist_current >= -1);
  assert(hist_current <= hist_count);
  hist_prev = hist_current + 1;
  if (hist_prev >= hist_count)
  {
    return NULL;
  }
  hist_current++;
  return &(ttyEditLines[hist_current]);
}

static field_t* tty_Hist_Next()
{
  assert(hist_count <= TTY_HISTORY);
  assert(hist_count >= 0);
  assert(hist_current >= -1);
  assert(hist_current <= hist_count);
  if (hist_current >= 0)
  {
    hist_current--;
  }
  if (hist_current == -1)
  {
    return NULL;
  }
  return &(ttyEditLines[hist_current]);
}


// initialize the console input (tty mode if wanted and possible)
static void Sys_ConsoleInputInit()
{
  struct termios tc;

  // TTimo
  // https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=390
  // ttycon 0 or 1, if the process is backgrounded (running non interactively)
  // then SIGTTIN or SIGTOU is emitted, if not catched, turns into a SIGSTP
  signal(SIGTTIN, SIG_IGN);
  signal(SIGTTOU, SIG_IGN);

  // FIXME TTimo initialize this in Sys_Init or something?
  ttycon = Cvar_Get("ttycon", "1", 0);
  if (ttycon && ttycon->value)
  {
    if (isatty(STDIN_FILENO)!=1)
    {
      Com_Printf("stdin is not a tty, tty console mode failed\n");
      Cvar_Set("ttycon", "0");
      ttycon_on = qfalse;
      return;
    }
    Com_Printf("Started tty console (use +set ttycon 0 to disable)\n");
    Field_Clear(&tty_con);
    tcgetattr (0, &tty_tc);
    tty_erase = tty_tc.c_cc[VERASE];
    tty_eof = tty_tc.c_cc[VEOF];
    tc = tty_tc;
    /*
     ECHO: don't echo input characters
     ICANON: enable canonical mode.  This  enables  the  special
              characters  EOF,  EOL,  EOL2, ERASE, KILL, REPRINT,
              STATUS, and WERASE, and buffers by lines.
     ISIG: when any of the characters  INTR,  QUIT,  SUSP,  or
              DSUSP are received, generate the corresponding signal
    */
    tc.c_lflag &= ~(ECHO | ICANON);
    /*
     ISTRIP strip off bit 8
     INPCK enable input parity checking
     */
    tc.c_iflag &= ~(ISTRIP | INPCK);
    tc.c_cc[VMIN] = 1;
    tc.c_cc[VTIME] = 0;
    tcsetattr (0, TCSADRAIN, &tc);
    ttycon_on = qtrue;

    ttycon_ansicolor = Cvar_Get( "ttycon_ansicolor", "0", CVAR_ARCHIVE );
    if( ttycon_ansicolor && ttycon_ansicolor->value )
    {
      ttycon_color_on = qtrue;
    }
  } else
    ttycon_on = qfalse;
}


const char* Sys_ConsoleInput()
{
  // we use this when sending back commands
  static char text[256];
  int avail;
  char key;
  field_t *history;

  if (ttycon && ttycon->value)
  {
    avail = read(0, &key, 1);
    if (avail != -1)
    {
      // we have something
      // backspace?
      // NOTE TTimo testing a lot of values .. seems it's the only way to get it to work everywhere
      if ((key == tty_erase) || (key == 127) || (key == 8))
      {
        if (tty_con.cursor > 0)
        {
          tty_con.cursor--;
          tty_con.buffer[tty_con.cursor] = '\0';
          tty_Back();
        }
        return NULL;
      }
      // check if this is a control char
      if ((key) && (key) < ' ')
      {
        if (key == '\n')
        {
          // push it in history
          tty_Hist_Add(&tty_con);
          strcpy(text, tty_con.buffer);
          Field_Clear(&tty_con);
          key = '\n';
          write(1, &key, 1);
          return text;
        }
        if (key == '\t')
        {
          tty_Hide();
          Field_AutoComplete( &tty_con );
          tty_Show();
          return NULL;
        }
        avail = read(0, &key, 1);
        if (avail != -1)
        {
          // VT 100 keys
          if (key == '[' || key == 'O')
          {
            avail = read(0, &key, 1);
            if (avail != -1)
            {
              switch (key)
              {
              case 'A':
                history = tty_Hist_Prev();
                if (history)
                {
                  tty_Hide();
                  tty_con = *history;
                  tty_Show();
                }
                tty_FlushIn();
                return NULL;
                break;
              case 'B':
                history = tty_Hist_Next();
                tty_Hide();
                if (history)
                {
                  tty_con = *history;
                } else
                {
                  Field_Clear(&tty_con);
                }
                tty_Show();
                tty_FlushIn();
                return NULL;
                break;
              case 'C':
                return NULL;
              case 'D':
                return NULL;
              }
            }
          }
        }
        Com_DPrintf("droping ISCTL sequence: %d, tty_erase: %d\n", key, tty_erase);
        tty_FlushIn();
        return NULL;
      }
      // push regular character
      tty_con.buffer[tty_con.cursor] = key;
      tty_con.cursor++;
      // print the current line (this is differential)
      write(1, &key, 1);
    }
    return NULL;
  } else
  {
    int     len;
    fd_set  fdset;
    struct timeval timeout;

    if (!com_dedicated || !com_dedicated->value)
      return NULL;

    if (!stdin_active)
      return NULL;

    FD_ZERO(&fdset);
    FD_SET(0, &fdset); // stdin
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    if (select (1, &fdset, NULL, NULL, &timeout) == -1 || !FD_ISSET(0, &fdset))
    {
      return NULL;
    }

    len = read (0, text, sizeof(text));
    if (len == 0)
    { // eof!
      stdin_active = qfalse;
      return NULL;
    }

    if (len < 1)
      return NULL;
    text[len-1] = 0;    // rip off the /n and terminate

    return text;
  }
}


// never exit without calling this, or your terminal will be left in a pretty bad state
static void Sys_ConsoleInputShutdown()
{
	if (!ttycon_on)
		return;
	Com_Printf("Shutdown tty console\n");
	tcsetattr (0, TCSADRAIN, &tty_tc);
}


///////////////////////////////////////////////////////////////


#define MEM_THRESHOLD 96*1024*1024

qboolean Sys_LowPhysicalMemory()
{
	return qfalse; // FIXME
}


void Sys_BeginProfiling( void ) {
}


// single exit point (regular exit or in case of signal fault)
void Sys_Exit( int ex )
{
	Sys_ConsoleInputShutdown();

#ifdef NDEBUG // regular behavior
	// We can't do this as long as GL DLL's keep installing with atexit...
	//exit(ex);
	_exit(ex);
#else
	// Give me a backtrace on error exits.
	assert( ex == 0 );
	exit(ex);
#endif
}


void Sys_Error( const char *error, ... )
{
	va_list     argptr;
	char        string[1024];

	// change stdin to nonblocking
	// NOTE TTimo not sure how well that goes with tty console mode
	fcntl( 0, F_SETFL, fcntl(0, F_GETFL, 0) & ~FNDELAY );

	if (ttycon_on)
		tty_Hide();

	CL_Shutdown();

	va_start (argptr,error);
	vsprintf (string,error,argptr);
	va_end (argptr);
	fprintf(stderr, "Sys_Error: %s\n", string);

	Sys_Exit( 1 );
}


void Sys_Quit()
{
	CL_Shutdown();
	fcntl( 0, F_SETFL, fcntl(0, F_GETFL, 0) & ~FNDELAY );
	Sys_Exit(0);
}


///////////////////////////////////////////////////////////////


const char* Sys_DllError()
{
#if USE_SDL_VIDEO
	return SDL_GetError();
#else
	return dlerror();
#endif
}


void Sys_UnloadDll( void* dllHandle )
{
	if ( !dllHandle )
		return;

#if USE_SDL_VIDEO
	SDL_UnloadObject( dllHandle );
#else
	dlclose( dllHandle );
#endif

	const char* err = Sys_DllError();
	if ( err ) {
		Com_Error( ERR_FATAL, "Sys_UnloadDll failed: %s\n", err );
	}
}


static void* try_dlopen( const char* base, const char* gamedir, const char* filename )
{
	void* libHandle;
	const char* fn = FS_BuildOSPath( base, gamedir, filename );

#if USE_SDL_VIDEO
	libHandle = SDL_LoadObject(fn);
#else
	libHandle = dlopen( fn, RTLD_NOW );
#endif

	if (!libHandle) {
		Com_Printf( "Sys_LoadDll(%s) failed: %s\n", fn, Sys_DllError() );
		return NULL;
	}

	Com_Printf( "Sys_LoadDll(%s) succeeded\n", fn );
	return libHandle;
}


// used to load a development dll instead of a virtual machine
// in release builds, the load procedure matches the VFS logic (fs_homepath, then fs_basepath)
// in debug builds, the current working directory is tried first

void* Sys_LoadDll( const char* name,
		intptr_t (**entryPoint)(intptr_t, ...),
		intptr_t (*systemcalls)(intptr_t, ...) )
{
	char filename[MAX_QPATH];
	Com_sprintf( filename, sizeof( filename ), "%s" ARCH_STRING DLL_EXT, name );

	void* libHandle = 0;
	// FIXME: use fs_searchpaths from files.c
	const char* homepath = Cvar_VariableString( "fs_homepath" );
	const char* basepath = Cvar_VariableString( "fs_basepath" );
	const char* gamedir = Cvar_VariableString( "fs_game" );

#ifndef NDEBUG
	libHandle = try_dlopen( Sys_Cwd(), gamedir, filename );
#endif

	if (!libHandle && homepath)
		libHandle = try_dlopen( homepath, gamedir, filename );

	if (!libHandle && basepath)
		libHandle = try_dlopen( basepath, gamedir, filename );

	if ( !libHandle )
		return NULL;

	void (QDECL *dllEntry)( int (QDECL *syscallptr)(int, ...) );

#if USE_SDL_VIDEO
	dllEntry = (void (QDECL *)( int (QDECL *)( int, ... ) ) )SDL_LoadFunction( libHandle, "dllEntry" );
	*entryPoint = (int (QDECL *)(intptr_t,...))SDL_LoadFunction( libHandle, "vmMain" );
#else
	dllEntry = (void (QDECL *)( int (QDECL *)( int, ... ) ) )dlsym( libHandle, "dllEntry" );
	*entryPoint = (int (QDECL *)(intptr_t,...))dlsym( libHandle, "vmMain" );
#endif

	if ( !*entryPoint || !dllEntry ) {
		const char* err = Sys_DllError();
		Com_Printf( "Sys_LoadDll(%s) failed dlsym: %s\n", name, err );
		Sys_UnloadDll( libHandle );
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
	// in vga this calls KBD_Update, under X, it calls GetEvent
	Sys_SendKeyEvents();

	// check for console commands
	const char* s = Sys_ConsoleInput();
	if ( s ) {
		int len = strlen( s ) + 1;
		char* b = (char*)Z_Malloc( len );
		Q_strncpyz( b, s, len-1 );
		Sys_QueEvent( 0, SE_CONSOLE, 0, 0, len, b );
	}

	// check for other input devices
	IN_Frame();

	// check for network packets
	msg_t		netmsg;
	netadr_t	adr;
	static byte sys_packetReceived[MAX_MSGLEN]; // static or it'll blow half the stack
	MSG_Init( &netmsg, sys_packetReceived, sizeof( sys_packetReceived ) );
	if ( Sys_GetPacket( &adr, &netmsg ) ) {
		// copy out to a separate buffer for queuing
		int len = sizeof( netadr_t ) + netmsg.cursize;
		netadr_t* buf = (netadr_t*)Z_Malloc( len );
		*buf = adr;
		memcpy( buf+1, netmsg.data, netmsg.cursize );
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


char *Sys_GetClipboardData(void)
{
  return NULL;
}


static struct Q3ToAnsiColorTable_s
{
  char Q3color;
  const char* ANSIcolor;
} const tty_colorTable[] =
{
  { COLOR_BLACK,    "30" },
  { COLOR_RED,      "31" },
  { COLOR_GREEN,    "32" },
  { COLOR_YELLOW,   "33" },
  { COLOR_BLUE,     "34" },
  { COLOR_CYAN,     "36" },
  { COLOR_MAGENTA,  "35" },
  { COLOR_WHITE,    "0" }
};

static const int tty_colorTableSize = sizeof( tty_colorTable ) / sizeof( tty_colorTable[0] );

static void Sys_ANSIColorify( const char *msg, char *buffer, int bufferSize )
{
  int   msgLength, pos;
  int   i, j;
  const char* escapeCode;
  char  tempBuffer[ 7 ];

  if( !msg || !buffer )
    return;

  msgLength = strlen( msg );
  pos = 0;
  i = 0;
  buffer[ 0 ] = '\0';

  while( i < msgLength )
  {
    if( msg[ i ] == '\n' )
    {
      Com_sprintf( tempBuffer, 7, "%c[0m\n", 0x1B );
      strncat( buffer, tempBuffer, bufferSize );
      i++;
    }
    else if( msg[ i ] == Q_COLOR_ESCAPE )
    {
      i++;

      if( i < msgLength )
      {
        escapeCode = NULL;
        for( j = 0; j < tty_colorTableSize; j++ )
        {
          if( msg[ i ] == tty_colorTable[ j ].Q3color )
          {
            escapeCode = tty_colorTable[ j ].ANSIcolor;
            break;
          }
        }

        if( escapeCode )
        {
          Com_sprintf( tempBuffer, 7, "%c[%sm", 0x1B, escapeCode );
          strncat( buffer, tempBuffer, bufferSize );
        }

        i++;
      }
    }
    else
    {
      Com_sprintf( tempBuffer, 7, "%c", msg[ i++ ] );
      strncat( buffer, tempBuffer, bufferSize );
    }
  }
}

void  Sys_Print( const char *msg )
{
  if (ttycon_on)
  {
    tty_Hide();
  }

  if( ttycon_on && ttycon_color_on )
  {
    char ansiColorString[ MAXPRINTMSG ];
    Sys_ANSIColorify( msg, ansiColorString, MAXPRINTMSG );
    fputs( ansiColorString, stderr );
  }
  else
    fputs(msg, stderr);

  if (ttycon_on)
  {
    tty_Show();
  }
}


///////////////////////////////////////////////////////////////


void Sys_Init()
{
	Cmd_AddCommand( "in_restart", Sys_In_Restart_f );
	Cvar_Set( "arch", OS_STRING " " ARCH_STRING );
	Cvar_Set( "username", Sys_GetCurrentUser() );
}


int main( int argc, char* argv[] )
{
	// merge the command line: we need it in a single chunk
	int len = 1, i;
	for (i = 1; i < argc; ++i)
		len += strlen(argv[i]) + 1;
	char* cmdline = (char*)malloc(len);
	*cmdline = 0;
	for (i = 1; i < argc; ++i) {
		if (i > 1)
			strcat( cmdline, " " );
		strcat( cmdline, argv[i] );
	}
	Com_Init( cmdline );

	NET_Init();

	Com_Printf( "Working directory: %s\n", Sys_Cwd() );

	Sys_ConsoleInputInit();

	fcntl( 0, F_SETFL, fcntl(0, F_GETFL, 0) | FNDELAY );

#ifdef DEDICATED
	// init here for dedicated, as we don't have GLimp_Init
	InitSig();
#endif

	while (qtrue) {
		// if running as a client but not focused, sleep a bit
		// (servers have their own sleep path)
#if !defined( DEDICATED ) && USE_SDL_VIDEO
		int appState = SDL_GetAppState();
		if ( !( appState & SDL_APPACTIVE ) )
			usleep( 20000 ); // minimised: sleep a lot
		if ( !( appState & ( SDL_APPMOUSEFOCUS | SDL_APPINPUTFOCUS ) ) )
			usleep( 10000 );
#endif

		Com_Frame();
	}

	return 0;
}

