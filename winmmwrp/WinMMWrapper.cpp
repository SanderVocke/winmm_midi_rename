#include <Windows.h>
//#include <string.h>
//#include <stdio.h>
//#include <locale.h>
//#include "mmddk.h"
//#include <time.h>

#include <cstdio>

// Stock WinMM funcs
extern "C" {
#include "WinMM.h"
}

FILE* logfile = NULL;

typedef struct {
	char from[32];
	char to[32];
} replacement_t;

typedef struct {
	wchar_t from[32];
	wchar_t to[32];
} wreplacement_t;

#define MAX_REPLACEMENTS 64
replacement_t input_replacements_a[MAX_REPLACEMENTS];
wreplacement_t input_replacements_w[MAX_REPLACEMENTS];
replacement_t output_replacements_a[MAX_REPLACEMENTS];
wreplacement_t output_replacements_w[MAX_REPLACEMENTS];

void parse_env() {
	char* maybe_env;
	if ((maybe_env = getenv("INPUT_MIDI_DEV_REPLACE")) != NULL) {

	}
	if ((maybe_env = getenv("OUTPUT_MIDI_DEV_REPLACE")) != NULL) {

	}
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
		if (logfile) { fclose(logfile); }

		return TRUE;
	}

	}

	return TRUE;
}

FILE* open_log_file() {
	if (!logfile) { logfile = fopen("wrapper_log.txt", "w"); }
	return logfile;
}

MMRESULT WINAPI OVERRIDE_midiOutGetDevCapsA(UINT_PTR deviceId, LPMIDIOUTCAPSA pmoc, UINT cpmoc) {
	MMRESULT rval = MMmidiOutGetDevCapsA(deviceId, pmoc, cpmoc);
	fprintf(open_log_file(), "MIDI output device caps:\n");
	fprintf(open_log_file(), "- Manufacturer: %d, Product: %d\n", pmoc->wMid, pmoc->wPid);
	fprintf(open_log_file(), "- Driver version: %d\n", pmoc->vDriverVersion);
	fprintf(open_log_file(), "- Name: %s\n", pmoc->szPname);
	return rval;
}

MMRESULT WINAPI OVERRIDE_midiOutGetDevCapsW(UINT_PTR deviceId, LPMIDIOUTCAPSW pmoc, UINT cpmoc) {
	MMRESULT rval = MMmidiOutGetDevCapsW(deviceId, pmoc, cpmoc);
	FILE* fp = open_log_file();
	fprintf(open_log_file(), "MIDI output device caps:\n");
	fprintf(open_log_file(), "- Manufacturer: %d, Product: %d\n", pmoc->wMid, pmoc->wPid);
	fprintf(open_log_file(), "- Driver version: %d\n", pmoc->vDriverVersion);
	fprintf(open_log_file(), "- Name: %ls\n", pmoc->szPname);
	return rval;
}

MMRESULT WINAPI OVERRIDE_midiInGetDevCapsA(UINT_PTR deviceId, LPMIDIINCAPSA pmoc, UINT cpmoc) {
	MMRESULT rval = MMmidiInGetDevCapsA(deviceId, pmoc, cpmoc);
	FILE* fp = open_log_file();
	fprintf(open_log_file(), "MIDI input device caps:\n");
	fprintf(open_log_file(), "- Manufacturer: %d, Product: %d\n", pmoc->wMid, pmoc->wPid);
	fprintf(open_log_file(), "- Driver version: %d\n", pmoc->vDriverVersion);
	fprintf(open_log_file(), "- Name: %s\n", pmoc->szPname);
	return rval;
}

MMRESULT WINAPI OVERRIDE_midiInGetDevCapsW(UINT_PTR deviceId, LPMIDIINCAPSW pmoc, UINT cpmoc) {
	MMRESULT rval = MMmidiInGetDevCapsW(deviceId, pmoc, cpmoc);
	FILE* fp = open_log_file();
	fprintf(open_log_file(), "MIDI input device caps:\n");
	fprintf(open_log_file(), "- Manufacturer: %d, Product: %d\n", pmoc->wMid, pmoc->wPid);
	fprintf(open_log_file(), "- Driver version: %d\n", pmoc->vDriverVersion);
	fprintf(open_log_file(), "- Name: %ls\n", pmoc->szPname);
	return rval;
}