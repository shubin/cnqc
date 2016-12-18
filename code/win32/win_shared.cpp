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


int Sys_Milliseconds()
{
	static int sys_timeBase = 0;

	if (!sys_timeBase)
		sys_timeBase = timeGetTime();

	return (timeGetTime() - sys_timeBase);
}


// disable all optimizations temporarily so this code works correctly!
#ifdef _MSC_VER
#pragma optimize( "", off )
#pragma warning(disable : 4748) // don't fkn WHINE about using that pragma
#endif


static void CPUID( int func, unsigned regs[4] )
{
	unsigned regEAX, regEBX, regECX, regEDX;

	__asm mov eax, func
	__asm __emit 00fh
	__asm __emit 0a2h
	__asm mov regEAX, eax
	__asm mov regEBX, ebx
	__asm mov regECX, ecx
	__asm mov regEDX, edx

	regs[0] = regEAX;
	regs[1] = regEBX;
	regs[2] = regECX;
	regs[3] = regEDX;
}

static qbool IsPentium()
{
	__asm
	{
		pushfd						// save eflags
		pop		eax
		test	eax, 0x00200000		// check ID bit
		jz		set21				// bit 21 is not set, so jump to set_21
		and		eax, 0xffdfffff		// clear bit 21
		push	eax					// save new value in register
		popfd						// store new value in flags
		pushfd
		pop		eax
		test	eax, 0x00200000		// check ID bit
		jz		good
		jmp		err					// cpuid not supported
set21:
		or		eax, 0x00200000		// set ID bit
		push	eax					// store new value
		popfd						// store new value in EFLAGS
		pushfd
		pop		eax
		test	eax, 0x00200000		// if bit 21 is on
		jnz		good
		jmp		err
	}

err:
	return qfalse;
good:
	return qtrue;
}

static const char* CPU_Name()
{
	static unsigned regs[4];

	CPUID( 0, regs );

	regs[0] = regs[1];
	regs[1] = regs[3];
	regs[3] = 0;
	return (const char*)regs;
}

static int CPU_Cores()
{
	unsigned regs[4];
	CPUID( 1, regs );
	return ((regs[1] & 0x00FF0000) >> 16);
}

struct CPU_FeatureBit { const char* s; int reg, bit; } CPU_FeatureBits[] =
{
	{ " MMX",  3, 23 },
	{ " SSE",  3, 25 },
	{ " SSE2", 3, 26 },
	{ " SSE3", 2, 26 },
	{ 0 }
};


int Sys_GetProcessorId()
{
#if defined _M_ALPHA
	return CPUID_AXP;
#elif !defined _M_IX86
	return CPUID_GENERIC;
#else

	// verify we're at least a Pentium or 486 w/ CPUID support
	if ( !IsPentium() ) {
		Cvar_Set( "sys_cpustring", "x86 (pre-Pentium)" );
		return CPUID_UNSUPPORTED;
	}

	char s[64] = "";
	Q_strcat( s, sizeof(s), CPU_Name() );

	if (CPU_Cores() > 1)
		Q_strcat( s, sizeof(s), va(" %d cores", CPU_Cores()) );

	unsigned regs[4];
	CPUID( 1, regs );

	for (int i = 0; CPU_FeatureBits[i].s; ++i) {
		if (regs[CPU_FeatureBits[i].reg] & (1 << regs[CPU_FeatureBits[i].bit])) {
			Q_strcat( s, sizeof(s), CPU_FeatureBits[i].s );
		}
	}

	Cvar_Set( "sys_cpustring", s );
	return CPUID_GENERIC;

#endif
}


#ifdef _MSC_VER
#pragma optimize( "", on )
#endif


const char* Sys_GetCurrentUser()
{
	return "player";
}


const char* Sys_DefaultHomePath()
{
	return NULL;
}

