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
#include <signal.h>

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#ifndef DEDICATED
#include "../renderer/tr_local.h"
#endif
#include "linux_local.h"


// columns: Symbol, IsCrash, Desc
#define SIGNAL_LIST(X) \
	X(SIGHUP,	qfalse,	"hangup detected on controlling terminal or death of controlling process") \
	X(SIGQUIT,	qfalse,	"quit from keyboard") \
	X(SIGILL,	qtrue,	"illegal instruction") \
	X(SIGTRAP,	qfalse,	"trace/breakpoint trap") \
	X(SIGIOT,	qtrue,	"IOT trap (a synonym for SIGABRT)") \
	X(SIGBUS,	qtrue,	"bus error (bad memory access)") \
	X(SIGFPE,	qtrue,	"fatal arithmetic error") \
	X(SIGSEGV,	qtrue,	"invalid memory reference") \
	X(SIGTERM,	qfalse,	"termination signal") \
	X(SIGINT,	qfalse,	"interrupt")


static qboolean Sig_IsCrashSignal(int sig)
{
#define SIGNAL_ITEM(Symbol, IsCrash, Desc) case Symbol: return IsCrash;
	switch (sig) {
		SIGNAL_LIST(SIGNAL_ITEM)
		default: return qfalse;
	}
#undef SIGNAL_ITEM
}


static const char* Sig_GetDescription(int sig)
{
#define SIGNAL_ITEM(Symbol, IsCrash, Desc) case Symbol: return Desc;
	switch (sig) {
		SIGNAL_LIST(SIGNAL_ITEM)
		default: return "unhandled signal";
	}
#undef SIGNAL_ITEM
}


static const char* Sig_GetName(int sig)
{
#define SIGNAL_ITEM(Symbol, IsCrash, Desc) case Symbol: return #Symbol;
	switch (sig) {
		SIGNAL_LIST(SIGNAL_ITEM)
		default: return "unhandled signal";
	}
#undef SIGNAL_ITEM
}


// Every call in there needs to be safe when called more than once.
static void SIG_HandleCrash()
{
	// We crashed and only care about restoring system settings
	// that the process clean-up won't handle for us.
#ifndef DEDICATED
	LIN_RestoreGamma();
#endif
}


static void Sig_HandleSignal(int sig)
{
	static int faultCounter = 0;
	static qbool crashHandled = qfalse;
	
	faultCounter++;

	if (faultCounter >= 3)
	{
		// We're here because the double fault handler failed.
		// We take no chances this time and exit right away to avoid
		// calling this function many more times.
		exit(3);
	}

	const qbool crashed = Sig_IsCrashSignal(sig);
	if (faultCounter == 2)
	{
		// The termination handler failed which means that if we exit right now,
		// some system settings might still be in a bad state.
		printf("DOUBLE SIGNAL FAULT: Received signal %d (%s), exiting...\n", sig, Sig_GetName(sig));
		if (crashed && !crashHandled)
		{
			SIG_HandleCrash();
		}
		exit(2);
	}

	fprintf(crashed ? stderr : stdout, "Received %s signal %d: %s (%s), exiting...\n",
		crashed ? "crash" : "termination", sig, Sig_GetName(sig), Sig_GetDescription(sig));
	
	if (crashed)
	{
		SIG_HandleCrash();
		crashHandled = qtrue;
		exit(1);
	}
	else
	{
		// attempt a proper shutdown sequence
#ifndef DEDICATED
		CL_Shutdown();
#endif
		SV_Shutdown("Signal caught");
		Sys_Exit(0);
	}
}


void SIG_Init(void)
{
	// This is unfortunately needed because some code might
	// call exit and bypass all the clean-up work without
	// there ever being a real crash.
	// This happens for instance with the "fatal IO error"
	// of the X server.
	atexit(SIG_HandleCrash);
	
	signal(SIGILL,  Sig_HandleSignal);
	signal(SIGIOT,  Sig_HandleSignal);
	signal(SIGBUS,  Sig_HandleSignal);
	signal(SIGFPE,  Sig_HandleSignal);
	signal(SIGSEGV, Sig_HandleSignal);
	signal(SIGHUP,  Sig_HandleSignal);
	signal(SIGQUIT, Sig_HandleSignal);
	signal(SIGTRAP, Sig_HandleSignal);
	signal(SIGTERM, Sig_HandleSignal);
	signal(SIGINT,  Sig_HandleSignal);
}
