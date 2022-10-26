/*
===========================================================================
Copyright (C) 2017-2019 Gian 'myT' Schellenbaum

This file is part of Challenge Quake 3 (CNQ3).

Challenge Quake 3 is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Challenge Quake 3 is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Challenge Quake 3. If not, see <https://www.gnu.org/licenses/>.
===========================================================================
*/
// Linux video using SDL 2

#include "macosx_local.h"
#include "macosx_public.h"
#include "../renderer/tr_local.h"

#include <SDL.h>
#include "sdl_local.h"

wsi_t wsi;
mtl_imp_t mtl_imp;

cvar_t* r_fullscreen;
static cvar_t* r_monitor;

#define help_r_monitor \
"0-based monitor index\n" \
"Use /" S_COLOR_CMD "monitorlist " S_COLOR_HELP "to print the list of detected monitors.\n" \
"The monitors are ordered top-to-bottom and left-to-right.\n" \
"This means " S_COLOR_VAL "0 " S_COLOR_HELP "specifies the top-left monitor."

static const cvarTableItem_t glimp_cvars[] = {
	{ &r_fullscreen, "r_fullscreen", "0", CVAR_ARCHIVE | CVAR_LATCH, CVART_BOOL, NULL, NULL, "full-screen mode" },
	{ &r_monitor, "r_monitor", "0", CVAR_ARCHIVE | CVAR_LATCH, CVART_INTEGER, "0", NULL, help_r_monitor }
};

static void sdl_MonitorList_f();

static const cmdTableItem_t glimp_cmds[] = {
	{ "monitorlist", &sdl_MonitorList_f, NULL, "refreshes and prints the monitor list" }
};


static qbool sdl_IsMonitorListValid()
{
	const int count = wsi.monitorCount;
	const int curr = wsi.monitor;

	return
		count >= 1 && count <= MAX_MONITOR_COUNT &&
		curr >= 0 && curr < count;
}


static int sdl_CompareMonitors( const void* aPtr, const void* bPtr )
{
	const SDL_Rect* const a = &((const monitor_t*)aPtr)->rect;
	const SDL_Rect* const b = &((const monitor_t*)bPtr)->rect;
	const int dy = a->y - b->y;
	if (dy != 0)
		return dy;

	return a->x - b->x;
}


static void sdl_CreateMonitorList()
{
	wsi.monitorCount = 0;

	const int count = SDL_GetNumVideoDisplays();
	if (count <= 0)
		return;

	int gi = 0;
	for (int si = 0; si < count; ++si) {
		if (gi >= MAX_MONITOR_COUNT)
			break;
		if (SDL_GetDisplayBounds(si, &wsi.monitors[gi].rect) == 0) {
			wsi.monitors[gi].sdlIndex = si;
			++gi;
		}
	}
	wsi.monitorCount = gi;

	if (sdl_IsMonitorListValid())
		qsort(wsi.monitors, (size_t)wsi.monitorCount, sizeof(wsi.monitors[0]), &sdl_CompareMonitors);
	else
		wsi.monitorCount = 0;
}


// call this before creating the window
static void sdl_UpdateMonitorIndexFromCvar()
{
	if (wsi.monitorCount <= 0 || wsi.monitorCount >= MAX_MONITOR_COUNT)
		return;

	const int monitor = Cvar_Get("r_monitor", "0", CVAR_ARCHIVE | CVAR_LATCH)->integer;
	if (monitor < 0 || monitor >= wsi.monitorCount) {
		wsi.monitor = 0;
		return;
	}
	wsi.monitor = monitor;
}


// call this after the window has been moved
void sdl_UpdateMonitorIndexFromWindow()
{
	if (wsi.monitorCount <= 0)
		return;

	// try to find the glimp index and update data accordingly
	const int sdlIndex = SDL_GetWindowDisplayIndex(wsi.window);
	for (int i = 0; i < wsi.monitorCount; ++i) {
		if (wsi.monitors[i].sdlIndex == sdlIndex) {
			wsi.monitor = i;
			Cvar_Set("r_monitor", va("%d", i));
			break;
		}
	}
}


static void sdl_GetSafeDesktopRect( SDL_Rect* rect )
{
	if (!sdl_IsMonitorListValid()) {
		rect->x = 0;
		rect->y = 0;
		rect->w = 1280;
		rect->h = 720;
	}

	*rect = wsi.monitors[wsi.monitor].rect;
}


static void sdl_PrintMonitorList()
{
	const int count = wsi.monitorCount;
	if (count <= 0) {
		Com_Printf("No monitor detected.\n");
		return;
	}

	Com_Printf("Monitors detected (left is " S_COLOR_CVAR "r_monitor ^7value):\n");
	for (int i = 0; i < count; ++i) {
		const SDL_Rect rect = wsi.monitors[i].rect;
		Com_Printf(S_COLOR_VAL "%d ^7%dx%d at %d,%d\n", i, rect.w, rect.h, rect.x, rect.y);
	}
}


static void sdl_MonitorList_f()
{
	sdl_CreateMonitorList();
	sdl_UpdateMonitorIndexFromCvar();
	sdl_PrintMonitorList();
}


void Sys_V_Init( galId_t type )
{
	if (wsi.window != NULL)
		return;

	Cvar_RegisterArray(glimp_cvars, MODULE_CLIENT);

	static qbool firstInit = qtrue;
	if (firstInit) {
		Cmd_RegisterArray(glimp_cmds, MODULE_CLIENT);
		firstInit = qfalse;
	}

	sdl_CreateMonitorList();
	sdl_UpdateMonitorIndexFromCvar();
	sdl_PrintMonitorList();

	SDL_Rect deskropRect;
	sdl_GetSafeDesktopRect(&deskropRect);
	R_ConfigureVideoMode(deskropRect.w, deskropRect.h);

	Uint32 windowFlags = SDL_WINDOW_METAL | SDL_WINDOW_SHOWN;
	if (glInfo.winFullscreen) {
		if (glInfo.vidFullscreen)
			windowFlags |= SDL_WINDOW_FULLSCREEN;
		else
			windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}

	wsi.window = SDL_CreateWindow("CNQ3", deskropRect.x, deskropRect.y, glConfig.vidWidth, glConfig.vidHeight, windowFlags);
	if (wsi.window == NULL)
		ri.Error(ERR_FATAL, "Sys_V_Init - SDL_CreateWindow failed: %s\n", SDL_GetError());

	glConfig.colorBits = 32;
	glConfig.depthBits = 24;
	glConfig.stencilBits = 8;

	mtl_imp.view = SDL_Metal_CreateView(wsi.window);
	mtl_imp.layer = SDL_Metal_GetLayer(mtl_imp.view);
}


void Sys_V_Shutdown()
{
	if (mtl_imp.view != NULL) {
		mtl_imp.layer = NULL;
		SDL_Metal_DestroyView(mtl_imp.view);
	}

	if (wsi.window != NULL) {
		SDL_DestroyWindow(wsi.window);
		wsi.window = NULL;
	}
}


void Sys_V_EndFrame()
{
}


qbool Sys_V_IsVSynced()
{
	return true;
}
