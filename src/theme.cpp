// SPDX-License-Identifier: MIT
// Copyright (c) 2026 KimHerV

#include "stdafx.h"
#include "sr_common.h"
#include <memory>
#include <objidl.h>
#include <gdiplus.h>

using namespace sr_fb2k;

namespace {

const GUID guid_cfg_custom_accent = { 0x3a8f2e61, 0x5d07, 0x4c9b, { 0xa1, 0x4e, 0x72, 0x0b, 0xd3, 0x95, 0x6c, 0x28 } };
cfg_int cfg_custom_accent(guid_cfg_custom_accent, -1); // -1: follow foobar2000 / Windows, else a COLORREF

ULONG_PTR g_gdiplus = 0;
HWND g_notify = nullptr;

COLORREF blend(COLORREF a, COLORREF b, double t) {
	auto ch = [&](int s) { return (int)std::lround(((a >> s) & 0xFF) * (1 - t) + ((b >> s) & 0xFF) * t); };
	return RGB(ch(0), ch(8), ch(16));
}

// The Windows accent in 8 shades: lighter ones (0-2), the accent itself (3), darker ones (4-6).
bool windows_palette(COLORREF out[8]) {
	BYTE buf[32]; DWORD size = sizeof buf;
	if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", L"AccentPalette",
		RRF_RT_REG_BINARY, nullptr, buf, &size) != ERROR_SUCCESS || size < sizeof buf) return false;
	for (int i = 0; i < 8; ++i) out[i] = RGB(buf[i * 4], buf[i * 4 + 1], buf[i * 4 + 2]);
	return true;
}
bool windows_accent(COLORREF& out) {
	DWORD v = 0, size = sizeof v;
	if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS) return false;
	out = v & 0xFFFFFF; // stored as 0xAABBGGRR, the same byte order as COLORREF
	return true;
}

// In dark mode foobar2000 reports its built-in highlight color as an override; only a different color is the user's choice.
constexpr COLORREF kFb2kDarkHighlight = RGB(0x56, 0x9C, 0xD6);

COLORREF pick_accent(COLORREF bg) {
	const bool dark = srui::luminance(bg) < 0.2;
	std::vector<srui::Rgb> candidates;
	t_ui_color c = 0;
	COLORREF pal[8];
	if (cfg_custom_accent.get() >= 0) {
		candidates.push_back((COLORREF)cfg_custom_accent.get());
	} else if (auto ui = ui_config_manager::tryGet(); ui.is_valid() && ui->query_color(ui_color_highlight, c) && !(dark && c == kFb2kDarkHighlight)) {
		candidates.push_back(c); // the user changed foobar2000's highlight color
	} else if (windows_palette(pal)) {
		// Windows itself uses the lighter shades on dark backgrounds and the darker ones on light backgrounds.
		if (dark) { candidates.push_back(pal[1]); candidates.push_back(pal[0]); }
		else { candidates.push_back(pal[4]); candidates.push_back(pal[5]); }
	} else if (COLORREF base; windows_accent(base)) {
		candidates.push_back(dark ? srui::mix(base, 0.45) : base);
	}
	return srui::readableAccent(candidates, bg);
}

// Hidden top-level window: system-wide color changes are only broadcast to top-level windows.
LRESULT CALLBACK NotifyProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
	switch (msg) {
	case WM_SETTINGCHANGE:
		if (lp && lstrcmpiW((LPCWSTR)lp, L"ImmersiveColorSet") == 0) broadcast();
		break;
	case WM_DWMCOLORIZATIONCOLORCHANGED:
	case WM_SYSCOLORCHANGE:
	case WM_THEMECHANGED:
		broadcast();
		break;
	}
	return DefWindowProcW(wnd, msg, wp, lp);
}

class colors_watcher : public ui_config_callback_impl {
public:
	void ui_colors_changed() override { broadcast(); }
};
std::unique_ptr<colors_watcher> g_colors_watcher;

class sr_initquit : public initquit {
public:
	void on_init() override { theme::init(); }
	void on_quit() override { theme::shutdown(); }
};
service_factory_single_t<sr_initquit> g_initquit;

}

theme::Colors theme::get(HWND wnd) {
	Colors c;
	// Ask the host dialog how it paints static controls: that is its background and text color, dark mode included.
	HDC dc = GetDC(wnd);
	SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
	HBRUSH br = (HBRUSH)SendMessageW(GetParent(wnd), WM_CTLCOLORSTATIC, (WPARAM)dc, (LPARAM)wnd);
	c.bg = GetSysColor(COLOR_BTNFACE);
	LOGBRUSH lb;
	if (br && br == (HBRUSH)GetStockObject(DC_BRUSH)) c.bg = GetDCBrushColor(dc); // what the dark mode hooks return
	else if (br && GetObjectW(br, sizeof lb, &lb) == sizeof lb && lb.lbStyle == BS_SOLID) c.bg = lb.lbColor;
	c.text = GetTextColor(dc);
	ReleaseDC(wnd, dc);

	const bool dark = srui::luminance(c.bg) < 0.2;
	c.dim = blend(c.text, c.bg, 0.38);
	c.track = blend(c.bg, c.text, dark ? 0.17 : 0.2);
	c.border = blend(c.bg, c.text, dark ? 0.26 : 0.3);
	c.field = dark ? blend(c.bg, RGB(0, 0, 0), 0.35) : RGB(255, 255, 255);
	c.accent = pick_accent(c.bg);
	c.onAccent = srui::textOn(c.accent);
	return c;
}

bool theme::has_custom_accent() { return cfg_custom_accent.get() >= 0; }
COLORREF theme::custom_accent() { return has_custom_accent() ? (COLORREF)cfg_custom_accent.get() : RGB(0x4C, 0xC2, 0xFF); }
void theme::set_custom_accent(bool on, COLORREF c) {
	cfg_custom_accent = on ? (t_int64)(c & 0xFFFFFF) : -1;
	broadcast();
}

void theme::init() {
	if (!g_gdiplus) {
		Gdiplus::GdiplusStartupInput in;
		Gdiplus::GdiplusStartup(&g_gdiplus, &in, nullptr);
	}
	if (!g_notify) {
		WNDCLASSW wc = {};
		wc.lpfnWndProc = NotifyProc;
		wc.hInstance = core_api::get_my_instance();
		wc.lpszClassName = L"SlowedReverbThemeWatcher";
		RegisterClassW(&wc);
		g_notify = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
	}
	if (!g_colors_watcher) g_colors_watcher = std::make_unique<colors_watcher>();
}

void theme::shutdown() {
	g_colors_watcher.reset();
	if (g_notify) { DestroyWindow(g_notify); g_notify = nullptr; }
	if (g_gdiplus) { Gdiplus::GdiplusShutdown(g_gdiplus); g_gdiplus = 0; }
}
