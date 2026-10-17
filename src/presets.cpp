// SPDX-License-Identifier: MIT
// Copyright (c) 2026 KimHerV

#include "stdafx.h"
#include "sr_common.h"
#include <cstdio>

// User preset library and the preset combo box / "…" menu / Save behaviour shared by the panel, the DSP popup and the exporter.
// Library changes (save, rename, delete, import) take effect immediately, like a plugin's preset browser:
// cancelling the DSP popup afterwards reverts the DSP settings, not the library.

using namespace sr_fb2k;

namespace {

const GUID guid_cfg_user_presets = { 0x7c3b9e04, 0x1f6a, 0x4d2b, { 0x8e, 0x57, 0x0a, 0xc9, 0x31, 0x6d, 0xf2, 0x8b } };
cfg_string cfg_user_presets(guid_cfg_user_presets, "");

srui::PresetStore& cache() {
	static srui::PresetStore s;
	static bool loaded = false;
	if (!loaded) { s = srui::PresetStore::parse(cfg_user_presets.get().c_str()); loaded = true; }
	return s;
}

std::wstring wide(const char* utf8) { return std::wstring(pfc::stringcvt::string_wide_from_utf8(utf8).get_ptr()); }
pfc::string8 utf8(const wchar_t* w) { return pfc::string8(pfc::stringcvt::string_utf8_from_wide(w).get_ptr()); }

int ask(HWND parent, const char* msg, unsigned flags) {
	return popup_message_v3::get()->messageBox(parent, msg, "Slowed & Reverb", flags);
}

// ---------------------------------------------------------------- name dialog

struct NameDlg {
	const wchar_t* title;
	pfc::string8 name;
	fb2k::CCoreDarkModeHooks dark;
};

INT_PTR CALLBACK NameDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
	NameDlg* ctx = (NameDlg*)GetWindowLongPtr(dlg, DWLP_USER);
	switch (msg) {
	case WM_INITDIALOG:
		SetWindowLongPtr(dlg, DWLP_USER, lp);
		ctx = (NameDlg*)lp;
		SetWindowTextW(dlg, ctx->title);
		SetDlgItemTextW(dlg, IDC_NAME, wide(ctx->name).c_str());
		SendDlgItemMessageW(dlg, IDC_NAME, EM_LIMITTEXT, 64, 0);
		SendDlgItemMessageW(dlg, IDC_NAME, EM_SETSEL, 0, -1);
		ctx->dark.AddDialogWithControls(dlg);
		SetFocus(GetDlgItem(dlg, IDC_NAME));
		return FALSE;
	case WM_COMMAND:
		if (LOWORD(wp) == IDOK) {
			wchar_t buf[128] = {};
			GetDlgItemTextW(dlg, IDC_NAME, buf, 128);
			ctx->name = utf8(buf);
			EndDialog(dlg, IDOK);
			return TRUE;
		}
		if (LOWORD(wp) == IDCANCEL) { EndDialog(dlg, IDCANCEL); return TRUE; }
		break;
	}
	return FALSE;
}

// Asks for a preset name until it is usable; offers to overwrite an existing user preset (unless it is `self`).
bool ask_name(HWND parent, const wchar_t* title, const char* initial, const char* self, std::string& out) {
	NameDlg ctx;
	ctx.title = title;
	ctx.name = initial;
	for (;;) {
		if (DialogBoxParamW(core_api::get_my_instance(), MAKEINTRESOURCEW(IDD_NAME), parent, NameDlgProc, (LPARAM)&ctx) != IDOK) return false;
		const std::string name = srui::cleanName(ctx.name.c_str());
		if (name.empty()) continue;
		if (srui::isFactoryName(name)) {
			pfc::string8 m; m << "\"" << name.c_str() << "\" is a built-in preset. Please choose another name.";
			ask(parent, m, MB_OK | MB_ICONINFORMATION);
			continue;
		}
		if (const srui::Preset* existing = presets::store().find(name)) {
			if (!self || srui::lower(existing->name) != srui::lower(self)) {
				pfc::string8 m; m << "A preset named \"" << existing->name.c_str() << "\" already exists. Replace it?";
				if (ask(parent, m, MB_YESNO | MB_ICONQUESTION) != IDYES) continue;
			}
		}
		out = name;
		return true;
	}
}

// ---------------------------------------------------------------- menu actions

void save_as(HWND owner, presetbar::Host& host) {
	const Preset cur = host.current();
	std::string suggestion = presets::is_user(cur.name) ? cur.name.c_str() : (cur.name.length() ? std::string(cur.name.c_str()) + " (my)" : std::string("My preset"));
	std::string name;
	if (!ask_name(owner, L"Save preset", suggestion.c_str(), nullptr, name)) return;
	srui::PresetStore st = presets::store();
	st.put(name, cur.params);
	presets::save(st);
	host.apply(cur.params, name.c_str());
}

void rename(HWND owner, presetbar::Host& host) {
	const Preset cur = host.current();
	if (!presets::is_user(cur.name)) return;
	std::string name;
	if (!ask_name(owner, L"Rename preset", cur.name, cur.name, name)) return;
	srui::PresetStore st = presets::store();
	if (srui::lower(name) != srui::lower(cur.name.c_str())) st.remove(name); // replacing another preset was confirmed
	st.rename(cur.name.c_str(), name);
	presets::save(st);
	host.apply(cur.params, name.c_str());
}

void remove(HWND owner, presetbar::Host& host) {
	const Preset cur = host.current();
	if (!presets::is_user(cur.name)) return;
	pfc::string8 m; m << "Delete the preset \"" << cur.name << "\"?";
	if (ask(owner, m, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
	srui::PresetStore st = presets::store();
	st.remove(cur.name.c_str());
	presets::save(st);
	host.apply(cur.params, ""); // keep the current sound; it is now unnamed ("Custom")
}

std::string read_file(const char* path) {
	std::string data;
	FILE* f = _wfopen(wide(path).c_str(), L"rb");
	if (!f) return data;
	char buf[4096]; size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0 && data.size() < (1 << 16)) data.append(buf, n);
	fclose(f);
	if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) data.erase(0, 3);
	return data;
}

void import_file(HWND owner, presetbar::Host& host) {
	pfc::string8 path;
	if (!uGetOpenFileName(owner, "Slowed & Reverb preset (*.srpreset)|*.srpreset|All files|*.*", 0, "srpreset", "Import preset", nullptr, path, FALSE)) return;
	srui::Preset p;
	if (!srui::parsePresetFile(read_file(path), p)) {
		ask(owner, "This file is not a Slowed & Reverb preset.", MB_OK | MB_ICONWARNING);
		return;
	}
	std::string name = p.name;
	if (srui::isFactoryName(name)) name += " (imported)";
	if (presets::store().find(name)) {
		pfc::string8 m; m << "A preset named \"" << name.c_str() << "\" already exists. Replace it?";
		if (ask(owner, m, MB_YESNO | MB_ICONQUESTION) != IDYES) {
			if (!ask_name(owner, L"Import preset as", name.c_str(), nullptr, name)) return;
		}
	}
	srui::PresetStore st = presets::store();
	st.put(name, p.params);
	presets::save(st);
	host.apply(p.params, name.c_str());
}

void export_file(HWND owner, presetbar::Host& host) {
	const Preset cur = host.current();
	srui::Preset p;
	p.name = cur.name.length() ? cur.name.c_str() : "My preset";
	p.params = cur.params;
	pfc::string8 path;
	for (char c : p.name) path.add_byte(strchr("\\/:*?\"<>|", c) ? '_' : c);
	path << ".srpreset";
	if (!uGetOpenFileName(owner, "Slowed & Reverb preset (*.srpreset)|*.srpreset", 0, "srpreset", "Export preset", nullptr, path, TRUE)) return;
	FILE* f = _wfopen(wide(path).c_str(), L"wb");
	if (!f) { ask(owner, "Could not write the file.", MB_OK | MB_ICONWARNING); return; }
	const std::string text = srui::formatPresetFile(p);
	fwrite(text.data(), 1, text.size(), f);
	fclose(f);
}

}

// ---------------------------------------------------------------- presets

const srui::PresetStore& presets::store() { return cache(); }

void presets::save(const srui::PresetStore& s) {
	cache() = s;
	cfg_user_presets.set(s.serialize().c_str());
	broadcast();
}

bool presets::lookup(const char* name, sr::Params& out, bool* isUser) {
	if (!name || !*name) return false;
	for (const srui::Preset& f : srui::factoryPresets()) if (srui::lower(f.name) == srui::lower(name)) { out = f.params; if (isUser) *isUser = false; return true; }
	if (const srui::Preset* u = store().find(name)) { out = u->params; if (isUser) *isUser = true; return true; }
	return false;
}

bool presets::is_user(const char* name) {
	sr::Params p; bool user = false;
	return lookup(name, p, &user) && user;
}

bool presets::is_modified(const Preset& p) {
	sr::Params ref;
	return !lookup(p.name, ref) || !srui::sameParams(ref, p.params);
}

std::wstring presets::label(const Preset& p) {
	sr::Params ref;
	if (!lookup(p.name, ref)) return L"Custom";
	return wide(p.name) + (srui::sameParams(ref, p.params) ? L"" : L" *");
}

// ---------------------------------------------------------------- dropdown and Save

void presetbar::save(HWND owner, Host& host) {
	const Preset cur = host.current();
	if (presets::is_user(cur.name)) {
		srui::PresetStore st = presets::store();
		st.put(cur.name.c_str(), cur.params);
		presets::save(st);
		host.apply(cur.params, cur.name);
	} else {
		save_as(owner, host); // factory presets are read-only
	}
}

// Combo box items: factory presets, then user presets; item data says which (kCustom: unnamed settings).
namespace { enum { kCustomItem = -1, kUserBase = 1000 }; }

void presetbar::fill(HWND combo, const Preset& cur) {
	if (SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0)) return; // don't rebuild the list under the user's mouse
	SendMessageW(combo, WM_SETREDRAW, FALSE, 0);
	SendMessageW(combo, CB_RESETCONTENT, 0, 0);
	const bool user = presets::is_user(cur.name);
	const bool modified = presets::is_modified(cur);
	int sel = -1;
	auto add = [&](const std::string& name, int data, bool selected) {
		std::wstring text = wide(name.c_str());
		if (selected && modified) text += L" *";
		const int idx = (int)SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)text.c_str());
		SendMessageW(combo, CB_SETITEMDATA, idx, data);
		if (selected) sel = idx;
	};
	const auto& factory = srui::factoryPresets();
	for (size_t i = 0; i < factory.size(); ++i) add(factory[i].name, (int)i, !user && srui::lower(factory[i].name) == srui::lower(cur.name.c_str()));
	const auto& items = presets::store().items;
	for (size_t i = 0; i < items.size(); ++i) add(items[i].name, kUserBase + (int)i, user && srui::lower(items[i].name) == srui::lower(cur.name.c_str()));
	if (sel < 0) { // settings that don't belong to any preset
		const int idx = (int)SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"Custom");
		SendMessageW(combo, CB_SETITEMDATA, idx, kCustomItem);
		sel = idx;
	}
	SendMessageW(combo, CB_SETCURSEL, sel, 0);
	SendMessageW(combo, WM_SETREDRAW, TRUE, 0);
	InvalidateRect(combo, nullptr, TRUE);
}

void presetbar::on_select(HWND owner, HWND combo, Host& host) {
	const int idx = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
	if (idx < 0) return;
	const int data = (int)SendMessageW(combo, CB_GETITEMDATA, idx, 0);
	const auto& factory = srui::factoryPresets();
	const auto& items = presets::store().items;
	if (data >= 0 && data < (int)factory.size()) host.apply(factory[data].params, factory[data].name.c_str());
	else if (data >= kUserBase && data - kUserBase < (int)items.size()) { const srui::Preset p = items[data - kUserBase]; host.apply(p.params, p.name.c_str()); }
	else fill(combo, host.current()); // "Custom": nothing to load
	(void)owner;
}

void presetbar::show_actions(HWND owner, HWND button, Host& host) {
	const Preset cur = host.current();
	const bool user = presets::is_user(cur.name), modified = presets::is_modified(cur);
	sr::Params ref;
	const bool known = presets::lookup(cur.name, ref);

	enum { kSave = 1, kSaveAs, kRename, kDelete, kRevert, kImport, kExport };
	HMENU m = CreatePopupMenu();
	if (user) AppendMenuW(m, MF_STRING | (modified ? 0 : MF_GRAYED), kSave, (L"Save \"" + wide(cur.name) + L"\"").c_str());
	AppendMenuW(m, MF_STRING, kSaveAs, L"Save as new preset...");
	AppendMenuW(m, MF_STRING | (user ? 0 : MF_GRAYED), kRename, L"Rename...");
	AppendMenuW(m, MF_STRING | (user ? 0 : MF_GRAYED), kDelete, L"Delete...");
	AppendMenuW(m, MF_STRING | (known && modified ? 0 : MF_GRAYED), kRevert, L"Revert changes");
	AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(m, MF_STRING, kImport, L"Import preset file...");
	AppendMenuW(m, MF_STRING, kExport, L"Export current settings...");

	RECT rc; GetWindowRect(button, &rc);
	const int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTALIGN | TPM_TOPALIGN, rc.right, rc.bottom + 2, 0, owner, nullptr);
	DestroyMenu(m);

	switch (cmd) {
	case kSave: save(owner, host); break;
	case kSaveAs: save_as(owner, host); break;
	case kRename: rename(owner, host); break;
	case kDelete: remove(owner, host); break;
	case kRevert: host.apply(ref, cur.name); break;
	case kImport: import_file(owner, host); break;
	case kExport: export_file(owner, host); break;
	}
}
