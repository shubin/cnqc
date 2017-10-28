#include "linux_local.h"


int    q_argc = 0;
char** q_argv = NULL;


int main( int argc, char** argv )
{
	q_argc = argc;
	q_argv = argv;

#ifdef DEDICATED
	Lin_HardRebootHandler(argc, argv);
#endif

	SIG_InitChild();

#ifndef DEDICATED
	if (!sdl_Init())
		return 1;
#endif

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

	Lin_ConsoleInputInit();
	Lin_TrackParentProcess();

#ifndef DEDICATED	
	sdl_InitCvarsAndCmds();
#endif

	for (;;) {
		SIG_Frame();
#ifndef DEDICATED
		sdl_Frame();
#endif
		Com_Frame();
	}

	return 0;
}
