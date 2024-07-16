#include <Windows.h>

#include <cstdio>
#include <type_traits>
#include <optional>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <io.h>
#include <regex>

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
	Direction direction;
	size_t man_id;
	size_t prod_id;
	size_t driver_version;
	std::string name;
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
	return midi_dev_caps{
		.direction = CapsDirection<dev_caps_struct>(),
		.man_id = v.wMid,
		.prod_id = v.wPid,
		.driver_version = v.vDriverVersion,
		.name = chars_to_str(v.szPname)
	};
}

struct replace_rule {
	std::optional<Direction> maybe_match_direction;
	std::optional<std::regex> maybe_match_name;
	std::optional<size_t> maybe_match_man_id;
	std::optional<size_t> maybe_match_prod_id;
	std::optional<size_t> maybe_match_driver_version;

	std::optional <std::string> maybe_replace_name;
	std::optional <size_t> maybe_replace_man_id;
	std::optional <size_t> maybe_replace_prod_id;
	std::optional <size_t> maybe_replace_driver_version;

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
inline void wrapper_log(Args... args) {
	if (g_maybe_wrapper_log_file) {
		fprintf(g_maybe_wrapper_log_file, args...);
	}
}

template<typename dev_caps_struct>
std::string stringify_caps(dev_caps_struct const& s) {
	return std::string("{\n") +
		"  name: " + chars_to_str((dev_caps_char_type<dev_caps_struct> *)s.szPname) + ",\n" +
		"  man id: " + std::to_string(s.wMid) + ",\n" +
		"  prod id: " + std::to_string(s.wPid) + ",\n" +
		"  driver version: " + std::to_string(s.vDriverVersion) + "\n" +
		std::string("}");
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
	std::ostream &log) {
	try {
		log << "Loading config from " << filename << "\n";

		std::string abspath;
		auto config_content = read_whole_file(filename, &abspath);
		out_config_abspath = abspath;
		log << "Config from " << abspath << ": " << config_content << "\n";
		json data = json::parse(config_content);

		if (data.contains("log")) { out_log_filename = data["log"].template get <std::string>(); }
		if (data.contains("popup")) { out_debug_popup = data["popup"].template get<bool>(); }
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

					if (!rval.maybe_replace_name.has_value() &&
						!rval.maybe_replace_driver_version.has_value() &&
						!rval.maybe_replace_man_id.has_value() &&
						!rval.maybe_replace_prod_id.has_value()) {
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
	std::optional<std::string> maybe_logfilename, maybe_configabspath;
	std::ostringstream config_log;

	try {
		// Config file
		char default_file_path[MAX_PATH];
		if ((maybe_env = getenv("MIDI_REPLACE_CONFIGFILE")) != NULL) {
			try_config_file = std::string(maybe_env);
		}
		if (try_config_file.length() > 0) {
			success = success && load_config(try_config_file, maybe_logfilename, maybe_configabspath, debug_popup, config_log);
		}

		// Log filename override
		if ((maybe_env = getenv("MIDI_REPLACE_LOGFILE")) != NULL) {
			std::string value {maybe_env};
			wrapper_log("Log file from config overridden by MIDI_REPLACE_LOGFILE env var:\n  before: %s\n  after: %s\n",
			            maybe_logfilename.value_or(std::string("none")).c_str(), value);
			maybe_logfilename = value;
		}

		// Open the logfile for writing
		if (maybe_logfilename.has_value()) {
			g_maybe_wrapper_log_file = fopen(maybe_logfilename.value().c_str(), "w");
			if (!g_maybe_wrapper_log_file) {
				wrapper_log("Error: Unable to open log file!\n");
			}

			// Write our log msgs from loading the config
			wrapper_log("%s", config_log.str().c_str());
		}

		wrapper_log("Starting MIDI replace with %d rules.\n", g_replace_rules.size());
	}
	catch (std::exception &e) {
		wrapper_log("Failed to start MIDI replace: %s\n", e.what());
		success = false;
	}
	catch (...) {
		wrapper_log("Failed to start MIDI replace: unknown exception\n");
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
		msg += "To disable this popup, set \"popup\" to false in the config.\n";

		msg += config_log.str();

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
	wrapper_log("\nRequest for output device capabilities: %s\n", stringify_caps(*pmoc).c_str());
	for (auto const& rule : g_replace_rules) {
		if (rule.apply_in_place_c(*pmoc)) {
			wrapper_log("--> Matched a replace rule. Returning: %s\n", stringify_caps(*pmoc).c_str());
		}
	}
	return rval;
}

MMRESULT WINAPI OVERRIDE_midiOutGetDevCapsW(UINT_PTR deviceId, LPMIDIOUTCAPSW pmoc, UINT cpmoc) {
	MMRESULT rval = MMmidiOutGetDevCapsW(deviceId, pmoc, cpmoc);
	wrapper_log("\nRequest for output device capabilities: %s\n", stringify_caps(*pmoc).c_str());
	for (auto const& rule : g_replace_rules) {
		if (rule.apply_in_place_c(*pmoc)) {
			wrapper_log("--> Matched a replace rule. Returning: %s\n", stringify_caps(*pmoc).c_str());
		}
	}
	return rval;
}

MMRESULT WINAPI OVERRIDE_midiInGetDevCapsA(UINT_PTR deviceId, LPMIDIINCAPSA pmoc, UINT cpmoc) {
	MMRESULT rval = MMmidiInGetDevCapsA(deviceId, pmoc, cpmoc);
	wrapper_log("\nRequest for input device capabilities: %s\n", stringify_caps(*pmoc).c_str());
	for (auto const& rule : g_replace_rules) {
		if (rule.apply_in_place_c(*pmoc)) {
			wrapper_log("--> Matched a replace rule. Returning: %s\n", stringify_caps(*pmoc).c_str());
		}
	}
	return rval;
}

MMRESULT WINAPI OVERRIDE_midiInGetDevCapsW(UINT_PTR deviceId, LPMIDIINCAPSW pmoc, UINT cpmoc) {
	MMRESULT rval = MMmidiInGetDevCapsW(deviceId, pmoc, cpmoc);
	wrapper_log("\nRequest for input device capabilities: %s\n", stringify_caps(*pmoc).c_str());
	for (auto const& rule : g_replace_rules) {
		if (rule.apply_in_place_c(*pmoc)) {
			wrapper_log("--> Matched a replace rule. Returning: %s\n", stringify_caps(*pmoc).c_str());
		}
	}
	return rval;
}