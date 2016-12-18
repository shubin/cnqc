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
// cmd.c -- Quake script command processing module

#include "q_shared.h"
#include "qcommon.h"

#define MAX_CMD_BUFFER	16384
#define MAX_CMD_LINE	1024

typedef struct {
	byte	*data;
	int		maxsize;
	int		cursize;
} cmd_t;

static int		cmd_wait;
static cmd_t	cmd_text;
static byte		cmd_text_buf[MAX_CMD_BUFFER];


// delay execution of the remainder of the command buffer until next frame
// so that scripts etc can work around race conditions

static void Cmd_Wait_f( void )
{
	if ( Cmd_Argc() == 2 ) {
		cmd_wait = atoi( Cmd_Argv( 1 ) );
	} else {
		cmd_wait = 1;
	}
}


void Cbuf_Init()
{
	cmd_text.data = cmd_text_buf;
	cmd_text.maxsize = MAX_CMD_BUFFER;
	cmd_text.cursize = 0;
}


// adds command text at the end of the buffer, does NOT add a final \n

void Cbuf_AddText( const char* text )
{
	int l = strlen(text);

	if (cmd_text.cursize + l >= cmd_text.maxsize) {
		Com_Printf( "Cbuf_AddText: overflow\n" );
		return;
	}

	Com_Memcpy( &cmd_text.data[cmd_text.cursize], text, l );
	cmd_text.cursize += l;
}


// adds command text (and \n) immediately after the current command
// should be (and now is) only EVER used by config file chaining

static void Cbuf_InsertText( const char* text )
{
	int len = strlen( text ) + 1;
	if ( len + cmd_text.cursize > cmd_text.maxsize ) {
		Com_Printf( "Cbuf_InsertText overflowed\n" );
		return;
	}

	// move the existing command text
	for ( int i = cmd_text.cursize - 1; i >= 0; --i ) {
		cmd_text.data[ i + len ] = cmd_text.data[ i ];
	}

	Com_Memcpy( cmd_text.data, text, len - 1 );
	cmd_text.data[ len - 1 ] = '\n';

	cmd_text.cursize += len;
}


void Cbuf_Execute()
{
	int		i;
	char	*text;
	char	line[MAX_CMD_LINE];
	int		quotes;

	while (cmd_text.cursize)
	{
		if ( cmd_wait ) {
			// skip out and leave the buffer alone until next frame
			cmd_wait--;
			break;
		}

		// find a \n or ; line break
		text = (char *)cmd_text.data;

		quotes = 0;
		for (i=0 ; i< cmd_text.cursize ; i++)
		{
			if (text[i] == '"')
				quotes++;
			if (!(quotes&1) && text[i] == ';')
				break;	// don't break if inside a quoted string
			if (text[i] == '\n' || text[i] == '\r')
				break;
		}

		if( i >= (MAX_CMD_LINE - 1)) {
			i = MAX_CMD_LINE - 1;
		}

		Com_Memcpy (line, text, i);
		line[i] = 0;

// delete the text from the command buffer and move remaining commands down
// this is necessary because commands (exec) can insert data at the
// beginning of the text buffer

		if (i == cmd_text.cursize)
			cmd_text.cursize = 0;
		else
		{
			i++;
			cmd_text.cursize -= i;
			memmove (text, text+i, cmd_text.cursize);
		}

// execute the command line

		Cmd_ExecuteString (line);
	}
}


///////////////////////////////////////////////////////////////
// SCRIPT COMMANDS


void Cmd_Exec_f()
{
	char	*f;
	int		len;
	char	filename[MAX_QPATH];

	if (Cmd_Argc() != 2) {
		Com_Printf( "exec <filename> : execute a script file\n" );
		return;
	}

	Q_strncpyz( filename, Cmd_Argv(1), sizeof( filename ) );
	COM_DefaultExtension( filename, sizeof( filename ), ".cfg" );
	len = FS_ReadFile( filename, (void **)&f );
	if (!f) {
		Com_Printf( "couldn't exec %s\n", Cmd_Argv(1) );
		return;
	}
	Com_Printf( "execing %s\n", Cmd_Argv(1) );

	Cbuf_InsertText(f);

	FS_FreeFile(f);
}


// inserts the current value of a variable as command text

void Cmd_Vstr_f()
{
	if (Cmd_Argc() != 2) {
		Com_Printf( "vstr <variablename> : execute a variable command\n" );
		return;
	}
	Cbuf_InsertText( Cvar_VariableString( Cmd_Argv( 1 ) ) );
}


// just prints the rest of the line to the console

void Cmd_Echo_f()
{
	for (int i = 1; i < Cmd_Argc(); ++i)
		Com_Printf( "%s ",Cmd_Argv(i) );
	Com_Printf("\n");
}


///////////////////////////////////////////////////////////////
// COMMAND EXECUTION


typedef struct cmd_function_s
{
	struct cmd_function_s	*next;
	char					*name;
	xcommand_t				function;
} cmd_function_t;


static	int		cmd_argc;
static	char*	cmd_argv[MAX_STRING_TOKENS];		// points into cmd_tokenized
static	char	cmd_tokenized[BIG_INFO_STRING+MAX_STRING_TOKENS];	// will have 0 bytes inserted
static	char	cmd_cmd[BIG_INFO_STRING]; // the original command we received (no token processing)

static	cmd_function_t	*cmd_functions;		// possible commands to execute


int Cmd_Argc()
{
	return cmd_argc;
}


const char* Cmd_Argv( int arg )
{
	if ((unsigned)arg >= cmd_argc)
		return "";
	return cmd_argv[arg];
}


// returns a single string containing argv(arg) to argv(argc()-1)

const char* Cmd_ArgsFrom( int arg )
{
	static char cmd_args[BIG_INFO_STRING];
	cmd_args[0] = 0;

	if (arg < 0)
		Com_Error( ERR_FATAL, "Cmd_ArgsFrom: arg < 0" );

	for (int i = arg; i < cmd_argc; ++i) {
		strcat( cmd_args, cmd_argv[i] );
		if (i != cmd_argc-1) {
			strcat( cmd_args, " " );
		}
	}

	return cmd_args;
}


const char* Cmd_Args()
{
	return Cmd_ArgsFrom(1);
}


// the interpreted versions use these because they can't have pointers returned to them

void Cmd_ArgvBuffer( int arg, char *buffer, int bufferLength ) {
	Q_strncpyz( buffer, Cmd_Argv( arg ), bufferLength );
}

void Cmd_ArgsBuffer( char *buffer, int bufferLength ) {
	Q_strncpyz( buffer, Cmd_Args(), bufferLength );
}


/*
Retrieve the unmodified command string
For rcon use when you want to transmit without altering quoting
https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=543
*/
const char* Cmd_Cmd()
{
	return cmd_cmd;
}


/*
parses the given string into command line tokens
the text is copied to a separate buffer with NULs inserted after each token
the argv array will point into this new buffer
*/
static void Cmd_TokenizeString2( const char* text, qbool ignoreQuotes )
{
	// clear previous args
	cmd_argc = 0;

	if ( !text )
		return;

	Q_strncpyz( cmd_cmd, text, sizeof(cmd_cmd) );

	char* out = cmd_tokenized;

	while ( 1 ) {
		if ( cmd_argc == MAX_STRING_TOKENS ) {
			return;			// this is usually something malicious
		}

		while ( 1 ) {
			// skip whitespace
			while ( *text && *text <= ' ' ) {
				text++;
			}
			if ( !*text ) {
				return;			// all tokens parsed
			}

			// skip // comments
			if ( text[0] == '/' && text[1] == '/' ) {
				return;			// all tokens parsed
			}

			// skip /* */ comments
			if ( text[0] == '/' && text[1] =='*' ) {
				while ( *text && ( text[0] != '*' || text[1] != '/' ) ) {
					text++;
				}
				if ( !*text ) {
					return;		// all tokens parsed
				}
				text += 2;
			} else {
				break;			// we are ready to parse a token
			}
		}

		// handle quoted strings - NOTE: this doesn't handle \" escaping
		if ( !ignoreQuotes && *text == '"' ) {
			cmd_argv[cmd_argc] = out;
			cmd_argc++;
			text++;
			while ( *text && *text != '"' ) {
				*out++ = *text++;
			}
			*out++ = 0;
			if ( !*text ) {
				return;		// all tokens parsed
			}
			text++;
			continue;
		}

		// regular token
		cmd_argv[cmd_argc] = out;
		cmd_argc++;

		// skip until whitespace, quote, or command
		while ( *text > ' ' ) {
			if ( !ignoreQuotes && text[0] == '"' ) {
				break;
			}

			if ( text[0] == '/' && text[1] == '/' ) {
				break;
			}

			// skip /* */ comments
			if ( text[0] == '/' && text[1] =='*' ) {
				break;
			}

			*out++ = *text++;
		}

		*out++ = 0;

		if ( !*text ) {
			return;		// all tokens parsed
		}
	}
}

void Cmd_TokenizeString( const char* text )
{
	Cmd_TokenizeString2( text, qfalse );
}

void Cmd_TokenizeStringIgnoreQuotes( const char* text )
{
	Cmd_TokenizeString2( text, qtrue );
}


void Cmd_AddCommand( const char *cmd_name, xcommand_t function )
{
	cmd_function_t* cmd;

	// fail if the command already exists
	for ( cmd = cmd_functions ; cmd ; cmd = cmd->next ) {
		if ( !strcmp( cmd_name, cmd->name ) ) {
			// allow completion-only commands to be silently doubled
			if ( function ) {
				Com_Printf( "Cmd_AddCommand: %s already defined\n", cmd_name );
			}
			return;
		}
	}

	// use a small malloc to avoid zone fragmentation
	cmd = (cmd_function_t*)S_Malloc(sizeof(cmd_function_t));
	cmd->name = CopyString( cmd_name );
	cmd->function = function;
	cmd->next = cmd_functions;
	cmd_functions = cmd;
}


void Cmd_RemoveCommand( const char *cmd_name )
{
	cmd_function_t** back = &cmd_functions;

	while( 1 ) {
		cmd_function_t* cmd = *back;
		if ( !cmd ) {
			// command wasn't active
			return;
		}
		if ( !strcmp( cmd_name, cmd->name ) ) {
			*back = cmd->next;
			if ( cmd->name ) {
				Z_Free( cmd->name );
			}
			Z_Free( cmd );
			return;
		}
		back = &cmd->next;
	}
}


void Cmd_CommandCompletion( void(*callback)(const char *s) )
{
	const cmd_function_t* cmd;
	for ( cmd = cmd_functions; cmd; cmd = cmd->next ) {
		callback( cmd->name );
	}
}


// a complete command line has been parsed, so try to execute it

void Cmd_ExecuteString( const char* text )
{
	Cmd_TokenizeString( text );
	if ( !Cmd_Argc() ) {
		return;		// no tokens
	}

	// check registered command functions
	cmd_function_t *cmd, **prev;
	for ( prev = &cmd_functions ; *prev ; prev = &cmd->next ) {
		cmd = *prev;
		if ( !Q_stricmp( cmd_argv[0],cmd->name ) ) {
			// rearrange the links so that the command will be
			// near the head of the list next time it is used
			*prev = cmd->next;
			cmd->next = cmd_functions;
			cmd_functions = cmd;

			// perform the action
			if ( !cmd->function ) {
				// let the cgame or game handle it
				break;
			} else {
				cmd->function();
			}
			return;
		}
	}

	// check cvars
	if ( Cvar_Command() ) {
		return;
	}

	// check client game commands
	if ( com_cl_running && com_cl_running->integer && CL_GameCommand() ) {
		return;
	}

	// check server game commands
	if ( com_sv_running && com_sv_running->integer && SV_GameCommand() ) {
		return;
	}

	// check ui commands
	if ( com_cl_running && com_cl_running->integer && UI_GameCommand() ) {
		return;
	}

	// send it as a server command if we are connected
	// this will usually result in a chat message
	CL_ForwardCommandToServer( text );
}


static void Cmd_List_f()
{
	const char* match = (Cmd_Argc() > 1) ? Cmd_Argv(1) : NULL;

	int i = 0;
	for (const cmd_function_t* cmd = cmd_functions; cmd; cmd = cmd->next) {
		if (match && !Com_Filter(match, cmd->name))
			continue;
		Com_Printf( "%s\n", cmd->name );
		++i;
	}

	Com_Printf( "%i commands\n", i );
}


void Cmd_Init()
{
	Cmd_AddCommand( "cmdlist", Cmd_List_f );
	Cmd_AddCommand( "exec", Cmd_Exec_f );
	Cmd_AddCommand( "vstr", Cmd_Vstr_f );
	Cmd_AddCommand( "echo", Cmd_Echo_f );
	Cmd_AddCommand( "wait", Cmd_Wait_f );
}

