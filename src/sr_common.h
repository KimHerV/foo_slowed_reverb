// SPDX-License-Identifier: MIT
// Copyright (c) 2026 KimHerV

#pragma once
// Shared between the DSP, its config popup, the panel and the exporter.
#include "sr_ui_logic.h"

namespace sr_fb2k {

static constexpr GUID guid_dsp = { 0xb1332b63, 0x0c34, 0x4fe0, { 0x85, 0x6a, 0xa8, 0xc2, 0xf0, 0xa4, 0xec, 0x8d } };
// v1: speed, wet, room, damp. v2 adds the bypass switch, so "off" keeps the DSP (and its position) in the chain.
// v3 adds pre-delay, low cut and width. v4 adds the name of the selected preset. v5 adds the high cut.
static constexpr uint32_t preset_version = 5;

struct Preset {
	sr::Params params;
	bool enabled = true;
	pfc::string8 name; // selected factory/user preset; empty = custom settings
};

// Settings saved before preset names existed get the name of a preset they match exactly.
inline void guess_name(Preset& p) {
	if (p.name.length()) return;
	for (const srui::Preset& f : srui::factoryPresets()) if (srui::sameParams(f.params, p.params)) { p.name = f.name.c_str(); return; }
}

inline void make_preset(const Preset& p, dsp_preset& out) {
	dsp_preset_builder b;
	b << preset_version << p.params.speed << p.params.wet << p.params.room << p.params.damp << (uint32_t)(p.enabled ? 1 : 0);
	b << p.params.predelay << p.params.lowcut << p.params.width;
	b << p.name;
	b << p.params.highcut;
	b.finish(guid_dsp, out);
}

inline Preset parse_preset(const dsp_preset& in) {
	Preset r;
	try {
		dsp_preset_parser parser(in);
		uint32_t ver = 0;
		parser >> ver;
		if (ver >= 1) parser >> r.params.speed >> r.params.wet >> r.params.room >> r.params.damp;
		if (ver >= 2) { uint32_t en = 1; parser >> en; r.enabled = en != 0; }
		if (ver >= 3) parser >> r.params.predelay >> r.params.lowcut >> r.params.width;
		else if (ver >= 1) r.params.setLegacyReverbShape(); // keep older chains sounding exactly as before
		if (ver >= 4) parser >> r.name;
		if (ver >= 5) parser >> r.params.highcut;
		else r.params.highcut = sr::Params::kMaxHighcutHz; // off, as before it existed
	} catch (exception_io_data const&) {
		r = Preset();
	}
	r.params.sanitize();
	guess_name(r);
	return r;
}

// Params as stored in our own config strings ("speed;wet;room;damp[;predelay;lowcut;width[;highcut]]").
inline bool parse_params_string(const char* s, sr::Params& p) {
	sr::Params r;
	const int n = sscanf_s(s, "%f;%f;%f;%f;%f;%f;%f;%f", &r.speed, &r.wet, &r.room, &r.damp, &r.predelay, &r.lowcut, &r.width, &r.highcut);
	if (n == 4) r.setLegacyReverbShape();
	else if (n == 7) r.highcut = sr::Params::kMaxHighcutHz;
	else if (n != 8) return false;
	r.sanitize();
	p = r;
	return true;
}
inline void format_params_string(const sr::Params& p, pfc::string_base& out) {
	char buf[224];
	sprintf_s(buf, "%.3f;%.3f;%.3f;%.3f;%.1f;%.1f;%.3f;%.1f", p.speed, p.wet, p.room, p.damp, p.predelay, p.lowcut, p.width, p.highcut);
	out = buf;
}

// Returns true and fills out if the playback DSP chain contains our DSP.
bool get_playback_chain_preset(Preset& out);

// Windows that want WM_SR_CHANGED when the live state, the preset library or the theme changes (main thread only).
constexpr UINT WM_SR_CHANGED = WM_APP + 0x5A1;
void add_listener(HWND wnd);
void remove_listener(HWND wnd);
void broadcast();

// Live playback state shared by the panel and the main menu toggle (main thread only).
// "Enabled" means our DSP is in the playback chain and not bypassed (fb2k persists the chain itself);
// the settings are also kept in our own config so they survive being switched off.
namespace live {
	bool is_enabled();
	void set_enabled(bool on);
	Preset get();                                       // params + preset name (enabled is filled in too)
	void set(const sr::Params& p, const char* name);
	// Hint fb2k to use low-latency playback while a slider is being dragged.
	void begin_drag();
	void end_drag();
}

// User preset library (main thread only). Saving notifies listeners.
namespace presets {
	const srui::PresetStore& store();
	void save(const srui::PresetStore& s);
	// Factory or user preset by name.
	bool lookup(const char* name, sr::Params& out, bool* isUser = nullptr);
	bool is_user(const char* name);
	// "Classic", "Classic *" (changed since picked) or "Custom".
	std::wstring label(const Preset& p);
	bool is_modified(const Preset& p);
}

// Colors for our owner-drawn controls: background and text follow the host dialog (dark mode hooks),
// the accent follows a custom choice, then a foobar2000 color override, then the Windows accent color.
namespace theme {
	struct Colors {
		COLORREF bg, text, dim, track, border, field, accent, onAccent;
	};
	Colors get(HWND wnd);     // wnd: our control; asks its parent for background/text
	bool has_custom_accent();
	COLORREF custom_accent();
	void set_custom_accent(bool on, COLORREF c);
	void init();              // main thread, at startup
	void shutdown();
}

// The slider rows (native controls IDC_SL_LABEL/TRACK/VALUE + param) of a dialog, used in all three windows.
namespace sliders {
	constexpr UINT WM_SR_COMMIT_EDIT = WM_APP + 0x5A2;
	enum Event { kChanged, kDragBegin, kDragEnd };

	class Block {
	public:
		std::function<void(int)> on_event;          // Event
		std::function<void(HWND)> on_new_control;   // lets the host apply dark mode to the value edit box

		void attach(HWND dlg);
		void set_params(const sr::Params& p);
		const sr::Params& params() const { return m_params; }
		void set_short_values(bool on);              // compact layout: no "(−5 semitones)"
		// Handles what the rows send to the dialog. Returns true if handled; result goes to DWLP_MSGRESULT.
		bool handle(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result);
		void cancel_edit();

	private:
		void update_text(int p);
		void begin_edit(int p);
		void end_edit(bool apply);
		bool custom_draw(NMCUSTOMDRAW* cd, LRESULT& result);

		HWND m_dlg = nullptr, m_edit = nullptr;
		int m_editParam = -1;
		bool m_dragging = false, m_short = false, m_darkPaint = false;
		int m_pos[srui::kParamCount] = {};
		sr::Params m_params;
		theme::Colors m_colors{};
	};
	int scale(HWND wnd, int px96);
}

// The export dialog for the tracks selected in the active playlist (the panel's Export button).
namespace exporter {
	void run_for_selection(HWND owner);
}

// Preset combo box, "…" menu and Save button behaviour shared by the three windows.
namespace presetbar {
	class Host {
	public:
		virtual Preset current() = 0;
		virtual void apply(const sr::Params& p, const char* name) = 0;
	};
	void fill(HWND combo, const Preset& cur);              // factory + user presets; the current one selected, "*" when changed
	void on_select(HWND owner, HWND combo, Host& host);    // CBN_SELCHANGE
	void show_actions(HWND owner, HWND button, Host& host); // the "…" button
	void save(HWND owner, Host& host);                     // overwrites the selected user preset, or asks for a name
}

}
