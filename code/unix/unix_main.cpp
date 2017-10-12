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
#include <sys/sysinfo.h>
#include <string.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <errno.h>
#include <dlfcn.h>

#if defined(__sun)
  #include <sys/file.h>
#endif

#include <termios.h>

#include "../client/client.h"
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "linux_local.h"


///////////////////////////////////////////////////////////////


int    q_argc = 0;
char** q_argv = NULL;

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

static history_t tty_history;



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
  write(STDOUT_FILENO, &key, 1);
  key = ' ';
  write(STDOUT_FILENO, &key, 1);
  key = '\b';
  write(STDOUT_FILENO, &key, 1);
}

// clear the display of the line currently edited
// bring cursor back to beginning of line
static void tty_Hide()
{
  assert(ttycon_on);
  if (ttycon_hide)
  {
    ttycon_hide++;
    return;
  }
  const int length = strlen(tty_con.buffer);
  if (length > 0)
  {
    const int stepsForward = length - tty_con.cursor;
    // a single write call for this didn't work on my terminal
    for (int i = 0; i < stepsForward; ++i)
    {
      write(STDOUT_FILENO, "\033[1C", 4);
    }
    for (int i = 0; i < length; ++i)
    {
      tty_Back();
    }
  }
  tty_Back(); // delete the leading "]"
  ttycon_hide++;
}

// show the current line
// FIXME TTimo need to position the cursor if needed??
static void tty_Show()
{
  assert(ttycon_on);
  assert(ttycon_hide>0);
  ttycon_hide--;
  if (ttycon_hide == 0)
  {
    write(STDOUT_FILENO, "]", 1);
    const int length = strlen(tty_con.buffer);
    if (length > 0)
    {
      const int stepsBack = length - tty_con.cursor;
      write(STDOUT_FILENO, tty_con.buffer, length);
      // a single write call for this didn't work on my terminal
      for (int i = 0; i < stepsBack; ++i)
      {
		write(STDOUT_FILENO, "\033[1D", 4);
	  }
    }
  }
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
      Com_Printf("stdin is not a tty, tty console mode failed: %s\n", strerror(errno));
      Cvar_Set("ttycon", "0");
      ttycon_on = qfalse;
      return;
    }
    Com_Printf("Started tty console (use +set ttycon 0 to disable)\n");
    Field_Clear(&tty_con);
    tcgetattr (STDIN_FILENO, &tty_tc);
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
    tcsetattr (STDIN_FILENO, TCSADRAIN, &tc);
    ttycon_on = qtrue;

    ttycon_ansicolor = Cvar_Get( "ttycon_ansicolor", "0", CVAR_ARCHIVE );
  } else
    ttycon_on = qfalse;

	// make stdin non-blocking
	fcntl( STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | FNDELAY );
}


const char* Sys_ConsoleInput()
{
  // we use this when sending back commands
  static char text[256];
  int avail;
  char key;
  field_t field;

  if (ttycon && ttycon->value)
  {
    avail = read(STDIN_FILENO, &key, 1);
    if (avail != -1)
    {
      // we have something
      
      if (key != '\t')
		tty_con.acOffset = 0;
      
      if (key == tty_erase || key == 127 || key == 8) // backspace
      {
		const int length = strlen(tty_con.buffer);
        if (length > 0)
        {
		  if (tty_con.cursor == length)
		  {
            tty_con.buffer[length - 1] = '\0';
            tty_con.cursor--;
            tty_Back();
		  }
		  else if (tty_con.cursor > 0)
		  {
			tty_Hide();
			const int toMove = length + 1 - tty_con.cursor; // with the null terminator
			memmove(tty_con.buffer + tty_con.cursor - 1, tty_con.buffer + tty_con.cursor, toMove);
			tty_con.cursor--;
			tty_Show();
		  }
        }
        return NULL;
      }

      // check if this is a control char
      if ((key) && (key) < ' ')
      {
        if (key == '\n')
        {
#ifdef DEDICATED
          History_SaveCommand(&tty_history, &tty_con);
          Q_strncpyz(text, tty_con.buffer, sizeof(text));
          Field_Clear(&tty_con);
          write(STDOUT_FILENO, "\n]", 2);
#else
          // not in a game yet and no leading slash?
          if (cls.state != CA_ACTIVE &&
              tty_con.buffer[0] != '/' && tty_con.buffer[0] != '\\')
          {
            // there's no one to chat with, so we consider this a command
            const int length = strlen(tty_con.buffer);
            if (length > 0)
            {
			  memmove(tty_con.buffer + 1, tty_con.buffer, length + 1);
			}
			else
			{
			  tty_con.buffer[1] = '\0';
			}
			tty_con.buffer[0] = '\\';
            tty_con.cursor++;
          }

          // decide what the final command will be
          const int length = strlen(tty_con.buffer);
          if (tty_con.buffer[0] == '/' || tty_con.buffer[0] == '\\')
            Q_strncpyz(text, tty_con.buffer + 1, sizeof(text));
          else if (length > 0)
            Com_sprintf(text, sizeof(text), "say %s", tty_con.buffer);
          else
            *text = '\0';
            
          History_SaveCommand(&tty_history, &tty_con);
          tty_Hide();
          Com_Printf("tty]%s\n", tty_con.buffer);
          Field_Clear(&tty_con);
          tty_Show();
#endif
          return text;
        }
        if (key == '\t')
        {
          tty_Hide();
#ifdef DEDICATED
          Field_AutoComplete( &tty_con, qfalse );
#else
          Field_AutoComplete( &tty_con, qtrue );
#endif
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
              case 'A': // up arrow (move cursor up)
                History_GetPreviousCommand(&field, &tty_history);
                if (field.buffer[0] != '\0')
                {
                  tty_Hide();
                  tty_con = field;
                  tty_Show();
                }
                tty_FlushIn();
                return NULL;
                break;
              case 'B': // down arrow (move cursor down)
                History_GetNextCommand(&field, &tty_history, 0);
                tty_Hide();
                if (tty_con.buffer[0] != '\0')
                {
                  tty_con = field;
                }
                else
                {
                  Field_Clear(&tty_con);
                }
                tty_Show();
                tty_FlushIn();
                return NULL;
                break;
              case 'C': // right arrow (move cursor right)
                if (tty_con.cursor < strlen(tty_con.buffer))
                {
				  write(STDOUT_FILENO, "\033[1C", 4);
                  tty_con.cursor++;
				}
                return NULL;
              case 'D': // left arrow (move cursor left)
                if (tty_con.cursor > 0)
                {
				  write(STDOUT_FILENO, "\033[1D", 4);
                  tty_con.cursor--;
				}
                return NULL;
              case '3': // delete (might not work on all terminals)
                {
                  const int length = strlen(tty_con.buffer);
                  if (tty_con.cursor < length)
                  {
	                tty_Hide();
	                const int toMove = length - tty_con.cursor; // with terminating NULL
	                memmove(tty_con.buffer + tty_con.cursor, tty_con.buffer + tty_con.cursor + 1, toMove);
	                tty_Show();  
                  }
			    }
			    tty_FlushIn();
			    return NULL;
              case 'H': // home (move cursor to upper left corner)
                if (tty_con.cursor > 0)
                {
				  tty_Hide();
	              tty_con.cursor = 0;
	              tty_Show();  
				}
			    return NULL;
			  case 'F': // end (might not work on all terminals)
                {
			      const int length = strlen(tty_con.buffer);
                  if (tty_con.cursor < length)
                  {
	                tty_Hide();
	                tty_con.cursor = length;
	                tty_Show();  
                  }
				}
			    return NULL;
              }
            }
          }
        }
        Com_DPrintf("droping ISCTL sequence: %d, tty_erase: %d\n", key, tty_erase);
        tty_FlushIn();
        return NULL;
      }
      
      // if we get here, key is a regular character
      const int length = strlen(tty_con.buffer);
      if (tty_con.cursor == length)
      {
        write(STDOUT_FILENO, &key, 1);
        tty_con.buffer[tty_con.cursor + 0] = key;
        tty_con.buffer[tty_con.cursor + 1] = '\0';
        tty_con.cursor++;
	  }
      else
      {
        tty_Hide();
        const int toMove = length + 1 - tty_con.cursor; // need to move the null terminator too
		memmove(tty_con.buffer + tty_con.cursor + 1, tty_con.buffer + tty_con.cursor, toMove);
		tty_con.buffer[tty_con.cursor] = key;
		tty_con.cursor++;
        tty_Show();
      }
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
    FD_SET(STDIN_FILENO, &fdset);
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    if (select (1, &fdset, NULL, NULL, &timeout) == -1 || !FD_ISSET(STDIN_FILENO, &fdset))
    {
      return NULL;
    }

    len = read (STDIN_FILENO, text, sizeof(text));
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
void Sys_ConsoleInputShutdown()
{
	if (ttycon_on)
	{
		tty_Back(); // delete the leading "]"
		tcsetattr (STDIN_FILENO, TCSADRAIN, &tty_tc);
		ttycon_on = qfalse;
	}

	// make stdin blocking
	fcntl( STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) & (~FNDELAY) );
}


///////////////////////////////////////////////////////////////


#define MEM_THRESHOLD 96*1024*1024

qboolean Sys_LowPhysicalMemory()
{
	return qfalse; // FIXME
}


void Sys_Error( const char *error, ... )
{
	va_list     argptr;
	char        string[1024];

	if (ttycon_on)
		tty_Hide();

#ifndef DEDICATED
	CL_Shutdown();
#endif

	va_start (argptr,error);
	vsprintf (string,error,argptr);
	va_end (argptr);
	fprintf(stderr, "Sys_Error: %s\n", string);

	Sys_ConsoleInputShutdown();
	exit(1);
}


void Sys_Quit( int status )
{
	Sys_ConsoleInputShutdown();
	exit( status );
}


///////////////////////////////////////////////////////////////


const char* Sys_DllError()
{
	return dlerror();
}


void Sys_UnloadDll( void* dllHandle )
{
	if ( !dllHandle )
		return;

	dlclose( dllHandle );

	const char* err = Sys_DllError();
	if ( err ) {
		Com_Error( ERR_FATAL, "Sys_UnloadDll failed: %s\n", err );
	}
}


static void* try_dlopen( const char* base, const char* gamedir, const char* filename )
{
	const char* fn = FS_BuildOSPath( base, gamedir, filename );
	void* libHandle = dlopen( fn, RTLD_NOW );

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

void* QDECL Sys_LoadDll( const char* name, dllSyscall_t *entryPoint, dllSyscall_t systemcalls )
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

	dllEntry_t dllEntry = (dllEntry_t)dlsym( libHandle, "dllEntry" );
	*entryPoint = (dllSyscall_t)dlsym( libHandle, "vmMain" );

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

#ifndef DEDICATED
	// pump the message loop
	// in vga this calls KBD_Update, under X, it calls GetEvent
	Sys_SendKeyEvents();
#endif

	// check for console commands
	const char* s = Sys_ConsoleInput();
	if ( s ) {
		const int slen = strlen( s );
		const int blen = slen + 1;
		char* b = (char*)Z_Malloc( blen );
		Q_strncpyz( b, s, blen );
		Sys_QueEvent( 0, SE_CONSOLE, 0, 0, slen, b );
	}

#ifndef DEDICATED
	// check for other input devices
	IN_Frame();
#endif

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


static char* CleanTermStr( char* string )
{
	char* s = string;
	char* d = string;
	char c;

	while ((c = *s) != 0 ) {
		if ( Q_IsColorString( s ) )
			s++;
		else
			*d++ = c;
		s++;
	}
	*d = '\0';

	return string;
}


void  Sys_Print( const char *msg )
{
  static char finalMsg[ MAXPRINTMSG ];
	
  if (ttycon_on)
  {
    tty_Hide();
  }

  if( ttycon_on && ttycon_ansicolor && ttycon_ansicolor->integer )
  {
    Sys_ANSIColorify( msg, finalMsg, sizeof(finalMsg) );
  }
  else
  {
    Q_strncpyz( finalMsg, msg, sizeof(finalMsg) );
    CleanTermStr( finalMsg );
  }
  fputs( finalMsg, stdout );

  if (ttycon_on)
  {
    tty_Show();
  }
}


///////////////////////////////////////////////////////////////


void Sys_Init()
{
	Cvar_Set( "arch", OS_STRING " " ARCH_STRING );
}


qbool Sys_HardReboot()
{
#ifdef DEDICATED
	return qtrue;
#else
	return qfalse;
#endif
}


#ifdef DEDICATED


static int Lin_RunProcess( char** argv )
{
	const pid_t pid = fork();
	if (pid == 0) {
		if (execve(argv[0], argv , NULL) == -1) {
			fprintf(stderr, "failed to launch child process: %s\n", strerror(errno));
			_exit(1); // quit without calling atexit handlers
			return 0;
		}
	}

	int status;
	while (waitpid(pid, &status, WNOHANG) == 0)
		sleep(1); // in seconds

    return WEXITSTATUS(status);
}


static void Lin_HardRebootHandler( int argc, char** argv )
{
	for (int i = 0; i < argc; ++i) {
		if (!Q_stricmp(argv[i], "nohardreboot")) {
			return;
		}
	}
	
	static char* args[256];
	if (argc + 2 >= sizeof(args) / sizeof(args[0])) {
		fprintf(stderr, "too many arguments: %d\n", argc);
		_exit(1); // quit without calling atexit handlers
		return;
	}

	for (int i = 0; i < argc; ++i)
		args[i] = argv[i];
	args[argc + 0] = (char*)"nohardreboot";
	args[argc + 1] = NULL;

	SIG_InitParent();

	for (;;) {
		if (Lin_RunProcess(args) == 0)
			_exit(0); // quit without calling atexit handlers
	}
}


#endif


static qbool lin_hasParent = qfalse;
static pid_t lix_parentPid;


static const char* Lin_GetExeName(const char* path)
{
	const char* lastSlash = strrchr(path, '/');
	if (lastSlash == NULL)
		return path;

	return lastSlash + 1;
}


static void Lin_TrackParentProcess()
{
	static char cmdLine[1024];

	char fileName[128];
	Com_sprintf(fileName, sizeof(fileName), "/proc/%d/cmdline", (int)getppid());

	const int fd = open(fileName, O_RDONLY);
	if (fd == -1)
		return;

	const qbool hasCmdLine = read(fd, cmdLine, sizeof(cmdLine)) > 0;
	close(fd);

	if (!hasCmdLine)
		return;

	cmdLine[sizeof(cmdLine) - 1] = '\0';
	lin_hasParent = strcmp(Lin_GetExeName(cmdLine), Lin_GetExeName(q_argv[0])) == 0;
}


qbool Sys_HasCNQ3Parent()
{
	return lin_hasParent;
}


static int Sys_GetProcessUptime( pid_t pid )
{
	// length must be in sync with the fscanf call!
	static char word[256];

	// The process start time is the 22nd column and
	// encoded as jiffies after system boot.
	const int jiffiesPerSec = sysconf(_SC_CLK_TCK);
	if (jiffiesPerSec <= 0)
		return -1;

	char fileName[128];
	Com_sprintf(fileName, sizeof(fileName), "/proc/%ld/stat", (long)pid);
	FILE* const file = fopen(fileName, "r");
	if (file == NULL)
		return -1;

	for (int i = 0; i < 21; ++i) {
		if (fscanf(file, "%255s", word) != 1) {
			fclose(file);
			return -1;
		}
	}

	int jiffies;
	const qbool success = fscanf(file, "%d", &jiffies) == 1;
	fclose(file);

	if (!success)
		return -1;

	const int secondsSinceBoot = jiffies / jiffiesPerSec;
	struct sysinfo info;
	sysinfo(&info);

	return (int)info.uptime - secondsSinceBoot;
}


int Sys_GetUptimeSeconds( qbool parent )
{
	if (!lin_hasParent)
		return -1;

	return Sys_GetProcessUptime( parent ? getppid() : getpid() );
}


void Sys_LoadHistory()
{
#ifdef DEDICATED
	History_LoadFromFile( &tty_history );
#else
	History_LoadFromFile( &g_history );
#endif
}


void Sys_SaveHistory()
{
#ifdef DEDICATED
	History_SaveToFile( &tty_history );
#else
	History_SaveToFile( &g_history );
#endif
}


int main( int argc, char** argv )
{
	q_argc = argc;
	q_argv = argv;

#ifdef DEDICATED
	Lin_HardRebootHandler(argc, argv);
#endif

	SIG_InitChild();
	
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
	Lin_TrackParentProcess();

	for (;;) {
		SIG_Frame();
		Com_Frame();
	}

	return 0;
}

