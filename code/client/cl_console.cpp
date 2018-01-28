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
// console.c

#include "client.h"
#include "client_help.h"


static cvar_t* con_noprint;
static cvar_t* con_notifytime;
static cvar_t* con_scale;
static cvar_t* con_scaleMode; // 0 = without res, 1 = with res, 2 = 8x12
static cvar_t* con_speed;
static cvar_t* con_drawHelp;

#define COLOR_LIST(X) \
	X(BG,		"101013F6",	qtrue,	"RGBA color of the background") \
	X(Border,	"4778B2FF",	qtrue,	"RGBA color of the border") \
	X(Arrow,	"4778B2FF",	qtrue,	"RGBA color of backscroll arrows") \
	X(Shadow,	"000000FF",	qtrue,	"RGBA color of text shadows") \
	X(Text,		"E2E2E2",	qfalse,	"RGB color of text") \
	X(CVar,		"4778B2",	qfalse,	"RGB color of variable names") \
	X(Cmd,		"4FA7BD",	qfalse,	"RGB color of command names") \
	X(Value,	"E5BC39",	qfalse,	"RGB color of variable values") \
	X(Help,		"ABC1C6",	qfalse,	"RGB color of help text") \
	X(HL,		"303033FF",	qtrue,	help_con_colHL)

#define COLOR_LIST_ITEM( Name, Default, HasAlpha, Help ) \
	static cvar_t* con_col##Name; \
	static vec4_t col##Name;
COLOR_LIST( COLOR_LIST_ITEM )
#undef COLOR_LIST_ITEM

#define CON_NOTIFYLINES	4

#define CON_TEXTSIZE	(256*1024)

// con_drawHelp flags
#define DRAWHELP_ENABLE_BIT		1
#define DRAWHELP_NOTFOUND_BIT	2
#define DRAWHELP_MODULES_BIT	4
#define DRAWHELP_ATTRIBS_BIT	8
#define DRAWHELP_MAX			15

struct console_t {
	qbool	initialized;

	short	text[CON_TEXTSIZE];
	int		current;		// line where next message will be printed
	int		x;				// offset in current line for next print
	int		y;				// virtual screen coordinate of bottom of console
	int		display;		// bottom of console displays this line

	float	cw, ch;			// actual font size in pixels
	float	xadjust;		// supposedly for wide aspect screens, but never actually set properly

	int		linewidth;		// characters across screen
	int		totallines;		// total lines in console scrollback

	int		rowsVisible;	// number of full rows that can currently be rendered in a page

	float	displayFrac;	// approaches finalFrac at con_speed
	float	finalFrac;		// 0.0 to 1.0 lines of console to display

	int		times[CON_NOTIFYLINES];	// cls.realtime time the line was generated
								// for transparent notify lines

	qbool	wasActive;		// was active before Con_PushConsoleInvisible was called?

	char			helpText[MAXPRINTMSG];
	int				helpX;		// char index
	float			helpY;		// top coordinate
	int				helpWidth;	// char count of the longest line
	int				helpLines;	// line count
	qbool			helpDraw;
	float			helpXAdjust;
};

static console_t con;

// base font size, optionally scaled by screen resolution and con_scale
#define CONCHAR_WIDTH	8
#define CONCHAR_HEIGHT	12

int g_console_field_width = CONSOLE_WIDTH;


static qbool IsValidHexChar( char c )
{
	return
		( c >= '0' && c <= '9' ) ||
		( c >= 'a' && c <= 'f' ) ||
		( c >= 'A' && c <= 'F' );
}

static qbool IsValidHexColor( const char* s, qbool hasAlpha )
{
	const int chars = hasAlpha ? 8 : 6;
	for ( int i = 0; i < chars; ++i ) {
		if ( *s == '\0' || !IsValidHexChar(*s) )
			return qfalse;
		s++;
	}

	return *s == '\0';
}

static void GetFloatColor( float* c, cvar_t* cvar, qbool hasAlpha )
{
	c[0] = 1.0f;
	c[1] = 1.0f;
	c[2] = 1.0f;
	c[3] = 1.0f;

	const char* s = cvar->string;
	if ( !IsValidHexColor(s, hasAlpha) ) {
		s = cvar->resetString;
		if ( !IsValidHexColor(s, hasAlpha) )
			return;
	}

	unsigned int uc[4];
	if ( hasAlpha ) {
		if ( sscanf(s, "%02X%02X%02X%02X", &uc[0], &uc[1], &uc[2], &uc[3]) != 4 )
			return;
		c[0] = uc[0] / 255.0f;
		c[1] = uc[1] / 255.0f;
		c[2] = uc[2] / 255.0f;
		c[3] = uc[3] / 255.0f;
	} else {
		if ( sscanf(s, "%02X%02X%02X", &uc[0], &uc[1], &uc[2]) != 3 )
			return;
		c[0] = uc[0] / 255.0f;
		c[1] = uc[1] / 255.0f;
		c[2] = uc[2] / 255.0f;
		c[3] = 1.0f;
	}
}

const float* ConsoleColorFromChar( char ccode )
{
	if ( ccode == COLOR_WHITE )
		return colText;
	if ( ccode == COLOR_CVAR )
		return colCVar;
	if ( ccode == COLOR_CMD )
		return colCmd;
	if ( ccode == COLOR_VAL )
		return colValue;
	if ( ccode == COLOR_HELP )
		return colHelp;

	return ColorFromChar( ccode );
}

float Con_SetConsoleVisibility( float fraction )
{
	const float oldValue = con.displayFrac;
	con.displayFrac = fraction;
	return oldValue;
}

void Con_ToggleConsole_f()
{
	g_consoleField.acOffset = 0;

	// closing a full screen console restarts the demo loop
	if ( cls.state == CA_DISCONNECTED && cls.keyCatchers == KEYCATCH_CONSOLE ) {
		CL_StartDemoLoop();
		return;
	}

	Field_Clear( &g_consoleField );
	g_consoleField.widthInChars = g_console_field_width;

	Con_ClearNotify();
	cls.keyCatchers ^= KEYCATCH_CONSOLE;
}

/*
================
Con_MessageMode_f
================
*/
void Con_MessageMode_f (void) {
	chat_playerNum = -1;
	chat_team = qfalse;
	Field_Clear( &chatField );
	chatField.widthInChars = 30;

	cls.keyCatchers ^= KEYCATCH_MESSAGE;
}

/*
================
Con_MessageMode2_f
================
*/
void Con_MessageMode2_f (void) {
	chat_playerNum = -1;
	chat_team = qtrue;
	Field_Clear( &chatField );
	chatField.widthInChars = 25;
	cls.keyCatchers ^= KEYCATCH_MESSAGE;
}

/*
================
Con_MessageMode3_f
================
*/
void Con_MessageMode3_f (void) {
	chat_playerNum = VM_Call( cgvm, CG_CROSSHAIR_PLAYER );
	if ( chat_playerNum < 0 || chat_playerNum >= MAX_CLIENTS ) {
		chat_playerNum = -1;
		return;
	}
	chat_team = qfalse;
	Field_Clear( &chatField );
	chatField.widthInChars = 30;
	cls.keyCatchers ^= KEYCATCH_MESSAGE;
}

/*
================
Con_MessageMode4_f
================
*/
void Con_MessageMode4_f (void) {
	chat_playerNum = VM_Call( cgvm, CG_LAST_ATTACKER );
	if ( chat_playerNum < 0 || chat_playerNum >= MAX_CLIENTS ) {
		chat_playerNum = -1;
		return;
	}
	chat_team = qfalse;
	Field_Clear( &chatField );
	chatField.widthInChars = 30;
	cls.keyCatchers ^= KEYCATCH_MESSAGE;
}


static void Con_Clear_f( void )
{
	int i;

	for ( i = 0 ; i < CON_TEXTSIZE ; i++ ) {
		con.text[i] = (COLOR_WHITE << 8) | ' ';
	}

	con.current = con.totallines - 1;

	Con_Bottom();
}


// save the console contents out to a file

void Con_Dump_f( void )
{
	int		l, x, i;
	short	*line;
	char	buffer[1024];

	if (Cmd_Argc() != 2)
	{
		Com_Printf ("usage: condump <filename>\n");
		return;
	}

	Q_strncpyz( buffer, Cmd_Argv(1), sizeof(buffer) );
	COM_DefaultExtension( buffer, sizeof(buffer), ".txt" );

	fileHandle_t f = FS_FOpenFileWrite( buffer );
	if (!f)
	{
		Com_Printf( "ERROR: couldn't open %s\n", buffer );
		return;
	}

	Com_Printf( "Dumped console text to %s\n", buffer );

	// skip empty lines
	for (l = con.current - con.totallines + 1 ; l <= con.current ; l++)
	{
		line = con.text + (l%con.totallines)*con.linewidth;
		for (x=0 ; x<con.linewidth ; x++)
			if ((line[x] & 0xff) != ' ')
				break;
		if (x != con.linewidth)
			break;
	}

	// write the remaining lines
	buffer[con.linewidth] = 0;
	for ( ; l <= con.current ; l++)
	{
		line = con.text + (l%con.totallines)*con.linewidth;
		for(i=0; i<con.linewidth; i++)
			buffer[i] = line[i] & 0xff;
		for (x=con.linewidth-1 ; x>=0 ; x--)
		{
			if (buffer[x] == ' ')
				buffer[x] = 0;
			else
				break;
		}
		strcat( buffer, "\n" );
		FS_Write( buffer, strlen(buffer), f );
	}

	FS_FCloseFile( f );
}


void Con_ClearNotify()
{
	for (int i = 0; i < CON_NOTIFYLINES; ++i) {
		con.times[i] = 0;
	}
}


// called on demand by the first CL_ConsolePrint
// before the client subsystem is actually up and running properly

static void Con_Init()
{
	con.initialized = qtrue;

	con.linewidth = CONSOLE_WIDTH;
	con.totallines = CON_TEXTSIZE / con.linewidth;
	
	Con_Clear_f();

	con.cw = CONCHAR_WIDTH;
	con.ch = CONCHAR_HEIGHT;
}


static void Con_ResizeFont()
{
	if (!cls.rendererStarted)
		return;

	con.cw = CONCHAR_WIDTH;
	con.ch = CONCHAR_HEIGHT;
	con.xadjust = CONCHAR_WIDTH;

	if (!con_scale || !con_scaleMode)
		return;

	if (con_scaleMode->integer == 2)
		return;

	if (con_scaleMode->integer == 1)
		SCR_AdjustFrom640( &con.cw, &con.ch, NULL, NULL );

	con.cw *= con_scale->value;
	con.ch *= con_scale->value;

	if ( cls.glconfig.vidWidth * SCREEN_HEIGHT > cls.glconfig.vidHeight * SCREEN_WIDTH ) {
		// the console distorts horribly on widescreens
		con.cw = con.ch * SCREEN_HEIGHT / SCREEN_WIDTH;
	}

	con.xadjust = con.cw;
}


static const cvarTableItem_t con_cvars[] =
{
#define COLOR_LIST_ITEM( Name, Default, HasAlpha, Help ) { &con_col##Name, "con_col" #Name, Default, CVAR_ARCHIVE, CVART_STRING, NULL, NULL, Help },
	COLOR_LIST( COLOR_LIST_ITEM )
#undef COLOR_LIST_ITEM
	// con_scale:
	// bugs in the renderer's overflow handling will cause crashes
	// if the console has too many polys/verts because of too small a font
	{ &con_noprint, "con_noprint", "0", 0, CVART_BOOL, NULL, NULL, "disables console printing and history writing" },
	{ &con_notifytime, "con_notifytime", "-1", CVAR_ARCHIVE, CVART_FLOAT, "-1", "30", help_con_notifytime },
	{ &con_scale, "con_scale", "1.2", CVAR_ARCHIVE, CVART_FLOAT, "0.25", "10", "console text scaling factor" },
	{ &con_scaleMode, "con_scaleMode", "0", CVAR_ARCHIVE, CVART_INTEGER, "0", "2", help_con_scaleMode },
	{ &con_speed, "con_speed", "1000", CVAR_ARCHIVE, CVART_FLOAT, "0.1", "1000", "console opening/closing speed" },
	{ &con_drawHelp, "con_drawHelp", "1", CVAR_ARCHIVE, CVART_BITMASK, "0", XSTRING(DRAWHELP_MAX), help_con_drawHelp }
};


static const cmdTableItem_t con_cmds[] =
{
	{ "toggleconsole", Con_ToggleConsole_f, NULL, "toggles console display" },
	{ "messagemode", Con_MessageMode_f, NULL, "chat with everyone" },
	{ "messagemode2", Con_MessageMode2_f, NULL, "chat with teammates" },
	{ "messagemode3", Con_MessageMode3_f, NULL, "chat with the player being aimed at" },
	{ "messagemode4", Con_MessageMode4_f, NULL, "chat with the last attacker" },
	{ "clear", Con_Clear_f, NULL, "clears the console" },
	{ "condump", Con_Dump_f, NULL, "dumps console history to a text file" }
};


void CL_ConInit()
{
	Cvar_RegisterArray( con_cvars, MODULE_CONSOLE );

	Field_Clear( &g_consoleField );
	g_consoleField.widthInChars = g_console_field_width;
	History_Clear( &g_history, g_console_field_width );
	Con_ClearNotify();

	Cmd_RegisterArray( con_cmds, MODULE_CONSOLE );
}


void CL_ConShutdown()
{
	Cmd_UnregisterModule( MODULE_CONSOLE );
}


static void Con_Linefeed()
{
	// mark time for transparent overlay
	if (con.current >= 0) {
		con.times[con.current % CON_NOTIFYLINES] = cls.realtime;
	}

	con.x = 0;
	if (con.display == con.current)
		con.display++;
	con.current++;

	for ( int i = 0; i < con.linewidth; ++i ) {
		con.text[(con.current%con.totallines)*con.linewidth+i] = (COLOR_WHITE << 8) | ' ';
	}
}


/*
Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be logged to disk
If no console is visible, the text will appear at the top of the game window
*/
void CL_ConsolePrint( const char* s )
{
	int c, w, y;

	// con_noprint disables ALL console functionality
	// use con_notifytime 0 to log the text without the annoying overlay
	if ( con_noprint && con_noprint->integer ) {
		return;
	}

	if (!con.initialized)
		Con_Init();

	char color = COLOR_WHITE;

	while ( c = *s ) {
		if ( Q_IsColorString( s ) ) {
			color = s[1];
			s += 2;
			continue;
		}

		// count word length and wordwrap if needed
		for (w = 0; w < con.linewidth; ++w) {
			if ( s[w] <= ' ') {
				break;
			}
		}
		if (w != con.linewidth && (con.x + w >= con.linewidth) ) {
			Con_Linefeed();
		}

		++s;

		switch (c)
		{
		case '\n':
			Con_Linefeed();
			break;
		case '\r':
			con.x = 0;
			break;
		default:	// display character and advance
			y = con.current % con.totallines;
			con.text[y*con.linewidth+con.x] = (color << 8) | c;
			con.x++;
			if (con.x >= con.linewidth) {
				Con_Linefeed();
				con.x = 0;
			}
			break;
		}
	}

	// mark time for transparent overlay
	if (con.current >= 0) {
		con.times[con.current % CON_NOTIFYLINES] = cls.realtime;
	}
}


/*
==============================================================================

DRAWING

==============================================================================
*/


// draw the editline with a prompt in front of it

static void Con_DrawInput()
{
	if ( cls.state != CA_DISCONNECTED && !(cls.keyCatchers & KEYCATCH_CONSOLE ) ) {
		return;
	}

	float y = con.y - (con.ch * 1.5f);

	// highlight the currently auto-completed part of the edit line
	if ( g_consoleField.acOffset > 0 ) {
		const int length = g_consoleField.acLength;
		if ( length > 0 ) {
			// note that Field_Draw takes integers as arguments so we need to truncate our coordinates and font sizes to match
			const int offset = g_consoleField.acOffset;
			re.SetColor( colHL );
			re.DrawStretchPic( con.xadjust + (offset + 1) * (int)con.cw, y, length * (int)con.cw, (int)con.ch, 0, 0, 0, 0, cls.whiteShader );
		}
	}

	re.SetColor( colShadow );
	SCR_DrawChar( con.xadjust + 1, y + 1, con.cw, con.ch, ']' );
	re.SetColor( colText );
	SCR_DrawChar( con.xadjust, y, con.cw, con.ch, ']' );

	Field_Draw( &g_consoleField, con.xadjust + con.cw, y, con.cw, con.ch, qtrue );
}


// draws the last few lines of output transparently over the game area

static void Con_DrawNotify()
{
	int		x;
	int		i;
	int		time;
	int		skip;

	if (cls.keyCatchers & (KEYCATCH_UI | KEYCATCH_CGAME)) {
		return;
	}

	char color = COLOR_WHITE;
	re.SetColor( ColorFromChar( color ) );

	int y = 0;
	for (i = con.current-CON_NOTIFYLINES+1 ; i <= con.current ; i++)
	{
		if (i < 0)
			continue;
		time = con.times[i % CON_NOTIFYLINES];
		if (time == 0)
			continue;
		time = cls.realtime - time;
		if (time >= con_notifytime->value*1000)
			continue;

		const short* text = con.text + (i % con.totallines)*con.linewidth;

		for (x = 0 ; x < con.linewidth ; x++) {
			if ( ( text[x] & 0xff ) == ' ' ) {
				continue;
			}
			if ( (text[x] >> 8) != color ) {
				color = (text[x] >> 8);
				re.SetColor( ColorFromChar( color ) );
			}
			SCR_DrawChar( con.xadjust + x * con.cw, y, con.cw, con.ch, text[x] & 0xff );
		}

		y += con.ch;
	}

	re.SetColor( NULL );

	// draw the chat line - this shit has NO business being in the engine >:(
	if ( cls.keyCatchers & KEYCATCH_MESSAGE )
	{
		float cw = 12, ch = 16;
		SCR_AdjustFrom640( &cw, &ch, NULL, NULL );

		if (chat_team)
		{
			SCR_DrawString( 8, y, cw, ch, "say_team:", qfalse );
			skip = 10;
		}
		else
		{
			SCR_DrawString( 8, y, cw, ch, "say:", qfalse );
			skip = 5;
		}

		Field_Draw( &chatField, skip * cw, y, cw, ch, qfalse );
		y += ch;
	}

}


///////////////////////////////////////////////////////////////


static void Con_FillRect( float x, float y, float w, float h, const vec4_t color )
{
	re.SetColor( color );
	re.DrawStretchPic( x, y, w, h, 0, 0, 0, 0, cls.whiteShader );
	re.SetColor( NULL );
}


static void QDECL Con_HelpPrintf( const char* fmt, ... )
{
	va_list argptr;
	va_start( argptr, fmt );
	Q_vsnprintf( con.helpText, sizeof(con.helpText), fmt, argptr );
	va_end( argptr );

	const float* color = colText;
	const char* c = con.helpText;
	while ( *c != '\0' ) {
		// measure the length of the current word
		int wl = 0;
		while ( c[wl] != '\0' && c[wl] > ' ' )
			wl++;

		const qbool wordBreak = (wl > 0) && (con.helpX + wl >= CONSOLE_WIDTH) && (wl < CONSOLE_WIDTH);
		const qbool forcedBreak = con.helpX >= CONSOLE_WIDTH;
		if ( *c == '\n' || forcedBreak || wordBreak ) {
			if ( !forcedBreak && !wordBreak )
				c++;
			con.helpWidth = max( con.helpWidth, con.helpX );
			con.helpX = 0;
			con.helpY += con.ch;
			con.helpLines++;
			continue;
		}

		if ( Q_IsColorString(c) ) {
			color = ConsoleColorFromChar( c[1] );
			c += 2;
			continue;
		}

		if ( con.helpDraw ) {
			re.SetColor( colShadow );
			SCR_DrawChar( con.helpXAdjust + con.helpX * con.cw + 1, con.helpY + 1, con.cw, con.ch, *c );
			re.SetColor( color );
			SCR_DrawChar( con.helpXAdjust + con.helpX * con.cw, con.helpY, con.cw, con.ch, *c );
		}
		c++;
		con.helpX++;
	}
}


static void Con_DrawHelp()
{
	if( !(con_drawHelp->integer & DRAWHELP_ENABLE_BIT) )
		return;

	if ( *g_consoleField.buffer == '\0' )
		return;

	if ( con.displayFrac == 0.0f || con.displayFrac < con.finalFrac )
		return;

	Cmd_TokenizeString( g_consoleField.buffer );
	if ( Cmd_Argc() < 1 )
		return;

	const char* name = Cmd_Argv(0);
	if ( *name == '/' || *name == '\\' )
		name++;

	if ( *name == '\0' )
		return;

	const qbool printAlways = (con_drawHelp->integer & DRAWHELP_NOTFOUND_BIT) != 0;
	const qbool printModules = (con_drawHelp->integer & DRAWHELP_MODULES_BIT) != 0;
	const qbool printAttribs = (con_drawHelp->integer & DRAWHELP_ATTRIBS_BIT) != 0;
	con.helpDraw = qfalse;
	con.helpX = 0;
	con.helpWidth = 0;
	con.helpLines = 0;
	con.helpXAdjust = con.xadjust + 2 * con.cw;
	const printHelpResult_t result = Com_PrintHelp( name, &Con_HelpPrintf, qfalse, printModules, printAttribs );
	if ( result == PHR_NOTFOUND || ( result == PHR_NOHELP && !printAlways ) )
		return;

	const float d = (int)con.ch;
	const float x = (int)(con.helpXAdjust - con.cw);
	const float y = (int)(cls.glconfig.vidHeight * con.displayFrac);
	const float w = (int)((con.helpWidth + 2) * con.cw);
	const float h = (int)((con.helpLines + 1) * con.ch);
	con.helpDraw = qtrue;
	con.helpX = 0;
	con.helpY = y + 1.5f * con.ch;
	const float yh = (int)(con.helpY - con.ch / 2.0f);
	re.SetColor( colBG );
	re.DrawTriangle( x, y, x + d, y + d, x, y + d, 0, 0, 0, 0, 0, 0, cls.whiteShader );
	Con_FillRect( x, yh, w, h, colBG );
	Con_FillRect( x + 1, yh + h + 0, w - 1, 1, colBorder );
	Con_FillRect( x + 2, yh + h + 1, w - 2, 1, colBorder );
	Con_FillRect( x + w + 0, yh + 1, 1, h + 1, colBorder );
	Con_FillRect( x + w + 1, yh + 2, 1, h + 0, colBorder );
	Com_PrintHelp( name, &Con_HelpPrintf, qfalse, printModules, printAttribs );
}


static void Con_DrawSolidConsole( float frac )
{
	int		i, x, y;
	int		rows;
	int		row;

	int scanlines = Com_Clamp( 0, cls.glconfig.vidHeight, cls.glconfig.vidHeight * frac );
	if (scanlines <= 0)
		return;

	// draw the background
	y = scanlines - 2;
	Con_FillRect( 0, 0, cls.glconfig.vidWidth, y, colBG );
	Con_FillRect( 0, y, cls.glconfig.vidWidth, 2, colBorder );

	re.SetColor( colText );
	i = sizeof( Q3_VERSION )/sizeof(char) - 1;
	x = cls.glconfig.vidWidth - SMALLCHAR_WIDTH;
	while (--i >= 0) {
		x -= SMALLCHAR_WIDTH;
		SCR_DrawChar( x, scanlines - (SMALLCHAR_HEIGHT * 1.5), SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT, Q3_VERSION[i] );
	}

	re.SetColor( NULL );
	rows = (scanlines - con.ch) / con.ch;
	con.y = scanlines;

	y = scanlines - (con.ch * 3);

	// draw the console text from the bottom up
	if (con.display != con.current) {
		// draw arrows to show the buffer is backscrolled
		const int xEnd = ( cls.glconfig.vidWidth - con.xadjust ) / con.cw;
		re.SetColor( colArrow );
		for (x = 0; x < xEnd; x += 4)
			SCR_DrawChar( con.xadjust + x * con.cw, y, con.cw, con.ch, '^' );
		y -= con.ch;
		--rows;
	}

	row = con.display;
	if ( con.x == 0 ) {
		row--;
	}

	char color = COLOR_WHITE;
	re.SetColor( ConsoleColorFromChar( color ) );

	con.rowsVisible = 0;
	for (i = 0; i < rows; ++i, --row, y -= con.ch )
	{
		if (row < 0)
			break;
		if (con.current - row >= con.totallines) {
			// past scrollback wrap point
			continue;
		}

		if (y >= 0)
			con.rowsVisible++;

		const short* text = con.text + (row % con.totallines)*con.linewidth;

		re.SetColor( colShadow );
		for (int i = 0; i < con.linewidth; ++i) {
			SCR_DrawChar( 1 + con.xadjust + i * con.cw, 1 + y, con.cw, con.ch, (text[i] & 0xFF) );
		}

		re.SetColor( colText );
		for (int j = 0; j < con.linewidth; ++j) {
			if ((text[j] >> 8) != color) {
				color = (text[j] >> 8);
				re.SetColor( ConsoleColorFromChar( color ) );
			}
			SCR_DrawChar( con.xadjust + j * con.cw, y, con.cw, con.ch, (text[j] & 0xFF) );
		}
	}

	Con_DrawInput();
	CL_MapDownload_DrawConsole( con.cw, con.ch );
	Con_DrawHelp();

	re.SetColor( NULL );
}


///////////////////////////////////////////////////////////////


void Con_DrawConsole()
{
	// check for console width changes from a vid mode change
	Con_ResizeFont();

	// if disconnected, render console full screen
	if ( cls.state == CA_DISCONNECTED ) {
		if ( !( cls.keyCatchers & (KEYCATCH_UI | KEYCATCH_CGAME)) ) {
			Con_DrawSolidConsole( 1.0 );
			return;
		}
	}

	if ( con.displayFrac ) {
		Con_DrawSolidConsole( con.displayFrac );
	} else {
		// draw notify lines
		if ( cls.state == CA_ACTIVE ) {
			Con_DrawNotify();
		}
	}
}


///////////////////////////////////////////////////////////////


// slide the console onto/off the screen

void Con_RunConsole()
{
	// decide on the destination height of the console
	if ( cls.keyCatchers & KEYCATCH_CONSOLE )
		con.finalFrac = 0.5f;		// half screen
	else
		con.finalFrac = 0.0f;		// none visible

	// scroll towards the destination height
	if (con.finalFrac < con.displayFrac)
	{
		con.displayFrac -= con_speed->value * (float)cls.realFrametime / 1000.0f;
		if (con.finalFrac > con.displayFrac)
			con.displayFrac = con.finalFrac;
	}
	else if (con.finalFrac > con.displayFrac)
	{
		con.displayFrac += con_speed->value * (float)cls.realFrametime / 1000.0f;
		if (con.finalFrac < con.displayFrac)
			con.displayFrac = con.finalFrac;
	}

#define COLOR_LIST_ITEM( Name, Default, HasAlpha, Help ) GetFloatColor( col##Name, con_col##Name, HasAlpha );
	COLOR_LIST( COLOR_LIST_ITEM )
#undef COLOR_LIST_ITEM
}


static void Con_FixPosition()
{
	if ( con.display < con.totallines ) {
		con.display = con.totallines;
	} else if ( con.display > con.current ) {
		con.display = con.current;
	}
}


void Con_ScrollLines( int lines )
{
	if (lines == 0)
		return;

	con.display += lines;
	Con_FixPosition();
}


void Con_ScrollPages( int pages )
{
	if (pages == 0)
		return;

	// we allow a single line of overlap for readability
	con.display += pages * (con.rowsVisible - 1);
	Con_FixPosition();
}


void Con_Top()
{
	con.display = con.totallines;
	if ( con.current - con.display >= con.totallines ) {
		con.display = con.current - con.totallines + 1;
	}
}


void Con_Bottom()
{
	con.display = con.current;
}


void Con_Close()
{
	if ( !com_cl_running->integer ) {
		return;
	}
	Field_Clear( &g_consoleField );
	Con_ClearNotify();
	cls.keyCatchers &= ~KEYCATCH_CONSOLE;
	con.finalFrac = 0;				// none visible
	con.displayFrac = 0;
}
