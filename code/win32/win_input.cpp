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
#include "win_local.h"

#define DIRECTINPUT_VERSION 0x0800

#include <dinput.h>
#pragma comment( lib, "dinput8" )
#pragma comment( lib, "dxguid" )

cvar_t	*in_keyboardShortcuts;	// 0: always off, 1: always on, 2: on/off if console is down/up, 3: on/off if windowed/fullscreen

#define DX_MOUSE_BUFFER_SIZE 64
#define MAX_MOUSE_BUTTONS 8

struct Mouse {
	virtual qbool Init() { return qtrue; }
	virtual qbool Activate( qbool active );
	virtual void Move() {}
	virtual void Shutdown() {}
	virtual void Read( int* mx, int* my ) = 0;
	virtual qbool ProcessMessage( UINT msg, WPARAM wParam, LPARAM lParam ) { return qfalse; }

	Mouse() : active(qfalse), wheel(0) {}
	void UpdateWheel( int delta );
private:
	qbool active;
	int wheel;
};

static Mouse* mouse;


qbool Mouse::Activate( qbool _active )
{
	if (active == _active)
		return qfalse;

	active = _active;
	wheel = 0;

	return qtrue;
}


void Mouse::UpdateWheel( int delta )
{
	wheel += delta;

	while (wheel >= WHEEL_DELTA) {
		Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, qtrue, 0, NULL );
		Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, qfalse, 0, NULL );
		wheel -= WHEEL_DELTA;
	}

	while (wheel <= -WHEEL_DELTA) {
		Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL );
		Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL );
		wheel += WHEEL_DELTA;
	}
}


///////////////////////////////////////////////////////////////

struct dimouse_t :public Mouse {
	virtual qbool Init();
	virtual qbool Activate( qbool active );
	virtual void Read( int* mx, int* my );
	virtual void Shutdown();

private:
	HRESULT (WINAPI *pDirectInputCreate)(HINSTANCE hinst, DWORD dwVersion, LPDIRECTINPUT* lplpDirectInput, LPUNKNOWN punkOuter);
	HINSTANCE			hInstDI;
	LPDIRECTINPUT		m_pdi;
	LPDIRECTINPUTDEVICE	m_pMouse;
};

static dimouse_t dimouse;

qbool dimouse_t::Init()
{
	HRESULT		hr;
	DIPROPDWORD	dipdw = {
		{
			sizeof(DIPROPDWORD),        // diph.dwSize
			sizeof(DIPROPHEADER),		// diph.dwHeaderSize
			0,							// diph.dwObj
			DIPH_DEVICE,				// diph.dwHow
		},
		DX_MOUSE_BUFFER_SIZE,           // dwData
	};

	Com_Printf( "Initializing DirectInput...\n" );

	if (!hInstDI) {
		hInstDI = LoadLibrary("dinput.dll");
		if (hInstDI == NULL) {
			Com_Printf( "Couldn't load dinput.dll\n" );
			return qfalse;
		}
	}

	if (!pDirectInputCreate) {
		pDirectInputCreate = (HRESULT (WINAPI *)(HINSTANCE, DWORD, LPDIRECTINPUT *, LPUNKNOWN))
			GetProcAddress(hInstDI, "DirectInputCreateA");

		if (!pDirectInputCreate) {
			Com_Printf( "Couldn't get DI proc addr\n" );
			return qfalse;
		}
	}

	// register with DirectInput and get an IDirectInput to play with.
	hr = DirectInput8Create( g_wv.hInstance, DIRECTINPUT_VERSION, IID_IDirectInput8, (LPVOID*)&m_pdi, NULL );

	if (FAILED(hr)) {
		Com_Printf( "DI8Create failed\n" );
		return qfalse;
	}

	// obtain an interface to the system mouse device.
	hr = IDirectInput_CreateDevice( m_pdi, GUID_SysMouse, &m_pMouse, NULL );

	if (FAILED(hr)) {
		Com_Printf( "Couldn't open DI mouse device\n" );
		return qfalse;
	}

	// set the data format to DIv8 mice (8 buttons)
	hr = IDirectInputDevice_SetDataFormat( m_pMouse, &c_dfDIMouse2 );

	if (FAILED(hr)) {
		Com_Printf( "Couldn't set DI mouse format\n" );
		return qfalse;
	}

	// set the cooperatively level.
	hr = IDirectInputDevice_SetCooperativeLevel( m_pMouse, g_wv.hWnd, DISCL_EXCLUSIVE | DISCL_FOREGROUND );

	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=50
	if (FAILED(hr)) {
		Com_Printf( "Couldn't set DI coop level\n" );
		return qfalse;
	}

	// set the buffer size to DINPUT_BUFFERSIZE elements.
	// the buffer size is a DWORD property associated with the device
	hr = IDirectInputDevice_SetProperty( m_pMouse, DIPROP_BUFFERSIZE, &dipdw.diph );

	if (FAILED(hr)) {
		Com_Printf( "Couldn't set DI buffer size\n" );
		return qfalse;
	}

	// clear any pending samples
	int x, y;
	Read( &x, &y );
	Read( &x, &y );

	Com_Printf( "DirectInput initialized.\n" );
	return qtrue;
}

void dimouse_t::Shutdown() 
{
	if (m_pMouse) {
		IDirectInputDevice_Release(m_pMouse);
		m_pMouse = NULL;
	}

	if (m_pdi) {
		IDirectInput_Release(m_pdi);
		m_pdi = NULL;
	}
}

qbool dimouse_t::Activate( qbool active )
{
	if (!m_pMouse) {
		return qfalse;
	}

	if (active)	{	

		// we may fail to reacquire if the window has been recreated
		HRESULT hr = IDirectInputDevice_Acquire( m_pMouse );
		if (FAILED(hr)) {
			if ( !Init() ) {
				Com_Printf( "Falling back to Win32 mouse support...\n" );
				Cvar_Set( "in_mouse", "-1" );
				return qfalse;
			}
		}
	} else {
		IDirectInputDevice_Unacquire( m_pMouse );
	}

	return qtrue;
}

void dimouse_t::Read( int* mx, int* my )
{
	DIDEVICEOBJECTDATA od[ DX_MOUSE_BUFFER_SIZE ];  // receives buffered data
	HRESULT hr;
	int value;

	if ( !m_pMouse ) {
		return;
	}

	// fetch new events
	for (;;)
	{
		DWORD dwElements = DX_MOUSE_BUFFER_SIZE;

		hr = IDirectInputDevice_GetDeviceData(m_pMouse, sizeof(DIDEVICEOBJECTDATA), od, &dwElements, 0);
		if ((hr == DIERR_INPUTLOST) || (hr == DIERR_NOTACQUIRED)) {
			// drakkar - vid_restart creates a new window, so we have a new window handle ( hWnd )
			IDirectInputDevice_SetCooperativeLevel( m_pMouse, g_wv.hWnd, DISCL_EXCLUSIVE | DISCL_FOREGROUND );
			// !drakkar
			IDirectInputDevice_Acquire(m_pMouse);
			return;
		}

		// unable to read data or no data available
		if ( FAILED(hr) || !dwElements ) {
			break;
		}

		for (DWORD i = 0; i < dwElements; ++i)
		{
			switch (od[i].dwOfs) {
			case DIMOFS_BUTTON0:
			case DIMOFS_BUTTON1:
			case DIMOFS_BUTTON2:
			case DIMOFS_BUTTON3:
			case DIMOFS_BUTTON4:
				Sys_QueEvent( od[i].dwTimeStamp, SE_KEY, K_MOUSE1 + od[i].dwOfs - DIMOFS_BUTTON0, (od[i].dwData & 0x80), 0, NULL );
				break;
				// the keycodes generated by these DI8 buttons are NOT contiguous with the original ones
			case DIMOFS_BUTTON5:
				Sys_QueEvent( od[i].dwTimeStamp, SE_KEY, K_MOUSE6, (od[i].dwData & 0x80), 0, NULL );
				break;
			case DIMOFS_BUTTON6:
				Sys_QueEvent( od[i].dwTimeStamp, SE_KEY, K_MOUSE7, (od[i].dwData & 0x80), 0, NULL );
				break;
			case DIMOFS_BUTTON7:
				Sys_QueEvent( od[i].dwTimeStamp, SE_KEY, K_MOUSE8, (od[i].dwData & 0x80), 0, NULL );
				break;

			case DIMOFS_Z:
				value = (int)od[i].dwData;
				if (value < 0) {
					Sys_QueEvent( od[i].dwTimeStamp, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL );
					Sys_QueEvent( od[i].dwTimeStamp, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL );
				} else if (value > 0) {
					Sys_QueEvent( od[i].dwTimeStamp, SE_KEY, K_MWHEELUP, qtrue, 0, NULL );
					Sys_QueEvent( od[i].dwTimeStamp, SE_KEY, K_MWHEELUP, qfalse, 0, NULL );
				}
				break;
			}
		}
	}

	// read the raw delta counter and ignore the individual sample time / values
	DIMOUSESTATE2 state;
	hr = IDirectInputDevice_GetDeviceState( m_pMouse, sizeof(state), &state );
	if ( FAILED(hr) ) {
		*mx = *my = 0;
		return;
	}

	*mx = state.lX;
	*my = state.lY;
}

///////////////////////////////////////////////////////////////

struct rawmouse_t : public Mouse {
	virtual qbool Init();
	virtual qbool Activate( qbool active );
	virtual void Read( int* mx, int* my );
	virtual qbool ProcessMessage( UINT msg, WPARAM wParam, LPARAM lParam );

	int x, y;
};

static rawmouse_t rawmouse;


qbool rawmouse_t::Init()
{
	return Activate( qtrue );
}


qbool rawmouse_t::Activate( qbool active )
{
	RAWINPUTDEVICE rid;

	rid.usUsagePage = 0x01;
	rid.usUsage = 0x02; // page 1 item 2 = mouse, gg constants you asswipes  >:(
	if (active)
		rid.dwFlags = RIDEV_NOLEGACY;
	else
		rid.dwFlags = RIDEV_REMOVE;
	rid.hwndTarget = 0;

	return RegisterRawInputDevices( &rid, 1, sizeof(rid) );
}


void rawmouse_t::Read( int* mx, int* my )
{
	*mx = rawmouse.x;
	*my = rawmouse.y;
	rawmouse.x = rawmouse.y = 0;
}


// MSDN says you have to always let DefWindowProc run for WM_INPUT
// regardless of whether you process the message or not, so ALWAYS return false here

qbool rawmouse_t::ProcessMessage( UINT msg, WPARAM wParam, LPARAM lParam )
{
	if (msg != WM_INPUT)
		return qfalse;

	HRAWINPUT h = (HRAWINPUT)lParam;

	UINT size;
	if (GetRawInputData( h, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER) ) == -1)
		return qfalse;

	RAWINPUT ri;
	if (GetRawInputData( h, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER) ) != size)
		return qfalse;

	if ( (ri.header.dwType != RIM_TYPEMOUSE) || (ri.data.mouse.usFlags != MOUSE_MOVE_RELATIVE) )
		return qfalse;

	rawmouse.x += ri.data.mouse.lLastX;
	rawmouse.y += ri.data.mouse.lLastY;

	if (!ri.data.mouse.usButtonFlags) // no button or wheel transitions
		return qfalse;

	if (ri.data.mouse.usButtonFlags & RI_MOUSE_WHEEL)
		UpdateWheel( (SHORT)ri.data.mouse.usButtonData );

	// typical MS clusterfuck for button handling, sigh... and better yet,
	// ulRawButtons isn't actually populated at all with the ms mouse drivers
#define QUEUE_RI_BUTTON( button ) \
	if (ri.data.mouse.usButtonFlags & (RI_MOUSE_BUTTON_##button##_DOWN)) \
		Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MOUSE##button##, qtrue, 0, NULL ); \
	if (ri.data.mouse.usButtonFlags & (RI_MOUSE_BUTTON_##button##_UP)) \
		Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MOUSE##button##, qfalse, 0, NULL );

	QUEUE_RI_BUTTON( 1 );
	QUEUE_RI_BUTTON( 2 );
	QUEUE_RI_BUTTON( 3 );
	QUEUE_RI_BUTTON( 4 );
	QUEUE_RI_BUTTON( 5 );

#undef QUEUE_RI_BUTTON

	return qfalse;
}


///////////////////////////////////////////////////////////////


struct winmouse_t : public Mouse {
	virtual qbool Activate( qbool active );
	virtual void Move();
	virtual void Read( int* mx, int* my );
	virtual qbool ProcessMessage( UINT msg, WPARAM wParam, LPARAM lParam );

	int window_center_x, window_center_y;
};

static winmouse_t winmouse;


qbool winmouse_t::Activate( qbool active )
{
	if (!active)
		return qtrue;

    Move();
	SetCursorPos( window_center_x, window_center_y );

	return qtrue;
}

void winmouse_t::Move() 
{
    HMONITOR monitor = MonitorFromWindow( g_wv.hWnd, MONITOR_DEFAULTTOPRIMARY );
    MONITORINFO minfo = { sizeof(MONITORINFO) };
    
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    
    if (GetMonitorInfo(monitor, &minfo))
    {
        sw = minfo.rcMonitor.right;
        sh = minfo.rcMonitor.bottom;
    }

    RECT rc;
    GetWindowRect( g_wv.hWnd, &rc );

    window_center_x = ( max(rc.left, 0) + min(rc.right, sw) ) / 2;
    window_center_y = ( max(rc.top, 0) + min(rc.bottom, sh) ) / 2;
}


void winmouse_t::Read( int* mx, int* my )
{
	POINT p;
	GetCursorPos( &p );
	*mx = p.x - window_center_x;
	*my = p.y - window_center_y;
	SetCursorPos( window_center_x, window_center_y );
}

qbool winmouse_t::ProcessMessage( UINT msg, WPARAM wParam, LPARAM lParam )
{
#define QUEUE_WM_BUTTON( qbutton, mask ) \
	Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, qbutton, (wParam & mask), 0, NULL );

	switch (msg) {
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
		    QUEUE_WM_BUTTON( K_MOUSE1, MK_LBUTTON );
		break;
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
		QUEUE_WM_BUTTON( K_MOUSE2, MK_RBUTTON );
		break;
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
		QUEUE_WM_BUTTON( K_MOUSE3, MK_MBUTTON );
		break;
	case WM_XBUTTONDOWN:
	case WM_XBUTTONUP:
		if (GET_XBUTTON_WPARAM(wParam) == XBUTTON1)
			QUEUE_WM_BUTTON( K_MOUSE4, MK_XBUTTON1 );
		if (GET_XBUTTON_WPARAM(wParam) == XBUTTON2)
			QUEUE_WM_BUTTON( K_MOUSE5, MK_XBUTTON2 );
		break;
#undef QUEUE_WM_BUTTON

	case WM_MOUSEWHEEL:
		UpdateWheel( GET_WHEEL_DELTA_WPARAM(wParam) );
		break;

	default:
		return qfalse;
	}

	return qtrue;
}


///////////////////////////////////////////////////////////////


static void IN_StartupMouse()
{
	assert( !mouse );
	mouse = 0;

	cvar_t* in_mouse = Cvar_Get( "in_mouse", "1", CVAR_ARCHIVE|CVAR_LATCH );
	in_mouse->modified = qfalse;

	if (!in_mouse->integer) {
		Com_Printf( "Mouse not active.\n" );
		return;
	}

	if (in_mouse->integer == 1) {
		if (rawmouse.Init()) {
			mouse = &rawmouse;
			Com_Printf( "Using raw mouse input\n" );
			return;
		}
		Com_Printf( "Raw mouse initialization failed\n" );
	} 

	if (in_mouse->integer == 2) {
		if (dimouse.Init()) {
			mouse = &dimouse;
			Com_Printf( "Using directInput mouse input\n" );
			return;
		}
		Com_Printf( "DirectInput mouse initialization failed\n" );
	} 

	mouse = &winmouse;
	mouse->Init();
	Com_Printf( "Using Win32 mouse input\n" );
}


qbool IN_ProcessMessage( UINT msg, WPARAM wParam, LPARAM lParam )
{
	if (!mouse)
		return qfalse;
    
	return mouse->ProcessMessage( msg, wParam, lParam );
}

void IN_Move()
{
    if (mouse)
        mouse->Move();
}

void IN_UpdateKeyboardShortcuts()
{
	int mode, enable = 1;

	mode = in_keyboardShortcuts->integer;

	if		( mode == 0 ) enable = 0;
	else if	( mode == 1 ) enable = 1;
	else if	( mode == 2 ) enable = ( cls.keyCatchers & KEYCATCH_CONSOLE ? 1 : 0 );
	else if	( mode == 3 ) enable = !Cvar_VariableIntegerValue( "r_fullscreen" );
	else
	{
		Com_Printf( "Invalid in_keyboardShortcuts value\n" );
		Cvar_Reset( "in_keyboardShortcuts" );
		return;
	}

	EnableKeyboardShortcuts( enable );
}

///////////////////////////////////////////////////////////////


static void IN_StartupMIDI();
static void IN_ShutdownMIDI();

static void IN_StartupJoystick();
static void IN_JoyMove();


cvar_t* in_joystick;
cvar_t* in_midi;


static void IN_Startup()
{
	QSUBSYSTEM_INIT_START( "Input" );

	IN_StartupMouse();
	IN_StartupJoystick();
	IN_StartupMIDI();

	QSUBSYSTEM_INIT_DONE( "Input" );

	in_joystick->modified = qfalse;
}


void IN_Init()
{
	in_midi		= Cvar_Get( "in_midi",							"0", CVAR_ARCHIVE );
	in_joystick	= Cvar_Get( "in_joystick",						"0", CVAR_ARCHIVE|CVAR_LATCH );
	in_keyboardShortcuts = Cvar_Get ("in_keyboardShortcuts",	"2", CVAR_ARCHIVE); 

	IN_Startup();
}


void IN_Shutdown()
{
	IN_Activate( qfalse );

	if (mouse) {
		mouse->Shutdown();
		mouse = 0;
	}

	IN_ShutdownMIDI();
}


// called when the window gains or loses focus or changes in some way
// the window may have been destroyed and recreated between a deactivate and an activate

void IN_Activate( qbool active )
{
	if ( !mouse || !mouse->Mouse::Activate( active ) )
		return;

	if (active) {
		while (ShowCursor(FALSE) >= 0)
			;
		RECT rc;
		GetWindowRect( g_wv.hWnd, &rc );
		SetCapture( g_wv.hWnd );
		ClipCursor( &rc );
	} else {
		while (ShowCursor(TRUE) < 0)
			;
		ClipCursor( NULL );
		ReleaseCapture();
	}

	mouse->Activate( active );
}


// called every frame, even if not generating commands

void IN_Frame()
{
	IN_JoyMove();

	if (!mouse)
		return;
	
	// drakkar
	if( in_keyboardShortcuts->modified ) {
		in_keyboardShortcuts->modified = qfalse;
		IN_UpdateKeyboardShortcuts();
	}
	// !drakkar

	if (cls.keyCatchers & KEYCATCH_CONSOLE) {
		// temporarily deactivate if not in the game and running on the desktop
		if (!Cvar_VariableValue("r_fullscreen")) {
			IN_Activate( qfalse );
			return;
		}
	}

    if (!g_wv.activeApp){
		IN_Activate( qfalse );
        return;
    }
    
    // this should really only happen on actual focus changes
    // but is needed to compensate for the console+windowed hack
    IN_Activate( qtrue );
    
	int mx, my;
	mouse->Read( &mx, &my );

	if ( !mx && !my )
		return;

	Sys_QueEvent( 0, SE_MOUSE, mx, my, 0, NULL );
}


/*
=========================================================================

JOYSTICK

=========================================================================
*/


typedef struct {
	qbool	avail;
	int			id;			// joystick number
	JOYCAPS		jc;

	int			oldbuttonstate;
	int			oldpovstate;

	JOYINFOEX	ji;
} joystickInfo_t;

static	joystickInfo_t	joy;


static const cvar_t* in_joyBallScale;
static const cvar_t* in_debugJoystick;
static const cvar_t* joy_threshold;


static void IN_StartupJoystick()
{
	// assume no joystick
	joy.avail = qfalse;

	if ( !in_joystick->integer )
		return;

	// verify joystick driver is present
	int numdevs;
	if ((numdevs = joyGetNumDevs()) == 0)
	{
		Com_Printf( "joystick not found -- driver not present\n" );
		return;
	}

	// cycle through the joystick ids for the first valid one
	MMRESULT mmr = !JOYERR_NOERROR;
	for (joy.id=0 ; joy.id<numdevs ; joy.id++)
	{
		Com_Memset (&joy.ji, 0, sizeof(joy.ji));
		joy.ji.dwSize = sizeof(joy.ji);
		joy.ji.dwFlags = JOY_RETURNCENTERED;

		if ((mmr = joyGetPosEx (joy.id, &joy.ji)) == JOYERR_NOERROR)
			break;
	}

	// abort startup if we didn't find a valid joystick
	if (mmr != JOYERR_NOERROR)
	{
		Com_Printf( "joystick not found -- no valid joysticks (%x)\n", mmr );
		return;
	}

	// get the capabilities of the selected joystick
	// abort startup if command fails
	Com_Memset (&joy.jc, 0, sizeof(joy.jc));
	if ((mmr = joyGetDevCaps (joy.id, &joy.jc, sizeof(joy.jc))) != JOYERR_NOERROR)
	{
		Com_Printf( "joystick not found -- invalid joystick capabilities (%x)\n", mmr );
		return;
	}

	// activate and archive the cvars since they actually want joystick support
	in_debugJoystick	= Cvar_Get ("in_debugjoystick",			"0",		CVAR_TEMP);
	in_joyBallScale		= Cvar_Get ("in_joyBallScale",			"0.02",		CVAR_ARCHIVE);
	joy_threshold		= Cvar_Get ("joy_threshold",			"0.15",		CVAR_ARCHIVE);

	Com_Printf( "Joystick found.\n" );
	Com_Printf( "Pname: %s\n", joy.jc.szPname );
	Com_Printf( "OemVxD: %s\n", joy.jc.szOEMVxD );
	Com_Printf( "RegKey: %s\n", joy.jc.szRegKey );

	Com_Printf( "Numbuttons: %i / %i\n", joy.jc.wNumButtons, joy.jc.wMaxButtons );
	Com_Printf( "Axis: %i / %i\n", joy.jc.wNumAxes, joy.jc.wMaxAxes );
	Com_Printf( "Caps: 0x%x\n", joy.jc.wCaps );
	if ( joy.jc.wCaps & JOYCAPS_HASPOV ) {
		Com_Printf( "HASPOV\n" );
	} else {
		Com_Printf( "no POV\n" );
	}

	// old button and POV states default to no buttons pressed
	joy.oldbuttonstate = 0;
	joy.oldpovstate = 0;

	// mark the joystick as available
	joy.avail = qtrue;
}


static float JoyToF( int value )
{
	// move centerpoint to zero
	value -= 32768;

	// convert range from -32768..32767 to -1..1
	float fValue = (float)value / 32768.0;

	return Com_Clamp( -1, 1, fValue );
}


static int JoyToI( int value )
{
	// move centerpoint to zero
	return (value - 32768);
}


static const int joyDirectionKeys[16] = {
	K_LEFTARROW, K_RIGHTARROW,
	K_UPARROW, K_DOWNARROW,
	K_JOY16, K_JOY17,
	K_JOY18, K_JOY19,
	K_JOY20, K_JOY21,
	K_JOY22, K_JOY23,
	K_JOY24, K_JOY25,
	K_JOY26, K_JOY27
};


static void IN_JoyMove()
{
	// verify joystick is available and that the user wants to use it
	if ( !joy.avail ) {
		return; 
	}

	// collect the joystick data, if possible
	Com_Memset (&joy.ji, 0, sizeof(joy.ji));
	joy.ji.dwSize = sizeof(joy.ji);
	joy.ji.dwFlags = JOY_RETURNALL;

	if ( joyGetPosEx (joy.id, &joy.ji) != JOYERR_NOERROR ) {
		// read error occurred
		// turning off the joystick seems too harsh for 1 read error,
		// but what should be done?
		// Com_Printf ("IN_ReadJoystick: no response\n");
		// joy.avail = qfalse;
		return;
	}

	if ( in_debugJoystick->integer ) {
		Com_Printf( "%8x %5i %5.2f %5.2f %5.2f %5.2f %6i %6i\n",
			joy.ji.dwButtons,
			joy.ji.dwPOV,
			JoyToF( joy.ji.dwXpos ), JoyToF( joy.ji.dwYpos ),
			JoyToF( joy.ji.dwZpos ), JoyToF( joy.ji.dwRpos ),
			JoyToI( joy.ji.dwUpos ), JoyToI( joy.ji.dwVpos ) );
	}

	// loop through the joystick buttons
	// key a joystick event or auxillary event for higher number buttons for each state change
	DWORD buttonstate = joy.ji.dwButtons;
	for (UINT i=0 ; i < joy.jc.wNumButtons ; i++ ) {
		if ( (buttonstate & (1<<i)) && !(joy.oldbuttonstate & (1<<i)) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_JOY1 + i, qtrue, 0, NULL );
		}
		if ( !(buttonstate & (1<<i)) && (joy.oldbuttonstate & (1<<i)) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_JOY1 + i, qfalse, 0, NULL );
		}
	}
	joy.oldbuttonstate = buttonstate;

	DWORD povstate = 0;

	// convert main joystick motion into 6 direction button bits
	for (UINT i = 0; i < joy.jc.wNumAxes && i < 4 ; i++) {
		// get the floating point zero-centered, potentially-inverted data for the current axis
		float fAxisValue = JoyToF( (&joy.ji.dwXpos)[i] );

		if ( fAxisValue < -joy_threshold->value ) {
			povstate |= (1<<(i*2));
		} else if ( fAxisValue > joy_threshold->value ) {
			povstate |= (1<<(i*2+1));
		}
	}

	// convert POV information from a direction into 4 button bits
	if ( joy.jc.wCaps & JOYCAPS_HASPOV ) {
		if ( joy.ji.dwPOV != JOY_POVCENTERED ) {
			if (joy.ji.dwPOV == JOY_POVFORWARD)
				povstate |= 1<<12;
			if (joy.ji.dwPOV == JOY_POVBACKWARD)
				povstate |= 1<<13;
			if (joy.ji.dwPOV == JOY_POVRIGHT)
				povstate |= 1<<14;
			if (joy.ji.dwPOV == JOY_POVLEFT)
				povstate |= 1<<15;
		}
	}

	// determine which bits have changed and key an auxillary event for each change
	for (UINT i=0 ; i < 16 ; i++) {
		if ( (povstate & (1<<i)) && !(joy.oldpovstate & (1<<i)) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, joyDirectionKeys[i], qtrue, 0, NULL );
		}

		if ( !(povstate & (1<<i)) && (joy.oldpovstate & (1<<i)) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, joyDirectionKeys[i], qfalse, 0, NULL );
		}
	}
	joy.oldpovstate = povstate;

	// if there is a trackball like interface, simulate mouse moves
	if ( joy.jc.wNumAxes >= 6 ) {
		int x = JoyToI( joy.ji.dwUpos ) * in_joyBallScale->value;
		int y = JoyToI( joy.ji.dwVpos ) * in_joyBallScale->value;
		if ( x || y ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_MOUSE, x, y, 0, NULL );
		}
	}
}


/*
=========================================================================

MIDI

=========================================================================
*/


#define MAX_MIDIIN_DEVICES	8

typedef struct {
	int			numDevices;
	MIDIINCAPS	caps[MAX_MIDIIN_DEVICES];
	HMIDIIN		hMidiIn;
} MidiInfo_t;

static MidiInfo_t s_midiInfo;


static const cvar_t* in_midiport;
static const cvar_t* in_midichannel;
static const cvar_t* in_mididevice;


static void MIDI_NoteOff( int note )
{
	int qkey = note - 60 + K_AUX1;

	if ( qkey > 255 || qkey < K_AUX1 )
		return;

	Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, qkey, qfalse, 0, NULL );
}

static void MIDI_NoteOn( int note, int velocity )
{
	if ( velocity == 0 )
		MIDI_NoteOff( note );

	int qkey = note - 60 + K_AUX1;

	if ( qkey > 255 || qkey < K_AUX1 )
		return;

	Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, qkey, qtrue, 0, NULL );
}

static void CALLBACK MidiInProc( HMIDIIN hMidiIn, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2 )
{
	switch ( uMsg )
	{
	case MIM_OPEN:
		break;
	case MIM_CLOSE:
		break;
	case MIM_DATA:
	    {
		    int message = dwParam1 & 0xff;
		    if ( ( message & 0xf0 ) == 0x90 )
		    {
			    if ( ( ( message & 0x0f ) + 1 ) == in_midichannel->integer )
				    MIDI_NoteOn( ( dwParam1 & 0xff00 ) >> 8, ( dwParam1 & 0xff0000 ) >> 16 );
		    }
		    else if ( ( message & 0xf0 ) == 0x80 )
		    {
			    if ( ( ( message & 0x0f ) + 1 ) == in_midichannel->integer )
				    MIDI_NoteOff( ( dwParam1 & 0xff00 ) >> 8 );
		    }
		}
		break;
	case MIM_LONGDATA:
		break;
	case MIM_ERROR:
		break;
	case MIM_LONGERROR:
		break;
	}

//	Sys_QueEvent( sys_msg_time, SE_KEY, wMsg, qtrue, 0, NULL );
}

static void MidiInfo_f( void )
{
	const char *enableStrings[] = { "disabled", "enabled" };

	Com_Printf( "\nMIDI control:       %s\n", enableStrings[in_midi->integer != 0] );
	Com_Printf( "port:               %d\n", in_midiport->integer );
	Com_Printf( "channel:            %d\n", in_midichannel->integer );
	Com_Printf( "current device:     %d\n", in_mididevice->integer );
	Com_Printf( "number of devices:  %d\n", s_midiInfo.numDevices );

	for (int i = 0; i < s_midiInfo.numDevices; i++ )
	{
		if ( i == Cvar_VariableValue( "in_mididevice" ) )
			Com_Printf( "***" );
		else
			Com_Printf( "..." );
		Com_Printf(    "device %2d:       %s\n", i, s_midiInfo.caps[i].szPname );
		Com_Printf( "...manufacturer ID: 0x%hx\n", s_midiInfo.caps[i].wMid );
		Com_Printf( "...product ID:      0x%hx\n", s_midiInfo.caps[i].wPid );
		Com_Printf( "\n" );
	}
}

static void IN_StartupMIDI()
{
	if ( !Cvar_VariableValue( "in_midi" ) )
		return;

	//
	// enumerate MIDI IN devices
	//
	s_midiInfo.numDevices = midiInGetNumDevs();

	for (int i = 0; i < s_midiInfo.numDevices; i++ )
	{
		midiInGetDevCaps( i, &s_midiInfo.caps[i], sizeof( s_midiInfo.caps[i] ) );
	}

	// activate and archive the cvars since they actually want midi support
	in_midiport		= Cvar_Get ("in_midiport",				"1",		CVAR_ARCHIVE);
	in_midichannel	= Cvar_Get ("in_midichannel",			"1",		CVAR_ARCHIVE);
	in_mididevice	= Cvar_Get ("in_mididevice",			"0",		CVAR_ARCHIVE);

	Cmd_AddCommand( "midiinfo", MidiInfo_f );

	//
	// open the MIDI IN port
	//
	if ( midiInOpen( &s_midiInfo.hMidiIn,
					 in_mididevice->integer,
					 ( unsigned long ) MidiInProc,
					 ( unsigned long ) NULL,
					 CALLBACK_FUNCTION ) != MMSYSERR_NOERROR )
	{
		Com_Printf( "WARNING: could not open MIDI device %d: '%s'\n", in_mididevice->integer , s_midiInfo.caps[( int ) in_mididevice->value] );
		return;
	}

	midiInStart( s_midiInfo.hMidiIn );
}

static void IN_ShutdownMIDI()
{
	if ( s_midiInfo.hMidiIn )
		midiInClose( s_midiInfo.hMidiIn );

	Com_Memset( &s_midiInfo, 0, sizeof( s_midiInfo ) );
	Cmd_RemoveCommand( "midiinfo" );
}

