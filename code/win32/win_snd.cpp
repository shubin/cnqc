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

#include "../client/snd_local.h"
#include "win_local.h"

#include <dsound.h>
#pragma comment( lib, "dxguid" )


#define SECONDARY_BUFFER_SIZE	0x10000

static int		sample16;
static DWORD	gSndBufSize;
static DWORD	locksize;
static LPDIRECTSOUND pDS;
static LPDIRECTSOUNDBUFFER pDSBuf, pDSPBuf;
static HINSTANCE hInstDS;


static const char* DSoundError( int error )
{
	switch ( error ) {
	case DSERR_BUFFERLOST:
		return "DSERR_BUFFERLOST";
	case DSERR_INVALIDCALL:
		return "DSERR_INVALIDCALLS";
	case DSERR_INVALIDPARAM:
		return "DSERR_INVALIDPARAM";
	case DSERR_PRIOLEVELNEEDED:
		return "DSERR_PRIOLEVELNEEDED";
	}

	return "unknown";
}


void SNDDMA_Shutdown()
{
	Com_DPrintf( "Shutting down sound system\n" );

	if ( pDS ) {
		Com_DPrintf( "Destroying DS buffers\n" );
		if ( pDS )
		{
			Com_DPrintf( "...setting NORMAL coop level\n" );
			pDS->SetCooperativeLevel( g_wv.hWnd, DSSCL_PRIORITY );
		}

		if ( pDSBuf )
		{
			Com_DPrintf( "...stopping and releasing sound buffer\n" );
			pDSBuf->Stop();
			pDSBuf->Release();
		}

		// only release primary buffer if it's not also the mixing buffer we just released
		if ( pDSPBuf && ( pDSBuf != pDSPBuf ) )
		{
			Com_DPrintf( "...releasing primary buffer\n" );
			pDSPBuf->Release();
		}
		pDSBuf = NULL;
		pDSPBuf = NULL;

		dma.buffer = NULL;

		Com_DPrintf( "...releasing DS object\n" );
		pDS->Release();
	}

	if ( hInstDS ) {
		Com_DPrintf( "...freeing DSOUND.DLL\n" );
		FreeLibrary( hInstDS );
		hInstDS = NULL;
	}

	pDS = NULL;
	pDSBuf = NULL;
	pDSPBuf = NULL;
	memset( &dma, 0, sizeof(dma) );
	CoUninitialize();
}


#define DS_INIT_ABORT_ON_ERROR( DSFN, MSG ) \
	if (DSFN != DS_OK) { Com_Printf( "DirectSound: " MSG " FAILED" ); SNDDMA_Shutdown(); return qfalse; }

static qbool SNDDMA_InitDS()
{
	HRESULT hresult;

	if (SUCCEEDED( hresult = CoCreateInstance( CLSID_DirectSound8, NULL, CLSCTX_INPROC_SERVER, IID_IDirectSound8, (void **)&pDS))) {
		Com_DPrintf( "Using DS8\n" );
	}
	else if (SUCCEEDED( hresult = CoCreateInstance( CLSID_DirectSound, NULL, CLSCTX_INPROC_SERVER, IID_IDirectSound, (void **)&pDS))) {
		Com_DPrintf( "Using Legacy DS\n" );
	}
	else {
		DS_INIT_ABORT_ON_ERROR( !DS_OK, "Create" );
	}

	DS_INIT_ABORT_ON_ERROR( pDS->Initialize(NULL), "Init" );
	DS_INIT_ABORT_ON_ERROR( pDS->SetCooperativeLevel( g_wv.hWnd, DSSCL_PRIORITY ), "SetCL" );

	// create the secondary buffer we'll actually work with
	dma.channels = 2;
	dma.samplebits = 16;
	dma.speed = 22050;

	WAVEFORMATEX format;
	memset( &format, 0, sizeof(format) );
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = dma.channels;
	format.wBitsPerSample = dma.samplebits;
	format.nSamplesPerSec = dma.speed;
	format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
	format.cbSize = 0;
	format.nAvgBytesPerSec = format.nSamplesPerSec*format.nBlockAlign;

	DSBUFFERDESC dsbuf;
	memset( &dsbuf, 0, sizeof(dsbuf) );
	dsbuf.dwSize = sizeof(DSBUFFERDESC);
	dsbuf.dwBufferBytes = SECONDARY_BUFFER_SIZE;
	dsbuf.lpwfxFormat = &format;

	dsbuf.dwFlags = DSBCAPS_LOCHARDWARE | DSBCAPS_GETCURRENTPOSITION2;
	if (DS_OK != pDS->CreateSoundBuffer( &dsbuf, &pDSBuf, NULL )) {
		dsbuf.dwFlags = DSBCAPS_LOCSOFTWARE | DSBCAPS_GETCURRENTPOSITION2;
		DS_INIT_ABORT_ON_ERROR( pDS->CreateSoundBuffer( &dsbuf, &pDSBuf, NULL ), "SW" );
		Com_Printf( "DirectSound: Using SW Buffers" );
	}

	DS_INIT_ABORT_ON_ERROR( pDSBuf->Play( 0, 0, DSBPLAY_LOOPING ), "Play" );

	DSBCAPS dsbcaps;
	memset( &dsbcaps, 0, sizeof(dsbcaps) );
	dsbcaps.dwSize = sizeof(dsbcaps);
	DS_INIT_ABORT_ON_ERROR( pDSBuf->GetCaps(&dsbcaps), "GetCaps" );

	gSndBufSize = dsbcaps.dwBufferBytes;

	dma.channels = format.nChannels;
	dma.samplebits = format.wBitsPerSample;
	dma.speed = format.nSamplesPerSec;
	dma.samples = gSndBufSize/(dma.samplebits/8);
	dma.submission_chunk = 1;
	dma.buffer = NULL;			// must be locked first

	sample16 = (dma.samplebits/8) - 1;

	SNDDMA_BeginPainting();
	if (dma.buffer)
		memset( dma.buffer, 0, dma.samples * dma.samplebits/8 );
	SNDDMA_Submit();

	return qtrue;
}

#undef DS_INIT_ABORT_ON_ERROR


qbool SNDDMA_Init()
{
	memset( &dma, 0, sizeof(dma) );

	CoInitialize(NULL);

	if (!SNDDMA_InitDS()) {
		assert(!pDSBuf);
		return qfalse;
	}

	assert(pDSBuf);
	return qtrue;
}


/*
return the current sample position (in mono samples read)
inside the recirculating dma buffer, so the mixing code will know
how many sample are required to fill it up.
*/
int SNDDMA_GetDMAPos()
{
	MMTIME	mmtime;
	DWORD	dwWrite;

	mmtime.wType = TIME_SAMPLES;
	pDSBuf->GetCurrentPosition( &mmtime.u.sample, &dwWrite );

	int s = mmtime.u.sample >> sample16;
	return (s & (dma.samples-1));
}


// makes sure dma.buffer is valid

void SNDDMA_BeginPainting()
{
	int		reps;
	DWORD	dwSize2;
	DWORD	*pbuf, *pbuf2;
	HRESULT	hresult;
	DWORD	dwStatus;

	if ( !pDSBuf )
		return;

	// if the buffer was lost or stopped, restore it and/or restart it
	pDSBuf->GetStatus(&dwStatus);

	if (dwStatus & DSBSTATUS_BUFFERLOST)
		pDSBuf->Restore();

	if (!(dwStatus & DSBSTATUS_PLAYING))
		pDSBuf->Play( 0, 0, DSBPLAY_LOOPING );

	// lock the dsound buffer

	reps = 0;
	dma.buffer = NULL;

	while ((hresult = pDSBuf->Lock( 0, gSndBufSize, (LPVOID*)&pbuf, &locksize, (LPVOID*)&pbuf2, &dwSize2, 0 )) != DS_OK)
	{
		if (hresult != DSERR_BUFFERLOST)
		{
			Com_Printf( "SNDDMA_BeginPainting: Lock failed with error '%s'\n", DSoundError( hresult ) );
			S_Shutdown();
			return;
		}
		else
		{
			pDSBuf->Restore();
		}

		if (++reps > 2)
			return;
	}
	dma.buffer = (unsigned char *)pbuf;
}


// send sound to the device if our buffer isn't really the dma buffer
// for DS, that just means signalling it with an unlock

void SNDDMA_Submit()
{
	if ( pDSBuf ) {
		pDSBuf->Unlock( dma.buffer, locksize, NULL, 0 );
	}
}


// reset the DS coop level if we lost focus but have now regained it

void SNDDMA_Activate()
{
	if ( !pDS )
		return;

	if ( DS_OK != pDS->SetCooperativeLevel( g_wv.hWnd, DSSCL_PRIORITY ) ) {
		Com_Printf( "DirectSound SetCL FAILED\n" );
		SNDDMA_Shutdown();
	}
}

