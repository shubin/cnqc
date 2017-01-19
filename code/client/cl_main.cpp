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
// cl_main.c  -- client main loop

#include "client.h"


cvar_t	*cl_debugMove;

cvar_t	*rcon_client_password;
cvar_t	*rconAddress;

cvar_t* cl_timeout;
cvar_t* cl_maxpackets;
cvar_t* cl_packetdup;
cvar_t* cl_timeNudge;
cvar_t* cl_showTimeDelta;
cvar_t* cl_serverStatusResendTime;
cvar_t* cl_shownet;
cvar_t* cl_showSend;

cvar_t	*cl_timedemo;
cvar_t	*cl_aviFrameRate;
cvar_t	*cl_aviMotionJpeg;

static cvar_t* cl_motd;
static cvar_t* cl_motdString;

cvar_t	*cl_allowDownload;
cvar_t	*cl_inGameVideo;

#if defined(USE_CURL)
cvar_t	*cl_dlURL;
#endif

clientActive_t		cl;
clientConnection_t	clc;
clientStatic_t		cls;
vm_t				*cgvm;

refexport_t re; // functions exported from refresh DLL

#define RETRANSMIT_TIMEOUT	3000	// time between connection packet retransmits


/*
===============
CL_CDDialog

Called by Com_Error when a cd is needed
===============
*/
void CL_CDDialog( void ) {
	cls.cddialog = qtrue;	// start it next frame
}


/*
=======================================================================

CLIENT RELIABLE COMMAND COMMUNICATION

=======================================================================
*/

/*
======================
CL_AddReliableCommand

The given command will be transmitted to the server, and is guaranteed to
not have future usercmd_t executed before it is executed
======================
*/
void CL_AddReliableCommand( const char *cmd ) {
	int		index;

	// if we would be losing an old command that hasn't been acknowledged,
	// we must drop the connection
	if ( clc.reliableSequence - clc.reliableAcknowledge > MAX_RELIABLE_COMMANDS ) {
		Com_Error( ERR_DROP, "Client command overflow" );
	}
	clc.reliableSequence++;
	index = clc.reliableSequence & ( MAX_RELIABLE_COMMANDS - 1 );
	Q_strncpyz( clc.reliableCommands[ index ], cmd, sizeof( clc.reliableCommands[ index ] ) );
}


/*
=======================================================================

CLIENT SIDE DEMO RECORDING

=======================================================================
*/


// saves the current net message to file, prefixed by the length

static void CL_WriteDemoMessage ( msg_t *msg, int headerBytes )
{
	int len, swlen;

	// write the packet sequence
	len = clc.serverMessageSequence;
	swlen = LittleLong( len );
	FS_Write( &swlen, 4, clc.demofile );

	// skip the packet sequencing information
	len = msg->cursize - headerBytes;
	swlen = LittleLong(len);
	FS_Write( &swlen, 4, clc.demofile );
	FS_Write( msg->data + headerBytes, len, clc.demofile );
}


static void CL_StopRecord_f( void )
{
	if ( !clc.demorecording ) {
		Com_Printf ("Not recording a demo.\n");
		return;
	}

	// finish up
	int len = -1;
	FS_Write (&len, 4, clc.demofile);
	FS_Write (&len, 4, clc.demofile);
	FS_FCloseFile (clc.demofile);
	clc.demofile = 0;
	clc.demorecording = qfalse;
	clc.showAnnoyingDemoRecordMessage = qfalse;
	Com_Printf ("Stopped demo.\n");
}


static const char* CL_DemoFilename()
{
	static char s[MAX_OSPATH];

	qtime_t t;
	Com_RealTime( &t );
	Com_sprintf( s, sizeof(s), "demos/%d_%02d_%02d-%02d_%02d_%02d.dm_%d",
			1900+t.tm_year, 1+t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, PROTOCOL_VERSION );

	return s;
}


// record [demoname]
// starts recording a demo from the current position

static void CL_Record_f()
{
	if ( Cmd_Argc() > 2 ) {
		Com_Printf ("record [demoname]\n");
		return;
	}

	if ( clc.demorecording ) {
		if (!clc.showAnnoyingDemoRecordMessage) {
			Com_Printf ("Already recording.\n");
		}
		return;
	}

	if ( cls.state != CA_ACTIVE ) {
		Com_Printf ("You must be in a level to record.\n");
		return;
	}

    char name[MAX_OSPATH];
    int i;
	const char* s;
	if ( Cmd_Argc() == 2 ) {
		Com_sprintf( name, sizeof(name), "demos/%s.dm_%d", Cmd_Argv(1), PROTOCOL_VERSION );
		s = name;
	} else {
		s = CL_DemoFilename();
	}

	clc.demofile = FS_FOpenFileWrite( s );
	if ( !clc.demofile ) {
		Com_Printf( "ERROR: couldn't open %s\n", s );
		return;
	}

	Com_Printf( "recording to %s\n", s );
	clc.demorecording = qtrue;
	clc.showAnnoyingDemoRecordMessage = !Cvar_VariableValue("ui_recordSPDemo");

	Q_strncpyz( clc.demoName, s, sizeof( clc.demoName ) );

	// don't start saving messages until a non-delta compressed message is received
	clc.demowaiting = qtrue;

	// write out the gamestate message
	msg_t msg;
	byte buf[MAX_MSGLEN];
	MSG_Init( &msg, buf, sizeof(buf) );
	MSG_Bitstream( &msg );

	// NOTE, MRE: all server->client messages now acknowledge
	MSG_WriteLong( &msg, clc.reliableSequence );

	MSG_WriteByte( &msg, svc_gamestate );
	MSG_WriteLong( &msg, clc.serverCommandSequence );

	// configstrings
	for ( i = 0 ; i < MAX_CONFIGSTRINGS ; i++ ) {
		if ( !cl.gameState.stringOffsets[i] )
			continue;
		MSG_WriteByte( &msg, svc_configstring );
		MSG_WriteShort( &msg, i );
		MSG_WriteBigString( &msg, cl.gameState.stringData + cl.gameState.stringOffsets[i] );
	}

	// baselines
	entityState_t nullstate;
	Com_Memset( &nullstate, 0, sizeof(nullstate) );
	for ( i = 0; i < MAX_GENTITIES ; i++ ) {
		entityState_t* ent = &cl.entityBaselines[i];
		if (!ent->number)
			continue;
		MSG_WriteByte( &msg, svc_baseline );
		MSG_WriteDeltaEntity( &msg, &nullstate, ent, qtrue );
	}

	MSG_WriteByte( &msg, svc_EOF ); // finished writing the gamestate stuff

	MSG_WriteLong( &msg, clc.clientNum );
	MSG_WriteLong( &msg, clc.checksumFeed );

	MSG_WriteByte( &msg, svc_EOF ); // finished writing the client packet

	int len;
	// write it to the demo file
	len = LittleLong( clc.serverMessageSequence - 1 );
	FS_Write( &len, 4, clc.demofile );

	len = LittleLong( msg.cursize );
	FS_Write( &len, 4, clc.demofile );
	FS_Write( msg.data, msg.cursize, clc.demofile );

	// the rest of the demo file will be copied from net messages
}


static void CL_CompleteDemoRecord_f( int startArg, int compArg )
{
	if ( startArg + 1 == compArg )
		Field_AutoCompleteDemoNameWrite( startArg, compArg );
}


/*
=======================================================================

CLIENT SIDE DEMO PLAYBACK

=======================================================================
*/


static void CL_DemoCompleted()
{
	if (cl_timedemo && cl_timedemo->integer) {
		int time = Sys_Milliseconds() - clc.timeDemoStart;
		if ( time > 0 ) {
			Com_Printf ("%i frames, %3.1f seconds: %3.1f fps\n", clc.timeDemoFrames,
			time/1000.0, clc.timeDemoFrames*1000.0 / time);
		}
	}

	CL_Disconnect( qtrue );
	CL_NextDemo();
}

/*
=================
CL_ReadDemoMessage
=================
*/
void CL_ReadDemoMessage( void ) {

	if ( !clc.demofile ) {
		CL_DemoCompleted();
		return;
	}

	// get the sequence number
    int	s;
	int r = FS_Read( &s, 4, clc.demofile);
	if ( r != 4 ) {
		CL_DemoCompleted();
		return;
	}
	clc.serverMessageSequence = LittleLong( s );

    msg_t		buf;
    byte		bufData[ MAX_MSGLEN ];
	// init the message
	MSG_Init( &buf, bufData, sizeof( bufData ) );

	// get the length
	r = FS_Read (&buf.cursize, 4, clc.demofile);
	if ( r != 4 ) {
		CL_DemoCompleted();
		return;
	}
	buf.cursize = LittleLong( buf.cursize );
	if ( buf.cursize == -1 ) {
		CL_DemoCompleted();
		return;
	}
	if ( buf.cursize > buf.maxsize ) {
		Com_Error (ERR_DROP, "CL_ReadDemoMessage: demoMsglen > MAX_MSGLEN");
	}
	r = FS_Read( buf.data, buf.cursize, clc.demofile );
	if ( r != buf.cursize ) {
		Com_Printf( "Demo file was truncated.\n");
		CL_DemoCompleted();
		return;
	}

	clc.lastPacketTime = cls.realtime;
	buf.readcount = 0;
	CL_ParseServerMessage( &buf );
}


static const int demo_protocols[] = { 68, 67, 66, 0 };

static void CL_WalkDemoExt( const char* arg, char* name, fileHandle_t* fh )
{
	*fh = 0;

	for (int i = 0; demo_protocols[i]; ++i)
	{
		Com_sprintf( name, MAX_OSPATH, "demos/%s.dm_%d", arg, demo_protocols[i] );
		FS_FOpenFileRead( name, fh, qtrue );
		if (*fh)
		{
			Com_Printf("Demo file: %s\n", name);
			return;
		}
	}

	Com_Printf( "No match: demos/%s.dm_*\n", arg );
}


void CL_PlayDemo_f()
{

	if (Cmd_Argc() != 2) {
		Com_Printf ("demo <demoname>\n");
		return;
	}

	// make sure a local server is killed
	Cvar_Set( "sv_killserver", "1" );

	CL_Disconnect( qtrue );
    char	name[MAX_OSPATH];

	// open the demo file
	const char* arg = Cmd_Argv(1);

	// check for an extension .dm_?? (?? is protocol)
	const char* ext = arg + strlen(arg) - 6;
	if ((strlen(arg) > 6) && (ext[0] == '.') && ((ext[1] == 'd') || (ext[1] == 'D')) && ((ext[2] == 'm') || (ext[2] == 'M')) && (ext[3] == '_'))
	{
		int protocol = atoi(ext + 4), i = 0;
		while (demo_protocols[i]) {
			if (demo_protocols[i] == protocol)
				break;
			++i;
		}
		if (demo_protocols[i])
		{
			Com_sprintf( name, sizeof(name), "demos/%s", arg );
			FS_FOpenFileRead( name, &clc.demofile, qtrue );
		} else {
            char	retry[MAX_OSPATH];

			Com_Printf("Protocol %d not supported for demos\n", protocol);
			Q_strncpyz(retry, arg, sizeof(retry));
			retry[strlen(retry)-6] = 0;
			CL_WalkDemoExt( retry, name, &clc.demofile );
		}
	} else {
		CL_WalkDemoExt( arg, name, &clc.demofile );
	}

	if (!clc.demofile) {
		Com_Error( ERR_DROP, "couldn't open %s", name);
		return;
	}
	Q_strncpyz( clc.demoName, Cmd_Argv(1), sizeof( clc.demoName ) );

	Con_Close();

	cls.state = CA_CONNECTED;
	clc.demoplaying = qtrue;
	Q_strncpyz( cls.servername, Cmd_Argv(1), sizeof( cls.servername ) );

	// read demo messages until connected
	while ( cls.state >= CA_CONNECTED && cls.state < CA_PRIMED ) {
		CL_ReadDemoMessage();
	}
	// don't get the first snapshot this frame, to prevent the long
	// time from the gamestate load from messing causing a time skip
	clc.firstDemoFrameSkipped = qfalse;
}


static void CL_CompleteDemoPlay_f( int startArg, int compArg )
{
	if ( startArg + 1 == compArg )
		Field_AutoCompleteDemoNameRead( startArg, compArg );
}


/*
====================
CL_StartDemoLoop

Closing the main menu will restart the demo loop
====================
*/
void CL_StartDemoLoop( void ) {
	// start the demo loop again
	Cbuf_AddText ("d1\n");
	cls.keyCatchers = 0;
}

/*
==================
CL_NextDemo

Called when a demo or cinematic finishes
If the "nextdemo" cvar is set, that command will be issued
==================
*/
void CL_NextDemo( void ) {
	char	v[MAX_STRING_CHARS];

	Q_strncpyz( v, Cvar_VariableString ("nextdemo"), sizeof(v) );
	v[MAX_STRING_CHARS-1] = 0;
	Com_DPrintf("CL_NextDemo: %s\n", v );
	if (!v[0]) {
		return;
	}

	Cvar_Set ("nextdemo","");
	Cbuf_AddText (v);
	Cbuf_AddText ("\n");
	Cbuf_Execute();
}


///////////////////////////////////////////////////////////////


/*
=====================
CL_ShutdownAll
=====================
*/
void CL_ShutdownAll(void)
{
#if defined(USE_CURL)
	CL_cURL_Shutdown();
#endif
	// clear sounds
	S_DisableSounds();
	// shutdown CGame
	CL_ShutdownCGame();
	// shutdown UI
	CL_ShutdownUI();

	// shutdown the renderer
	if ( re.Shutdown ) {
		re.Shutdown( qfalse );		// don't destroy window or context
	}

	cls.uiStarted = qfalse;
	cls.cgameStarted = qfalse;
	cls.rendererStarted = qfalse;
	cls.soundRegistered = qfalse;
}


/*
Authorization server protocol
-----------------------------

All commands are text in Q3 out of band packets (leading 0xff 0xff 0xff 0xff).

Whenever the client tries to get a challenge from the server it wants to
connect to, it also blindly fires off a packet to the authorize server:

getKeyAuthorize <challenge> <cdkey>

cdkey may be "demo"


#OLD The authorize server returns a:
#OLD 
#OLD keyAthorize <challenge> <accept | deny>
#OLD 
#OLD A client will be accepted if the cdkey is valid and it has not been used by any other IP
#OLD address in the last 15 minutes.


The server sends a:

getIpAuthorize <challenge> <ip>

The authorize server returns a:

ipAuthorize <challenge> <accept | deny | demo | unknown >

A client will be accepted if a valid cdkey was sent by that ip (only) in the last 15 minutes.
If no response is received from the authorize server after two tries, the client will be let
in anyway.
*/
static void CL_RequestAuthorization()
{
	char key[CDKEY_LEN + 1];

	if ( !cls.authorizeServer.port ) {
		Com_Printf( "Resolving %s\n", AUTHORIZE_SERVER_NAME );
		if ( !NET_StringToAdr( AUTHORIZE_SERVER_NAME, &cls.authorizeServer  ) ) {
			Com_Printf( "Couldn't resolve address\n" );
			return;
		}

		cls.authorizeServer.port = BigShort( PORT_AUTHORIZE );
		Com_Printf( "%s resolved to %i.%i.%i.%i:%i\n", AUTHORIZE_SERVER_NAME,
			cls.authorizeServer.ip[0], cls.authorizeServer.ip[1],
			cls.authorizeServer.ip[2], cls.authorizeServer.ip[3],
			BigShort( cls.authorizeServer.port ) );
	}
	if ( cls.authorizeServer.type == NA_BAD ) {
		return;
	}

	int i;
	for (i = 0; (i < CDKEY_LEN) && cl_cdkey[i]; ++i) {
		// the cd key should have already been checked for validity, but just in case:
		if ( ( cl_cdkey[i] >= '0' && cl_cdkey[i] <= '9' )
				|| ( cl_cdkey[i] >= 'a' && cl_cdkey[i] <= 'z' )
				|| ( cl_cdkey[i] >= 'A' && cl_cdkey[i] <= 'Z' )
				) {
			key[i] = cl_cdkey[i];
		} else {
			Com_Error( ERR_DROP, "Invalid CD Key" );
		}
	}
	key[i] = 0;

	const cvar_t* anon = Cvar_Get( "cl_anonymous", "0", CVAR_INIT|CVAR_SYSTEMINFO );
	NET_OutOfBandPrint( NS_CLIENT, cls.authorizeServer, va("getKeyAuthorize %i %s", anon->integer, key) );
}


// resend a connect message if the last one has timed out

static void CL_CheckForResend()
{
	// don't send anything if playing back a demo
	if ( clc.demoplaying ) {
		return;
	}

	// resend if we haven't gotten a reply yet
	if ( cls.state != CA_CONNECTING && cls.state != CA_CHALLENGING ) {
		return;
	}

	if ( cls.realtime - clc.connectTime < RETRANSMIT_TIMEOUT ) {
		return;
	}

	clc.connectTime = cls.realtime;	// for retransmit requests
	clc.connectPacketCount++;


	switch ( cls.state ) {
	case CA_CONNECTING:
		// requesting a challenge
		if ( !Sys_IsLANAddress( clc.serverAddress ) ) {
			CL_RequestAuthorization();
		}
		NET_OutOfBandPrint(NS_CLIENT, clc.serverAddress, "getchallenge");
		break;

	case CA_CHALLENGING:
	    {
            int		port, i;

            char	info[MAX_INFO_STRING];
            char	data[MAX_INFO_STRING];
		    // sending back the challenge
		    port = Cvar_VariableIntegerValue ("net_qport");

		    Q_strncpyz( info, Cvar_InfoString( CVAR_USERINFO ), sizeof( info ) );
		    Info_SetValueForKey( info, "protocol", va("%i", PROTOCOL_VERSION ) );
		    Info_SetValueForKey( info, "qport", va("%i", port ) );
		    Info_SetValueForKey( info, "challenge", va("%i", clc.challenge ) );

		    strcpy(data, "connect ");
		    // TTimo adding " " around the userinfo string to avoid truncated userinfo on the server
		    //   (Com_TokenizeString tokenizes around spaces)
		    data[8] = '"';

		    for(i=0;i<strlen(info);i++) {
			    data[9+i] = info[i];	// + (clc.challenge)&0x3;
		    }
		    data[9+i] = '"';
		    data[10+i] = 0;

		    NET_OutOfBandData( NS_CLIENT, clc.serverAddress, (byte *) &data[0], i+10 );
		    // the most current userinfo has been sent, so watch for any
		    // newer changes to userinfo variables
		    cvar_modifiedFlags &= ~CVAR_USERINFO;
		}
		break;

	default:
		Com_Error( ERR_FATAL, "CL_CheckForResend: bad cls.state" );
	}
}


/*
Called by CL_MapLoading, CL_Connect_f, CL_PlayDemo_f, and CL_ParseGamestate, the only
ways a client gets into a game
Also called by Com_Error
*/
void CL_FlushMemory()
{
	// shutdown all the client stuff
	CL_ShutdownAll();

	// if not running a server clear the whole hunk
	if ( !com_sv_running->integer ) {
		// clear the whole hunk
		Hunk_Clear();
		// clear collision map data
		CM_ClearMap();
	}
	else {
		// clear all the client data on the hunk
		Hunk_ClearToMark();
	}

	CL_StartHunkUsers();
}

/*
=====================
CL_MapLoading

A local server is starting to load a map, so update the
screen to let the user know about it, then dump all client
memory on the hunk from cgame, ui, and renderer
=====================
*/
void CL_MapLoading( void ) {
	if ( !com_cl_running->integer ) {
		return;
	}

	Con_Close();
	cls.keyCatchers = 0;

	// if we are already connected to the local host, stay connected
	if ( cls.state >= CA_CONNECTED && !Q_stricmp( cls.servername, "localhost" ) ) {
		cls.state = CA_CONNECTED;		// so the connect screen is drawn
		Com_Memset( cls.updateInfoString, 0, sizeof( cls.updateInfoString ) );
		Com_Memset( clc.serverMessage, 0, sizeof( clc.serverMessage ) );
		Com_Memset( &cl.gameState, 0, sizeof( cl.gameState ) );
		clc.lastPacketSentTime = -9999;
		SCR_UpdateScreen();
	} else {
		// clear nextmap so the cinematic shutdown doesn't execute it
		Cvar_Set( "nextmap", "" );
		CL_Disconnect( qtrue );
		Q_strncpyz( cls.servername, "localhost", sizeof(cls.servername) );
		cls.state = CA_CHALLENGING;		// so the connect screen is drawn
		cls.keyCatchers = 0;
		SCR_UpdateScreen();
		clc.connectTime = -RETRANSMIT_TIMEOUT;
		NET_StringToAdr( cls.servername, &clc.serverAddress);
		// we don't need a challenge on the localhost

		CL_CheckForResend();
	}
}


// called before parsing a gamestate

void CL_ClearState()
{
	//S_StopAllSounds();
	Com_Memset( &cl, 0, sizeof( cl ) );
}


/*
=====================
CL_Disconnect

Called when a connection, demo, or cinematic is being terminated.
Goes from a connected state to either a menu state or a console state
Sends a disconnect message to the server
This is also called on Com_Error and Com_Quit, so it shouldn't cause any errors
=====================
*/
void CL_Disconnect( qbool showMainMenu ) {
	if ( !com_cl_running || !com_cl_running->integer ) {
		return;
	}

	// shutting down the client so enter full screen ui mode
	Cvar_Set("r_uiFullScreen", "1");

	if ( clc.demorecording ) {
		CL_StopRecord_f ();
	}

	if (clc.download) {
		FS_FCloseFile( clc.download );
		clc.download = 0;
	}
	*clc.downloadTempName = *clc.downloadName = 0;
	Cvar_Set( "cl_downloadName", "" );

	if ( clc.demofile ) {
		FS_FCloseFile( clc.demofile );
		clc.demofile = 0;
	}

	if ( uivm && showMainMenu ) {
		VM_Call( uivm, UI_SET_ACTIVE_MENU, UIMENU_NONE );
	}

	SCR_StopCinematic ();
	S_ClearSoundBuffer();

	// send a disconnect message to the server
	// send it a few times in case one is dropped
	if ( cls.state >= CA_CONNECTED ) {
		CL_AddReliableCommand( "disconnect" );
		CL_WritePacket();
		CL_WritePacket();
		CL_WritePacket();
	}

	CL_ClearState();

	// wipe the client connection
	Com_Memset( &clc, 0, sizeof( clc ) );

	cls.state = CA_DISCONNECTED;

	// allow cheats locally
	Cvar_Set( "sv_cheats", "1" );

	// not connected to a pure server anymore
	cl_connectedToPureServer = qfalse;

	// Stop recording any video
	if( CL_VideoRecording( ) ) {
		CL_CloseAVI( );
	}
}


/*
adds the current command line as a clientCommand
things like godmode, noclip, etc, are commands directed to the server,
so when they are typed in at the console, they will need to be forwarded.
*/
void CL_ForwardCommandToServer( const char *string )
{
	const char* cmd = Cmd_Argv(0);

	// ignore key up commands
	if ( cmd[0] == '-' ) {
		return;
	}

	if ( clc.demoplaying || cls.state < CA_CONNECTED || cmd[0] == '+' ) {
		Com_Printf ("Unknown command \"%s\"\n", cmd);
		return;
	}

	if ( Cmd_Argc() > 1 ) {
		CL_AddReliableCommand( string );
	} else {
		CL_AddReliableCommand( cmd );
	}
}


static void CL_RequestMotd()
{
	char info[MAX_INFO_STRING];

	if ( !cl_motd->integer ) {
		return;
	}

	Com_Printf( "Resolving %s\n", UPDATE_SERVER_NAME );
	if ( !NET_StringToAdr( UPDATE_SERVER_NAME, &cls.updateServer  ) ) {
		Com_Printf( "Couldn't resolve address\n" );
		return;
	}

	cls.updateServer.port = BigShort( PORT_UPDATE );
	Com_Printf( "%s resolved to %i.%i.%i.%i:%i\n", UPDATE_SERVER_NAME,
		cls.updateServer.ip[0], cls.updateServer.ip[1],
		cls.updateServer.ip[2], cls.updateServer.ip[3],
		BigShort( cls.updateServer.port ) );

	info[0] = 0;
	// NOTE TTimo xoring against Com_Milliseconds, otherwise we may not have a qtrue randomization
	// only srand I could catch before here is tr_noise.c l:26 srand(1001)
	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=382
	// NOTE: the Com_Milliseconds xoring only affects the lower 16-bit word,
	//   but I decided it was enough randomization
	Com_sprintf( cls.updateChallenge, sizeof( cls.updateChallenge ), "%i", ((rand() << 16) ^ rand()) ^ Com_Milliseconds());

	Info_SetValueForKey( info, "challenge", cls.updateChallenge );
	Info_SetValueForKey( info, "renderer", cls.glconfig.renderer_string );
	Info_SetValueForKey( info, "version", com_version->string );

	NET_OutOfBandPrint( NS_CLIENT, cls.updateServer, "getmotd \"%s\"\n", info );
}


/*
======================================================================

CONSOLE COMMANDS

======================================================================
*/


static void CL_ForwardToServer_f()
{
	if ( cls.state != CA_ACTIVE || clc.demoplaying ) {
		Com_Printf ("Not connected to a server.\n");
		return;
	}
	// don't forward the first argument
	if ( Cmd_Argc() > 1 ) {
		CL_AddReliableCommand( Cmd_Args() );
	}
}


void CL_Disconnect_f( void )
{
	SCR_StopCinematic();
	if ( cls.state != CA_DISCONNECTED && cls.state != CA_CINEMATIC ) {
		Com_Error (ERR_DISCONNECT, "Disconnected from server");
	}
}


static void CL_Reconnect_f()
{
	if ( !strlen( cls.servername ) || !strcmp( cls.servername, "localhost" ) ) {
		Com_Printf( "Can't reconnect to localhost.\n" );
		return;
	}
	Cbuf_AddText( va("connect %s\n", cls.servername ) );
}


static void CL_Connect_f()
{
	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "usage: connect <server>\n");
		return;
	}

	// fire a message off to the motd server
	CL_RequestMotd();

	// clear any previous "server full" type messages
	clc.serverMessage[0] = 0;

	const char* server = Cmd_Argv(1);

	if ( com_sv_running->integer && !strcmp( server, "localhost" ) ) {
		// if running a local server, kill it
		SV_Shutdown( "Server quit" );
	}

	// make sure a local server is killed
	Cvar_Set( "sv_killserver", "1" );
	SV_Frame( 0 );

	CL_Disconnect( qtrue );
	Con_Close();

	Q_strncpyz( cls.servername, server, sizeof(cls.servername) );

	if (!NET_StringToAdr( cls.servername, &clc.serverAddress) ) {
		Com_Printf ("Bad server address\n");
		cls.state = CA_DISCONNECTED;
		return;
	}
	if (clc.serverAddress.port == 0) {
		clc.serverAddress.port = BigShort( PORT_SERVER );
	}
	Com_Printf( "%s resolved to %i.%i.%i.%i:%i\n", cls.servername,
		clc.serverAddress.ip[0], clc.serverAddress.ip[1],
		clc.serverAddress.ip[2], clc.serverAddress.ip[3],
		BigShort( clc.serverAddress.port ) );

	// if we aren't playing on a lan, we need to authenticate
	// with the cd key
	if ( NET_IsLocalAddress( clc.serverAddress ) ) {
		cls.state = CA_CHALLENGING;
	} else {
		cls.state = CA_CONNECTING;
	}

	cls.keyCatchers = 0;
	clc.connectTime = -99999;	// CL_CheckForResend() will fire immediately
	clc.connectPacketCount = 0;

	// server connection string
	Cvar_Set( "cl_currentServerAddress", server );
}


#define MAX_RCON_MESSAGE 1024

// send the rest of the command line over as an unconnected command

static void CL_Rcon_f( void )
{
	if ( !rcon_client_password->string ) {
		Com_Printf ("You must set 'rconpassword' before\n"
					"issuing an rcon command.\n");
		return;
	}

	char message[MAX_RCON_MESSAGE];
	message[0] = -1;
	message[1] = -1;
	message[2] = -1;
	message[3] = -1;
	message[4] = 0;

	Q_strcat (message, MAX_RCON_MESSAGE, "rcon ");

	Q_strcat (message, MAX_RCON_MESSAGE, rcon_client_password->string);
	Q_strcat (message, MAX_RCON_MESSAGE, " ");

	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=543
	Q_strcat (message, MAX_RCON_MESSAGE, Cmd_Cmd()+5);

	netadr_t to;
	if ( cls.state >= CA_CONNECTED ) {
		to = clc.netchan.remoteAddress;
	} else {
		if (!strlen(rconAddress->string)) {
			Com_Printf ("You must either be connected,\n"
						"or set the 'rconAddress' cvar\n"
						"to issue rcon commands\n");

			return;
		}
		NET_StringToAdr (rconAddress->string, &to);
		if (to.port == 0) {
			to.port = BigShort (PORT_SERVER);
		}
	}

	NET_SendPacket (NS_CLIENT, strlen(message)+1, message, to);
}


static void CL_CompleteRcon_f( int startArg, int compArg )
{
	if ( startArg < compArg )
		Field_AutoCompleteFrom( startArg + 1, compArg, qtrue, qtrue );
}


// if we are pure we need to send back a command with our referenced pk3 checksums

static void CL_SendPureChecksums()
{
	char cMsg[MAX_INFO_VALUE];
	Com_sprintf( cMsg, sizeof(cMsg), "cp " );
	Q_strcat( cMsg, sizeof(cMsg), va("%d ", cl.serverId) );
	Q_strcat( cMsg, sizeof(cMsg), FS_ReferencedPakPureChecksums() );
	CL_AddReliableCommand( cMsg );
}


static void CL_ResetPureClientAtServer()
{
	CL_AddReliableCommand( "vdr" );
}


static void CL_OpenedPK3List_f( void )
{
	Com_Printf( "Opened PK3 Names: %s\n", FS_LoadedPakNames() );
}


static void CL_ReferencedPK3List_f( void )
{
	Com_Printf( "Referenced PK3 Names: %s\n", FS_ReferencedPakNames() );
}


static void CL_Configstrings_f( void )
{
	int		i;
	int		ofs;

	if ( cls.state != CA_ACTIVE ) {
		Com_Printf( "Not connected to a server.\n");
		return;
	}

	for ( i = 0 ; i < MAX_CONFIGSTRINGS ; i++ ) {
		ofs = cl.gameState.stringOffsets[ i ];
		if ( !ofs ) {
			continue;
		}
		Com_Printf( "%4i: %s\n", i, cl.gameState.stringData + ofs );
	}
}


static void CL_Clientinfo_f( void )
{
	Com_Printf( "--------- Client Information ---------\n" );
	Com_Printf( "state: %i\n", cls.state );
	Com_Printf( "Server: %s\n", cls.servername );
	Com_Printf( "User info settings:\n" );
	Info_Print( Cvar_InfoString_Big( CVAR_USERINFO ) ); // 1024 chars + 'userinfo '
	Com_Printf( "--------------------------------------\n" );
}


///////////////////////////////////////////////////////////////


static void CL_DownloadsComplete()
{
#if defined(USE_CURL)
	// if we downloaded using cURL
	if( clc.cURLUsed ) {
		clc.cURLUsed = qfalse;
		CL_cURL_Shutdown();
		if( clc.cURLDisconnected ) {
			if( clc.downloadRestart ) {
				FS_Restart( clc.checksumFeed );
				clc.downloadRestart = qfalse;
			}
			clc.cURLDisconnected = qfalse;
			CL_Reconnect_f();
			return;
		}
	}
#endif

	// if we downloaded files we need to restart the file system
	if (clc.downloadRestart) {
		clc.downloadRestart = qfalse;

		FS_Restart(clc.checksumFeed); // we possibly downloaded a pak, restart the file system to load it

		// inform the server so we get new gamestate info
		CL_AddReliableCommand( "donedl" );

		// by sending the donedl command we request a new gamestate
		// so we don't want to load stuff yet
		return;
	}

	// let the client game init and load data
	cls.state = CA_LOADING;

	// Pump the loop, this may change gamestate!
	Com_EventLoop();

	// if the gamestate was changed by calling Com_EventLoop
	// then we loaded everything already and we don't want to do it again.
	if ( cls.state != CA_LOADING ) {
		return;
	}

	// starting to load a map so we get out of full screen ui mode
	Cvar_Set("r_uiFullScreen", "0");

	// flush client memory and start loading stuff
	// this will also (re)load the UI
	// if this is a local client then only the client part of the hunk
	// will be cleared, note that this is done after the hunk mark has been set
	CL_FlushMemory();

	// initialize the CGame
	cls.cgameStarted = qtrue;
	CL_InitCGame();

	// set pure checksums
	CL_SendPureChecksums();

	CL_WritePacket();
	CL_WritePacket();
	CL_WritePacket();
}


// requests a file to download from the server
// stores it in the current game directory

static void CL_BeginDownload( const char *localName, const char *remoteName )
{
	Com_DPrintf("***** CL_BeginDownload *****\n"
				"Localname: %s\n"
				"Remotename: %s\n"
				"****************************\n", localName, remoteName);

	Q_strncpyz ( clc.downloadName, localName, sizeof(clc.downloadName) );
	Com_sprintf( clc.downloadTempName, sizeof(clc.downloadTempName), "%s.tmp", localName );

	// Set so UI gets access to it
	Cvar_Set( "cl_downloadName", remoteName );
	Cvar_Set( "cl_downloadSize", "0" );
	Cvar_Set( "cl_downloadCount", "0" );
	Cvar_SetValue( "cl_downloadTime", cls.realtime );

	clc.downloadBlock = 0;
	clc.downloadCount = 0;

	CL_AddReliableCommand( va("download %s", remoteName) );
}


/*
=================
CL_NextDownload

A download completed or failed
=================
*/
void CL_NextDownload(void) {
	char *s;
	char *remoteName, *localName;
#if defined(USE_CURL)
	char *dlURL;
#endif

	qbool useCURL = qfalse;
	// We are looking to start a download here
	if (*clc.downloadList) {
		s = clc.downloadList;

		// format is:
		//  @remotename@localname@remotename@localname, etc.

		if (*s == '@')
			s++;
		remoteName = s;

		if ( (s = strchr(s, '@')) == NULL ) {
			CL_DownloadsComplete();
			return;
		}

		*s++ = 0;
		localName = s;
		if ( (s = strchr(s, '@')) != NULL )
			*s++ = 0;
		else
			s = localName + strlen(localName); // point at the nul byte

#if defined(USE_CURL)
		dlURL = clc.sv_dlURL;
		if( !*dlURL || ( cl_allowDownload->integer == 2 )) {
			dlURL = Cvar_VariableString( "cl_dlURL" );
		}

		if(!*dlURL) {
			Com_Printf("WARNING: no valid download URL.\n"
				"cl_allowDownload is %d\n"
				"To force, enter a valid cl_dlURL and set "
				"cl_allowdownload to 2.\n",
				cl_allowDownload->integer);
		}
		else if(!CL_cURL_Init()) {
			Com_Printf("WARNING: could not load "
				"cURL library\n");
		}
		else {
			CL_cURL_BeginDownload(localName, va("%s/%s",
				dlURL, remoteName));
			useCURL = qtrue;
		}
#endif

		if( !useCURL ) {
			if( !cl_allowDownload->integer ) {
				Com_Error(ERR_DROP, "UDP Downloads are "
					"disabled on your client. "
					"(cl_allowDownload is %d)",
					cl_allowDownload->integer);
			return;
			}
			else {
				CL_BeginDownload( localName, remoteName );
			}
		}
		clc.downloadRestart = qtrue;

		// move over the rest
		memmove( clc.downloadList, s, strlen(s) + 1);

		return;
	}

	CL_DownloadsComplete();
}


/*
=================
CL_InitDownloads

After receiving a valid game state, we valid the cgame and local zip files here
and determine if we need to download them
=================
*/
void CL_InitDownloads(void) {
  char missingfiles[1024];

  if ( !cl_allowDownload->integer )
  {
    // autodownload is disabled on the client
    // but it's possible that some referenced files on the server are missing
    if (FS_ComparePaks( missingfiles, sizeof( missingfiles ), qfalse ) )
    {      
      // NOTE TTimo I would rather have that printed as a modal message box
      //   but at this point while joining the game we don't know wether we will successfully join or not
      Com_Printf( "\nWARNING: You are missing some files referenced by the server:\n%s"
                  "You might not be able to join the game\n"
                  "Go to the setting menu to turn on autodownload, or get the file elsewhere\n\n", missingfiles );
    }
  }
  else if ( FS_ComparePaks( clc.downloadList, sizeof( clc.downloadList ) , qtrue ) ) {

    Com_Printf("Need paks: %s\n", clc.downloadList );

		if ( *clc.downloadList ) {
			// if autodownloading is not enabled on the server
			cls.state = CA_CONNECTED;
			CL_NextDownload();
			return;
		}

	}

	CL_DownloadsComplete();
}


/*
Sometimes the server can drop the client and the netchan based
disconnect can be lost.  If the client continues to send packets
to the server, the server will send out of band disconnect packets
to the client so it doesn't have to wait for the full timeout period.
*/
static void CL_DisconnectPacket( const netadr_t& from )
{
	if ( cls.state < CA_AUTHORIZING ) {
		return;
	}

	// if not from our server, ignore it
	if ( !NET_CompareAdr( from, clc.netchan.remoteAddress ) ) {
		return;
	}

	// if we have received packets within three seconds, ignore it
	// (it might be a malicious spoof)
	if ( cls.realtime - clc.lastPacketTime < 3000 ) {
		return;
	}

	// drop the connection
	Com_Printf( "Server disconnected for unknown reason\n" );
	Cvar_Set("com_errorMessage", "Server disconnected for unknown reason\n" );
	CL_Disconnect( qtrue );
}


static void CL_MotdPacket( const netadr_t& from )
{
	// if not from our server, ignore it
	if ( !NET_CompareAdr( from, cls.updateServer ) ) {
		return;
	}

	const char* s;
	const char* info = Cmd_Argv(1);

	s = Info_ValueForKey( info, "challenge" );
	if ( strcmp( s, cls.updateChallenge ) )
		return;

	s = Info_ValueForKey( info, "motd" );
	Q_strncpyz( cls.updateInfoString, info, sizeof( cls.updateInfoString ) );
	Cvar_Set( "cl_motdString", s );
}


// responses to broadcasts, etc

static void CL_ConnectionlessPacket( const netadr_t& from, msg_t* msg )
{
	MSG_BeginReadingOOB( msg );
	MSG_ReadLong( msg );	// skip the -1

	char* s = MSG_ReadStringLine( msg );
	Cmd_TokenizeString( s );

	const char* c = Cmd_Argv(0);
	Com_DPrintf ("CL packet %s: %s\n", NET_AdrToString(from), c);

	// challenge from the server we are connecting to
	if ( !Q_stricmp(c, "challengeResponse") ) {
		if ( cls.state != CA_CONNECTING ) {
			Com_Printf( "Unwanted challenge response received.  Ignored.\n" );
		} else {
			// start sending challenge repsonse instead of challenge request packets
			clc.challenge = atoi(Cmd_Argv(1));
			cls.state = CA_CHALLENGING;
			clc.connectPacketCount = 0;
			clc.connectTime = -99999;

			// take this address as the new server address.  This allows
			// a server proxy to hand off connections to multiple servers
			clc.serverAddress = from;
			Com_DPrintf ("challengeResponse: %d\n", clc.challenge);
		}
		return;
	}

	// server connection
	if ( !Q_stricmp(c, "connectResponse") ) {
		if ( cls.state >= CA_CONNECTED ) {
			Com_Printf ("Dup connect received.  Ignored.\n");
			return;
		}
		if ( cls.state != CA_CHALLENGING ) {
			Com_Printf ("connectResponse packet while not connecting.  Ignored.\n");
			return;
		}
		if ( !NET_CompareBaseAdr( from, clc.serverAddress ) ) {
			Com_Printf( "connectResponse from a different address.  Ignored.\n" );
			Com_Printf( "%s should have been %s\n", NET_AdrToString( from ), 
				NET_AdrToString( clc.serverAddress ) );
			return;
		}
		Netchan_Setup (NS_CLIENT, &clc.netchan, from, Cvar_VariableIntegerValue( "net_qport" ) );
		cls.state = CA_CONNECTED;
		clc.lastPacketSentTime = -9999;		// send first packet immediately
		return;
	}

	// server responding to an info broadcast
	if ( !Q_stricmp(c, "infoResponse") ) {
		CL_ServerInfoPacket( from, msg );
		return;
	}

	// server responding to a get playerlist
	if ( !Q_stricmp(c, "statusResponse") ) {
		CL_ServerStatusResponse( from, msg );
		return;
	}

	// a disconnect message from the server, which will happen if the server
	// dropped the connection but it is still getting packets from us
	if (!Q_stricmp(c, "disconnect")) {
		CL_DisconnectPacket( from );
		return;
	}

	// echo request from server
	if ( !Q_stricmp(c, "echo") ) {
		NET_OutOfBandPrint( NS_CLIENT, from, "%s", Cmd_Argv(1) );
		return;
	}

	// cd check
	if ( !Q_stricmp(c, "keyAuthorize") ) {
		// we don't use these now, so dump them on the floor
		return;
	}

	// global MOTD from id
	if ( !Q_stricmp(c, "motd") ) {
		CL_MotdPacket( from );
		return;
	}

	// echo request from server
	if ( !Q_stricmp(c, "print") ) {
		s = MSG_ReadString( msg );
		Q_strncpyz( clc.serverMessage, s, sizeof( clc.serverMessage ) );
		Com_Printf( "%s", s );
		if ( !Q_stricmpn(s, "/reconnect ASAP!", 16) ) {
			CL_Reconnect_f();
		}
		return;
	}

	// echo request from server
	if ( !Q_strncmp(c, "getserversResponse", 18) ) {
		CL_ServersResponsePacket( from, msg );
		return;
	}

	Com_DPrintf ("Unknown connectionless packet command.\n");
}


// a packet has arrived from the main event loop

void CL_PacketEvent( netadr_t from, msg_t* msg )
{
	clc.lastPacketTime = cls.realtime;

	if ( msg->cursize >= 4 && *(int *)msg->data == -1 ) {
		CL_ConnectionlessPacket( from, msg );
		return;
	}

	if ( cls.state < CA_CONNECTED ) {
		return;		// can't be a valid sequenced packet
	}

	if ( msg->cursize < 4 ) {
		Com_Printf( "%s: Runt packet\n", NET_AdrToString( from ) );
		return;
	}

	//
	// packet from server
	//
	if ( !NET_CompareAdr( from, clc.netchan.remoteAddress ) ) {
		Com_DPrintf( "%s:sequenced packet without connection\n", NET_AdrToString( from ) );
		// FIXME: send a client disconnect?
		return;
	}

	if (!CL_Netchan_Process( &clc.netchan, msg )) {
		return;		// out of order, duplicated, etc
	}

	// the header is different lengths for reliable and unreliable messages
	int headerBytes = msg->readcount;

	// track the last message received so it can be returned in 
	// client messages, allowing the server to detect a dropped
	// gamestate
	clc.serverMessageSequence = LittleLong( *(int *)msg->data );

	clc.lastPacketTime = cls.realtime;
	CL_ParseServerMessage( msg );

	//
	// we don't know if it is ok to save a demo message until
	// after we have parsed the frame
	//
	if ( clc.demorecording && !clc.demowaiting ) {
		CL_WriteDemoMessage( msg, headerBytes );
	}
}


static void CL_CheckTimeout()
{
	if (clc.demoplaying && (com_timescale->value == 0)) {
		cl.timeoutcount = 0;
		return;
	}

	if ( ( !cl_paused->integer || !sv_paused->integer )
		&& cls.state >= CA_CONNECTED && cls.state != CA_CINEMATIC
		&& cls.realtime - clc.lastPacketTime > cl_timeout->value*1000) {
		if (++cl.timeoutcount > 5) {	// timeoutcount saves debugger
			Com_Printf ("\nServer connection timed out.\n");
			CL_Disconnect( qtrue );
			return;
		}
	} else {
		cl.timeoutcount = 0;
	}
}


static void CL_CheckUserinfo()
{
	// don't add reliable commands when not yet connected
	if ( cls.state < CA_CHALLENGING ) {
		return;
	}
	// don't overflow the reliable command buffer when paused
	if ( cl_paused->integer ) {
		return;
	}
	// send a reliable userinfo update if needed
	if ( cvar_modifiedFlags & CVAR_USERINFO ) {
		cvar_modifiedFlags &= ~CVAR_USERINFO;
		CL_AddReliableCommand( va("userinfo \"%s\"", Cvar_InfoString( CVAR_USERINFO ) ) );
	}
}


void CL_Frame( int msec )
{
	if ( !com_cl_running->integer ) {
		return;
	}

#if defined(USE_CURL)
	if( clc.downloadCURLM ) {
		CL_cURL_PerformDownload();
		// we can't process frames normally when in disconnected
		// download mode since the ui vm expects cls.state to be
		// CA_CONNECTED
		if( clc.cURLDisconnected ) {
			cls.realFrametime = msec;
			cls.frametime = msec;
			cls.realtime += cls.frametime;
			SCR_UpdateScreen();
			S_Update();
			Con_RunConsole();
			cls.framecount++;
			return;
		}
	}
#endif

	if ( cls.cddialog ) {
		// bring up the cd error dialog if needed
		cls.cddialog = qfalse;
		VM_Call( uivm, UI_SET_ACTIVE_MENU, UIMENU_NEED_CD );
	} else if ( cls.state == CA_DISCONNECTED && !( cls.keyCatchers & KEYCATCH_UI )
		&& !com_sv_running->integer ) {
		// if disconnected, bring up the menu
		S_StopAllSounds();
		VM_Call( uivm, UI_SET_ACTIVE_MENU, UIMENU_MAIN );
	}

	// if recording an avi, lock to a fixed fps
	if ( CL_VideoRecording() && cl_aviFrameRate->integer && msec ) {
		// save the current screen
		CL_TakeVideoFrame();
		// fixed time for next frame
		msec = (int)ceil( (1000.0f / cl_aviFrameRate->value) * com_timescale->value );
		if (msec == 0) {
			msec = 1;
		}
	}

	// save the msec before checking pause
	cls.realFrametime = msec;

	// decide the simulation time
	cls.frametime = msec;

	cls.realtime += cls.frametime;

	if ( cl_timegraph->integer ) {
		SCR_DebugGraph ( cls.realFrametime * 0.25, 0 );
	}

	// see if we need to update any userinfo
	CL_CheckUserinfo();

	// if we haven't gotten a packet in a long time,
	// drop the connection
	CL_CheckTimeout();

	// send intentions now
	CL_SendCmd();

	// resend a connection request if necessary
	CL_CheckForResend();

	// decide on the serverTime to render
	CL_SetCGameTime();

	// update the screen
	SCR_UpdateScreen();

	// update audio
	S_Update();

	// advance local effects for next frame
	SCR_RunCinematic();

	Con_RunConsole();

	cls.framecount++;
}


///////////////////////////////////////////////////////////////


int CL_ScaledMilliseconds()
{
	return Sys_Milliseconds()*com_timescale->value;
}


static void CL_InitRenderer()
{
	// this sets up the renderer and calls R_Init
	re.BeginRegistration( &cls.glconfig );

	// load character sets
	cls.charSetShader = re.RegisterShader( "gfx/2d/bigchars" );
	cls.whiteShader = re.RegisterShader( "white" );
	g_console_field_width = cls.glconfig.vidWidth / SMALLCHAR_WIDTH - 2;
	g_consoleField.widthInChars = g_console_field_width;
}


// after the server has cleared the hunk, these will need to be restarted
// this is the only place that any of these functions are called from

void CL_StartHunkUsers( void ) {
	if (!com_cl_running) {
		return;
	}

	if ( !com_cl_running->integer ) {
		return;
	}

	if ( !cls.rendererStarted ) {
		cls.rendererStarted = qtrue;
		CL_InitRenderer();
	}

	if ( !cls.soundStarted ) {
		cls.soundStarted = qtrue;
		S_Init();
	}

	if ( !cls.soundRegistered ) {
		cls.soundRegistered = qtrue;
		S_BeginRegistration();
	}

	if ( !cls.uiStarted ) {
		cls.uiStarted = qtrue;
		CL_InitUI();
	}
}


static void QDECL CL_RefPrintf( printParm_t print_level, const char* fmt, ... )
{
	va_list va;
	char msg[MAXPRINTMSG];

	va_start( va, fmt );
	Q_vsnprintf( msg, sizeof(msg), fmt, va );
	va_end( va );

	if ( print_level == PRINT_ALL ) {
		Com_Printf( "%s", msg );
	} else if ( print_level == PRINT_WARNING ) {
		Com_Printf( S_COLOR_YELLOW "%s", msg );
	} else if ( print_level == PRINT_DEVELOPER ) {
		Com_DPrintf( S_COLOR_CYAN "%s", msg );
	}
}


static void* CL_RefMalloc( int size )
{
	return Z_TagMalloc( size, TAG_RENDERER );
}


static void CL_ShutdownRef()
{
	if ( !re.Shutdown ) {
		return;
	}
	re.Shutdown( qtrue );
	Com_Memset( &re, 0, sizeof( re ) );
}


static void CL_InitRef()
{
	refimport_t ri;

	ri.Cmd_AddCommand = Cmd_AddCommand;
	ri.Cmd_RemoveCommand = Cmd_RemoveCommand;
	ri.Cmd_Argc = Cmd_Argc;
	ri.Cmd_Argv = Cmd_Argv;
	ri.Printf = CL_RefPrintf;
	ri.Error = Com_Error;
	ri.Milliseconds = CL_ScaledMilliseconds;
	ri.Malloc = CL_RefMalloc;
	ri.Free = Z_Free;
#ifdef HUNK_DEBUG
	ri.Hunk_AllocDebug = Hunk_AllocDebug;
#else
	ri.Hunk_Alloc = Hunk_Alloc;
#endif
	ri.Hunk_AllocateTempMemory = Hunk_AllocateTempMemory;
	ri.Hunk_FreeTempMemory = Hunk_FreeTempMemory;
	ri.CM_DrawDebugSurface = CM_DrawDebugSurface;
	ri.FS_ReadFile = FS_ReadFile;
	ri.FS_FreeFile = FS_FreeFile;
	ri.FS_WriteFile = FS_WriteFile;
	ri.FS_FreeFileList = FS_FreeFileList;
	ri.FS_ListFiles = FS_ListFiles;
	ri.Cvar_Get = Cvar_Get;
	ri.Cvar_Set = Cvar_Set;

	// cinematic stuff

	ri.CIN_UploadCinematic = CIN_UploadCinematic;
	ri.CIN_PlayCinematic = CIN_PlayCinematic;
	ri.CIN_RunCinematic = CIN_RunCinematic;

	ri.CL_WriteAVIVideoFrame = CL_WriteAVIVideoFrame;

	re = *(GetRefAPI(&ri));

	// unpause so the cgame definitely gets a snapshot and renders a frame
	Cvar_Set( "cl_paused", "0" );
}


/*
Restart the video subsystem

we also have to reload the UI and CGame because the renderer
doesn't know what graphics to reload
*/
static void CL_Vid_Restart_f()
{
	// Settings may have changed so stop recording now
	if( CL_VideoRecording( ) ) {
		CL_CloseAVI( );
	}

	S_StopAllSounds(); // don't let them loop during the restart

	CL_ShutdownUI();
	CL_ShutdownCGame();
	CL_ShutdownRef();

	// client is no longer pure until new checksums are sent
	CL_ResetPureClientAtServer();

	FS_ClearPakReferences( FS_UI_REF | FS_CGAME_REF );
	// reinitialize the filesystem if the game directory or checksum has changed
	FS_ConditionalRestart( clc.checksumFeed );

	cls.rendererStarted = qfalse;
	cls.uiStarted = qfalse;
	cls.cgameStarted = qfalse;
	cls.soundRegistered = qfalse;

	// unpause so the cgame definitely gets a snapshot and renders a frame
	Cvar_Set( "cl_paused", "0" );

	// if not running a server clear the whole hunk
	if ( !com_sv_running->integer ) {
		Hunk_Clear();
	}
	else {
		// clear all the client data on the hunk
		Hunk_ClearToMark();
	}

	// initialize the renderer interface
	CL_InitRef();

	// startup all the client stuff
	CL_StartHunkUsers();

	// start the cgame if connected
	if ( cls.state > CA_CONNECTED && cls.state != CA_CINEMATIC ) {
		cls.cgameStarted = qtrue;
		CL_InitCGame();
		// send pure checksums
		CL_SendPureChecksums();
	}
}


// restart the sound subsystem
// the cgame must also be forced to restart because handles will be invalid

static void CL_Snd_Restart_f()
{
	S_Shutdown();
	S_Init();

	CL_Vid_Restart_f();
}


///////////////////////////////////////////////////////////////


static void CL_SetModel_f()
{
	char name[256];
	const char* arg = Cmd_Argv(1);

	if (arg[0]) {
		Cvar_Set( "model", arg );
	} else {
		Cvar_VariableStringBuffer( "model", name, sizeof(name) );
		Com_Printf("model is set to %s\n", name);
	}
}


///////////////////////////////////////////////////////////////


static void CL_Video_f()
{
	char s[ MAX_OSPATH ];

	if( Cmd_Argc( ) == 2 )
	{
		Com_sprintf( s, MAX_OSPATH, "videos/%s.avi", Cmd_Argv( 1 ) );
	}
	else
	{
		qtime_t t;
		Com_RealTime( &t );
		Com_sprintf( s, sizeof(s), "videos/%d_%02d_%02d-%02d_%02d_%02d.avi",
				1900+t.tm_year, 1+t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec );
	}

	Com_Printf( "recording to %s\n", s );
	CL_OpenAVIForWriting( s );
}


static void CL_StopVideo_f()
{
	CL_CloseAVI();
}


///////////////////////////////////////////////////////////////


static void CL_ShowIP_f()
{
	Sys_ShowIP();
}


qbool CL_CDKeyValidate( const char *key, const char *checksum )
{
	char	ch;
	byte	sum;
	char	chs[3];
	int i, len;

	len = strlen(key);
	if( len != CDKEY_LEN ) {
		return qfalse;
	}

	if( checksum && strlen( checksum ) != CDCHKSUM_LEN ) {
		return qfalse;
	}

	sum = 0;
	// for loop gets rid of conditional assignment warning
	for (i = 0; i < len; i++) {
		ch = *key++;
		if (ch>='a' && ch<='z') {
			ch -= 32;
		}
		switch( ch ) {
		case '2':
		case '3':
		case '7':
		case 'A':
		case 'B':
		case 'C':
		case 'D':
		case 'G':
		case 'H':
		case 'J':
		case 'L':
		case 'P':
		case 'R':
		case 'S':
		case 'T':
		case 'W':
			sum += ch;
			continue;
		default:
			return qfalse;
		}
	}

	sprintf(chs, "%02x", sum);

	if (checksum && !Q_stricmp(chs, checksum)) {
		return qtrue;
	}

	if (!checksum) {
		return qtrue;
	}

	return qfalse;
}


static void CL_CallVote_f()
{
	CL_ForwardCommandToServer( Cmd_Cmd() );
}


static void CL_CompleteCallVote_f( int startArg, int compArg )
{
	if ( compArg == startArg + 2 && !Q_stricmp( Cmd_Argv( startArg + 1 ), "map" ) )
		Field_AutoCompleteMapName( startArg, compArg );
}


void CL_Init()
{
	//QSUBSYSTEM_INIT_START( "Client" );

	CL_ConInit();

	CL_ClearState();

	cls.state = CA_DISCONNECTED;	// no longer CA_UNINITIALIZED

	cls.realtime = 0;

	CL_InitInput();

	// register our variables
	//
	cl_timeout = Cvar_Get ("cl_timeout", "200", 0);

	cl_timeNudge = Cvar_Get ("cl_timeNudge", "0", CVAR_TEMP );
	cl_shownet = Cvar_Get ("cl_shownet", "0", CVAR_TEMP );
	cl_showSend = Cvar_Get ("cl_showSend", "0", CVAR_TEMP );
	cl_showTimeDelta = Cvar_Get ("cl_showTimeDelta", "0", CVAR_TEMP );
	rcon_client_password = Cvar_Get ("rconPassword", "", CVAR_TEMP );

	cl_timedemo = Cvar_Get ("timedemo", "0", 0);
	cl_aviFrameRate = Cvar_Get ("cl_aviFrameRate", "25", CVAR_ARCHIVE);
	cl_aviMotionJpeg = Cvar_Get ("cl_aviMotionJpeg", "1", CVAR_ARCHIVE);

	rconAddress = Cvar_Get ("rconAddress", "", 0);

	cl_maxpackets = Cvar_Get ("cl_maxpackets", "30", CVAR_ARCHIVE );
	cl_packetdup = Cvar_Get ("cl_packetdup", "1", CVAR_ARCHIVE );

	cl_allowDownload = Cvar_Get ("cl_allowDownload", "0", CVAR_ARCHIVE);
#if defined(USE_CURL)
	cl_dlURL = Cvar_Get( "cl_dlURL", "", 0 );
#if defined(USE_CURL_DLOPEN)
	cl_cURLLib = Cvar_Get( "cl_cURLLib", DEFAULT_CURL_LIB, 0 );
#endif
#endif

#ifdef MACOS_X
	// In game video is REALLY slow in Mac OS X right now due to driver slowness
	cl_inGameVideo = Cvar_Get ("r_inGameVideo", "0", CVAR_ARCHIVE);
#else
	cl_inGameVideo = Cvar_Get ("r_inGameVideo", "1", CVAR_ARCHIVE);
#endif

	cl_serverStatusResendTime = Cvar_Get ("cl_serverStatusResendTime", "750", 0);

	cl_motd = Cvar_Get( "cl_motd", "1", 0 );
	cl_motdString = Cvar_Get( "cl_motdString", "", CVAR_ROM );

	Cvar_Get( "cl_maxPing", "800", CVAR_ARCHIVE );


	// userinfo
	Cvar_Get ("name", "UnnamedPlayer", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get ("rate", "3000", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get ("snaps", "20", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get ("cl_anonymous", "0", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get ("password", "", CVAR_USERINFO);

	// register our commands
	//
	Cmd_AddCommand ("cmd", CL_ForwardToServer_f);
	Cmd_AddCommand ("configstrings", CL_Configstrings_f);
	Cmd_AddCommand ("clientinfo", CL_Clientinfo_f);
	Cmd_AddCommand ("snd_restart", CL_Snd_Restart_f);
	Cmd_AddCommand ("vid_restart", CL_Vid_Restart_f);
	Cmd_AddCommand ("disconnect", CL_Disconnect_f);
	Cmd_AddCommand ("record", CL_Record_f);
	Cmd_SetAutoCompletion ("record", CL_CompleteDemoRecord_f);
	Cmd_AddCommand ("demo", CL_PlayDemo_f);
	Cmd_SetAutoCompletion ("demo", CL_CompleteDemoPlay_f);
	Cmd_AddCommand ("cinematic", CL_PlayCinematic_f);
	Cmd_AddCommand ("stoprecord", CL_StopRecord_f);
	Cmd_AddCommand ("connect", CL_Connect_f);
	Cmd_AddCommand ("reconnect", CL_Reconnect_f);
	Cmd_AddCommand ("localservers", CL_LocalServers_f);
	Cmd_AddCommand ("globalservers", CL_GlobalServers_f);
	Cmd_AddCommand ("rcon", CL_Rcon_f);
	Cmd_SetAutoCompletion ("rcon", CL_CompleteRcon_f);
	Cmd_AddCommand ("ping", CL_Ping_f );
	Cmd_AddCommand ("serverstatus", CL_ServerStatus_f );
	Cmd_AddCommand ("showip", CL_ShowIP_f );
	Cmd_AddCommand ("fs_openedList", CL_OpenedPK3List_f );
	Cmd_AddCommand ("fs_referencedList", CL_ReferencedPK3List_f );
	Cmd_AddCommand ("model", CL_SetModel_f );
	Cmd_AddCommand ("video", CL_Video_f );
	Cmd_AddCommand ("stopvideo", CL_StopVideo_f );

	// we use these until we get proper handling on the mod side
	Cmd_AddCommand ("cv", CL_CallVote_f );
	Cmd_AddCommand ("callvote", CL_CallVote_f );
	Cmd_SetAutoCompletion ("cv", CL_CompleteCallVote_f );
	Cmd_SetAutoCompletion ("callvote", CL_CompleteCallVote_f );

	CL_InitRef();

	SCR_Init();

	Cbuf_Execute();

	Cvar_Set( "cl_running", "1" );

	//QSUBSYSTEM_INIT_DONE( "Client" );
}


void CL_Shutdown()
{
	static qbool recursive = qfalse;

	// check whether the client is running at all.
	if(!(com_cl_running && com_cl_running->integer))
		return;

	Com_Printf( "----- CL_Shutdown -----\n" );

	if ( recursive ) {
		printf ("recursive shutdown\n");
		return;
	}
	recursive = qtrue;

	CL_Disconnect( qtrue );

	S_Shutdown();
	CL_ShutdownRef();

	CL_ShutdownUI();

	Cmd_RemoveCommand ("cmd");
	Cmd_RemoveCommand ("configstrings");
	Cmd_RemoveCommand ("userinfo");
	Cmd_RemoveCommand ("snd_restart");
	Cmd_RemoveCommand ("vid_restart");
	Cmd_RemoveCommand ("disconnect");
	Cmd_RemoveCommand ("record");
	Cmd_RemoveCommand ("demo");
	Cmd_RemoveCommand ("cinematic");
	Cmd_RemoveCommand ("stoprecord");
	Cmd_RemoveCommand ("connect");
	Cmd_RemoveCommand ("localservers");
	Cmd_RemoveCommand ("globalservers");
	Cmd_RemoveCommand ("rcon");
	Cmd_RemoveCommand ("setenv");
	Cmd_RemoveCommand ("ping");
	Cmd_RemoveCommand ("serverstatus");
	Cmd_RemoveCommand ("showip");
	Cmd_RemoveCommand ("model");
	Cmd_RemoveCommand ("video");
	Cmd_RemoveCommand ("stopvideo");

	// we use these until we get proper handling on the mod side
	Cmd_RemoveCommand ("cv");
	Cmd_RemoveCommand ("callvote");

	Cvar_Set( "cl_running", "0" );

	recursive = qfalse;

	Com_Memset( &cls, 0, sizeof( cls ) );

	Com_Printf( "-----------------------\n" );
}
