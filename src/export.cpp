// SPDX-License-Identifier: MIT
// Copyright (c) 2026 KimHerV

#include "stdafx.h"
#include "sr_common.h"
#include <set>
#include <atomic>

using namespace sr_fb2k;

namespace {

// ---------------------------------------------------------------- settings

const GUID guid_cfg_params = { 0xaa9909e8, 0x22b4, 0x4d28, { 0x8a, 0xd2, 0x6d, 0x7a, 0x6b, 0x4d, 0xcf, 0x03 } };
const GUID guid_cfg_format = { 0xb01a38b0, 0x0a90, 0x4dc6, { 0x89, 0x9e, 0xb9, 0xd1, 0xda, 0xdc, 0xe9, 0x53 } };
const GUID guid_cfg_outdir = { 0x52273433, 0xce84, 0x4aac, { 0x8d, 0x00, 0x63, 0x84, 0x78, 0xcc, 0x3f, 0x02 } };
const GUID guid_cfg_pattern = { 0x302896bc, 0xdbac, 0x477a, { 0x8e, 0x0a, 0xb1, 0x1a, 0x0a, 0xef, 0x48, 0xc1 } };
const GUID guid_cfg_ffmpeg = { 0x30ef52f6, 0x973c, 0x4870, { 0x9b, 0xae, 0x3e, 0x12, 0x91, 0x22, 0x63, 0x50 } };

const GUID guid_cfg_preset_name = { 0x5e2c8b17, 0x94d3, 0x4a60, { 0xb7, 0x2f, 0x18, 0xe6, 0x0d, 0x4a, 0x93, 0xc5 } };
const GUID guid_cfg_suffix = { 0x8d41f6a2, 0x3c7e, 0x4b95, { 0xa1, 0x60, 0x2e, 0xd9, 0x57, 0xb3, 0x0c, 0x84 } };

const char* const kDefaultPattern = "[%artist% - ]%title% '(Slowed & Reverb)'";
const char* const kOldDefaultPattern = "[%artist% - ]%title% '(slowed & reverb)'"; // pre-release builds

cfg_string cfg_params(guid_cfg_params, "");
cfg_string cfg_preset_name(guid_cfg_preset_name, "");
cfg_int cfg_format(guid_cfg_format, 4); // WAV: works without ffmpeg
cfg_string cfg_outdir(guid_cfg_outdir, "");
cfg_string cfg_pattern(guid_cfg_pattern, kDefaultPattern);
cfg_string cfg_ffmpeg(guid_cfg_ffmpeg, "");
cfg_string cfg_suffix(guid_cfg_suffix, "(Slowed & Reverb)");

struct Format { const wchar_t* label; const char* ext; const char* codecArgs; bool cover; };
const Format kFormats[] = {
	{ L"MP3 (320 kbps)", "mp3", "-c:a libmp3lame -b:a 320k -id3v2_version 3", true },
	{ L"FLAC (lossless, 24-bit)", "flac", "-c:a flac -sample_fmt s32 -bits_per_raw_sample 24", true },
	{ L"OGG Vorbis (q6)", "ogg", "-c:a libvorbis -q:a 6", false },
	{ L"M4A / AAC (256 kbps)", "m4a", "-c:a aac -b:a 256k", true },
	{ L"WAV (32-bit float, no ffmpeg needed)", "wav", nullptr, false },
};
const int kFormatCount = (int)(sizeof(kFormats) / sizeof(kFormats[0]));

// The exporter keeps its own settings; the first time it starts from the playback settings.
Preset load_settings() {
	Preset p;
	pfc::string8 s = cfg_params.get();
	if (s.length() && parse_params_string(s, p.params)) p.name = cfg_preset_name.get();
	else p = live::get();
	p.params.sanitize();
	guess_name(p);
	return p;
}
void save_settings(const Preset& p) {
	pfc::string8 s;
	format_params_string(p.params, s);
	cfg_params.set(s);
	cfg_preset_name.set(p.name);
}

std::wstring wide(const char* utf8) { return std::wstring(pfc::stringcvt::string_wide_from_utf8(utf8).get_ptr()); }
pfc::string8 utf8(const wchar_t* w) { return pfc::string8(pfc::stringcvt::string_utf8_from_wide(w).get_ptr()); }

bool file_exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

// Explicit path if set, else PATH, else the WinGet links folder.
std::wstring find_ffmpeg(const char* configured) {
	if (configured && *configured) {
		std::wstring w = wide(configured);
		return file_exists(w) ? w : std::wstring();
	}
	wchar_t buf[MAX_PATH];
	if (SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, buf, nullptr)) return buf;
	if (GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH)) {
		std::wstring w = std::wstring(buf) + L"\\Microsoft\\WinGet\\Links\\ffmpeg.exe";
		if (file_exists(w)) return w;
	}
	return std::wstring();
}

// ---------------------------------------------------------------- float WAV writer

class WavWriter {
public:
	~WavWriter() { if (m_h != INVALID_HANDLE_VALUE) CloseHandle(m_h); }
	void open(const std::wstring& path, unsigned srate, unsigned ch, unsigned mask) {
		m_srate = srate; m_ch = ch; m_mask = mask;
		m_h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (m_h == INVALID_HANDLE_VALUE) throw exception_io("Cannot create output file");
		writeHeader();
	}
	void write(const audio_sample* data, size_t samples) {
		m_conv.resize(samples);
		for (size_t i = 0; i < samples; ++i) m_conv[i] = (float)data[i];
		const BYTE* p = (const BYTE*)m_conv.data();
		size_t bytes = samples * sizeof(float);
		while (bytes) {
			DWORD n = (DWORD)(std::min<size_t>)(bytes, 1 << 24), done = 0;
			if (!WriteFile(m_h, p, n, &done, nullptr) || done != n) throw exception_io("Write failed (disk full?)");
			p += n; bytes -= n; m_dataBytes += n;
		}
	}
	void finish() {
		if (m_dataBytes > 0xFFFFFFF0ull - 80) throw exception_io("Output exceeds the 4 GB WAV limit");
		SetFilePointer(m_h, 0, nullptr, FILE_BEGIN);
		writeHeader();
		CloseHandle(m_h); m_h = INVALID_HANDLE_VALUE;
	}
private:
	void put(std::vector<BYTE>& b, const void* d, size_t n) { b.insert(b.end(), (const BYTE*)d, (const BYTE*)d + n); }
	template<typename T> void put(std::vector<BYTE>& b, T v) { put(b, &v, sizeof(v)); }
	void writeHeader() {
		const bool ext = m_ch > 2;
		std::vector<BYTE> h;
		const uint32_t fmtLen = ext ? 40 : 16;
		const uint32_t frames = (uint32_t)(m_dataBytes / (4ull * m_ch));
		put(h, "RIFF", 4); put<uint32_t>(h, (uint32_t)(4 + 8 + fmtLen + 12 + 8 + m_dataBytes)); put(h, "WAVE", 4);
		put(h, "fmt ", 4); put<uint32_t>(h, fmtLen);
		put<uint16_t>(h, ext ? 0xFFFE : 3); put<uint16_t>(h, (uint16_t)m_ch); put<uint32_t>(h, m_srate);
		put<uint32_t>(h, m_srate * m_ch * 4); put<uint16_t>(h, (uint16_t)(m_ch * 4)); put<uint16_t>(h, 32);
		if (ext) {
			static const BYTE subtypeFloat[16] = { 3, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71 };
			put<uint16_t>(h, 22); put<uint16_t>(h, 32); put<uint32_t>(h, m_mask); put(h, subtypeFloat, 16);
		}
		put(h, "fact", 4); put<uint32_t>(h, 4); put<uint32_t>(h, frames);
		put(h, "data", 4); put<uint32_t>(h, (uint32_t)m_dataBytes);
		DWORD done = 0;
		if (!WriteFile(m_h, h.data(), (DWORD)h.size(), &done, nullptr) || done != h.size()) throw exception_io("Write failed");
	}
	HANDLE m_h = INVALID_HANDLE_VALUE;
	unsigned m_srate = 0, m_ch = 0, m_mask = 0;
	uint64_t m_dataBytes = 0;
	std::vector<float> m_conv;
};

// ---------------------------------------------------------------- ffmpeg

// Quotes one argument per the MSVC CRT command-line rules.
void append_arg(std::wstring& cmd, const std::wstring& a) {
	if (!cmd.empty()) cmd += L' ';
	if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) { cmd += a; return; }
	cmd += L'"';
	size_t slashes = 0;
	for (wchar_t c : a) {
		if (c == L'\\') { ++slashes; continue; }
		if (c == L'"') cmd.append(slashes * 2 + 1, L'\\');
		else cmd.append(slashes, L'\\');
		slashes = 0;
		cmd += c;
	}
	cmd.append(slashes * 2, L'\\');
	cmd += L'"';
}

std::wstring temp_path(const wchar_t* suffix) {
	wchar_t dir[MAX_PATH];
	GetTempPathW(MAX_PATH, dir);
	wchar_t name[64];
	swprintf_s(name, L"foo_slowed_reverb_%lu_%llu%s", GetCurrentProcessId(), (unsigned long long)GetTickCount64(), suffix);
	static std::atomic<unsigned> counter{ 0 };
	return std::wstring(dir) + std::to_wstring(counter++) + L"_" + name;
}

// Runs ffmpeg hidden; returns true on exit code 0. stderr goes to errOut on failure.
bool run_ffmpeg(const std::wstring& exe, const std::vector<std::wstring>& args, abort_callback& abort, pfc::string8& errOut) {
	std::wstring cmd;
	append_arg(cmd, exe);
	for (const auto& a : args) append_arg(cmd, a);

	const std::wstring logPath = temp_path(L".log");
	SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
	HANDLE log = CreateFileW(logPath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);

	STARTUPINFOW si = { sizeof(si) };
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = nullptr;
	si.hStdOutput = log;
	si.hStdError = log;
	PROCESS_INFORMATION pi = {};
	std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end()); cmdBuf.push_back(0);
	if (!CreateProcessW(exe.c_str(), cmdBuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
		if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
		errOut = "Could not start ffmpeg.exe";
		return false;
	}
	CloseHandle(pi.hThread);
	while (WaitForSingleObject(pi.hProcess, 100) == WAIT_TIMEOUT) {
		if (abort.is_aborting()) {
			TerminateProcess(pi.hProcess, 1);
			WaitForSingleObject(pi.hProcess, 5000);
			CloseHandle(pi.hProcess);
			if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
			throw exception_aborted();
		}
	}
	DWORD code = 1;
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hProcess);

	if (code != 0 && log != INVALID_HANDLE_VALUE) {
		std::string text;
		char buf[4096]; DWORD n = 0;
		SetFilePointer(log, 0, nullptr, FILE_BEGIN);
		while (ReadFile(log, buf, sizeof(buf), &n, nullptr) && n) text.append(buf, n);
		if (text.size() > 1500) text = "..." + text.substr(text.size() - 1500);
		errOut = text.c_str();
	}
	if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
	return code == 0;
}

// ---------------------------------------------------------------- export job

struct JobItem {
	metadb_handle_ptr handle;
	pfc::string8 displayName;
	std::wstring sourceNative;   // empty when the track is not a local file
	std::wstring outPath;
	std::vector<std::pair<std::string, std::string>> meta; // ffmpeg key -> value
	file_info_impl sourceInfo;   // for formats tagged by foobar2000 itself (WAV)
	std::string title;           // with the suffix
};

struct JobSettings {
	sr::Params params;
	int format = 0;
	std::wstring ffmpeg;
};

class ExportProcess : public threaded_process_callback {
public:
	ExportProcess(std::vector<JobItem>&& items, const JobSettings& s) : m_items(std::move(items)), m_s(s) {}

	void run(threaded_process_status& status, abort_callback& abort) override {
		for (size_t i = 0; i < m_items.size(); ++i) {
			const JobItem& item = m_items[i];
			status.set_progress(i, m_items.size());
			status.set_progress_secondary(0);
			status.set_item_path(item.handle->get_path());
			try {
				exportOne(item, status, abort);
				m_done.push_back(utf8(item.outPath.c_str()));
			} catch (exception_aborted const&) {
				m_aborted = true;
				return;
			} catch (std::exception const& e) {
				pfc::string8 msg; msg << item.displayName << "\n    " << e.what();
				m_failed.push_back(msg);
			}
		}
	}

	void on_done(ctx_t, bool wasAborted) override {
		pfc::string8 msg;
		if (wasAborted || m_aborted) msg << "Export was cancelled.\n\n";
		msg << "Exported: " << m_done.size();
		if (!m_failed.empty()) msg << ", failed: " << m_failed.size();
		msg << "\n";
		for (auto& p : m_done) msg << "\n  " << p;
		if (!m_untagged.empty()) {
			msg << "\n\nExported, but the tags could not be written:";
			for (auto& f : m_untagged) msg << "\n  " << f;
		}
		if (!m_failed.empty()) {
			msg << "\n\nFailed:";
			for (auto& f : m_failed) msg << "\n  " << f;
		}
		popup_message::g_show(msg, "Slowed & Reverb export");
	}

private:
	void exportOne(const JobItem& item, threaded_process_status& status, abort_callback& abort) {
		const Format& fmt = kFormats[m_s.format];
		const bool direct = fmt.codecArgs == nullptr;
		const std::wstring wavPath = direct ? item.outPath : temp_path(L".wav");
		struct TempGuard {
			std::wstring path; bool active;
			~TempGuard() { if (active) DeleteFileW(path.c_str()); }
		} tempGuard{ wavPath, true }; // removes the temp file, or the partial output on failure

		input_decoder::ptr dec;
		input_entry::g_open_for_decoding(dec, nullptr, item.handle->get_path(), abort);
		const t_uint32 subsong = item.handle->get_subsong_index();
		dec->initialize(subsong, input_flag_no_seeking | input_flag_no_looping, abort);
		double length = 0;
		{
			file_info_impl info;
			dec->get_info(subsong, info, abort);
			length = info.get_length();
		}

		sr::Processor proc;
		WavWriter wav;
		std::vector<audio_sample> out;
		unsigned srate = 0, ch = 0;
		audio_sample peak = 0;
		double decoded = 0;
		auto emit = [&] {
			for (audio_sample v : out) { const audio_sample a = std::fabs(v); if (a > peak) peak = a; }
			wav.write(out.data(), out.size());
		};

		audio_chunk_impl chunk;
		while (dec->run(chunk, abort)) {
			if (ch == 0) {
				srate = chunk.get_srate(); ch = chunk.get_channels();
				proc.setup(srate, ch, m_s.params);
				wav.open(wavPath, srate, ch, chunk.get_channel_config());
			} else if (chunk.get_srate() != srate || chunk.get_channels() != ch) {
				throw exception_io_data("Sample rate or channel count changes mid-track; not supported");
			}
			proc.process(chunk.get_data(), chunk.get_sample_count(), out);
			emit();
			decoded += chunk.get_duration();
			if (length > 0) status.set_progress_secondary_float((std::min)(1.0, decoded / length) * (direct ? 1.0 : 0.8));
			abort.check();
		}
		if (ch == 0) throw exception_io_data("No audio decoded");
		proc.drain(out);
		emit();
		wav.finish();
		dec.release();

		if (direct) {
			tempGuard.active = false;
			tagOutput(item, abort);
			return;
		}

		// Encode with ffmpeg. Tags come from foobar2000; cover art is copied from the source file when ffmpeg can read it.
		std::vector<std::wstring> base = { L"-hide_banner", L"-nostdin", L"-y", L"-loglevel", L"error", L"-i", wavPath };
		std::vector<std::wstring> tail = { L"-map", L"0:a" };
		const audio_sample target = (audio_sample)0.97; // reverb can push peaks past full scale; keep headroom for the encoder
		if (peak > target) {
			wchar_t vol[64]; swprintf_s(vol, L"volume=%.6f", (double)(target / peak));
			tail.insert(tail.end(), { L"-af", vol });
		}
		{
			pfc::string8 a = fmt.codecArgs;
			pfc::string_list_impl parts;
			pfc::splitStringSimple_toList(parts, ' ', a);
			for (t_size k = 0; k < parts.get_count(); ++k) tail.push_back(wide(parts[k]));
		}
		tail.insert(tail.end(), { L"-map_metadata", L"-1" });
		for (auto& kv : item.meta) {
			tail.push_back(L"-metadata");
			tail.push_back(wide((kv.first + "=" + kv.second).c_str()));
		}
		tail.push_back(item.outPath);

		status.set_progress_secondary_float(0.85);
		pfc::string8 err;
		bool ok = false;
		if (fmt.cover && !item.sourceNative.empty()) {
			std::vector<std::wstring> args = base;
			args.insert(args.end(), { L"-i", item.sourceNative });
			args.insert(args.end(), tail.begin(), tail.begin() + 2); // -map 0:a
			args.insert(args.end(), { L"-map", L"1:v?", L"-c:v", L"copy", L"-disposition:v:0", L"attached_pic" });
			args.insert(args.end(), tail.begin() + 2, tail.end());
			ok = run_ffmpeg(m_s.ffmpeg, args, abort, err);
		}
		if (!ok) { // no cover, or ffmpeg could not use the source's picture stream
			std::vector<std::wstring> args = base;
			args.insert(args.end(), tail.begin(), tail.end());
			ok = run_ffmpeg(m_s.ffmpeg, args, abort, err);
		}
		if (!ok) {
			DeleteFileW(item.outPath.c_str());
			pfc::string8 msg; msg << "ffmpeg failed: " << err;
			throw exception_io(msg.c_str());
		}
	}

	// Tags and cover art through foobar2000's own tag writers, for formats ffmpeg does not handle here.
	// A failure keeps the audio and is reported separately.
	void tagOutput(const JobItem& item, abort_callback& abort) {
		pfc::string8 path;
		filesystem::g_get_canonical_path(utf8(item.outPath.c_str()), path);
		try {
			input_info_writer::ptr w;
			input_entry::g_open_for_info_write(w, nullptr, path, abort);
			file_info_impl info;
			w->get_info(0, info, abort);
			info.copy_meta(item.sourceInfo);
			info.meta_set("title", item.title.c_str());
			w->set_info(0, info, abort);
			w->commit(abort);
		} catch (exception_aborted const&) {
			throw;
		} catch (std::exception const&) {
			m_untagged.push_back(utf8(item.outPath.c_str()));
			return;
		}
		try { // cover art is a bonus: skip quietly when the source has none or the format cannot hold it
			album_art_data::ptr art;
			auto src = album_art_extractor::g_open_allowempty(nullptr, item.handle->get_path(), abort);
			if (src.is_valid() && src->query(album_art_ids::cover_front, art, abort)) {
				auto dst = album_art_editor::g_open(nullptr, path, abort);
				dst->set(album_art_ids::cover_front, art, abort);
				dst->commit(abort);
			}
		} catch (exception_aborted const&) {
			throw;
		} catch (std::exception const&) {
		}
	}

	std::vector<JobItem> m_items;
	JobSettings m_s;
	std::vector<pfc::string8> m_done, m_failed, m_untagged;
	bool m_aborted = false;
};

// ---------------------------------------------------------------- preparing items (main thread)

pfc::string8 sanitize_filename(const char* in) {
	pfc::string8 out;
	for (const char* p = in; *p; ++p) {
		const char c = *p;
		if ((unsigned char)c < 32 || strchr("\\/:*?\"<>|", c)) out.add_byte('_');
		else out.add_byte(c);
	}
	// Windows refuses trailing dots/spaces.
	while (out.length() && (out[out.length() - 1] == '.' || out[out.length() - 1] == ' ')) out.truncate(out.length() - 1);
	if (out.length() == 0) out = "output";
	return out;
}

std::string join_meta(const file_info& info, const char* name) {
	const t_size idx = info.meta_find(name);
	if (idx == pfc_infinite) return std::string();
	std::string v;
	for (t_size k = 0; k < info.meta_enum_value_count(idx); ++k) {
		if (k) v += "; ";
		v += info.meta_enum_value(idx, k);
	}
	return v;
}

bool prepare_items(metadb_handle_list_cref data, const pfc::string8& outDirCfg, const pfc::string8& pattern, const pfc::string8& suffix,
	int format, std::vector<JobItem>& items, pfc::string8& error) {
	titleformat_object::ptr script;
	titleformat_compiler::get()->compile_safe(script, pattern.length() ? pattern.c_str() : "%filename% '(Slowed & Reverb)'");
	std::set<std::wstring> reserved;
	const char* ext = kFormats[format].ext;

	for (t_size i = 0; i < data.get_count(); ++i) {
		const metadb_handle_ptr& h = data[i];
		JobItem item;
		item.handle = h;

		pfc::string8 native;
		if (filesystem::g_get_native_path(h->get_path(), native)) item.sourceNative = wide(native);

		pfc::string8 dir = outDirCfg;
		if (dir.length() == 0) {
			if (native.length() == 0) {
				error << "Some tracks are not local files. Please choose an output folder:\n" << h->get_path();
				return false;
			}
			dir = pfc::string_directory(native);
		}
		std::wstring wdir = wide(dir);
		if (!wdir.empty() && wdir.back() != L'\\' && wdir.back() != L'/') wdir += L'\\';

		pfc::string8 name;
		h->format_title(nullptr, name, script, nullptr);
		name = sanitize_filename(name);
		item.displayName = name;

		std::wstring stem = wdir + wide(name), path = stem + L"." + wide(ext);
		for (int n = 2; file_exists(path) || reserved.count(path); ++n) path = stem + L" (" + std::to_wstring(n) + L")." + wide(ext);
		reserved.insert(path);
		item.outPath = path;

		metadb_info_container::ptr infoRef = h->get_info_ref();
		const file_info& info = infoRef->info();
		static const char* const kMeta[][2] = {
			{ "artist", "artist" }, { "album", "album" }, { "album artist", "album_artist" }, { "date", "date" },
			{ "genre", "genre" }, { "tracknumber", "track" }, { "discnumber", "disc" }, { "composer", "composer" },
		};
		item.sourceInfo.copy_meta(info);
		std::string title = join_meta(info, "title");
		if (title.empty()) title = pfc::string_filename(h->get_path()).c_str();
		if (suffix.length()) title = title + " " + suffix.c_str();
		item.title = title;
		item.meta.emplace_back("title", title);
		for (auto& m : kMeta) {
			std::string v = join_meta(info, m[0]);
			if (!v.empty()) item.meta.emplace_back(m[1], v);
		}
		items.push_back(std::move(item));
	}
	return true;
}

// ---------------------------------------------------------------- export dialog

pfc::string8 get_text(HWND dlg, int id) {
	const int len = GetWindowTextLengthW(GetDlgItem(dlg, id));
	std::wstring w(len + 1, L'\0');
	GetDlgItemTextW(dlg, id, w.data(), len + 1);
	w.resize(len);
	pfc::string8 s = utf8(w.c_str());
	// Trim surrounding whitespace and quotes (pasted paths often have them).
	const char* b = s.c_str(); const char* e = b + s.length();
	while (b < e && (*b == ' ' || *b == '"')) ++b;
	while (e > b && (e[-1] == ' ' || e[-1] == '"')) --e;
	return pfc::string8(b, e - b);
}
void set_text(HWND dlg, int id, const char* s) { SetDlgItemTextW(dlg, id, wide(s).c_str()); }

void update_ffmpeg_status(HWND dlg) {
	const int format = (int)SendDlgItemMessage(dlg, IDC_FORMAT, CB_GETCURSEL, 0, 0);
	const bool needs = format >= 0 && format < kFormatCount && kFormats[format].codecArgs != nullptr;
	const std::wstring found = find_ffmpeg(get_text(dlg, IDC_FFMPEG));
	std::wstring s;
	if (!found.empty()) s = L"Using: " + found;
	else if (get_text(dlg, IDC_FFMPEG).length()) s = L"⚠ ffmpeg.exe was not found at this path.";
	else s = needs ? L"⚠ ffmpeg.exe not found. Set its path or choose WAV." : L"(WAV export does not need ffmpeg)";
	SetDlgItemTextW(dlg, IDC_FFMPEG_STATUS, s.c_str());
}

struct ExportDlgCtx : presetbar::Host {
	metadb_handle_list items;
	HWND dlg = nullptr;
	Preset cur;
	sliders::Block rows;
	fb2k::CCoreDarkModeHooks dark;

	Preset current() override { return cur; }
	void apply(const sr::Params& p, const char* name) override {
		cur.params = p; cur.name = name;
		rows.set_params(p);
		refresh();
	}
	void refresh() {
		presetbar::fill(GetDlgItem(dlg, IDC_PRESET), cur);
		EnableWindow(GetDlgItem(dlg, IDC_PRESET_SAVE), presets::is_modified(cur));
	}
};

bool on_export_ok(HWND dlg, ExportDlgCtx* ctx) {
	JobSettings s;
	s.params = ctx->cur.params;
	s.format = (int)SendDlgItemMessage(dlg, IDC_FORMAT, CB_GETCURSEL, 0, 0);
	if (s.format < 0 || s.format >= kFormatCount) s.format = 0;
	const pfc::string8 outDir = get_text(dlg, IDC_OUTDIR), pattern = get_text(dlg, IDC_PATTERN), ffmpegCfg = get_text(dlg, IDC_FFMPEG);
	const pfc::string8 suffix = get_text(dlg, IDC_SUFFIX);

	if (outDir.length()) {
		const DWORD a = GetFileAttributesW(wide(outDir).c_str());
		if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) {
			MessageBoxW(dlg, L"The output folder does not exist.", L"Slowed & Reverb", MB_ICONWARNING);
			return false;
		}
	}
	if (kFormats[s.format].codecArgs) {
		s.ffmpeg = find_ffmpeg(ffmpegCfg);
		if (s.ffmpeg.empty()) {
			MessageBoxW(dlg, L"This format needs ffmpeg.exe.\nSet the ffmpeg path or choose WAV.", L"Slowed & Reverb", MB_ICONWARNING);
			return false;
		}
	}

	save_settings(ctx->cur);
	cfg_format = s.format;
	cfg_outdir.set(outDir);
	cfg_pattern.set(pattern);
	cfg_ffmpeg.set(ffmpegCfg);
	cfg_suffix.set(suffix);

	std::vector<JobItem> items;
	pfc::string8 error;
	if (!prepare_items(ctx->items, outDir, pattern, suffix, s.format, items, error)) {
		MessageBoxW(dlg, wide(error).c_str(), L"Slowed & Reverb", MB_ICONWARNING);
		return false;
	}

	auto proc = fb2k::service_new<ExportProcess>(std::move(items), s);
	threaded_process::get()->run_modeless(proc,
		threaded_process::flag_show_progress_dual | threaded_process::flag_show_item | threaded_process::flag_show_abort,
		core_api::get_main_window(), "Exporting with Slowed & Reverb");
	return true;
}

INT_PTR CALLBACK ExportDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
	ExportDlgCtx* ctx = (ExportDlgCtx*)GetWindowLongPtr(dlg, DWLP_USER);
	if (ctx) {
		LRESULT r;
		if (ctx->rows.handle(msg, wp, lp, r)) { SetWindowLongPtr(dlg, DWLP_MSGRESULT, r); return TRUE; }
	}
	switch (msg) {
	case WM_INITDIALOG: {
		SetWindowLongPtr(dlg, DWLP_USER, lp);
		ctx = (ExportDlgCtx*)lp;
		ctx->dlg = dlg;
		ctx->cur = load_settings();
		ctx->rows.on_event = [ctx](int e) { if (e == sliders::kChanged) { ctx->cur.params = ctx->rows.params(); ctx->refresh(); } };
		ctx->rows.on_new_control = [ctx](HWND w) { ctx->dark.AddCtrlAuto(w); };
		ctx->rows.attach(dlg);
		ctx->rows.set_params(ctx->cur.params);
		ctx->refresh();
		add_listener(dlg);
		for (const Format& f : kFormats) SendDlgItemMessageW(dlg, IDC_FORMAT, CB_ADDSTRING, 0, (LPARAM)f.label);
		int fmt = (int)cfg_format.get();
		SendDlgItemMessage(dlg, IDC_FORMAT, CB_SETCURSEL, (fmt >= 0 && fmt < kFormatCount) ? fmt : 0, 0);
		set_text(dlg, IDC_OUTDIR, cfg_outdir.get());
		if (strcmp(cfg_pattern.get(), kOldDefaultPattern) == 0) cfg_pattern.set(kDefaultPattern);
		set_text(dlg, IDC_PATTERN, cfg_pattern.get());
		set_text(dlg, IDC_SUFFIX, cfg_suffix.get());
		set_text(dlg, IDC_FFMPEG, cfg_ffmpeg.get());
		update_ffmpeg_status(dlg);
		((ExportDlgCtx*)lp)->dark.AddDialogWithControls(dlg);
		return TRUE;
	}
	case WM_SR_CHANGED: // preset library or theme changed
		if (ctx) { ctx->refresh(); RedrawWindow(dlg, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN); }
		return TRUE;
	case WM_DESTROY:
		remove_listener(dlg);
		break;
	case WM_COMMAND: {
		if (!ctx) break;
		const int id = LOWORD(wp);
		switch (id) {
		case IDC_PRESET:
			if (HIWORD(wp) == CBN_SELCHANGE) presetbar::on_select(dlg, (HWND)lp, *ctx);
			return TRUE;
		case IDC_PRESET_MORE:
			if (HIWORD(wp) == BN_CLICKED) presetbar::show_actions(dlg, (HWND)lp, *ctx);
			return TRUE;
		case IDC_PRESET_SAVE:
			if (HIWORD(wp) == BN_CLICKED) presetbar::save(dlg, *ctx);
			return TRUE;
		case IDC_FROM_PLAYBACK: {
			const Preset p = live::get();
			ctx->apply(p.params, p.name);
			return TRUE;
		}
		case IDC_FORMAT:
			if (HIWORD(wp) == CBN_SELCHANGE) update_ffmpeg_status(dlg);
			return TRUE;
		case IDC_FFMPEG:
			if (HIWORD(wp) == EN_CHANGE) update_ffmpeg_status(dlg);
			return TRUE;
		case IDC_OUTDIR_BROWSE: {
			pfc::string8 dir = get_text(dlg, IDC_OUTDIR);
			if (uBrowseForFolder(dlg, "Choose output folder", dir)) set_text(dlg, IDC_OUTDIR, dir);
			return TRUE;
		}
		case IDC_FFMPEG_BROWSE: {
			pfc::string8 file = get_text(dlg, IDC_FFMPEG);
			if (uGetOpenFileName(dlg, "ffmpeg.exe|ffmpeg.exe|Programs (*.exe)|*.exe", 0, "exe", "Locate ffmpeg.exe", nullptr, file, FALSE))
				set_text(dlg, IDC_FFMPEG, file);
			return TRUE;
		}
		case IDOK: {
			LRESULT r; ctx->rows.handle(sliders::WM_SR_COMMIT_EDIT, 1, 0, r); // a value still being typed counts
			if (on_export_ok(dlg, ctx)) EndDialog(dlg, IDOK);
		}
			return TRUE;
		case IDCANCEL:
			EndDialog(dlg, IDCANCEL);
			return TRUE;
		}
		break;
	}
	}
	return FALSE;
}

void run_export_dialog(metadb_handle_list_cref data) {
	ExportDlgCtx ctx;
	ctx.items = data;
	DialogBoxParamW(core_api::get_my_instance(), MAKEINTRESOURCEW(IDD_EXPORT), core_api::get_main_window(), ExportDlgProc, (LPARAM)&ctx);
}

// ---------------------------------------------------------------- context menu

const GUID guid_menu_group = { 0xc5d844b3, 0xa8c1, 0x464f, { 0x86, 0x12, 0x16, 0x04, 0xe9, 0x38, 0x4d, 0xc4 } };
const GUID guid_menu_export = { 0xd5a9bde3, 0x8fc7, 0x46d3, { 0xad, 0x0c, 0x86, 0x11, 0xed, 0x4b, 0xf9, 0xbe } };

contextmenu_group_popup_factory g_menu_group(guid_menu_group, contextmenu_groups::root, "Slowed & Reverb", 0);

class menu_export : public contextmenu_item_simple {
public:
	GUID get_parent() override { return guid_menu_group; }
	unsigned get_num_items() override { return 1; }
	void get_item_name(unsigned, pfc::string_base& out) override { out = "Export..."; }
	void context_command(unsigned, metadb_handle_list_cref data, const GUID&) override { run_export_dialog(data); }
	GUID get_item_guid(unsigned) override { return guid_menu_export; }
	bool get_item_description(unsigned, pfc::string_base& out) override {
		out = "Export the selected tracks with Slowed & Reverb applied.";
		return true;
	}
};

contextmenu_item_factory_t<menu_export> g_menu_export;

}

void sr_fb2k::exporter::run_for_selection(HWND owner) {
	metadb_handle_list items;
	playlist_manager::get()->activeplaylist_get_selected_items(items);
	if (items.get_count() == 0) {
		popup_message_v3::get()->messageBox(owner, "Select the tracks to export in a playlist first.", "Slowed & Reverb", MB_OK | MB_ICONINFORMATION);
		return;
	}
	run_export_dialog(items);
}
