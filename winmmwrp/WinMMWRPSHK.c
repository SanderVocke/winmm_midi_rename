#include <Windows.h>
#include <string.h>
#include <stdio.h>
#include <locale.h>
#include "mmddk.h"
#include <time.h>

#error unused file

#define KDMAPI_UDID			0		// KDMAPI default uDeviceID
#define KDMAPI_NOTLOADED	0		// KDMAPI is not loaded
#define KDMAPI_LOADED		4		// KDMAPI is loaded

#define LONGMSG_MAXSIZE		65535	// Maximum size for a long message/MIDIHDR

#define LOCK_VM_IN_WORKING_SET 1
#define LOCK_VM_IN_RAM 2

// We don't need MMDDK lel
#define MODM_GETVOLUME		10
#define MODM_SETVOLUME		11

#pragma once

// Required KDMAPI version
#define REQ_MAJOR	1
#define REQ_MINOR	30
#define REQ_BUILD	0
#define REQ_REV		51

// OM funcs
static BOOL IsCallbackWindow = FALSE;																					// WMMC

// Dummy pointer, used for KDMAPI Output
static HMIDIOUT OMDummy1 = 0x10001;	
static HMIDIOUT OMDummy2 = 0x10002;	
static HMIDIOUT OMDummy3 = 0x10003;	
static HMIDIOUT OMDummy4 = 0x10004;

static MMRESULT(WINAPI* mM)(UINT uDeviceID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dwParam1, DWORD_PTR dwParam2) = 0;	// modMessage

typedef unsigned __int64 QWORD;
typedef long NTSTATUS;

#ifdef _DAWRELEASE
// WinMM funcs, just replace MM with "midiOut" to get the real version
static UINT(WINAPI* MMOutGetNumDevs)(void) = 0;
static MMRESULT(WINAPI* MMOutGetDevCapsW)(UINT_PTR uDeviceID, LPMIDIOUTCAPSW lpCaps, UINT uSize) = 0;
static MMRESULT(WINAPI* MMOutOpen)(LPHMIDIOUT lphMidiOut, UINT uDeviceID, DWORD_PTR dwCallback, DWORD_PTR dwCallbackInstance, DWORD dwFlags) = 0;
static MMRESULT(WINAPI* MMOutClose)(HMIDIOUT hMidiOut) = 0;
static MMRESULT(WINAPI* MMOutReset)(HMIDIOUT hMidiOut) = 0;
static MMRESULT(WINAPI* MMOutShortMsg)(HMIDIOUT hMidiOut, DWORD dwMsg) = 0;
static MMRESULT(WINAPI* MMOutPrepareHeader)(HMIDIOUT hMidiOut, MIDIHDR* lpMidiOutHdr, UINT uSize) = 0;
static MMRESULT(WINAPI* MMOutUnprepareHeader)(HMIDIOUT hMidiOut, MIDIHDR* lpMidiOutHdr, UINT uSize) = 0;
static MMRESULT(WINAPI* MMOutLongMsg)(HMIDIOUT hMidiOut, MIDIHDR* lpMidiOutHdr, UINT uSize) = 0;
static MMRESULT(WINAPI* MMOutCachePatches)(HMIDIOUT hMidiOut, UINT wPatch, WORD* lpPatchArray, UINT wFlags) = 0;
static MMRESULT(WINAPI* MMOutCacheDrumPatches)(HMIDIOUT hMidiOut, UINT wPatch, WORD* lpKeyArray, UINT wFlags) = 0;
static MMRESULT(WINAPI* MMOutMessage)(HMIDIOUT hMidiOut, UINT uMsg, DWORD_PTR dw1, DWORD_PTR dw2) = 0;
static MMRESULT(WINAPI* MMOutSetVolume)(HMIDIOUT hMidiOut, DWORD dwVolume) = 0;
static MMRESULT(WINAPI* MMOutGetVolume)(HMIDIOUT hMidiOut, LPDWORD lpdwVolume) = 0;
static MMRESULT(WINAPI* MMOutGetID)(HMIDIOUT hMidiOut, LPUINT puDeviceID) = 0;
#endif

// Callback, used for old apps that require one
static DWORD_PTR WMMCI;
static void(CALLBACK* WMMC)(HMIDIOUT hmo, DWORD wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) = 0;

static DWORD OMDI = KDMAPI_NOTLOADED;

// WinNT Kernel funcs
DWORD(WINAPI* TGT)() = 0;																		// timeGetTime
HMODULE NTDLL = NULL;
ULONGLONG TickStart = 0;																		// For TGT64
NTSTATUS(NTAPI* NQST)(QWORD*) = 0;																// NtQuerySystemTime

// Stock WinMM funcs
#include "WinMM.h"

BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID fImpLoad) {
	if (fdwReason == DLL_PROCESS_ATTACH) {
		if (InitializeWinMM()) {
			return TRUE;
		}
	}
	return TRUE;
}


UINT WINAPI OVERRIDE_midiOutGetNumDevs(void) {
	auto rval = MMOutGetNumDevs();
	printf("RPSHKWrapper: midiOutGetNumDevs -> %d", rval);
	return rval;
}

MMRESULT WINAPI KDMAPI_midiOutGetDevCapsW(UINT_PTR uDeviceID, LPMIDIOUTCAPSW lpCaps, UINT uSize) {
	MIDIOUTCAPSW myCapsW;
	DWORD CapsSupport = MIDICAPS_VOLUME;

	wchar_t SynthName[MAXPNAMELEN];
	WORD VID = 0xFFFF;
	WORD PID = 0x000A;
	DWORD DM = MOD_SWSYNTH;

	// Read key
	HKEY hKey;
	long lResult;
	DWORD dwType;
	DWORD dwSize = sizeof(DWORD);
	DWORD dwSizeW = sizeof(SynthName);

	// Return the output device, but Unicode (UTF-8)
	if (lpCaps == NULL) return MMSYSERR_INVALPARAM;

#ifdef _DAWRELEASE
	switch (uDeviceID) {
	case MIDI_MAPPER:
		return MMOutGetDevCapsW(uDeviceID, lpCaps, uSize);

	case KDMAPI_UDID:
		memset(SynthName, 0, MAXPNAMELEN);
		swprintf_s(SynthName, MAXPNAMELEN, L"Shakra Alpha D%d (DAW)\0", uDeviceID);
		DM = MOD_WAVETABLE;

		// ChecOM done, assign device type
		DM = SNT[ST];

		// Assign values
		myCapsW.wMid = VID;
		myCapsW.wPid = PID;
		memcpy(myCapsW.szPname, SynthName, sizeof(SynthName));
		myCapsW.wVoices = 0xFFFF;
		myCapsW.wNotes = 0x0000;
		myCapsW.wTechnology = DM;
		myCapsW.wChannelMask = 0xFFFF;
		myCapsW.dwSupport = MIDICAPS_VOLUME;

		// Copy values to pointer, and return 0
		memcpy(lpCaps, &myCapsW, min(uSize, sizeof(myCapsW)));

		return MMSYSERR_NOERROR;

	default:
		return MMOutGetDevCapsW(uDeviceID - 1, lpCaps, uSize);
	}
#else
	if (uDeviceID > 3) return MMSYSERR_BADDEVICEID;

	memset(SynthName, 0, MAXPNAMELEN);
	swprintf_s(SynthName, MAXPNAMELEN, L"Shakra Alpha D%d (MM)\0", uDeviceID);
	DM = MOD_WAVETABLE;

	// Assign values
	myCapsW.wMid = VID;
	myCapsW.wPid = PID;
	memcpy(myCapsW.szPname, SynthName, sizeof(SynthName));
	myCapsW.wVoices = 0xFFFF;
	myCapsW.wNotes = 0x0000;
	myCapsW.wTechnology = DM;
	myCapsW.wChannelMask = 0xFFFF;
	myCapsW.dwSupport = MIDICAPS_VOLUME;

	// Copy values to pointer, and return 0
	memcpy(lpCaps, &myCapsW, min(uSize, sizeof(myCapsW)));

	return MMSYSERR_NOERROR;
#endif
}