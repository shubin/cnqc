#pragma once


#define MAX_MONITOR_COUNT 16


struct monitor_t {
	SDL_Rect		rect;
	int				sdlIndex;
};

struct glImp_t {
	SDL_Window*		window;
	SDL_GLContext	glContext;

	monitor_t		monitors[MAX_MONITOR_COUNT];
	int				monitorCount;
	int				monitor; // indexes monitors
};


extern glImp_t glimp;
