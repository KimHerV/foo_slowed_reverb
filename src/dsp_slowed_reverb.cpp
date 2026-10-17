// SPDX-License-Identifier: MIT
// Copyright (c) 2026 KimHerV

#include "stdafx.h"
#include "sr_common.h"

using namespace sr_fb2k;

// dsp_v3 lets the DSP Manager push slider changes live without recreating (and resetting) the DSP.
class dsp_slowed_reverb : public dsp_impl_base_t<dsp_v3> {
public:
	dsp_slowed_reverb(const dsp_preset& in) : m_preset(parse_preset(in)) {}

	static GUID g_get_guid() { return guid_dsp; }
	static void g_get_name(pfc::string_base& out) { out = "Slowed & Reverb"; }
	static bool g_get_default_preset(dsp_preset& out) {
		Preset p; p.name = srui::kDefaultPresetName;
		make_preset(p, out);
		return true;
	}
	static bool g_have_config_popup() { return true; }
	static void g_show_config_popup(const dsp_preset& data, HWND parent, dsp_preset_edit_callback& cb);
	// Shown in the DSP Manager list, so the state is visible without opening the dialog.
	static void g_get_display_name(const dsp_preset& data, pfc::string_base& out) {
		const Preset p = parse_preset(data);
		out = "Slowed & Reverb";
		if (!p.enabled) { out << " (bypassed)"; return; }
		out << " (";
		if (p.name.length()) out << pfc::stringcvt::string_utf8_from_wide(presets::label(p).c_str()) << ", ";
		out << (int)std::lround(p.params.speed * 100) << "%, mix " << (int)std::lround(p.params.wet * 100) << "%)";
	}
	// No modeless dialog; the host falls back to show_config_popup_v2.
	static service_ptr g_show_config_popup_v3(HWND, dsp_preset_edit_callback_v2::ptr) { throw pfc::exception_not_implemented(); }

	bool apply_preset(const dsp_preset& preset) override {
		std::lock_guard<std::mutex> lock(m_pendingSync);
		m_pending = parse_preset(preset);
		m_hasPending = true;
		return true;
	}

	bool on_chunk(audio_chunk* chunk, abort_callback&) override {
		pullPending();
		if (!isActive()) {
			// True passthrough: untouched samples, no resampling.
			if (m_ch) { m_proc.reset(); m_ch = 0; }
			return true;
		}
		const unsigned srate = chunk->get_srate(), ch = chunk->get_channels();
		if (!m_proc.isSetup(srate, ch) || m_ch == 0) m_proc.setup(srate, ch, m_preset.params);
		m_srate = srate; m_ch = ch; m_chConfig = chunk->get_channel_config();

		m_proc.process(chunk->get_data(), chunk->get_sample_count(), m_out);
		const size_t frames = m_out.size() / ch;
		if (frames == 0) return false;
		chunk->set_data(m_out.data(), frames, ch, srate, m_chConfig);
		return true;
	}

	void on_endofplayback(abort_callback&) override {
		if (m_ch == 0) return;
		m_proc.drain(m_out);
		m_proc.reset();
		const size_t frames = m_out.size() / m_ch;
		if (frames) insert_chunk(m_out.size())->set_data(m_out.data(), frames, m_ch, m_srate, m_chConfig);
	}
	void on_endoftrack(abort_callback&) override {}
	void flush() override { m_proc.reset(); }
	double get_latency() override { return m_ch ? m_proc.latencySeconds() : 0; }
	bool need_track_change_mark() override { return false; }

private:
	// Bypassed, or settings that would leave the audio unchanged (100% speed, no reverb).
	bool isActive() const { return m_preset.enabled && (m_preset.params.speed < 0.999f || m_preset.params.wet > 0.0f); }

	void pullPending() {
		std::lock_guard<std::mutex> lock(m_pendingSync);
		if (!m_hasPending) return;
		m_hasPending = false;
		m_preset = m_pending;
		m_proc.setParams(m_preset.params);
	}

	Preset m_preset;
	sr::Processor m_proc;
	std::vector<audio_sample> m_out;
	unsigned m_srate = 0, m_ch = 0, m_chConfig = 0; // m_ch == 0: not processing

	std::mutex m_pendingSync;
	Preset m_pending;
	bool m_hasPending = false;
};

static service_factory_single_t<dsp_entry_v3_impl_t<dsp_slowed_reverb>> g_dsp_factory;

bool sr_fb2k::get_playback_chain_preset(Preset& out) {
	dsp_preset_impl preset;
	if (!dsp_config_manager::get()->core_query_dsp(guid_dsp, preset)) return false;
	out = parse_preset(preset);
	return true;
}

// ---------------------------------------------------------------- DSP Manager popup

namespace {
	struct PopupCtx : presetbar::Host {
		const dsp_preset* initial = nullptr;
		dsp_preset_edit_callback* cb = nullptr;
		HWND dlg = nullptr;
		Preset cur;
		sliders::Block rows;
		fb2k::CCoreDarkModeHooks dark;

		Preset current() override { return cur; }
		void apply(const sr::Params& p, const char* name) override {
			cur.params = p; cur.name = name;
			rows.set_params(p);
			push();
		}
		void push() {
			dsp_preset_impl preset;
			make_preset(cur, preset);
			cb->on_preset_changed(preset);
			refresh();
		}
		void refresh() {
			CheckDlgButton(dlg, IDC_ENABLE, cur.enabled ? BST_CHECKED : BST_UNCHECKED);
			presetbar::fill(GetDlgItem(dlg, IDC_PRESET), cur);
			EnableWindow(GetDlgItem(dlg, IDC_PRESET_SAVE), presets::is_modified(cur));
		}
	};

	INT_PTR CALLBACK DspPopupProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
		PopupCtx* ctx = (PopupCtx*)GetWindowLongPtr(dlg, DWLP_USER);
		if (ctx) {
			LRESULT r;
			if (ctx->rows.handle(msg, wp, lp, r)) { SetWindowLongPtr(dlg, DWLP_MSGRESULT, r); return TRUE; }
		}
		switch (msg) {
		case WM_INITDIALOG:
			SetWindowLongPtr(dlg, DWLP_USER, lp);
			ctx = (PopupCtx*)lp;
			ctx->dlg = dlg;
			ctx->cur = parse_preset(*ctx->initial);
			ctx->rows.on_event = [ctx](int e) {
				if (e == sliders::kChanged) { ctx->cur.params = ctx->rows.params(); ctx->push(); }
				else if (e == sliders::kDragBegin) live::begin_drag();
				else live::end_drag();
			};
			ctx->rows.on_new_control = [ctx](HWND w) { ctx->dark.AddCtrlAuto(w); };
			ctx->rows.attach(dlg);
			ctx->rows.set_params(ctx->cur.params);
			ctx->dark.AddDialogWithControls(dlg);
			ctx->refresh();
			add_listener(dlg);
			return TRUE;
		case WM_SR_CHANGED: // preset library or theme changed
			if (ctx) { ctx->refresh(); RedrawWindow(dlg, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN); }
			return TRUE;
		case WM_COMMAND: {
			if (!ctx) break;
			const int id = LOWORD(wp), code = HIWORD(wp);
			if (id == IDC_ENABLE && code == BN_CLICKED) { ctx->cur.enabled = IsDlgButtonChecked(dlg, IDC_ENABLE) == BST_CHECKED; ctx->push(); return TRUE; }
			if (id == IDC_PRESET && code == CBN_SELCHANGE) { presetbar::on_select(dlg, (HWND)lp, *ctx); return TRUE; }
			if (id == IDC_PRESET_MORE && code == BN_CLICKED) { presetbar::show_actions(dlg, (HWND)lp, *ctx); return TRUE; }
			if (id == IDC_PRESET_SAVE && code == BN_CLICKED) { presetbar::save(dlg, *ctx); return TRUE; }
			if (id == IDOK || id == IDCANCEL) {
				LRESULT r; ctx->rows.handle(sliders::WM_SR_COMMIT_EDIT, id == IDOK, 0, r); // a value still being typed counts on OK
				live::end_drag();
				EndDialog(dlg, id);
				return TRUE;
			}
			break;
		}
		case WM_DESTROY:
			remove_listener(dlg);
			break;
		}
		return FALSE;
	}
}

void dsp_slowed_reverb::g_show_config_popup(const dsp_preset& data, HWND parent, dsp_preset_edit_callback& cb) {
	PopupCtx ctx;
	ctx.initial = &data;
	ctx.cb = &cb;
	const INT_PTR ret = DialogBoxParamW(core_api::get_my_instance(), MAKEINTRESOURCEW(IDD_DSP), parent, DspPopupProc, (LPARAM)&ctx);
	// Anything but OK reverts to the settings the dialog was opened with.
	if (ret != IDOK) cb.on_preset_changed(data);
}

