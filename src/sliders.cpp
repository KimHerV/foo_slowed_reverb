// SPDX-License-Identifier: MIT
// Copyright (c) 2026 KimHerV

#include "stdafx.h"
#include "sr_common.h"
#include <objidl.h>
#include <gdiplus.h>

// Slider rows built from native controls: a label, a trackbar whose positions are the ticks (so dragging,
// arrow keys, Page Up/Down and the mouse wheel all snap natively), and a value text that can be double-clicked
// to type an exact value. Windows has no dark trackbar, so in dark mode the track, ticks and thumb are painted
// through the trackbar's custom-draw notifications; in light mode it is drawn entirely by Windows.

using namespace sr_fb2k;
namespace G = Gdiplus;

namespace {

int track_pos(int p, float v) {
	const srui::ParamDef& d = srui::def(p);
	return (int)std::lround((srui::clampTo(p, v) - d.min) / d.step);
}
float pos_value(int p, int pos) {
	const srui::ParamDef& d = srui::def(p);
	return srui::clampTo(p, d.min + pos * d.step);
}

COLORREF blend(COLORREF a, COLORREF b, double t) {
	auto ch = [&](int s) { return (int)std::lround(((a >> s) & 0xFF) * (1 - t) + ((b >> s) & 0xFF) * t); };
	return RGB(ch(0), ch(8), ch(16));
}
G::Color gc(COLORREF c) { return G::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)); }

LRESULT CALLBACK EditProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
	HWND dlg = (HWND)ref;
	switch (msg) {
	case WM_GETDLGCODE: return DefSubclassProc(wnd, msg, wp, lp) | DLGC_WANTALLKEYS;
	case WM_KEYDOWN:
		if (wp == VK_RETURN) { PostMessageW(dlg, sliders::WM_SR_COMMIT_EDIT, 1, 0); return 0; }
		if (wp == VK_ESCAPE) { PostMessageW(dlg, sliders::WM_SR_COMMIT_EDIT, 0, 0); return 0; }
		break;
	case WM_CHAR:
		if (wp == VK_RETURN || wp == VK_ESCAPE) return 0; // no beep
		break;
	case WM_KILLFOCUS:
		PostMessageW(dlg, sliders::WM_SR_COMMIT_EDIT, 1, 0);
		break;
	case WM_NCDESTROY:
		RemoveWindowSubclass(wnd, EditProc, 0);
		break;
	}
	return DefSubclassProc(wnd, msg, wp, lp);
}

}

int sliders::scale(HWND wnd, int px96) {
	static auto getDpi = (UINT(WINAPI*)(HWND))GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
	UINT dpi = getDpi ? getDpi(wnd) : 96;
	return MulDiv(px96, dpi ? (int)dpi : 96, 96);
}

void sliders::Block::attach(HWND dlg) {
	m_dlg = dlg;
	for (int p = 0; p < srui::kParamCount; ++p) {
		HWND t = GetDlgItem(dlg, IDC_SL_TRACK + p);
		SendMessageW(t, TBM_SETRANGE, FALSE, MAKELPARAM(0, srui::tickCount(p) - 1));
		SendMessageW(t, TBM_SETTICFREQ, 1, 0);
		SendMessageW(t, TBM_SETLINESIZE, 0, 1);
		SendMessageW(t, TBM_SETPAGESIZE, 0, 2);
	}
	set_params(m_params);
}

void sliders::Block::update_text(int p) {
	const float v = srui::get(m_params, p);
	std::wstring text = srui::format(p, v);
	if (!m_short) {
		const std::wstring extra = srui::detail(p, v);
		if (!extra.empty()) text += L"  (" + extra + L")";
	}
	SetDlgItemTextW(m_dlg, IDC_SL_VALUE + p, text.c_str());
}

void sliders::Block::set_params(const sr::Params& p) {
	if (m_dragging) return; // don't fight the user's drag with echoes of it
	m_params = p;
	if (!m_dlg) return;
	for (int i = 0; i < srui::kParamCount; ++i) {
		m_pos[i] = track_pos(i, srui::get(m_params, i));
		SendDlgItemMessageW(m_dlg, IDC_SL_TRACK + i, TBM_SETPOS, TRUE, m_pos[i]);
		InvalidateRect(GetDlgItem(m_dlg, IDC_SL_TRACK + i), nullptr, TRUE); // our round thumb is wider than the area Windows repaints
		update_text(i);
	}
}

void sliders::Block::set_short_values(bool on) {
	m_short = on;
	if (m_dlg) for (int i = 0; i < srui::kParamCount; ++i) update_text(i);
}

void sliders::Block::begin_edit(int p) {
	end_edit(true);
	HWND value = GetDlgItem(m_dlg, IDC_SL_VALUE + p);
	RECT r; GetWindowRect(value, &r);
	MapWindowPoints(HWND_DESKTOP, m_dlg, (POINT*)&r, 2);
	const int h = scale(m_dlg, 24), cy = (r.top + r.bottom) / 2, w = (std::max)((int)(r.right - r.left) / 2, scale(m_dlg, 80));
	m_edit = CreateWindowExW(0, L"EDIT", srui::editText(p, srui::get(m_params, p)).c_str(),
		WS_CHILD | WS_VISIBLE | WS_BORDER | ES_RIGHT | ES_AUTOHSCROLL, r.right - w, cy - h / 2, w, h, m_dlg, nullptr, core_api::get_my_instance(), nullptr);
	SendMessageW(m_edit, WM_SETFONT, SendMessageW(m_dlg, WM_GETFONT, 0, 0), FALSE);
	if (on_new_control) on_new_control(m_edit);
	SetWindowSubclass(m_edit, EditProc, 0, (DWORD_PTR)m_dlg);
	SetWindowPos(m_edit, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	ShowWindow(value, SW_HIDE);
	m_editParam = p;
	SendMessageW(m_edit, EM_SETSEL, 0, -1);
	SetFocus(m_edit);
}

void sliders::Block::end_edit(bool apply) {
	if (!m_edit) return;
	HWND edit = m_edit;
	m_edit = nullptr; // first, so the focus change below doesn't commit twice
	const int p = m_editParam;
	m_editParam = -1;
	const int len = GetWindowTextLengthW(edit);
	std::wstring text(len + 1, L'\0');
	GetWindowTextW(edit, text.data(), len + 1);
	text.resize(len);
	const bool hadFocus = GetFocus() == edit;
	DestroyWindow(edit);
	ShowWindow(GetDlgItem(m_dlg, IDC_SL_VALUE + p), SW_SHOW);
	if (hadFocus) SetFocus(GetDlgItem(m_dlg, IDC_SL_TRACK + p));
	float v;
	if (!apply) return;
	if (!srui::parse(p, text, v)) { MessageBeep(MB_OK); return; }
	srui::set(m_params, p, v);
	m_pos[p] = track_pos(p, v);
	SendDlgItemMessageW(m_dlg, IDC_SL_TRACK + p, TBM_SETPOS, TRUE, m_pos[p]);
	InvalidateRect(GetDlgItem(m_dlg, IDC_SL_TRACK + p), nullptr, TRUE);
	update_text(p);
	if (on_event) on_event(kChanged);
}

void sliders::Block::cancel_edit() { end_edit(false); }

bool sliders::Block::custom_draw(NMCUSTOMDRAW* cd, LRESULT& result) {
	HWND t = cd->hdr.hwndFrom;
	if (cd->dwDrawStage == CDDS_PREPAINT) {
		m_colors = theme::get(t);
		m_darkPaint = srui::luminance(m_colors.bg) < 0.2;
		if (m_darkPaint) { // our round thumb is taller than the thumb rect Windows clears when it moves
			HBRUSH bg = CreateSolidBrush(m_colors.bg);
			FillRect(cd->hdc, &cd->rc, bg);
			DeleteObject(bg);
		}
		result = m_darkPaint ? CDRF_NOTIFYITEMDRAW : CDRF_DODEFAULT;
		return true;
	}
	if (cd->dwDrawStage != CDDS_ITEMPREPAINT || !m_darkPaint) { result = CDRF_DODEFAULT; return true; }

	const theme::Colors& c = m_colors;
	G::Graphics g(cd->hdc);
	RECT client; GetClientRect(t, &client);
	g.SetClip(G::Rect(0, 0, client.right, client.bottom)); // the trackbar paints with its parent's clipping; stay inside it
	g.SetSmoothingMode(G::SmoothingModeAntiAlias);
	g.SetPixelOffsetMode(G::PixelOffsetModeHalf);
	RECT ch; SendMessageW(t, TBM_GETCHANNELRECT, 0, (LPARAM)&ch);
	RECT th; SendMessageW(t, TBM_GETTHUMBRECT, 0, (LPARAM)&th);
	const float cy = (ch.top + ch.bottom) / 2.0f, thumbX = (th.left + th.right) / 2.0f;
	const float half = (th.right - th.left) / 2.0f;

	switch (cd->dwItemSpec) {
	case TBCD_CHANNEL: {
		const float w = (float)scale(t, 4), x0 = ch.left + half, x1 = ch.right - half;
		G::Pen track(gc(c.track), w); track.SetStartCap(G::LineCapRound); track.SetEndCap(G::LineCapRound);
		g.DrawLine(&track, x0, cy, x1, cy);
		if (thumbX > x0) {
			G::Pen fill(gc(c.accent), w); fill.SetStartCap(G::LineCapRound); fill.SetEndCap(G::LineCapRound);
			g.DrawLine(&fill, x0, cy, thumbX, cy);
		}
		break;
	}
	case TBCD_TICS: {
		// Interior tick positions come from the control; the two end ticks continue the same spacing.
		const int n = (int)SendMessageW(t, TBM_GETNUMTICS, 0, 0);
		if (n >= 4) {
			const float first = (float)SendMessageW(t, TBM_GETTICPOS, 0, 0), last = (float)SendMessageW(t, TBM_GETTICPOS, n - 3, 0);
			const float step = (last - first) / (n - 3);
			RECT cr; GetClientRect(t, &cr);
			const float y = (float)(std::min)((int)cr.bottom - scale(t, 5), (int)th.bottom + scale(t, 2));
			G::SolidBrush tick(gc(c.border));
			for (int k = 0; k < n; ++k) g.FillRectangle(&tick, first + (k - 1) * step - 0.75f, y, 1.5f, (G::REAL)scale(t, 4));
		}
		break;
	}
	case TBCD_THUMB: {
		// Stay inside the native thumb rect vertically: the trackbar only repaints that band when the thumb moves,
		// so anything drawn above it would be left behind at the old position.
		const float r = (std::min)((float)scale(t, 9), cy - th.top);
		const bool pressed = (cd->uItemState & CDIS_SELECTED) != 0;
		G::SolidBrush ring(gc(blend(c.bg, RGB(255, 255, 255), 0.17)));
		g.FillEllipse(&ring, thumbX - r, cy - r, 2 * r, 2 * r);
		G::Pen edge(gc(c.border), 1);
		g.DrawEllipse(&edge, thumbX - r + 0.5f, cy - r + 0.5f, 2 * r - 1, 2 * r - 1);
		const float dot = (float)scale(t, pressed ? 4 : 5);
		G::SolidBrush acc(gc(c.accent));
		g.FillEllipse(&acc, thumbX - dot, cy - dot, 2 * dot, 2 * dot);
		break;
	}
	default:
		result = CDRF_DODEFAULT;
		return true;
	}
	result = CDRF_SKIPDEFAULT;
	return true;
}

bool sliders::Block::handle(UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
	result = 0;
	switch (msg) {
	case WM_HSCROLL: {
		const int p = GetDlgCtrlID((HWND)lp) - IDC_SL_TRACK;
		if (!lp || p < 0 || p >= srui::kParamCount) return false;
		const int code = LOWORD(wp);
		if (code == TB_THUMBTRACK && !m_dragging) { m_dragging = true; if (on_event) on_event(kDragBegin); }
		const int pos = (int)SendMessageW((HWND)lp, TBM_GETPOS, 0, 0);
		InvalidateRect((HWND)lp, nullptr, TRUE);
		if (pos != m_pos[p]) {
			m_pos[p] = pos;
			srui::set(m_params, p, pos_value(p, pos));
			update_text(p);
			if (on_event) on_event(kChanged);
		}
		if (code == TB_ENDTRACK && m_dragging) { m_dragging = false; if (on_event) on_event(kDragEnd); }
		return true;
	}
	case WM_COMMAND: {
		const int p = LOWORD(wp) - IDC_SL_VALUE;
		if (p < 0 || p >= srui::kParamCount || HIWORD(wp) != STN_DBLCLK) return false;
		begin_edit(p);
		return true;
	}
	case WM_NOTIFY: {
		NMHDR* h = (NMHDR*)lp;
		const int p = (int)h->idFrom - IDC_SL_TRACK;
		if (h->code != NM_CUSTOMDRAW || p < 0 || p >= srui::kParamCount) return false;
		return custom_draw((NMCUSTOMDRAW*)lp, result);
	}
	default:
		if (msg == WM_SR_COMMIT_EDIT) { end_edit(wp != 0); return true; }
		return false;
	}
}
