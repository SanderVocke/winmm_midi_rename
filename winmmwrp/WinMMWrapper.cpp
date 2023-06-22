#include <Windows.h>
//#include <string.h>
//#include <stdio.h>
//#include <locale.h>
//#include "mmddk.h"
//#include <time.h>

#include <cstdio>
#include <type_traits>
#include <optional>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>

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
	std::optional<std::string> maybe_match_name;
	std::optional<size_t> maybe_match_man_id;
	std::optional<size_t> maybe_match_prod_id;
	std::optional<size_t> maybe_match_driver_version;

	std::optional <std::string> maybe_replace_name;
	std::optional <size_t> maybe_replace_man_id;
	std::optional <size_t> maybe_replace_prod_id;
	std::optional <size_t> maybe_replace_driver_version;

	bool is_match(midi_dev_caps const& m) const {
		bool rval = true;
		if (maybe_match_direction.has_value()) { rval = rval && (maybe_match_direction.value() == m.direction); }
		if (maybe_match_name.has_value()) { rval = rval && (maybe_match_name.value() == m.name); }
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

void load_config(std::string filename) {
	try {
		wrapper_log("Loading config from %s\n", filename.c_str());

		std::ifstream f(filename);
		json data = json::parse(f);

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
					wrapper_log("Skipping rule:\n%s\n", e.what());
				}
				catch (...) {
					wrapper_log("Skipping rule (unknown exception)\n");
				}
			}
		}
	}
	catch (std::exception& e) {
		wrapper_log("Unable to load config from %s. Continuing without replace rules. Exception:\n%s\n", filename.c_str(), e.what());
	}
	catch (...) {
		wrapper_log("Unable to load config from %s. Continuing without replace rules. (unknown exception)\n", filename.c_str());
	}
}

void configure() {
	char* maybe_env;
	std::string try_config_file;

	// Log file
	if ((maybe_env = getenv("MIDI_REPLACE_LOGFILE")) != NULL) {
		g_maybe_wrapper_log_file = fopen(maybe_env, "w+");
		fprintf(g_maybe_wrapper_log_file, "Start MIDI replace\n");
	}

	// Config file
	char default_file_path[MAX_PATH];
	if ((maybe_env = getenv("MIDI_REPLACE_CONFIGFILE")) != NULL) {
		try_config_file = std::string(maybe_env);
	} else if(GetModuleFileNameA(NULL, default_file_path, MAX_PATH) > 0) {
		// By default, load a file from the executable's location
		try_config_file = std::string(default_file_path) + "/midi_replace_config.json";
	}
	if (try_config_file.length() > 0) { load_config(try_config_file); }

	wrapper_log("Starting MIDI replace with %d rules.\n", g_replace_rules.size());
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