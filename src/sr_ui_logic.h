// SPDX-License-Identifier: MIT
// Copyright (c) 2026 KimHerV

#pragma once
// UI logic shared by the panel, the DSP popup and the export dialog: parameter table, value text in and out,
// snapping, presets (factory, user store, preset files) and accent color choice.
// No foobar2000 or Win32 dependencies, so it is unit-tested together with the DSP core.

#include "slowed_reverb_core.h"
#include <string>
#include <vector>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <cctype>
#include <cstdlib>
#include <utility>

namespace srui {

enum Param { kSpeed, kMix, kRoom, kDamp, kPredelay, kLowcut, kHighcut, kWidth, kParamCount };

// Values in the UI are in display units (percent, ms, Hz); `scale` converts to sr::Params units.
// Sliders show a tick every `step` and snap to the ticks while dragging; exact values are typed in.
struct ParamDef {
	const wchar_t* label;
	const char* key;          // preset file key
	float min, max;
	float step, fine;         // tick spacing (drag/arrow/wheel step), and the step with Shift held
	float scale;
	float sr::Params::* field;
	bool primary;             // shown when "Reverb shape" is collapsed
};

inline const ParamDef& def(int p) {
	static const ParamDef kDefs[kParamCount] = {
		{ L"Speed", "speed", 50, 100, 5, 0.5f, 0.01f, &sr::Params::speed, true },
		{ L"Reverb mix", "mix", 0, 100, 5, 1, 0.01f, &sr::Params::wet, true },
		{ L"Room size", "room", 0, 100, 5, 1, 0.01f, &sr::Params::room, false },
		{ L"Damping", "damping", 0, 100, 5, 1, 0.01f, &sr::Params::damp, false },
		{ L"Pre-delay", "predelay", 0, sr::Params::kMaxPredelayMs, 10, 1, 1.0f, &sr::Params::predelay, false },
		{ L"Low cut", "lowcut", 0, sr::Params::kMaxLowcutHz, 25, 5, 1.0f, &sr::Params::lowcut, false },
		{ L"High cut", "highcut", sr::Params::kMinHighcutHz, sr::Params::kMaxHighcutHz, 1000, 100, 1.0f, &sr::Params::highcut, false },
		{ L"Width", "width", 0, 100, 5, 1, 0.01f, &sr::Params::width, false },
	};
	return kDefs[p];
}
inline int tickCount(int p) { const ParamDef& d = def(p); return (int)std::lround((d.max - d.min) / d.step) + 1; }

inline float clampTo(int p, float v) { const ParamDef& d = def(p); return v < d.min ? d.min : (v > d.max ? d.max : v); }
inline float get(const sr::Params& s, int p) { return s.*def(p).field / def(p).scale; }
inline void set(sr::Params& s, int p, float v) { s.*def(p).field = clampTo(p, v) * def(p).scale; }
inline float defaultValue(int p) { return get(sr::Params(), p); }
inline float position(int p, float v) { const ParamDef& d = def(p); return (v - d.min) / (d.max - d.min); } // 0..1 along the slider

inline double semitones(float speedPercent) { return 12.0 * std::log2(speedPercent / 100.0); }

// Snaps a dragged value to the nearest tick (or the fine grid with Shift).
inline float quantize(int p, float raw, bool fine) {
	const ParamDef& d = def(p);
	const float grid = fine ? d.fine : d.step;
	return clampTo(p, d.min + std::round((raw - d.min) / grid) * grid);
}
// One arrow/wheel step: to the next tick in that direction, so a typed in-between value lands back on the grid.
inline float nudge(int p, float v, int dir, bool fine) {
	const ParamDef& d = def(p);
	const float grid = fine ? d.fine : d.step;
	const float pos = (v - d.min) / grid;
	float k = dir > 0 ? std::floor(pos + 1e-3f) + dir : std::ceil(pos - 1e-3f) + dir;
	return clampTo(p, d.min + k * grid);
}

inline std::wstring num(float v) { // integer when it is one, else one decimal
	wchar_t b[32];
	if (std::fabs(v - std::round(v)) < 0.05f) swprintf(b, 32, L"%d", (int)std::lround(v));
	else swprintf(b, 32, L"%.1f", v);
	return b;
}

// Low cut is off at its minimum, high cut at its maximum.
inline bool hasOff(int p) { return p == kLowcut || p == kHighcut; }
inline bool isOff(int p, float v) { return (p == kLowcut && v <= 0) || (p == kHighcut && v >= def(kHighcut).max); }
inline float offValue(int p) { return p == kHighcut ? def(kHighcut).max : 0.0f; }

inline std::wstring format(int p, float v) {
	switch (p) {
	case kPredelay: return num(v) + L" ms";
	case kLowcut: return v <= 0 ? std::wstring(L"Off") : num(v) + L" Hz";
	case kHighcut: return isOff(p, v) ? std::wstring(L"Off") : num(v / 1000) + L" kHz";
	default: return num(v) + L"%";
	}
}
// Extra, dimmer text next to the value: what the speed does to the pitch.
inline std::wstring detail(int p, float v) {
	if (p != kSpeed) return std::wstring();
	const double st = semitones(v), r = std::round(st);
	if (std::fabs(st) < 0.05) return L"original pitch";
	wchar_t b[48];
	if (std::fabs(st - r) < 0.05) swprintf(b, 48, L"−%d semitone%ls", (int)std::fabs(r), std::fabs(r) == 1 ? L"" : L"s");
	else swprintf(b, 48, L"−%.1f semitones", std::fabs(st));
	return b;
}
// What the value box starts with when it is opened for typing.
inline std::wstring editText(int p, float v) {
	if (isOff(p, v)) return L"Off";
	return p == kHighcut ? num(v / 1000) + L"k" : num(v);
}

// Typed values: "80", "80%", "-3st", "0.8x" (speed); "25ms", "0.05s" (pre-delay); "1k", "120hz", "off" (low/high cut).
// A bare high cut number below 100 means kHz ("8" = 8 kHz).
inline bool parse(int p, const std::wstring& text, float& out) {
	std::wstring s;
	for (wchar_t c : text) {
		if (c == L' ' || c == L'\t') continue;
		s += (c == 0x2212) ? L'-' : (wchar_t)std::towlower(c);
	}
	if (s.empty()) return false;
	auto endsWith = [&](const wchar_t* suf) {
		const size_t n = std::wcslen(suf);
		if (s.size() < n || s.compare(s.size() - n, n, suf) != 0) return false;
		s.erase(s.size() - n);
		return true;
	};
	if (hasOff(p) && (s == L"off" || s == L"-")) { out = offValue(p); return true; }
	double mult = 1;
	bool semis = false, ratio = false;
	if (p == kSpeed && (endsWith(L"st") || endsWith(L"semitones") || endsWith(L"semi"))) semis = true;
	else if (p == kSpeed && endsWith(L"x")) ratio = true;
	else if (p == kPredelay && endsWith(L"ms")) {}
	else if (p == kPredelay && endsWith(L"s")) mult = 1000;
	else if (hasOff(p) && endsWith(L"khz")) mult = 1000;
	else if (hasOff(p) && endsWith(L"hz")) {}
	else if (hasOff(p) && endsWith(L"k")) mult = 1000;
	else endsWith(L"%");
	wchar_t* end = nullptr;
	const double n = std::wcstod(s.c_str(), &end);
	if (end == s.c_str() || *end != 0 || !(n == n)) return false;
	double v;
	if (semis) v = 100.0 * std::pow(2.0, -std::fabs(n) / 12.0); // "3st" and "-3st" both mean three semitones down
	else if (ratio) v = n * 100.0;
	else v = n * mult;
	if (p == kHighcut && mult == 1 && v < 100) v *= 1000;
	out = clampTo(p, (float)v);
	return true;
}

// ---------------------------------------------------------------- presets

struct Preset { std::string name; sr::Params params; }; // name in UTF-8

inline const std::vector<Preset>& factoryPresets() {
	static const std::vector<Preset> kFactory = [] {
		auto make = [](const char* name, float speed, float mix, float room, float damp, float predelay, float lowcut, float highcut, float width) {
			Preset p; p.name = name;
			set(p.params, kSpeed, speed); set(p.params, kMix, mix); set(p.params, kRoom, room); set(p.params, kDamp, damp);
			set(p.params, kPredelay, predelay); set(p.params, kLowcut, lowcut); set(p.params, kHighcut, highcut); set(p.params, kWidth, width);
			return p;
		};
		return std::vector<Preset>{
			make("Light", 90, 25, 70, 50, 10, 150, 20000, 90),
			make("Classic", 85, 35, 80, 50, 20, 120, 10000, 100),
			make("Dreamy", 75, 50, 92, 65, 40, 100, 6000, 100),
		};
	}();
	return kFactory;
}
inline const char* kDefaultPresetName = "Classic"; // matches sr::Params defaults

inline bool sameParams(const sr::Params& a, const sr::Params& b) {
	for (int p = 0; p < kParamCount; ++p) if (std::fabs(get(a, p) - get(b, p)) > 0.001f) return false;
	return true;
}

inline std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
inline bool isFactoryName(const std::string& name) {
	for (const Preset& f : factoryPresets()) if (lower(f.name) == lower(name)) return true;
	return false;
}
// Trims, drops control characters, limits the length. Empty result = not a usable name.
inline std::string cleanName(const std::string& in) {
	std::string s;
	for (unsigned char c : in) if (c >= 0x20 && c != 0x7F) s += (char)c;
	const size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
	if (a == std::string::npos) return std::string();
	s = s.substr(a, b - a + 1);
	if (s.size() > 64) { s.resize(64); while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80) s.pop_back(); if (!s.empty() && ((unsigned char)s.back() & 0x80)) s.pop_back(); }
	return s;
}

// User presets, kept in a config string: one "name<TAB>speed;mix;room;damp;predelay;lowcut;width[;highcut]" line each,
// values in display units. High cut came last; lines without it are from before it existed and keep it off.
class PresetStore {
public:
	std::vector<Preset> items;

	const Preset* find(const std::string& name) const {
		for (const Preset& p : items) if (lower(p.name) == lower(name)) return &p;
		return nullptr;
	}
	// Adds or overwrites. Refuses factory names and empty names.
	bool put(const std::string& rawName, const sr::Params& params) {
		const std::string name = cleanName(rawName);
		if (name.empty() || isFactoryName(name)) return false;
		for (Preset& p : items) if (lower(p.name) == lower(name)) { p.name = name; p.params = params; return true; }
		items.push_back({ name, params });
		return true;
	}
	bool remove(const std::string& name) {
		for (size_t i = 0; i < items.size(); ++i) if (lower(items[i].name) == lower(name)) { items.erase(items.begin() + i); return true; }
		return false;
	}
	bool rename(const std::string& from, const std::string& rawTo) {
		const std::string to = cleanName(rawTo);
		if (to.empty() || isFactoryName(to)) return false;
		const Preset* clash = find(to);
		Preset* src = nullptr;
		for (Preset& p : items) if (lower(p.name) == lower(from)) src = &p;
		if (!src || (clash && clash != src)) return false;
		src->name = to;
		return true;
	}

	std::string serialize() const {
		std::string out;
		for (const Preset& p : items) {
			out += p.name;
			char b[192];
			snprintf(b, sizeof b, "\t%.2f;%.2f;%.2f;%.2f;%.2f;%.2f;%.2f;%.2f\n", get(p.params, kSpeed), get(p.params, kMix), get(p.params, kRoom),
				get(p.params, kDamp), get(p.params, kPredelay), get(p.params, kLowcut), get(p.params, kWidth), get(p.params, kHighcut));
			out += b;
		}
		return out;
	}
	static PresetStore parse(const std::string& text) {
		PresetStore st;
		size_t pos = 0;
		while (pos < text.size()) {
			size_t nl = text.find('\n', pos);
			if (nl == std::string::npos) nl = text.size();
			const std::string line = text.substr(pos, nl - pos);
			pos = nl + 1;
			const size_t tab = line.find('\t');
			if (tab == std::string::npos) continue;
			static const int kOrder[] = { kSpeed, kMix, kRoom, kDamp, kPredelay, kLowcut, kWidth, kHighcut };
			float v[8] = { 0, 0, 0, 0, 0, 0, 0, offValue(kHighcut) };
			const int n = sscanf(line.c_str() + tab + 1, "%f;%f;%f;%f;%f;%f;%f;%f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
			if (n != 7 && n != 8) continue;
			sr::Params prm;
			for (int i = 0; i < 8; ++i) set(prm, kOrder[i], v[i]);
			st.put(line.substr(0, tab), prm);
		}
		return st;
	}
};

// Preset files (.srpreset) for sharing: a small INI-style text file.
inline std::string formatPresetFile(const Preset& p) {
	std::string out = "[Slowed & Reverb preset]\nname=" + p.name + "\n";
	for (int i = 0; i < kParamCount; ++i) {
		char b[64]; snprintf(b, sizeof b, "%s=%.2f\n", def(i).key, get(p.params, i));
		out += b;
	}
	return out;
}
// Unknown keys are ignored and missing values keep their defaults, so files from newer versions still load.
// Files from before the high cut existed keep it off.
inline bool parsePresetFile(const std::string& text, Preset& out) {
	if (text.find("[Slowed & Reverb preset]") == std::string::npos) return false;
	Preset r; r.params = sr::Params(); set(r.params, kHighcut, offValue(kHighcut));
	size_t pos = 0;
	while (pos < text.size()) {
		size_t nl = text.find('\n', pos);
		if (nl == std::string::npos) nl = text.size();
		std::string line = text.substr(pos, nl - pos);
		pos = nl + 1;
		if (!line.empty() && line.back() == '\r') line.pop_back();
		const size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		const std::string k = lower(line.substr(0, eq)), v = line.substr(eq + 1);
		if (k == "name") { r.name = cleanName(v); continue; }
		for (int i = 0; i < kParamCount; ++i) if (k == def(i).key) set(r.params, i, (float)std::atof(v.c_str()));
	}
	if (r.name.empty()) r.name = "Imported preset";
	out = r;
	return true;
}

// ---------------------------------------------------------------- colors

typedef unsigned long Rgb; // 0x00BBGGRR, same layout as COLORREF
inline Rgb rgb(int r, int g, int b) { return (Rgb)(r | (g << 8) | (b << 16)); }
inline double luminance(Rgb c) {
	auto ch = [](int v) { const double s = v / 255.0; return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4); };
	return 0.2126 * ch(c & 0xFF) + 0.7152 * ch((c >> 8) & 0xFF) + 0.0722 * ch((c >> 16) & 0xFF);
}
inline double contrast(Rgb a, Rgb b) {
	double la = luminance(a), lb = luminance(b);
	if (la < lb) std::swap(la, lb);
	return (la + 0.05) / (lb + 0.05);
}
// Blends toward white (amount > 0) or black (amount < 0).
inline Rgb mix(Rgb c, double amount) {
	auto f = [&](int v) { return (int)std::lround(amount >= 0 ? v + (255 - v) * amount : v * (1 + amount)); };
	return rgb(f(c & 0xFF), f((c >> 8) & 0xFF), f((c >> 16) & 0xFF));
}
// Picks the first candidate that stands out from the background (3:1, the bar for UI graphics),
// and keeps lightening/darkening the last one if none does.
inline Rgb readableAccent(const std::vector<Rgb>& candidates, Rgb background) {
	const bool darkBg = luminance(background) < 0.2;
	for (Rgb c : candidates) if (contrast(c, background) >= 3.0) return c;
	Rgb c = candidates.empty() ? (darkBg ? rgb(0x4C, 0xC2, 0xFF) : rgb(0x00, 0x67, 0xC0)) : candidates.back();
	for (int i = 1; i <= 10 && contrast(c, background) < 3.0; ++i) c = mix(candidates.empty() ? c : candidates.back(), darkBg ? 0.1 * i : -0.1 * i);
	return c;
}
// Text drawn on top of the accent (e.g. a filled button).
inline Rgb textOn(Rgb accent) { return contrast(accent, rgb(0, 0, 0)) >= contrast(accent, rgb(255, 255, 255)) ? rgb(0, 0, 0) : rgb(255, 255, 255); }

} // namespace srui
