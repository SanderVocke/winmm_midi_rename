#include <Windows.h>
#include <string.h>
#include <stdio.h>
#include <locale.h>
#include "mmddk.h"
#include <time.h>

#define KDMAPI_UDID			0		// KDMAPI default uDeviceID
#define KDMAPI_NOTLOADED	0		// KDMAPI is not loaded
#define KDMAPI_LOADED		1		// KDMAPI is loaded

#define LONGMSG_MAXSIZE		65535	// Maximum size for a long message/MIDIHDR

#define NT_SUCCESS(StatCode) ((NTSTATUS)(StatCode) == 0)

typedef unsigned __int64 QWORD;
typedef long NTSTATUS;

// Required KDMAPI version
#define REQ_MAJOR	4
#define REQ_MINOR	1
#define REQ_BUILD	0
#define REQ_REV		5

// KDMAPI version from library
DWORD DrvMajor = 0, DrvMinor = 0, DrvBuild = 0, DrvRevision = 0;

// OM funcs
BOOL OMAlreadyInit = FALSE;																		// To check if OM has already been initialized
HMODULE OM = NULL;																				// OM lib
DWORD_PTR OMUser;																				// Dummy pointer, used for KDMAPI Output
HMIDI OMDummy = 0x10001;																		// Dummy pointer, used for KDMAPI Output
DWORD(WINAPI* TGT)() = 0;																		// TGT

// WinNT Kernel funcs
HMODULE NTDLL = NULL;
DOUBLE SpeedHack = 1.0;																			// TGT speedhack
DWORD TickStart = 0;																			// For TGT
QWORD UTickStart = 0;																			// For TGT64
NTSTATUS(NTAPI* NQST)(QWORD*) = 0;																// NtQuerySystemTime

// Stock WinMM funcs
#include "WinMM.h"

BOOL InitializeOMDirectAPI() {
	//HMODULE NTDLL;

	// Get OM's handle and load the secret sauce functions
	//NTDLL = GetModuleHandleA("ntdll");

	return TRUE;
}

BOOL DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID fImpLoad) {
	switch (fdwReason) {

	case DLL_PROCESS_ATTACH:
	{
		if (InitializeWinMM())
			return TRUE;

		return FALSE;
	}

	case DLL_PROCESS_DETACH:
	{
		if (OWINMM) {
			for (int i = 0; i < sizeof(MMImports) / sizeof(MMImports[0]); i++)
				*(MMImports[i].ptr) = 0;

			FreeLibrary(OWINMM);
		}

		return TRUE;
	}

	}

	return TRUE;
}

UINT WINAPI OVERRIDE_midiOutGetNumDevs(void) {
	auto rval = MMmidiOutGetNumDevs();
	printf("Wrapper: midiOutGetNumDevs -> %d\r\n", rval);
	return rval;
}