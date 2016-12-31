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

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "win_local.h"
#include <intrin.h>


int Sys_Milliseconds()
{
	static int sys_timeBase = 0;

	if (!sys_timeBase)
		sys_timeBase = timeGetTime();

	return (timeGetTime() - sys_timeBase);
}


static const char* CPU_Name()
{
	static int regs[4];

	__cpuid( regs, 0 );
	regs[0] = regs[1];
	regs[1] = regs[3];
	regs[3] = 0;

	return (const char*)regs;
}


struct CPU_FeatureBit { const char* s; int reg, bit; } CPU_FeatureBits[] =
{
#if id386 // x64 always has those anyway
	{ " MMX",  3, 23 },
	{ " SSE",  3, 25 },
	{ " SSE2", 3, 26 },
#endif
	{ " SSE3", 2, 0 },
	{ " SSSE3", 2, 9 },
	{ " SSE4.1", 2, 19 },
	{ " SSE4.2", 2, 20 },
	{ " AVX", 2, 28 }
	// for AVX2 and later, you'd need to call cpuid with eax=7 and ecx=0 ("extended features")
};
static const int CPU_FeatureBitCount = sizeof(CPU_FeatureBits) / sizeof(CPU_FeatureBits[0]);


int Sys_GetProcessorId()
{
	char s[256] = "";
	Q_strcat( s, sizeof(s), CPU_Name() );

	int regs[4];
	__cpuid( regs, 1 );

	for (int i = 0; i < CPU_FeatureBitCount; ++i) {
		if (regs[CPU_FeatureBits[i].reg] & (1 << regs[CPU_FeatureBits[i].bit])) {
			Q_strcat( s, sizeof(s), CPU_FeatureBits[i].s );
		}
	}

	Cvar_Set( "sys_cpustring", s );

	return CPUID_GENERIC;
}


const char* Sys_GetCurrentUser()
{
	return "player";
}


const char* Sys_DefaultHomePath()
{
	return NULL;
}

