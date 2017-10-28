#pragma once


#define MAX_MONITOR_COUNT 16


struct glImp_t {
	SDL_Window*		window;
	SDL_GLContext	glContext;

	SDL_Rect		monitorRects[MAX_MONITOR_COUNT];
	int				monitorCount;
	int				primaryMonitor;	// primary monitor, 0-based
	int				monitor;		// current monitor, 0-based
};


extern glImp_t glimp;
