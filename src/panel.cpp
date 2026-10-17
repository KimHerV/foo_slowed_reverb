// SPDX-License-Identifier: MIT
// Copyright (c) 2026 KimHerV

#include "stdafx.h"
#include "sr_common.h"
#include <windowsx.h>
#include <columns_ui-sdk/ui_extension.h>

using namespace sr_fb2k;

// The panel, for the Default UI (Layout editing mode, or View > Slowed & Reverb as a popup) and Columns UI,
// plus the main-menu toggle. Two layouts: full (switch, presets, the sliders with "Reverb shape" folded)
// and compact (one row). Right-click the panel for the layout and accent color options.

namespace {

const GUID guid_panel = { 0x9c535a3d, 0x93d8, 0x4c30, { 0xbc, 0x7b, 0xac, 0xa3, 0xfb, 0xe3, 0xd2, 0x8e } };
const GUID guid_cui_panel = { 0x2f6d8e1a, 0x47b3, 0x4c95, { 0x9e, 0x0d, 0x61, 0xa8, 0x3c, 0x52, 0xb7, 0x04 } };
const GUID guid_mm_toggle = { 0x9aa74f0b, 0x7e17, 0x4422, { 0xac, 0x7f, 0xcc, 0x54, 0x98, 0xf2, 0x48, 0x4d } };

// Per-instance settings, saved with the layout.
struct PanelConfig {
	bool compact = false;
	bool expanded = false;
	static constexpr uint32_t kVersion = 1;
	uint32_t flags() const { return (compact ? 1 : 0) | (expanded ? 2 : 0); }
	void set_flags(uint32_t f) { compact = (f & 1) != 0; expanded = (f & 2) != 0; }
};

// ---------------------------------------------------------------- panel window

struct PanelState : presetbar::Host {
	HWND dlg = nullptr;
	PanelConfig cfg;
	Preset cur;
	sliders::Block rows;
	fb2k::coreDarkModeObj::ptr dark;
	bool forcedDark = false;                 // Columns UI: dark mode follows its own setting, not foobar2000's
	std::function<void()> on_size_change;    // host: our height or minimum width changed
	std::function<bool()> host_edit_mode;    // host: right-clicks belong to its layout editor

	Preset current() override { return cur; }
	void apply(const sr::Params& p, const char* name) override {
		cur.params = p; cur.name = name;
		rows.set_params(p);
		live::set(p, name);
		refresh_header();
	}
	void refresh_header() {
		CheckDlgButton(dlg, IDC_ENABLE, cur.enabled ? BST_CHECKED : BST_UNCHECKED);
		presetbar::fill(GetDlgItem(dlg, IDC_PRESET), cur);
		EnableWindow(GetDlgItem(dlg, IDC_PRESET_SAVE), presets::is_modified(cur));
	}
	void refresh() {
		cur = live::get();
		rows.set_params(cur.params);
		refresh_header();
	}

	int S(int px) const { return sliders::scale(dlg, px); }
	int text_w(int id) const {
		HWND w = GetDlgItem(dlg, id);
		wchar_t buf[128]; GetWindowTextW(w, buf, 128);
		HDC dc = GetDC(w);
		HGDIOBJ old = SelectObject(dc, (HFONT)SendMessageW(w, WM_GETFONT, 0, 0));
		SIZE sz{}; GetTextExtentPoint32W(dc, buf, lstrlenW(buf), &sz);
		SelectObject(dc, old); ReleaseDC(w, dc);
		return sz.cx;
	}
	int text_w(const wchar_t* s) const {
		HDC dc = GetDC(dlg);
		HGDIOBJ old = SelectObject(dc, (HFONT)SendMessageW(dlg, WM_GETFONT, 0, 0));
		SIZE sz{}; GetTextExtentPoint32W(dc, s, lstrlenW(s), &sz);
		SelectObject(dc, old); ReleaseDC(dlg, dc);
		return sz.cx;
	}

	int row_h() const { return S(32); }
	int height() const {
		if (cfg.compact) return S(8) + S(34) + S(6);
		int rowsShown = 0;
		for (int p = 0; p < srui::kParamCount; ++p) if (srui::def(p).primary || cfg.expanded) ++rowsShown;
		return S(8) + S(26) + S(6) + rowsShown * row_h() + S(30) + S(8);
	}
	int min_width() const { return cfg.compact ? S(600) : S(440); }

	void show(int id, bool on) { ShowWindow(GetDlgItem(dlg, id), on ? SW_SHOWNA : SW_HIDE); }

	void layout() {
		RECT rc; GetClientRect(dlg, &rc);
		const int w = (std::max)((int)rc.right, min_width());
		const int m = S(10);
		const UINT f = SWP_NOZORDER | SWP_NOACTIVATE;
		HWND en = GetDlgItem(dlg, IDC_ENABLE), combo = GetDlgItem(dlg, IDC_PRESET), more = GetDlgItem(dlg, IDC_PRESET_MORE),
			save = GetDlgItem(dlg, IDC_PRESET_SAVE), all = GetDlgItem(dlg, IDC_MORE), fold = GetDlgItem(dlg, IDC_FOLD);
		auto place = [&](HWND h, int x, int y, int cx, int cy) { SetWindowPos(h, nullptr, x, y, cx, cy, f); };
		auto row = [&](int p, int x, int y, int h, int labelW, int right, int valueW) {
			place(GetDlgItem(dlg, IDC_SL_LABEL + p), x, y + (h - S(18)) / 2, labelW, S(18));
			place(GetDlgItem(dlg, IDC_SL_VALUE + p), right - valueW, y + (h - S(18)) / 2, valueW, S(18));
			place(GetDlgItem(dlg, IDC_SL_TRACK + p), x + labelW + S(4), y + (h - S(30)) / 2, right - valueW - S(8) - (x + labelW + S(4)), S(30));
		};
		rows.set_short_values(cfg.compact);
		SetDlgItemTextW(dlg, IDC_SL_LABEL + srui::kMix, cfg.compact ? L"Mix" : srui::def(srui::kMix).label);
		SetWindowTextW(en, cfg.compact ? L"On" : L"Enable Slowed && Reverb");

		if (cfg.compact) {
			const int y = S(8), h = S(34), bh = S(26), by = y + (h - bh) / 2;
			int x = m;
			const int enW = text_w(IDC_ENABLE) + S(28);
			place(en, x, y + (h - S(20)) / 2, enW, S(20)); x += enW + S(8);
			place(combo, x, by, S(140), S(300)); x += S(140) + S(14);
			const int allW = text_w(IDC_MORE) + S(24);
			place(all, w - m - allW, by, allW, bh);
			const int right = w - m - allW - S(12), half = (right - x) / 2, valueW = text_w(L"100%") + S(10);
			row(srui::kSpeed, x, y, h, text_w(IDC_SL_LABEL + srui::kSpeed) + S(6), x + half - S(8), valueW);
			row(srui::kMix, x + half + S(8), y, h, text_w(IDC_SL_LABEL + srui::kMix) + S(6), right, valueW);
			for (int id : { IDC_PRESET_MORE, IDC_PRESET_SAVE, IDC_FOLD, IDC_EXPORT }) show(id, false);
			show(IDC_MORE, true);
			for (int p = 0; p < srui::kParamCount; ++p) {
				const bool on = srui::def(p).primary;
				show(IDC_SL_LABEL + p, on); show(IDC_SL_TRACK + p, on); show(IDC_SL_VALUE + p, on);
			}
		} else {
			const int y = S(8), hh = S(26);
			const int saveW = text_w(IDC_PRESET_SAVE) + S(32), moreW = S(32), comboW = (std::min)(S(190), w / 3);
			// Only the box and its text are clickable, not the empty space up to the preset bar.
			const int enW = (std::min)(text_w(IDC_ENABLE) + S(28), (std::max)(S(40), w - 2 * m - saveW - moreW - comboW - S(24)));
			place(en, m, y + (hh - S(20)) / 2, enW, S(20));
			place(save, w - m - saveW, y, saveW, hh);
			place(more, w - m - saveW - S(4) - moreW, y, moreW, hh);
			place(combo, w - m - saveW - S(4) - moreW - S(6) - comboW, y, comboW, S(300));
			int labelW = 0;
			for (int p = 0; p < srui::kParamCount; ++p) labelW = (std::max)(labelW, text_w(IDC_SL_LABEL + p));
			labelW += S(8);
			const int valueW = text_w(L"88.8%  (−10.0 semitones)") + S(10);
			int ry = y + hh + S(6);
			for (int p = 0; p < srui::kParamCount; ++p) if (srui::def(p).primary) { row(p, m, ry, row_h(), labelW, w - m, valueW); ry += row_h(); }
			SetWindowTextW(fold, cfg.expanded ? L"▾  Reverb shape" : L"▸  Reverb shape");
			place(fold, m, ry + S(2), text_w(IDC_FOLD) + S(28), S(26));
			const int exportW = text_w(IDC_EXPORT) + S(24);
			place(GetDlgItem(dlg, IDC_EXPORT), w - m - exportW, ry + S(2), exportW, S(26)); ry += S(30);
			for (int p = 0; p < srui::kParamCount; ++p) if (!srui::def(p).primary) {
				if (cfg.expanded) { row(p, m, ry, row_h(), labelW, w - m, valueW); ry += row_h(); }
			}
			for (int id : { IDC_PRESET_MORE, IDC_PRESET_SAVE, IDC_FOLD, IDC_EXPORT }) show(id, true);
			show(IDC_MORE, false);
			for (int p = 0; p < srui::kParamCount; ++p) {
				const bool on = srui::def(p).primary || cfg.expanded;
				show(IDC_SL_LABEL + p, on); show(IDC_SL_TRACK + p, on); show(IDC_SL_VALUE + p, on);
			}
		}
		RedrawWindow(dlg, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
	}
	void relayout() {
		layout();
		if (on_size_change) on_size_change();
	}

	void context_menu(POINT pt) {
		enum { kCompact = 1, kFollow, kCustom, kShowAll };
		HMENU m = CreatePopupMenu();
		AppendMenuW(m, MF_STRING | (cfg.compact ? MF_CHECKED : 0), kCompact, L"Compact layout");
		AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
		AppendMenuW(m, MF_STRING | (theme::has_custom_accent() ? 0 : MF_CHECKED), kFollow, L"Slider color: follow foobar2000 / Windows");
		AppendMenuW(m, MF_STRING | (theme::has_custom_accent() ? MF_CHECKED : 0), kCustom, L"Slider color: custom...");
		const int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, dlg, nullptr);
		DestroyMenu(m);
		switch (cmd) {
		case kCompact: cfg.compact = !cfg.compact; relayout(); break;
		case kFollow: theme::set_custom_accent(false, 0); break;
		case kCustom: {
			static COLORREF custom[16];
			CHOOSECOLORW cc = { sizeof cc };
			cc.hwndOwner = dlg;
			cc.rgbResult = theme::custom_accent();
			cc.lpCustColors = custom;
			cc.Flags = CC_RGBINIT | CC_FULLOPEN;
			if (ChooseColorW(&cc)) theme::set_custom_accent(true, cc.rgbResult);
			break;
		}
		}
	}
	void show_all(); // the full panel in a popup window (below)
	void set_dark(bool on) {
		if (!forcedDark || !dark.is_valid()) return;
		dark->setDarkMode(on);
		RedrawWindow(dlg, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
	}
};

INT_PTR CALLBACK PanelProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
	PanelState* st = (PanelState*)GetWindowLongPtr(dlg, DWLP_USER);
	if (st) {
		LRESULT r;
		if (st->rows.handle(msg, wp, lp, r)) { SetWindowLongPtr(dlg, DWLP_MSGRESULT, r); return TRUE; }
	}
	switch (msg) {
	case WM_INITDIALOG: {
		st = (PanelState*)lp;
		st->dlg = dlg;
		SetWindowLongPtr(dlg, DWLP_USER, (LONG_PTR)st);
		if (auto api = fb2k::coreDarkMode::tryGet(); api.is_valid()) {
			st->dark = st->forcedDark ? api->create(cui::colours::is_dark_mode_active()) : api->createAuto();
			st->dark->addDialog(dlg);
			st->dark->addControls(dlg);
		}
		st->rows.on_event = [st](int e) {
			if (e == sliders::kChanged) { st->cur.params = st->rows.params(); live::set(st->cur.params, st->cur.name); st->refresh_header(); }
			else if (e == sliders::kDragBegin) live::begin_drag();
			else live::end_drag();
		};
		st->rows.on_new_control = [st](HWND w) { if (st->dark.is_valid()) st->dark->addCtrlAuto(w); };
		st->rows.attach(dlg);
		st->refresh();
		st->layout();
		add_listener(dlg);
		return FALSE;
	}
	case WM_SIZE:
		if (st) st->layout();
		return TRUE;
	case WM_GETMINMAXINFO: // Columns UI reads the size limits this way
		if (st) {
			MINMAXINFO* mmi = (MINMAXINFO*)lp;
			mmi->ptMinTrackSize.x = st->min_width();
			mmi->ptMinTrackSize.y = mmi->ptMaxTrackSize.y = st->height();
			return TRUE;
		}
		break;
	case WM_SR_CHANGED:
		if (st) { st->refresh(); RedrawWindow(dlg, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN); }
		return TRUE;
	case WM_CONTEXTMENU: {
		if (!st || (st->host_edit_mode && st->host_edit_mode())) break; // the host's layout editing menu
		POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
		if (pt.x == -1 && pt.y == -1) { RECT rc; GetWindowRect(dlg, &rc); pt = { rc.left + 8, rc.top + 8 }; }
		st->context_menu(pt);
		return TRUE;
	}
	case WM_COMMAND: {
		if (!st) break;
		const int id = LOWORD(wp), code = HIWORD(wp);
		if (id == IDC_PRESET && code == CBN_SELCHANGE) { presetbar::on_select(dlg, (HWND)lp, *st); return TRUE; }
		if (code != BN_CLICKED) break;
		if (id == IDC_ENABLE) { live::set_enabled(IsDlgButtonChecked(dlg, IDC_ENABLE) == BST_CHECKED); return TRUE; }
		if (id == IDC_PRESET_MORE) { presetbar::show_actions(dlg, (HWND)lp, *st); return TRUE; }
		if (id == IDC_PRESET_SAVE) { presetbar::save(dlg, *st); return TRUE; }
		if (id == IDC_EXPORT) { exporter::run_for_selection(dlg); return TRUE; }
		if (id == IDC_FOLD) { st->cfg.expanded = !st->cfg.expanded; st->relayout(); return TRUE; }
		if (id == IDC_MORE) { st->show_all(); return TRUE; }
		break;
	}
	case WM_DESTROY:
		remove_listener(dlg);
		live::end_drag();
		break;
	}
	return FALSE;
}

HWND create_panel(HWND parent, PanelState& st) {
	HWND wnd = CreateDialogParamW(core_api::get_my_instance(), MAKEINTRESOURCEW(IDD_PANEL), parent, PanelProc, (LPARAM)&st);
	if (!wnd) throw exception_win32(GetLastError());
	return wnd;
}

// ---------------------------------------------------------------- Default UI element

ui_element_config::ptr make_config(const PanelConfig& c) {
	ui_element_config_builder b;
	b << PanelConfig::kVersion << c.flags();
	return b.finish(guid_panel);
}
PanelConfig parse_config(ui_element_config::ptr cfg) {
	PanelConfig c;
	try {
		ui_element_config_parser p(cfg);
		uint32_t ver = 0, flags = 0;
		p >> ver >> flags;
		c.set_flags(flags);
	} catch (exception_io_data const&) {} // empty (new instance) or unknown: defaults
	return c;
}

class panel_instance : public ui_element_instance {
public:
	panel_instance(HWND parent, ui_element_config::ptr cfg, ui_element_instance_callback::ptr cb) : m_cb(cb) {
		m_state.cfg = parse_config(cfg);
		m_state.on_size_change = [this] { m_cb->on_min_max_info_change(); };
		m_state.host_edit_mode = [this] { return m_cb->is_edit_mode_enabled(); };
		m_wnd = create_panel(parent, m_state);
	}
	~panel_instance() { if (m_wnd) DestroyWindow(m_wnd); }

	fb2k::hwnd_t get_wnd() override { return m_wnd; }
	void set_configuration(ui_element_config::ptr cfg) override {
		m_state.cfg = parse_config(cfg);
		m_state.relayout();
	}
	ui_element_config::ptr get_configuration() override { return make_config(m_state.cfg); }
	GUID get_guid() override { return guid_panel; }
	GUID get_subclass() override { return ui_element_subclass_utility; }
	ui_element_min_max_info get_min_max_info() override {
		ui_element_min_max_info info;
		info.m_min_width = m_state.min_width();
		info.m_min_height = info.m_max_height = m_state.height();
		return info;
	}
	void notify(const GUID& what, t_size, const void*, t_size) override {
		if (what == ui_element_notify_colors_changed || what == ui_element_notify_font_changed) InvalidateRect(m_wnd, nullptr, TRUE);
	}

private:
	ui_element_instance_callback::ptr m_cb;
	PanelState m_state; // must outlive m_wnd: the dialog keeps a pointer to it
	HWND m_wnd = nullptr;
};

class panel_element : public ui_element_v2 {
public:
	GUID get_guid() override { return guid_panel; }
	GUID get_subclass() override { return ui_element_subclass_utility; }
	void get_name(pfc::string_base& out) override { out = "Slowed & Reverb"; }
	bool get_description(pfc::string_base& out) override {
		out = "Slowed & Reverb on/off switch, presets and live controls for the playback DSP. Right-click it for a compact layout.";
		return true;
	}
	ui_element_instance::ptr instantiate(fb2k::hwnd_t parent, ui_element_config::ptr cfg, ui_element_instance_callback::ptr cb) override {
		return fb2k::service_new<panel_instance>(parent, cfg, cb);
	}
	ui_element_config::ptr get_default_configuration() override { return make_config(PanelConfig()); }
	ui_element_children_enumerator_ptr enumerate_children(ui_element_config::ptr) override { return nullptr; }
	// Lets fb2k add "View > Slowed & Reverb" that opens the panel as a popup window.
	t_uint32 get_flags() override { return KFlagHavePopupCommand; }
	bool bump() override { return false; }
};

service_factory_single_t<panel_element> g_panel_element;

// "All controls..." from the compact layout: a popup that always opens with the full layout, rows unfolded.
ui_element_popup_host::ptr g_allControls;

class all_controls_callback : public ui_element_popup_host_callback {
public:
	void on_resize(t_uint32, t_uint32) override {}
	void on_close() override { if (g_allControls.is_valid()) DestroyWindow(g_allControls->get_wnd()); }
	void on_destroy() override { g_allControls.release(); }
};

void PanelState::show_all() {
	if (g_allControls.is_valid()) { SetForegroundWindow(g_allControls->get_wnd()); return; }
	PanelConfig full; full.expanded = true;
	g_allControls = ui_element_common_methods::get()->spawn_host(core_api::get_main_window(), make_config(full),
		fb2k::service_new<all_controls_callback>(), service_by_guid<ui_element>(guid_panel));
	HWND w = g_allControls->get_wnd();
	if (ui_element_popup_host_v2::ptr v2; v2 &= g_allControls) v2->set_window_title("Slowed & Reverb");
	RECT rc; GetWindowRect(dlg, &rc);
	SetWindowPos(w, nullptr, rc.left, rc.bottom + S(8), S(720), S(480), SWP_NOZORDER | SWP_NOACTIVATE);
	ShowWindow(w, SW_SHOW);
}

// ---------------------------------------------------------------- Columns UI panel (also usable as a toolbar)

class cui_panel : public uie::window {
public:
	const GUID& get_extension_guid() const override { return guid_cui_panel; }
	void get_name(pfc::string_base& out) const override { out = "Slowed & Reverb"; }
	void get_category(pfc::string_base& out) const override { out = "Panels"; }
	bool get_description(pfc::string_base& out) const override {
		out = "Slowed & Reverb on/off switch, presets and live controls. Right-click it for a compact layout.";
		return true;
	}
	unsigned get_type() const override { return uie::type_panel | uie::type_toolbar; }
	const bool get_is_single_instance() const override { return false; }
	bool is_available(const uie::window_host_ptr&) const override { return true; }

	HWND create_or_transfer_window(HWND parent, const uie::window_host_ptr& host, const ui_helpers::window_position_t& pos) override {
		if (m_wnd) {
			ShowWindow(m_wnd, SW_HIDE);
			SetParent(m_wnd, parent);
			m_host->relinquish_ownership(m_wnd);
			m_host = host;
		} else {
			m_host = host;
			m_state.forcedDark = true;
			m_state.on_size_change = [this] { if (m_host.is_valid()) m_host->on_size_limit_change(m_wnd, uie::size_limit_all); };
			m_wnd = create_panel(parent, m_state);
			m_darkNotifier = std::make_unique<cui::colours::dark_mode_notifier>([this] { m_state.set_dark(cui::colours::is_dark_mode_active()); });
		}
		SetWindowPos(m_wnd, nullptr, pos.x, pos.y, pos.cx, pos.cy, SWP_NOZORDER);
		return m_wnd;
	}
	void destroy_window() override {
		m_darkNotifier.reset();
		if (m_wnd) { DestroyWindow(m_wnd); m_wnd = nullptr; }
		m_host.release();
	}
	HWND get_wnd() const override { return m_wnd; }

	void set_config(stream_reader* r, t_size size, abort_callback& abort) override {
		if (size < 8) return;
		uint32_t ver = 0, flags = 0;
		r->read_lendian_t(ver, abort);
		r->read_lendian_t(flags, abort);
		m_state.cfg.set_flags(flags);
		if (m_wnd) m_state.relayout();
	}
	void get_config(stream_writer* w, abort_callback& abort) const override {
		w->write_lendian_t(PanelConfig::kVersion, abort);
		w->write_lendian_t(m_state.cfg.flags(), abort);
	}

private:
	HWND m_wnd = nullptr;
	uie::window_host_ptr m_host;
	PanelState m_state;
	std::unique_ptr<cui::colours::dark_mode_notifier> m_darkNotifier;
};

uie::window_factory<cui_panel> g_cui_panel;

// ---------------------------------------------------------------- Playback menu toggle (assignable to a keyboard shortcut / toolbar)

class mainmenu_toggle : public mainmenu_commands {
public:
	t_uint32 get_command_count() override { return 1; }
	GUID get_command(t_uint32) override { return guid_mm_toggle; }
	void get_name(t_uint32, pfc::string_base& out) override { out = "Enable Slowed & Reverb"; }
	bool get_description(t_uint32, pfc::string_base& out) override { out = "Turns the Slowed & Reverb playback DSP on or off (bypass; it stays in the DSP chain)."; return true; }
	GUID get_parent() override { return mainmenu_groups::playback_etc; }
	bool get_display(t_uint32 idx, pfc::string_base& text, t_uint32& flags) override {
		get_name(idx, text);
		flags = live::is_enabled() ? flag_checked : 0;
		return true;
	}
	void execute(t_uint32, service_ptr_t<service_base>) override { live::set_enabled(!live::is_enabled()); }
};

mainmenu_commands_factory_t<mainmenu_toggle> g_mainmenu_toggle;

}
