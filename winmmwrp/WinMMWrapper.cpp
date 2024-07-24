#include <Windows.h>

#include <cstdio>
#include <type_traits>
#include <optional>
#include <string>
#include <cstring>
#include <vector>
#include <iostream>
#include <fstream>
#include <io.h>
#include <regex>
#include <mmddk.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

// Stock WinMM funcs
extern "C" {
#include "WinMM.h"
}

enum class Direction {
	Input,
	Output
};

template<typename dev_caps_struct>
consteval Direction CapsDirection() {
	return (std::is_same<dev_caps_struct, MIDIINCAPSW>::value || std::is_same<dev_caps_struct, MIDIINCAPSA>::value) ?
		Direction::Input : Direction::Output;
}

struct midi_dev_caps {
	Direction direction;                                // MIDIINCAPS or MIDIOUTCAPS

	// Common
	size_t man_id;										// wMid
	size_t prod_id;										// wPid
	size_t driver_version;								// vDriverVersion
	std::string name;									// szPname

	// MIDIOUTCAPS only
	std::optional<size_t> technology;					// wTechnology
	std::optional<size_t> voices;						// wVoices
	std::optional<size_t> notes;						// wNotes
	std::optional<size_t> channel_mask;					// wChannelMask
	std::optional<size_t> support; 					    // dwSupport
};

template<typename dev_caps_struct>
using dev_caps_char_type = typename std::remove_all_extents<decltype(dev_caps_struct::szPname)>::type;

template<typename char_t>
std::string chars_to_str(char_t* c) {
	if (std::is_same<char_t, WCHAR>::value) {
		std::wstring w((WCHAR*)c);
		return std::string(w.begin(), w.end());
	}
	return std::string((CHAR*)c);
}

template<typename dev_caps_struct>
midi_dev_caps to_our_dev_caps(dev_caps_struct v) {
	auto constexpr direction = CapsDirection<dev_caps_struct>();
	auto rval = midi_dev_caps{
		.direction = direction,
		.man_id = v.wMid,
		.prod_id = v.wPid,
		.driver_version = v.vDriverVersion,
		.name = chars_to_str(v.szPname)
	};

	if constexpr (direction == Direction::Output) {
		rval.technology = v.wTechnology;
		rval.voices = v.wVoices;
		rval.notes = v.wNotes;
		rval.channel_mask = v.wChannelMask;
		rval.support = v.dwSupport;
	}

	return rval;
}

struct replace_rule {
	// Matching only on common properties
	std::optional<Direction> maybe_match_direction;
	std::optional<std::regex> maybe_match_name;
	std::optional<size_t> maybe_match_man_id;
	std::optional<size_t> maybe_match_prod_id;
	std::optional<size_t> maybe_match_driver_version;

	// Replacing common properties
	std::optional <std::string> maybe_replace_name;
	std::optional <size_t> maybe_replace_man_id;
	std::optional <size_t> maybe_replace_prod_id;
	std::optional <size_t> maybe_replace_driver_version;

	// Replacing output device properties
	std::optional<size_t> maybe_replace_technology;
	std::optional<size_t> maybe_replace_voices;
	std::optional<size_t> maybe_replace_notes;
	std::optional<size_t> maybe_replace_channel_mask;
	std::optional<size_t> maybe_replace_support;

	// Replacing device interface name
	std::optional<std::wstring>  maybe_replace_interface_name;

	bool is_match(midi_dev_caps const& m) const {
		bool rval = true;
		std::smatch rmatch;
		if (maybe_match_direction.has_value()) { rval = rval && (maybe_match_direction.value() == m.direction); }
		if (maybe_match_name.has_value()) { rval = rval && std::regex_match(m.name, rmatch, maybe_match_name.value()); }
		if (maybe_match_man_id.has_value()) { rval = rval && (maybe_match_man_id.value() == m.man_id); }
		if (maybe_match_prod_id.has_value()) { rval = rval && (maybe_match_prod_id.value() == m.prod_id); }
		if (maybe_match_driver_version.has_value()) { rval = rval && (maybe_match_driver_version.value() == m.driver_version); }
		return rval;
	}

	bool apply_in_place(midi_dev_caps& m) const {
		bool match = is_match(m);
		if (match) {
			if (maybe_replace_name.has_value()) { m.name = maybe_replace_name.value(); }
			if (maybe_replace_man_id.has_value()) { m.man_id = maybe_replace_man_id.value(); }
			if (maybe_replace_prod_id.has_value()) { m.prod_id = maybe_replace_prod_id.value(); }
			if (maybe_replace_driver_version.has_value()) { m.driver_version = maybe_replace_driver_version.value(); }
			if (maybe_replace_technology.has_value()) { m.technology = maybe_replace_technology.value(); }
			if (maybe_replace_voices.has_value()) { m.voices = maybe_replace_voices.value(); }
			if (maybe_replace_notes.has_value()) { m.notes = maybe_replace_notes.value(); }
			if (maybe_replace_channel_mask.has_value()) { m.channel_mask = maybe_replace_channel_mask.value(); }
			if (maybe_replace_support.has_value()) { m.support = maybe_replace_support.value(); }
		}
		return match;
	}

	template<typename dev_caps_struct>
	bool apply_in_place_c(dev_caps_struct& s) const {
		auto ours = to_our_dev_caps(s);
		bool matched = apply_in_place(ours);
		if (matched) {
			s.wMid = ours.man_id;
			s.wPid = ours.prod_id;
			s.vDriverVersion = ours.driver_version;
			if (std::is_same<dev_caps_char_type<dev_caps_struct>, WCHAR>::value) {
				wcscpy((WCHAR*)s.szPname, std::wstring(ours.name.begin(), ours.name.end()).c_str());
			} else {
				strcpy((CHAR*)s.szPname, ours.name.c_str());
			}

			if constexpr (CapsDirection<dev_caps_struct>() == Direction::Output) {
				s.wTechnology = ours.technology.value();
				s.wVoices = ours.voices.value();
				s.wNotes = ours.notes.value();
				s.wChannelMask = ours.channel_mask.value();
				s.dwSupport = ours.support.value();
			}
		}
		return matched;
	}
};

// To illustrate and check
static_assert(std::is_same<WCHAR, dev_caps_char_type<MIDIINCAPSW>>::value, "error");
static_assert(std::is_same<CHAR, dev_caps_char_type<MIDIINCAPSA>>::value, "error");
static_assert(std::is_same<WCHAR, dev_caps_char_type<MIDIOUTCAPSW>>::value, "error");
static_assert(std::is_same<CHAR, dev_caps_char_type<MIDIOUTCAPSA>>::value, "error");

std::vector<replace_rule> g_replace_rules;
FILE* g_maybe_wrapper_log_file = NULL;

template<typename ...Args>
inline void wrapper_log(std::ostringstream* maybe_os, Args... args) {
	std::vector<char> logbuf(1024);

	if (g_maybe_wrapper_log_file) {
		fprintf(g_maybe_wrapper_log_file, args...);
	}
	if (maybe_os) {
		auto n_needed = snprintf(logbuf.data(), 0, args...);
		if (n_needed >= logbuf.size()) { logbuf.resize(n_needed+1); }
		snprintf(logbuf.data(), logbuf.size(), args...);
		(*maybe_os) << logbuf.data();
	}
}

template<typename dev_caps_struct>
std::string stringify_common_caps(dev_caps_struct const& s) {
	return
		"  name: " + chars_to_str((dev_caps_char_type<dev_caps_struct> *)s.szPname) + "\n" +
		"  man id: " + std::to_string(s.wMid) + "\n" +
		"  prod id: " + std::to_string(s.wPid) + "\n" +
		"  driver version: " + std::to_string(s.vDriverVersion) + "\n";
}

template<typename out_dev_caps_struct>
std::string stringify_output_caps(out_dev_caps_struct const& s) {
	return stringify_common_caps(s) +
	       "  technology: " + std::to_string(s.wTechnology) + "\n" +
		   "  voices: " + std::to_string(s.wVoices) + "\n" +
	       "  notes: " + std::to_string(s.wNotes) + "\n" +
	       "  channel mask: " + std::to_string(s.wChannelMask) + "\n" +
	       "  support: " + std::to_string(s.dwSupport) + "\n";
}

template<typename in_dev_caps_struct>
std::string stringify_input_caps(in_dev_caps_struct const& s) {
	return stringify_common_caps(s);
}

template<typename dev_caps_struct>
std::string stringify_caps(dev_caps_struct const& s) {
	constexpr bool is_out = CapsDirection<dev_caps_struct>() == Direction::Output;
	if constexpr (is_out) {
		return stringify_output_caps(s);
	} else {
		return stringify_input_caps(s);
	}
}

std::string abs_path_of(FILE* file) {
	char file_name_info[MAX_PATH + sizeof(DWORD)];
	if (GetFileInformationByHandleEx((HANDLE)_get_osfhandle(fileno(file)),
		FileNameInfo,
		(void*)file_name_info,
		sizeof(file_name_info))) {
		auto _info = (FILE_NAME_INFO*)file_name_info;
		std::wstring name(_info->FileName);
		return std::string(name.begin(), name.end());
	}
	throw std::runtime_error("Unable to get filename from descriptor");
}

std::string read_whole_file(std::string filename, std::string* maybe_abs_path_out) {
	FILE* f = fopen(filename.c_str(), "rb");
	if (!f) {
		throw std::runtime_error("Unable to open for reading: " + filename);
	}
	if (maybe_abs_path_out) {
		*maybe_abs_path_out = abs_path_of(f);
	}
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);  /* same as rewind(f); */

	char* string = (char*)malloc(fsize + 1);
	if (!string) {
		throw std::runtime_error("Unable to alloc");
	}
	fread(string, fsize, 1, f);
	fclose(f);

	string[fsize] = 0;
	return std::string(string);
}


bool load_config(
	std::string filename,
	std::optional<std::string> &out_log_filename,
	std::optional<std::string> &out_config_abspath,
	bool &out_debug_popup,
	bool &out_debug_popup_verbose,
	std::ostream &log) {
	try {
		log << "Loading config from " << filename << "\n";

		std::string abspath;
		auto config_content = read_whole_file(filename, &abspath);
		out_config_abspath = abspath;
		json data = json::parse(config_content);
		log << "Parsed config: " << data.dump() << "\n";

		if (data.contains("log")) { out_log_filename = data["log"].template get <std::string>(); log << "LOG " << out_log_filename.value_or("no") << std::endl; }
		if (data.contains("popup")) { out_debug_popup = data["popup"].template get<bool>(); }
		if (data.contains("popup_verbose")) { out_debug_popup_verbose = data["popup_verbose"].template get <bool>(); }
		if (data.contains("rules")) {
			auto& rules = data["rules"];
			for (auto& rule : rules) {
				try {
					replace_rule rval;
					if (rule.contains("match_name")) { rval.maybe_match_name = rule["match_name"].template get<std::string>(); }
					if (rule.contains("match_man_id")) { rval.maybe_match_man_id = rule["match_man_id"].template get<size_t>(); }
					if (rule.contains("match_prod_id")) { rval.maybe_match_prod_id = rule["match_prod_id"].template get<size_t>(); }
					if (rule.contains("match_driver_version")) { rval.maybe_match_driver_version = rule["match_driver_version"].template get<size_t>(); }
					if (rule.contains("match_direction")) {
						auto text = rule["match_direction"].template get<std::string>();
						if (text == "in") { rval.maybe_match_direction = Direction::Input; }
						else if (text == "out") { rval.maybe_match_direction = Direction::Output; }
						else {
							throw std::runtime_error("Invalid value for match_direction (should be in or out): " + text);
						}
					}
					if (rule.contains("replace_name")) { rval.maybe_replace_name = rule["replace_name"].template get<std::string>(); }
					if (rule.contains("replace_man_id")) { rval.maybe_replace_man_id = rule["replace_man_id"].template get<size_t>(); }
					if (rule.contains("replace_prod_id")) { rval.maybe_replace_prod_id = rule["replace_prod_id"].template get<size_t>(); }
					if (rule.contains("replace_driver_version")) { rval.maybe_replace_driver_version = rule["replace_driver_version"].template get<size_t>(); }
					if (rule.contains("replace_technology")) { rval.maybe_replace_technology = rule["replace_technology"].template get<size_t>(); }
					if (rule.contains("replace_voices")) { rval.maybe_replace_voices = rule["replace_voices"].template get<size_t>(); }
					if (rule.contains("replace_notes")) { rval.maybe_replace_notes = rule["replace_notes"].template get<size_t>(); }
					if (rule.contains("replace_channel_mask")) { rval.maybe_replace_channel_mask = rule["replace_channel_mask"].template get<size_t>(); }
					if (rule.contains("replace_support")) { rval.maybe_replace_support = rule["replace_support"].template get<size_t>(); }
					if (rule.contains("replace_interface_name")) { rval.maybe_replace_interface_name = rule["replace_interface_name"].template get<std::wstring>(); }

					if (!rval.maybe_replace_name.has_value() &&
						!rval.maybe_replace_driver_version.has_value() &&
						!rval.maybe_replace_man_id.has_value() &&
						!rval.maybe_replace_prod_id.has_value() &&
						!rval.maybe_replace_technology.has_value() &&
						!rval.maybe_replace_voices.has_value() &&
						!rval.maybe_replace_notes.has_value() &&
						!rval.maybe_replace_channel_mask.has_value() &&
						!rval.maybe_replace_support.has_value()) {
						throw std::runtime_error("No replace items set for rule, would not affect anything.");
					}

					g_replace_rules.push_back(rval);
				}
				catch (std::exception& e) {
					log << "Skipping rule:\n" << e.what() << "\n";
				}
				catch (...) {
					log << "Skipping rule (unknown exception)\n";
				}
			}
		}
	}
	catch (std::exception& e) {
		log << "Unable to load config from " << filename << ".Continuing without replace rules.Exception:\n" << e.what() << "\n";
		return false;
	}
	catch (...) {
		log << "Unable to load config from " << filename << ".Continuing without replace rules. (unknown exception)\n";
		return false;
	}
	return true;
}

std::string last_error_string()
{
	//Get the error message ID, if any.
	DWORD errorMessageID = ::GetLastError();
	if (errorMessageID == 0) {
		return std::string(); //No error message has been recorded
	}

	LPSTR messageBuffer = nullptr;

	//Ask Win32 to give us the string version of that message ID.
	//The parameters we pass in, tell Win32 to create the buffer that holds the message for us (because we don't yet know how long the message string will be).
	size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

	//Copy the error message into a std::string.
	std::string message(messageBuffer, size);

	//Free the Win32's string's buffer.
	LocalFree(messageBuffer);

	return message;
}

void configure() {
	char* maybe_env;
	std::string try_config_file = "midi_rename_config.json";
	bool success = true;
	bool debug_popup = true;
	bool debug_popup_verbose = false;
	std::optional<std::string> maybe_logfilename, maybe_configabspath;
	std::ostringstream config_log;
	std::ostringstream pre_popup_log;

	try {
		// Config file
		char default_file_path[MAX_PATH];
		if ((maybe_env = getenv("MIDI_REPLACE_CONFIGFILE")) != NULL) {
			try_config_file = std::string(maybe_env);
		}
		if (try_config_file.length() > 0) {
			success = success && load_config(try_config_file, maybe_logfilename, maybe_configabspath, debug_popup, debug_popup_verbose, config_log);
		}

		// Log filename override
		if ((maybe_env = getenv("MIDI_REPLACE_LOGFILE")) != NULL) {
			std::string value {maybe_env};
			wrapper_log(&pre_popup_log, "Log file from config overridden by MIDI_REPLACE_LOGFILE env var:\n  before: %s\n  after: %s\n",
			            maybe_logfilename.value_or(std::string("none")).c_str(), value);
			maybe_logfilename = value;
		}

		// Open the logfile for writing
		if (maybe_logfilename.has_value()) {
		    wrapper_log(&pre_popup_log, "Opening log file: %s\n", maybe_logfilename.value().c_str());
			g_maybe_wrapper_log_file = fopen(maybe_logfilename.value().c_str(), "w");
			if (!g_maybe_wrapper_log_file) {
				wrapper_log(&pre_popup_log, "Error: Unable to open log file (%s)!\n", strerror(errno));
			}

			// Write our log msgs from loading the config
			wrapper_log(&pre_popup_log, "%s", config_log.str().c_str());
		}

		wrapper_log(&pre_popup_log, "Starting MIDI replace with %d replace rules.\n", g_replace_rules.size());
	}
	catch (std::exception &e) {
		wrapper_log(&pre_popup_log, "Failed to start MIDI replace: %s\n", e.what());
		success = false;
	}
	catch (...) {
		wrapper_log(&pre_popup_log, "Failed to start MIDI replace: unknown exception\n");
		success = false;
	}

	if (debug_popup) {
		std::string msg = success ?
			"MIDI device renamer started successfully.\n" :
			"MIDI device renamer failed to initialize.\n";

		if (!success) {
			msg += config_log.str() + "\n";
		}

		if (g_maybe_wrapper_log_file) {
			msg += "Logging to: " + abs_path_of(g_maybe_wrapper_log_file) + "\n";
		}
		else {
			msg += "No log file specified.\n";
		}

		msg += "Config search path: " + try_config_file + "\n";
		if (maybe_configabspath.has_value()) {
			msg += "Config found @: " + maybe_configabspath.value() + "\n";
		}
		else {
			msg += "Config not found!\n";
		}
		msg += "# of rules loaded: " + std::to_string(g_replace_rules.size()) + "\n";

		if (debug_popup_verbose) {
			msg += "Detailed log (desable by setting \"popup_verbose\" to false in the config):\n";
			msg += pre_popup_log.str();
		} else {
			msg += "To include detailed log info up to this point into the popup, set \"popup_verbose\" to true in the config.\n";
		}

		msg += "To disable this popup, set \"popup\" to false in the config.\n";

		MessageBoxA(NULL, msg.c_str(), "Info", 0);
	}
}

BOOL DllMain(HINSTANCE hInstDLL, DWORD fdwReason, LPVOID fImpLoad) {
	switch (fdwReason) {

	case DLL_PROCESS_ATTACH:
	{
		configure();

		if (InitializeWinMM())
			return TRUE;

		return FALSE;
	}

	case DLL_PROCESS_DETACH:
	{
		if (g_maybe_wrapper_log_file) { fclose(g_maybe_wrapper_log_file); }

		return TRUE;
	}

	}

	return TRUE;
}

MMRESULT WINAPI OVERRIDE_midiOutGetDevCapsA(UINT_PTR deviceId, LPMIDIOUTCAPSA pmoc, UINT cpmoc) {
	MMRESULT rval = MMmidiOutGetDevCapsA(deviceId, pmoc, cpmoc);
	wrapper_log(nullptr, "\nRequest for output device capabilities: %s\n", stringify_caps(*pmoc).c_str());
	for (auto const& rule : g_replace_rules) {
		if (rule.apply_in_place_c(*pmoc)) {
			wrapper_log(nullptr, "--> Matched a replace rule. Returning: %s\n", stringify_caps(*pmoc).c_str());
		}
	}
	return rval;
}

MMRESULT WINAPI OVERRIDE_midiOutGetDevCapsW(UINT_PTR deviceId, LPMIDIOUTCAPSW pmoc, UINT cpmoc) {
	MMRESULT rval = MMmidiOutGetDevCapsW(deviceId, pmoc, cpmoc);
	wrapper_log(nullptr, "\nRequest for output device capabilities: %s\n", stringify_caps(*pmoc).c_str());
	for (auto const& rule : g_replace_rules) {
		if (rule.apply_in_place_c(*pmoc)) {
			wrapper_log(nullptr, "--> Matched a replace rule. Returning: %s\n", stringify_caps(*pmoc).c_str());
		}
	}
	return rval;
}

MMRESULT WINAPI OVERRIDE_midiInGetDevCapsA(UINT_PTR deviceId, LPMIDIINCAPSA pmoc, UINT cpmoc) {
	MMRESULT rval = MMmidiInGetDevCapsA(deviceId, pmoc, cpmoc);
	wrapper_log(nullptr, "\nRequest for input device capabilities: %s\n", stringify_caps(*pmoc).c_str());
	for (auto const& rule : g_replace_rules) {
		if (rule.apply_in_place_c(*pmoc)) {
			wrapper_log(nullptr, "--> Matched a replace rule. Returning: %s\n", stringify_caps(*pmoc).c_str());
		}
	}
	return rval;
}

MMRESULT WINAPI OVERRIDE_midiInGetDevCapsW(UINT_PTR deviceId, LPMIDIINCAPSW pmoc, UINT cpmoc) {
	MMRESULT rval = MMmidiInGetDevCapsW(deviceId, pmoc, cpmoc);
	wrapper_log(nullptr, "\nRequest for input device capabilities: %s\n", stringify_caps(*pmoc).c_str());
	for (auto const& rule : g_replace_rules) {
		if (rule.apply_in_place_c(*pmoc)) {
			wrapper_log(nullptr, "--> Matched a replace rule. Returning: %s\n", stringify_caps(*pmoc).c_str());
		}
	}
	return rval;
}

std::optional<std::wstring> get_maybe_interface_name_override(Direction devDirection, UINT_PTR deviceId) {
	std::optional<std::wstring> rval;
	if (devDirection == Direction::Input) {
		MIDIINCAPSA pmoc;
		MMmidiInGetDevCapsA(deviceId, &pmoc, 0);
		wrapper_log(nullptr, "--> Transparently queried the device interface with result: %s", pmoc.szPname);
		auto ours = to_our_dev_caps(pmoc);
		for (auto &rule : g_replace_rules) {
			if (rule.is_match(ours)) {
				rval = rule.maybe_replace_interface_name;
				break;
			}
		}
	} else {
		MIDIOUTCAPSA pmoc;
		MMMidiOutGetDevCapsA(deviceId, &pmoc, 0);
		wrapper_log(nullptr, "--> Transparently queried the device interface with result: %s", pmoc.szPname);
		auto ours = to_our_dev_caps(pmoc);
		for (auto &rule : g_replace_rules) {
			if (rule.is_match(ours)) {
				rval = rule.maybe_replace_interface_name;
				break;
			}
		}
	}
	return rval;
}

template<typename HM>
MMRESULT handle_QUERYDEVICEINTERFACESIZE(Direction devDirection, HM hm, DWORD_PTR dw1, DWORD_PTR dw2) {
	ULONG sz;
	MMRESULT rval;
	rval = devDirection == Direction::Input ?
		   MMmidiInMessage((HMIDIIN)hm, DRV_QUERYDEVICEINTERFACESIZE, reinterpret_cast<DWORD_PTR>(&sz), 0) :
		   MMmidiOutMessage((HMIDIOUT)hm, DRV_QUERYDEVICEINTERFACESIZE, reinterpret_cast<DWORD_PTR>(&sz), 0);
	wrapper_log(nullptr, "Queried device interface size for %s. Native result: %d\n",
	                     (devDirection == Direction::Input ? "input" : "output"), sz);
	std::optional<std::wstring> maybe_substitute = get_maybe_interface_name_override(devDirection, (UINT_PTR)hm);
	auto &out_size = *reinterpret_cast<ULONG*>(dw1);
	if (maybe_substitute.has_value()) {
		int new_sz = sizeof(wchar_t) * maybe_substitute.value().size() + 1;
		wrapper_log(nullptr, "--> Matched a replace rule. Returning size %d of: %ls\n", new_sz, maybe_substitute.value().c_str());
		auto *ptr = reinterpret_cast<ULONG*>(dw1);
		out_size = new_sz;
	} else {
		auto *ptr = reinterpret_cast<ULONG*>(dw1);
		out_size = sz;
	}
	return rval;
}

template<typename HM>
MMRESULT handle_QUERYDEVICEINTERFACE(Direction devDirection, HM hm, DWORD_PTR dw1, DWORD_PTR dw2) {
	MMRESULT rval;
	rval = devDirection == Direction::Input ?
		MMmidiInMessage((HMIDIIN)hm, DRV_QUERYDEVICEINTERFACE, dw1, dw2) :
		MMmidiOutMessage((HMIDIOUT)hm, DRV_QUERYDEVICEINTERFACE, dw1, dw2);
	wrapper_log(nullptr, "Queried device interface name for %s. Native result: %ls\n",
	                     (devDirection == Direction::Input ? "input" : "output"), dw1);
	std::optional<std::wstring> maybe_substitute = get_maybe_interface_name_override(devDirection, (UINT_PTR)hm);
	auto &out_size = *reinterpret_cast<ULONG*>(dw1);
	if (maybe_substitute.has_value()) {
		wrapper_log(nullptr, "--> Matched a replace rule. Returning: %ls\n", maybe_substitute.value().c_str());
		wcscpy(reinterpret_cast<wchar_t*>(dw1), maybe_substitute.value().c_str());
	}
	return rval;
}

MMRESULT WINAPI OVERRIDE_WINMM_midiOutMessage(
	_In_opt_ HMIDIOUT hmo,
	_In_ UINT uMsg,
	_In_opt_ DWORD_PTR dw1,
	_In_opt_ DWORD_PTR dw2
) {
	switch (uMsg) {
		case DRV_QUERYDEVICEINTERFACESIZE:
			return handle_QUERYDEVICEINTERFACESIZE<LPMIDIOUTCAPSA, HMIDIOUT>(Direction::Output, hmo, dw1, dw2);
		case DRV_QUERYDEVICEINTERFACE:
			return handle_QUERYDEVICEINTERFACE<LPMIDIOUTCAPSA, HMIDIOUT>(Direction::Output, hmo, dw1, dw2);
		default:
			return MMmidiOutMessage(hmo, uMsg, dw1, dw2);
	};
}

MMRESULT WINAPI OVERRIDE_WINMM_midiInMessage(
	_In_opt_ HMIDIIN hmi,
	_In_ UINT uMsg,
	_In_opt_ DWORD_PTR dw1,
	_In_opt_ DWORD_PTR dw2
) {
	switch (uMsg) {
		case DRV_QUERYDEVICEINTERFACESIZE:
			return handle_QUERYDEVICEINTERFACESIZE<LPMIDIINCAPSA, HMIDIIN>(Direction::Input, hmi, dw1, dw2);
		case DRV_QUERYDEVICEINTERFACE:
			return handle_QUERYDEVICEINTERFACE<LPMIDIINCAPSA, HMIDIIN>(Direction::Input, hmi, dw1, dw2);
		default:
			return MMmidiInMessage(hmi, uMsg, dw1, dw2);
	};
}