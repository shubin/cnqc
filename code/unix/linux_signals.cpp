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
#include <execinfo.h>
#include <backtrace.h>

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../qcommon/crash.h"
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


static void Sig_WriteJSON(int sig)
{
	FILE* const file = fopen(va("%s-crash.json", q_argv[0]), "w");
	if (file == NULL)
		return;

	JSONW_BeginFile(file);
	JSONW_IntegerValue("signal", sig);
	JSONW_StringValue("signal_name", Sig_GetName(sig));
	JSONW_StringValue("signal_description", Sig_GetDescription(sig));
	Crash_PrintToFile(q_argv[0]);
	JSONW_EndFile();
	fclose(file);
}


// only uses functions safe to call in a signal handler
static void Sig_WriteBacktraceSafe()
{
	FILE* const file = fopen(va("%s-crash.bt", q_argv[0]), "w");
	if (file == NULL)
		return;

	void* addresses[64];
	const int addresscount = backtrace(addresses, sizeof(addresses));
	if (addresscount > 0)
	{
		fprintf(file, "backtrace_symbols_fd stack trace:\r\n");
		fflush(file);
		backtrace_symbols_fd(addresses, addresscount, fileno(file));
	}
	else
	{
		fprintf(file, "The call to backtrace failed\r\n");
	}
	
	fclose(file);
}


static void libbt_ErrorCallback(void* data, const char* msg, int errnum)
{
	fprintf((FILE*)data, "libbacktrace error: %s (%d)\r\n", msg, errnum);
}


// might not be safe in a signal handler
static void Sig_WriteBacktraceUnsafe()
{
	FILE* const file = fopen(va("%s-crash.bt", q_argv[0]), "a");
	if (file == NULL)
		return;

	fprintf(file, "\r\n\r\n");

	backtrace_state* const state = backtrace_create_state(q_argv[0], 0, libbt_ErrorCallback, file);
	if (state)
	{
		fprintf(file, "libbacktrace stack trace:\r\n");
		fflush(file);
		backtrace_print(state, 0, file);
	}
	else
	{
		fprintf(file, "The call to backtrace_create_state failed\r\n");
	}
	
	fclose(file);
}


static qbool sig_crashed = qfalse;
static int sig_signal = 0;


// Every call in there needs to be safe when called more than once.
static void SIG_HandleCrash()
{
	// We crashed and only care about restoring system settings
	// that the process clean-up won't handle for us.
#ifndef DEDICATED
	LIN_RestoreGamma();
#endif
	Sys_ConsoleInputShutdown();
	
	if (!sig_crashed)
		return;
		
	Sig_WriteJSON(sig_signal);
	Sig_WriteBacktraceSafe();
	Sig_WriteBacktraceUnsafe();
}


static void Sig_HandleSignal(int sig)
{
	static int faultCounter = 0;
	static qbool crashHandled = qfalse;

	sig_signal = sig;
	faultCounter++;

	if (faultCounter >= 3)
	{
		// We're here because the double fault handler failed.
		// We take no chances this time and exit right away to avoid
		// calling this function many more times.
		exit(3);
	}

	sig_crashed = Sig_IsCrashSignal(sig);
	if (faultCounter == 2)
	{
		// The termination handler failed which means that if we exit right now,
		// some system settings might still be in a bad state.
		printf("DOUBLE SIGNAL FAULT: Received signal %d (%s), exiting...\n", sig, Sig_GetName(sig));
		if (sig_crashed && !crashHandled)
		{
			SIG_HandleCrash();
		}
		exit(2);
	}

	fprintf(sig_crashed ? stderr : stdout, "Received %s signal %d: %s (%s), exiting...\n",
		sig_crashed ? "crash" : "termination", sig, Sig_GetName(sig), Sig_GetDescription(sig));
	
	if (sig_crashed)
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
		Sys_ConsoleInputShutdown();
		exit(0);
	}
}


void SIG_Init()
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
