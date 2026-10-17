// SPDX-License-Identifier: MIT
// Copyright (c) 2026 KimHerV

#include "stdafx.h"
#include "sr_common.h"
#include <algorithm>

using namespace sr_fb2k;

namespace {
	const GUID guid_cfg_live_params = { 0x6b0e3f7a, 0x2c41, 0x4d8e, { 0x9a, 0x15, 0x3e, 0x77, 0xc2, 0x08, 0x51, 0xd4 } };
	const GUID guid_cfg_live_name = { 0x0f1d7c52, 0x8a3e, 0x4b6f, { 0x93, 0x0c, 0x5e, 0x21, 0x7a, 0xd4, 0x46, 0x19 } };
	cfg_string cfg_live_params(guid_cfg_live_params, "");
	cfg_string cfg_live_name(guid_cfg_live_name, "");

	std::vector<HWND> g_listeners;
	service_ptr g_dragScope;

	void store(const Preset& p) {
		pfc::string8 s;
		format_params_string(p.params, s);
		cfg_live_params.set(s);
		cfg_live_name.set(p.name);
	}
	// Writes our entry into the playback chain, keeping its position if it is already there.
	void write_chain(const Preset& p) {
		dsp_preset_impl preset;
		make_preset(p, preset);
		dsp_config_manager::get()->core_enable_dsp(preset, dsp_config_manager::default_insert_last);
	}

	// Picks up edits made in the DSP Manager (or anywhere else) so panels and the remembered values follow.
	class chain_watcher : public dsp_config_callback {
	public:
		void on_core_settings_change(const dsp_chain_config& chain) override {
			for (t_size i = 0; i < chain.get_count(); ++i) {
				if (chain.get_item(i).get_owner() == guid_dsp) { store(parse_preset(chain.get_item(i))); break; }
			}
			broadcast();
		}
	};
	service_factory_single_t<chain_watcher> g_chain_watcher;
}

void sr_fb2k::add_listener(HWND wnd) { g_listeners.push_back(wnd); }
void sr_fb2k::remove_listener(HWND wnd) { g_listeners.erase(std::remove(g_listeners.begin(), g_listeners.end(), wnd), g_listeners.end()); }
void sr_fb2k::broadcast() {
	for (HWND w : g_listeners) PostMessage(w, WM_SR_CHANGED, 0, 0);
}

bool live::is_enabled() {
	Preset p;
	return get_playback_chain_preset(p) && p.enabled;
}

void live::set_enabled(bool on) {
	if (on == is_enabled()) return;
	// Switching off only flips the bypass flag: the DSP keeps its place in the chain.
	Preset p = get();
	p.enabled = on;
	write_chain(p);
}

Preset live::get() {
	Preset chain;
	if (get_playback_chain_preset(chain)) return chain;
	Preset p;
	pfc::string8 s = cfg_live_params.get();
	if (s.length() == 0 || !parse_params_string(s, p.params)) p.params = sr::Params();
	p.params.sanitize();
	p.name = cfg_live_name.get();
	p.enabled = false;
	guess_name(p);
	return p;
}

void live::set(const sr::Params& params, const char* name) {
	Preset p;
	p.params = params; p.params.sanitize();
	p.name = name ? name : "";
	store(p);
	Preset chain;
	if (get_playback_chain_preset(chain)) { p.enabled = chain.enabled; write_chain(p); }
	else broadcast();
}

void live::begin_drag() {
	if (g_dragScope.is_valid()) return;
	auto v3 = dsp_config_manager_v3::tryGet();
	if (v3.is_valid()) g_dragScope = v3->acquire_live_edit_scope();
}
void live::end_drag() { g_dragScope.release(); }
