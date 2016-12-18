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
// cvar.c -- dynamic variable tracking

#include "q_shared.h"
#include "qcommon.h"

static cvar_t* cvar_vars;
static cvar_t* cvar_cheats;
int cvar_modifiedFlags;

#define MAX_CVARS 1024
static cvar_t cvar_indexes[MAX_CVARS];
static int cvar_numIndexes;

#define CVAR_HASH_SIZE 256
static cvar_t* hashTable[CVAR_HASH_SIZE];


static long Cvar_Hash( const char* s )
{
	long hash = 0;

	for (int i = 0; s[i]; ++i) {
		hash += (long)(tolower(s[i])) * (i+119);
	}

	return (hash & (CVAR_HASH_SIZE-1));
}


static qbool Cvar_ValidateString( const char *s )
{
	if ( !s ) {
		return qfalse;
	}
	if ( strchr( s, '\\' ) ) {
		return qfalse;
	}
	if ( strchr( s, '\"' ) ) {
		return qfalse;
	}
	if ( strchr( s, ';' ) ) {
		return qfalse;
	}
	return qtrue;
}


static cvar_t* Cvar_FindVar( const char* var_name )
{
	long hash = Cvar_Hash(var_name);

	for (cvar_t* var = hashTable[hash]; var; var = var->hashNext) {
		if (!Q_stricmp(var_name, var->name)) {
			return var;
		}
	}

	return NULL;
}


float Cvar_VariableValue( const char *var_name )
{
	const cvar_t* var = Cvar_FindVar(var_name);
	if (!var)
		return 0;
	return var->value;
}


int Cvar_VariableIntegerValue( const char *var_name )
{
	const cvar_t* var = Cvar_FindVar(var_name);
	if (!var)
		return 0;
	return var->integer;
}


const char *Cvar_VariableString( const char *var_name )
{
	const cvar_t* var = Cvar_FindVar(var_name);
	if (!var)
		return "";
	return var->string;
}


void Cvar_VariableStringBuffer( const char *var_name, char *buffer, int bufsize )
{
	const cvar_t* var = Cvar_FindVar(var_name);
	if (!var) {
		*buffer = 0;
		return;
	}
	Q_strncpyz( buffer, var->string, bufsize );
}


int Cvar_Flags(const char *var_name)
{
	const cvar_t* var;

	if (!(var = Cvar_FindVar(var_name)))
		return CVAR_NONEXISTENT;

	return var->flags;
}


void Cvar_CommandCompletion( void(*callback)(const char *s) )
{
	for (const cvar_t* cvar = cvar_vars; cvar; cvar = cvar->next) {
		if ( !(cvar->flags & CVAR_CHEAT) || cvar_cheats->integer ) {
			callback( cvar->name );
		}
	}
}


static cvar_t* Cvar_Set2( const char *var_name, const char *value, qbool force )
{
//	Com_DPrintf( "Cvar_Set2: %s %s\n", var_name, value );

	if ( !Cvar_ValidateString( var_name ) ) {
		Com_Printf( "invalid cvar name string: %s\n", var_name );
		var_name = "BADNAME";
	}

	cvar_t* var = Cvar_FindVar(var_name);
	if (!var) {
		if ( !value )
			return NULL;
		// create it
		return Cvar_Get( var_name, value, force ? 0 : CVAR_USER_CREATED );
	}

	if (!value) {
		value = var->resetString;
	}

	if (!strcmp(value,var->string)) {
		return var;
	}
	// note what types of cvars have been modified (userinfo, archive, serverinfo, systeminfo)
	cvar_modifiedFlags |= var->flags;

	if (!force)
	{
		if (var->flags & CVAR_ROM)
		{
			Com_Printf( "%s is read only.\n", var_name );
			return var;
		}

		if (var->flags & CVAR_INIT)
		{
			Com_Printf( "%s is write protected.\n", var_name );
			return var;
		}

		if ( (var->flags & CVAR_CHEAT) && !cvar_cheats->integer )
		{
			Com_Printf( "%s is cheat protected.\n", var_name );
			return var;
		}

		if (var->flags & CVAR_LATCH)
		{
			if (var->latchedString)
			{
				if (strcmp(value, var->latchedString) == 0)
					return var;
				Z_Free( var->latchedString );
			}
			else
			{
				if (strcmp(value, var->string) == 0)
					return var;
			}

			Com_Printf( "%s will be changed upon restarting.\n", var_name );
			var->latchedString = CopyString(value);
			var->modified = qtrue;
			var->modificationCount++;
			return var;
		}

	}
	else
	{
		if (var->latchedString)
		{
			Z_Free( var->latchedString );
			var->latchedString = NULL;
		}
	}

	if (!strcmp(value, var->string))
		return var;		// not changed

	var->modified = qtrue;
	var->modificationCount++;

	Z_Free( var->string );	// free the old value string

	var->string = CopyString(value);
	var->value = atof( var->string );
	var->integer = atoi( var->string );

	return var;
}


/*
============
Cvar_Get

If the variable already exists, the value will not be set unless CVAR_ROM
The flags will be or'ed in if the variable exists.
============
*/
cvar_t* Cvar_Get( const char *var_name, const char *var_value, int flags )
{
	if ( !var_name || !var_value ) {
		Com_Error( ERR_FATAL, "Cvar_Get: NULL parameter" );
	}

	if ( !Cvar_ValidateString( var_name ) ) {
		Com_Printf("invalid cvar name string: %s\n", var_name );
		var_name = "BADNAME";
	}

	cvar_t* var = Cvar_FindVar( var_name );
	if ( var ) {
		// if the C code is now specifying a variable that the user already
		// set a value for, take the new value as the reset value
		if ( ( var->flags & CVAR_USER_CREATED ) && !( flags & CVAR_USER_CREATED )
			&& var_value[0] ) {
			var->flags &= ~CVAR_USER_CREATED;
			Z_Free( var->resetString );
			var->resetString = CopyString( var_value );
			// needs to be set so that cvars the game tags as SERVERINFO get sent to clients
			cvar_modifiedFlags |= flags;
		}

		var->flags |= flags;
		// only allow one non-empty reset string without a warning
		// KHB 071110  no, that's wrong for several reasons, notably vm changes caused by pure
		if ((flags & CVAR_ROM) || !var->resetString[0]) {
			Z_Free( var->resetString );
			var->resetString = CopyString( var_value );
		} else if ( var_value[0] && strcmp( var->resetString, var_value ) ) {
			Com_DPrintf( "Warning: cvar \"%s\" given initial values: \"%s\" and \"%s\"\n",
				var_name, var->resetString, var_value );
		}

		// if we have a latched string, take that value now
		if ( var->latchedString ) {
			char* s = var->latchedString;
			var->latchedString = NULL;	// otherwise cvar_set2 would free it
			Cvar_Set2( var_name, s, qtrue );
			Z_Free( s );
		}

/* KHB  note that this is #if 0'd in the 132 code, but is actually REQUIRED for correctness
	consider a cgame that sets a CVAR_ROM client version:
	you connect to a v1 server, load the v1 cgame, and set the ROM version to v1
	you then connect to a v2 server and correctly load the v2 cgame, but
	when that registers its GENUINELY "NEW" version var, the value is ignored
	so now you have a CVAR_ROM with the wrong value in it. gg.
i'm preserving this incorrect behavior FOR NOW for compatability, because
game\ai_main.c(1352): trap_Cvar_Register( &mapname, "mapname", "", CVAR_SERVERINFO | CVAR_ROM );
breaks every single mod except CPMA otherwise, but it IS wrong, and critically so
		// CVAR_ROM always overrides
		if (flags & CVAR_ROM) {
			Cvar_Set2( var_name, var_value, qtrue );
		}
*/
		return var;
	}

	//
	// allocate a new cvar
	//
	if ( cvar_numIndexes >= MAX_CVARS ) {
		Com_Error( ERR_FATAL, "MAX_CVARS" );
	}
	var = &cvar_indexes[cvar_numIndexes];
	cvar_numIndexes++;
	var->name = CopyString( var_name );
	var->string = CopyString( var_value );
	var->modified = qtrue;
	var->modificationCount = 1;
	var->value = atof(var->string);
	var->integer = atoi(var->string);
	var->resetString = CopyString( var_value );

	// link the variable in
	var->next = cvar_vars;
	cvar_vars = var;

	var->flags = flags;
	cvar_modifiedFlags |= flags; // needed so USERINFO cvars created by cgame actually get sent

	long hash = Cvar_Hash(var_name);
	var->hashNext = hashTable[hash];
	hashTable[hash] = var;

	return var;
}


void Cvar_Set( const char *var_name, const char *value )
{
	Cvar_Set2( var_name, value, qtrue );
}


void Cvar_SetValue( const char *var_name, float value )
{
	if ( value == (int)value ) {
		Cvar_Set( var_name, va("%i", (int)value) );
	} else {
		Cvar_Set( var_name, va("%f", value) );
	}
}


void Cvar_Reset( const char *var_name )
{
	Cvar_Set2( var_name, NULL, qfalse );
}


// any testing variables will be reset to the safe values

void Cvar_SetCheatState()
{
	for (cvar_t* var = cvar_vars; var; var = var->next) {
		if ( var->flags & CVAR_CHEAT ) {
			// the CVAR_LATCHED|CVAR_CHEAT vars might escape the reset here
			// because of a different var->latchedString
			if (var->latchedString) {
				Z_Free(var->latchedString);
				var->latchedString = NULL;
			}
			if (strcmp(var->resetString,var->string)) {
				Cvar_Set( var->name, var->resetString );
			}
		}
	}
}


// handles variable inspection and changing from the console

qbool Cvar_Command()
{
	const cvar_t* v = Cvar_FindVar(Cmd_Argv(0));
	if (!v)
		return qfalse;

	// perform a variable print or set
	if ( Cmd_Argc() == 1 ) {
		Com_Printf ("\"%s\" is:\"%s" S_COLOR_WHITE "\" default:\"%s" 
			S_COLOR_WHITE "\"\n", v->name, v->string, v->resetString );
		if ( v->latchedString ) {
			Com_Printf( "latched: \"%s\"\n", v->latchedString );
		}
		return qtrue;
	}

	// set the value if forcing isn't required
	Cvar_Set2( v->name, Cmd_Argv(1), qfalse );
	return qtrue;
}


// toggles a cvar for easy single key binding

static void Cvar_Toggle_f( void )
{
	if ( Cmd_Argc() != 2 ) {
		Com_Printf ("usage: toggle <variable>\n");
		return;
	}

	int v = !Cvar_VariableValue( Cmd_Argv( 1 ) );
	Cvar_Set2( Cmd_Argv(1), va("%i", v), qfalse );
}


// Allows setting and defining of arbitrary cvars from console
// even if they weren't declared in C code

static void Cvar_Set_f( void )
{
	int c = Cmd_Argc();
	if ( c < 3 ) {
		Com_Printf ("usage: set <variable> <value>\n");
		return;
	}

	char combined[MAX_STRING_TOKENS];
	combined[0] = 0;
	int l = 0;
	for (int i = 2; i < c; i++) {
		int len = strlen ( Cmd_Argv( i ) ) + 1;
		if ( l + len >= MAX_STRING_TOKENS - 2 ) {
			break;
		}
		strcat( combined, Cmd_Argv( i ) );
		if ( i != c-1 ) {
			strcat( combined, " " );
		}
		l += len;
	}

	Cvar_Set2( Cmd_Argv(1), combined, qfalse );
}


static void Cvar_SetAndFlag( const char* cmd, int flag )
{
	if ( Cmd_Argc() != 3 ) {
		Com_Printf( "usage: %s <variable> <value>\n", cmd );
		return;
	}

	Cvar_Set_f();
	cvar_t* v = Cvar_FindVar( Cmd_Argv(1) );
	if ( !v ) {
		Com_DPrintf( "Warning: couldn't find cvar %s\n", Cmd_Argv(1) );
		return;
	}

	v->flags |= flag;
}


static void Cvar_SetU_f( void )
{
	Cvar_SetAndFlag( "setu", CVAR_USERINFO );
}


static void Cvar_SetS_f( void )
{
	Cvar_SetAndFlag( "sets", CVAR_SERVERINFO );
}


static void Cvar_SetA_f( void )
{
	Cvar_SetAndFlag( "seta", CVAR_ARCHIVE );
}


static void Cvar_Reset_f( void )
{
	if ( Cmd_Argc() != 2 ) {
		Com_Printf ("usage: reset <variable>\n");
		return;
	}
	Cvar_Reset( Cmd_Argv( 1 ) );
}


// appends lines containing "seta variable value" for all cvars with the archive flag set

void Cvar_WriteVariables( fileHandle_t f )
{
	for (const cvar_t* var = cvar_vars; var; var = var->next) {
		if ((Q_stricmp( var->name, "cl_cdkey" ) == 0) || !(var->flags & CVAR_ARCHIVE))
			continue;
		// write the latched value, even if it hasn't taken effect yet
		FS_Printf( f, "seta %s \"%s\"\n", var->name, var->latchedString ? var->latchedString : var->string );
	}
}


static void Cvar_List_f( void )
{
	const char* match = (Cmd_Argc() > 1) ? Cmd_Argv(1) : NULL;

	int i = 0;
	for (const cvar_t* var = cvar_vars; var; var = var->next, ++i)
	{
		if (match && !Com_Filter(match, var->name))
			continue;

		Com_Printf( (var->flags & CVAR_SERVERINFO) ? "S" : " " );
		Com_Printf( (var->flags & CVAR_USERINFO) ? "U" : " " );
		Com_Printf( (var->flags & CVAR_ROM) ? "R" : " " );
		Com_Printf( (var->flags & CVAR_INIT) ? "I" : " " );
		Com_Printf( (var->flags & CVAR_ARCHIVE) ? "A" : " " );
		Com_Printf( (var->flags & CVAR_LATCH) ? "L" : " " );
		Com_Printf( (var->flags & CVAR_CHEAT) ? "C" : " " );

		Com_Printf(" %s \"%s\"\n", var->name, var->string);
	}

	Com_Printf("\n%i total cvars\n", i);
	Com_Printf("%i cvar indexes\n", cvar_numIndexes);
}


// resets all cvars to their hardcoded values

static void Cvar_Restart_f( void )
{
	cvar_t	*var;
	cvar_t	**prev;

	prev = &cvar_vars;
	while ( 1 ) {
		var = *prev;
		if ( !var ) {
			break;
		}

		// don't mess with rom values, or some inter-module
		// communication will get broken (com_cl_running, etc)
		if ( var->flags & ( CVAR_ROM | CVAR_INIT | CVAR_NORESTART ) ) {
			prev = &var->next;
			continue;
		}

		// throw out any variables the user created
		if ( var->flags & CVAR_USER_CREATED ) {
			*prev = var->next;
			if ( var->name ) {
				Z_Free( var->name );
			}
			if ( var->string ) {
				Z_Free( var->string );
			}
			if ( var->latchedString ) {
				Z_Free( var->latchedString );
			}
			if ( var->resetString ) {
				Z_Free( var->resetString );
			}
			// clear the var completely, since we
			// can't remove the index from the list
			Com_Memset( var, 0, sizeof( var ) );
			continue;
		}

		Cvar_Set( var->name, var->resetString );

		prev = &var->next;
	}
}


const char* Cvar_InfoString( int bit )
{
	static char info[MAX_INFO_STRING];

	info[0] = 0;

	for ( const cvar_t* var = cvar_vars ; var ; var = var->next ) {
		if (var->flags & bit) {
			Info_SetValueForKey( info, var->name, var->string );
		}
	}

	return info;
}


// special version for very large infostrings, ie CS_SYSTEMINFO

const char* Cvar_InfoString_Big( int bit )
{
	static char info[BIG_INFO_STRING];

	info[0] = 0;

	for ( const cvar_t* var = cvar_vars ; var ; var = var->next ) {
		if (var->flags & bit) {
			Info_SetValueForKey_Big( info, var->name, var->string );
		}
	}

	return info;
}


// pointless function solely for the UI VM, which never uses it
// but since it's not TECHNICALLY defective we'll keep it for now

void Cvar_InfoStringBuffer( int bit, char* buff, int buffsize )
{
	Q_strncpyz( buff, Cvar_InfoString(bit), buffsize );
}


// basically a slightly modified Cvar_Get for the interpreted modules

void Cvar_Register( vmCvar_t *vmCvar, const char *varName, const char *defaultValue, int flags )
{
	cvar_t* cv = Cvar_Get( varName, defaultValue, flags );
	if ( !vmCvar )
		return;

	vmCvar->handle = cv - cvar_indexes;
	vmCvar->modificationCount = -1;
	Cvar_Update( vmCvar );
}


// update an interpreted module's version of a cvar

void Cvar_Update( vmCvar_t *vmCvar )
{
	cvar_t* cv = NULL;
	assert(vmCvar);

	if ( (unsigned)vmCvar->handle >= cvar_numIndexes ) {
		Com_Error( ERR_DROP, "Cvar_Update: handle out of range" );
	}

	cv = cvar_indexes + vmCvar->handle;

	if ( cv->modificationCount == vmCvar->modificationCount ) {
		return;
	}
	if ( !cv->string ) {
		return;		// variable might have been cleared by a cvar_restart
	}
	vmCvar->modificationCount = cv->modificationCount;

	if ( strlen(cv->string)+1 > MAX_CVAR_VALUE_STRING )
			Com_Error( ERR_DROP, "Cvar_Update: src %s length %d exceeds MAX_CVAR_VALUE_STRING",
			cv->string, strlen(cv->string) );

	Q_strncpyz( vmCvar->string, cv->string,  MAX_CVAR_VALUE_STRING );
	vmCvar->value = cv->value;
	vmCvar->integer = cv->integer;
}


void Cvar_Init()
{
	cvar_cheats = Cvar_Get( "sv_cheats", "1", CVAR_ROM | CVAR_SYSTEMINFO );

	Cmd_AddCommand( "toggle", Cvar_Toggle_f );
	Cmd_AddCommand( "set", Cvar_Set_f );
	Cmd_AddCommand( "sets", Cvar_SetS_f );
	Cmd_AddCommand( "setu", Cvar_SetU_f );
	Cmd_AddCommand( "seta", Cvar_SetA_f );
	Cmd_AddCommand( "reset", Cvar_Reset_f );
	Cmd_AddCommand( "cvarlist", Cvar_List_f );
	Cmd_AddCommand( "cvar_restart", Cvar_Restart_f );
}
