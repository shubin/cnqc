#include "linux_local.h"
#include "../renderer/tr_local.h"
#include "../renderer/qgl.h"

#include <SDL2/SDL.h>
#include "sdl_local.h"


glImp_t glimp;


cvar_t* r_fullscreen;
static cvar_t* r_monitor; // 1-based, 0 means use primary monitor

static const cvarTableItem_t glimp_cvars[] = {
	{ &r_fullscreen, "r_fullscreen", "0", CVAR_ARCHIVE | CVAR_LATCH, CVART_BOOL, NULL, NULL, "full-screen mode" },
	{ &r_monitor, "r_monitor", "0", CVAR_ARCHIVE | CVAR_LATCH, CVART_INTEGER, "0", NULL, "1-based monitor index, 0=primary" }
};

static void sdl_PrintMonitorList();

static const cmdTableItem_t glimp_cmds[] = {
	{ "monitorlist", &sdl_PrintMonitorList, NULL, "prints the list of monitors" }
};


static qbool sdl_IsMonitorListValid()
{
	const int count = glimp.monitorCount;
	const int curr = glimp.monitor;
	const int prim = glimp.primaryMonitor;

	return
		count >= 1 &&
		curr >= 0 &&
		curr < count &&
		prim >= 0 &&
		prim < count &&
		glimp.monitorRects[prim].x == 0 &&
		glimp.monitorRects[prim].y == 0;
}


static void sdl_CreateMonitorList()
{
	const int count = SDL_GetNumVideoDisplays();
	if (count <= 0) {
		glimp.monitorCount = 0;
		return;
	}

	int gi = 0;
	for (int si = 0; si < count; ++si) {
		if (gi >= MAX_MONITOR_COUNT)
			break;
		if (SDL_GetDisplayBounds(si, &glimp.monitorRects[gi]) == 0)
			++gi;
	}
	glimp.monitorCount = gi;

	glimp.primaryMonitor = -1;
	const int finalCount = glimp.monitorCount;
	for(int i = 0; i < finalCount; ++i) {
		const SDL_Rect rect = glimp.monitorRects[i];
		if (rect.x == 0 && rect.y == 0) {
			glimp.primaryMonitor = i;
			break;
		}
	}

	if (!sdl_IsMonitorListValid())
		glimp.monitorCount = 0;
}


// call this before creating the window
static void sdl_UpdateMonitorIndexFromCvar()
{
	if (glimp.monitorCount <= 0)
		return;

	const int monitor = Cvar_Get("r_monitor", "0", CVAR_ARCHIVE | CVAR_LATCH)->integer;
	if (monitor <= 0 || monitor > glimp.monitorCount) {
		glimp.monitor = glimp.primaryMonitor;
		return;
	}
	glimp.monitor = Com_ClampInt(0, glimp.monitorCount - 1, monitor - 1);
}


// call this after the window has been moved
void sdl_UpdateMonitorIndexFromWindow()
{
	if (glimp.monitorCount <= 0)
		return;

	// update the glimp index
	const int current = SDL_GetWindowDisplayIndex(glimp.window);
	if (current < 0) {
		glimp.monitorCount = 0;
		return;
	}
	glimp.monitor = current;

	// update the cvar index
	if( r_monitor->integer == 0 &&
		glimp.monitor == glimp.primaryMonitor)
		return;
	Cvar_Set("r_monitor", va("%d", glimp.monitor + 1));
}


static void sdl_GetSafeDesktopRect( SDL_Rect* rect )
{
	if (glimp.monitorCount <= 0 ||
		glimp.monitor < 0 ||
		glimp.monitor >= glimp.monitorCount) {
		rect->x = 0;
		rect->y = 0;
		rect->w = 1280;
		rect->h = 720;
	}

	*rect = glimp.monitorRects[glimp.monitor];
}


static void sdl_PrintMonitorList()
{
	const int count = glimp.monitorCount;
	Com_Printf("Monitor count: %d\n", count);

	for (int i = 0; i < count; ++i) {
		const SDL_Rect rect = glimp.monitorRects[i];
		Com_Printf("Monitor #%d: %d,%d %dx%d\n", i + 1, rect.x, rect.y, rect.w, rect.h);
	}
}


void Sys_GL_Init()
{
	if (glimp.window != NULL)
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

	Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
	if (glInfo.winFullscreen) {
		if (glInfo.vidFullscreen)
			windowFlags |= SDL_WINDOW_FULLSCREEN;
		else
			windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}

	// @TODO: make a cvar defaulting to an empty string for this? e.g. value: "libGL.so.1"
	if (SDL_GL_LoadLibrary(NULL) < 0)
		ri.Error(ERR_FATAL, "Sys_GL_Init - SDL_GL_LoadLibrary failed: %s\n", SDL_GetError());

	glimp.window = SDL_CreateWindow("CNQ3", deskropRect.x, deskropRect.y, glConfig.vidWidth, glConfig.vidHeight, windowFlags);
	if (glimp.window == NULL)
		ri.Error(ERR_FATAL, "Sys_GL_Init - SDL_CreateWindow failed: %s\n", SDL_GetError());

	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
	SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
	glimp.glContext = SDL_GL_CreateContext(glimp.window);
	if (glimp.glContext == NULL)
		ri.Error(ERR_FATAL, "Sys_GL_Init - SDL_GL_CreateContext failed: %s\n", SDL_GetError());
	glConfig.colorBits = 32;
	glConfig.depthBits = 24;
	glConfig.stencilBits = 8;

	if (SDL_GL_MakeCurrent(glimp.window, glimp.glContext) < 0)
		ri.Error(ERR_FATAL, "Sys_GL_Init - SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());

	if (!Lin_LoadGL())
		ri.Error(ERR_FATAL, "Sys_GL_Init - failed to initialize core OpenGL\n");
}


void Sys_GL_Shutdown()
{
	if (glimp.glContext != NULL) {
		SDL_GL_DeleteContext(glimp.glContext);
		glimp.glContext = NULL;
	}

	if (glimp.window != NULL) {
		SDL_DestroyWindow(glimp.window);
		glimp.window = NULL;
	}

	SDL_GL_UnloadLibrary();
	Lin_UnloadGL();
}


void Sys_GL_EndFrame()
{
	if (r_swapInterval->modified) {
		r_swapInterval->modified = qfalse;
		SDL_GL_SetSwapInterval(r_swapInterval->integer);
	}

	SDL_GL_SwapWindow(glimp.window);
}
