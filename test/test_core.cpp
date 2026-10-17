// SPDX-License-Identifier: MIT
// Copyright (c) 2026 KimHerV

// Standalone check of the DSP core: pitch/length of a slowed sine, reverb tail, chunk-size independence.
// Optional: test_core.exe in.f32 out.wav [legacy]  (raw stereo float32 44100 Hz in, processed float WAV out;
// "legacy" uses the pre-1.0 reverb shape, for comparing against output of older builds)
#include "../src/slowed_reverb_core.h"
#include "../src/sr_ui_logic.h"
#include <cstdio>
#include <cstdint>
#include <cstring>

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++fails; printf("FAIL: "); } else printf("ok:   "); printf(__VA_ARGS__); printf("\n"); } while (0)

static std::vector<float> runChunked(const std::vector<float>& in, unsigned ch, unsigned srate, sr::Params p, size_t chunk) {
	sr::Processor proc; proc.setup(srate, ch, p);
	std::vector<float> out, tmp;
	for (size_t f = 0; f < in.size() / ch; f += chunk) {
		size_t n = std::min(chunk, in.size() / ch - f);
		proc.process(&in[f * ch], n, tmp);
		out.insert(out.end(), tmp.begin(), tmp.end());
	}
	proc.drain(tmp);
	out.insert(out.end(), tmp.begin(), tmp.end());
	return out;
}

static double zeroCrossFreq(const float* x, size_t n, unsigned stride, unsigned srate) {
	size_t zc = 0;
	for (size_t i = 1; i < n; ++i) if ((x[(i - 1) * stride] < 0) != (x[i * stride] < 0)) ++zc;
	return zc / 2.0 / ((double)n / srate);
}

static void writeWavFloat(const char* path, const std::vector<float>& d, unsigned ch, unsigned srate) {
	FILE* f = fopen(path, "wb"); if (!f) return;
	uint32_t dataBytes = (uint32_t)(d.size() * 4), riff = 36 + dataBytes, fmtLen = 16, byteRate = srate * ch * 4;
	uint16_t fmt = 3, chs = (uint16_t)ch, align = (uint16_t)(ch * 4), bits = 32;
	fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtLen, 4, 1, f);
	fwrite(&fmt, 2, 1, f); fwrite(&chs, 2, 1, f); fwrite(&srate, 4, 1, f); fwrite(&byteRate, 4, 1, f);
	fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
	fwrite(d.data(), 4, d.size(), f); fclose(f);
}

int main(int argc, char** argv) {
	const unsigned sr_ = 44100, ch = 2;
	const size_t frames = sr_ * 2;
	std::vector<float> sine(frames * ch);
	for (size_t i = 0; i < frames; ++i) sine[i * 2] = sine[i * 2 + 1] = 0.5f * (float)std::sin(2 * 3.14159265358979 * 440.0 * i / sr_);

	// 1. Pure slowdown (no reverb): length and pitch scale by 1/speed.
	sr::Params dryP; dryP.speed = 0.8f; dryP.wet = 0;
	auto slow = runChunked(sine, ch, sr_, dryP, 4096);
	double outSec = (double)slow.size() / ch / sr_;
	CHECK(std::fabs(outSec - 2.0 / 0.8) < 0.01, "length %.4fs (expect 2.5)", outSec);
	double fq = zeroCrossFreq(&slow[sr_ / 2 * ch], sr_, ch, sr_);
	CHECK(std::fabs(fq - 352.0) < 1.5, "pitch %.2f Hz (expect 352)", fq);
	float peak = 0; for (size_t i = sr_ * ch; i < 2 * sr_ * ch; ++i) peak = std::max(peak, std::fabs(slow[i]));
	CHECK(std::fabs(peak - 0.5f) < 0.01f, "amplitude %.4f (expect 0.5)", peak);

	// 2. Chunk-size independence (state carried across chunk boundaries).
	auto slowA = runChunked(sine, ch, sr_, dryP, 4096);
	auto slowB = runChunked(sine, ch, sr_, dryP, 37);
	float maxDiff = 0; size_t nmin = std::min(slowA.size(), slowB.size());
	for (size_t i = 0; i < nmin; ++i) maxDiff = std::max(maxDiff, std::fabs(slowA[i] - slowB[i]));
	CHECK(slowA.size() == slowB.size() && maxDiff < 1e-6f, "chunked output identical (sizes %zu/%zu, diff %g)", slowA.size(), slowB.size(), maxDiff);

	// 3. Reverb: tail after the input ends, decays, no blow-up.
	sr::Params p; // defaults
	auto rev = runChunked(sine, ch, sr_, p, 4096);
	size_t bodyFrames = (size_t)(2.0 / p.speed * sr_);
	double tailSec = (double)(rev.size() / ch - bodyFrames) / sr_;
	float tailPeak = 0; for (size_t i = (bodyFrames + sr_ / 10) * ch; i < (bodyFrames + sr_ / 5) * ch && i < rev.size(); ++i) tailPeak = std::max(tailPeak, std::fabs(rev[i]));
	float allPeak = 0; for (float v : rev) allPeak = std::max(allPeak, std::fabs(v));
	CHECK(tailSec > 0.5 && tailSec < 8.1, "reverb tail %.2fs", tailSec);
	CHECK(tailPeak > 0.01f, "tail audible 100-200ms after end (peak %.4f)", tailPeak);
	CHECK(allPeak < 1.0f, "overall peak %.3f (< 1.0)", allPeak);

	// 4. Mono and 6-channel run without issues.
	std::vector<float> mono(frames); for (size_t i = 0; i < frames; ++i) mono[i] = sine[i * 2];
	auto m = runChunked(mono, 1, sr_, p, 1000);
	CHECK(m.size() > frames, "mono processed (%zu frames)", m.size());
	std::vector<float> six(frames * 6, 0.1f);
	auto s6 = runChunked(six, 6, 48000, p, 999);
	CHECK(s6.size() % 6 == 0 && s6.size() > six.size(), "6ch processed");

	// 5. Pre-delay: the reverb onset moves later by the pre-delay time.
	{
		std::vector<float> click(sr_ * ch, 0.0f); click[0] = click[1] = 1.0f;
		auto onset = [&](float predelayMs) {
			sr::Params q; q.speed = 1.0f; q.wet = 1.0f; q.lowcut = 0; q.predelay = predelayMs;
			auto o = runChunked(click, ch, sr_, q, 512);
			for (size_t i = 64 * ch; i < o.size(); ++i) if (std::fabs(o[i]) > 1e-4f) return (double)(i / ch) / sr_ * 1000.0;
			return -1.0;
		};
		const double a = onset(0), b = onset(50);
		CHECK(std::fabs((b - a) - 50.0) < 0.5, "pre-delay shifts reverb onset by %.2f ms (expect 50)", b - a);
	}

	// 6. Width 0: the wet signal is identical in both channels (dry input is mono here too).
	{
		sr::Params q; q.speed = 1.0f; q.width = 0;
		auto o = runChunked(sine, ch, sr_, q, 4096);
		float d = 0; for (size_t i = 0; i < o.size(); i += 2) d = std::max(d, std::fabs(o[i] - o[i + 1]));
		sr::Params w = q; w.width = 1;
		auto o2 = runChunked(sine, ch, sr_, w, 4096);
		float d2 = 0; for (size_t i = 0; i < o2.size(); i += 2) d2 = std::max(d2, std::fabs(o2[i] - o2[i + 1]));
		CHECK(d < 1e-6f && d2 > 1e-3f, "width 0 -> mono reverb (L-R %.2g), width 1 -> stereo (L-R %.3f)", d, d2);
	}

	// 7. Low cut: a 60 Hz tone gets much less reverb with a 300 Hz cut; a 3 kHz tone barely changes.
	{
		auto tailEnergy = [&](double hz, float lowcut) {
			std::vector<float> t(sr_ * ch);
			for (size_t i = 0; i < sr_; ++i) t[i * 2] = t[i * 2 + 1] = 0.5f * (float)std::sin(2 * 3.14159265358979 * hz * i / sr_);
			sr::Params q; q.speed = 1.0f; q.predelay = 0; q.lowcut = lowcut;
			auto o = runChunked(t, ch, sr_, q, 4096);
			double e = 0; for (size_t i = sr_ * ch; i < o.size(); ++i) e += (double)o[i] * o[i]; // tail only
			return e;
		};
		const double lowRatio = tailEnergy(60, 300) / tailEnergy(60, 0);
		const double highRatio = tailEnergy(3000, 300) / tailEnergy(3000, 0);
		CHECK(lowRatio < 0.1 && highRatio > 0.8, "low cut 300 Hz: 60 Hz tail x%.3f, 3 kHz tail x%.3f", lowRatio, highRatio);
	}

	// 7b. High cut: a 6 kHz tone gets much less reverb with a 2 kHz cut; a 200 Hz tone barely changes.
	{
		auto tailEnergy = [&](double hz, float highcut) {
			std::vector<float> t(sr_ * ch);
			for (size_t i = 0; i < sr_; ++i) t[i * 2] = t[i * 2 + 1] = 0.5f * (float)std::sin(2 * 3.14159265358979 * hz * i / sr_);
			sr::Params q; q.speed = 1.0f; q.predelay = 0; q.lowcut = 0; q.highcut = highcut;
			auto o = runChunked(t, ch, sr_, q, 4096);
			double e = 0; for (size_t i = sr_ * ch; i < o.size(); ++i) e += (double)o[i] * o[i];
			return e;
		};
		const double off = sr::Params::kMaxHighcutHz;
		const double highRatio = tailEnergy(6000, 2000) / tailEnergy(6000, (float)off);
		const double lowRatio = tailEnergy(200, 2000) / tailEnergy(200, (float)off);
		CHECK(highRatio < 0.15 && lowRatio > 0.9, "high cut 2 kHz: 6 kHz tail x%.3f, 200 Hz tail x%.3f", highRatio, lowRatio);
	}

	// 8. UI logic: typed values, labels, snapping, presets, colors.
	{
		using namespace srui;
		float v = 0;
		CHECK(parse(kSpeed, L"80", v) && v == 80, "parse speed '80' -> %.2f", v);
		CHECK(parse(kSpeed, L"-3st", v) && std::fabs(v - 84.09f) < 0.01f, "parse speed '-3st' -> %.2f", v);
		CHECK(parse(kSpeed, L"− 3 st", v) && std::fabs(v - 84.09f) < 0.01f, "parse speed '\\u2212 3 st' -> %.2f", v);
		CHECK(parse(kSpeed, L"0.8x", v) && std::fabs(v - 80) < 0.01f, "parse speed '0.8x' -> %.2f", v);
		CHECK(parse(kSpeed, L"20", v) && v == 50, "parse speed '20' clamps to %.0f", v);
		CHECK(parse(kLowcut, L"1k", v) && v == 500, "parse lowcut '1k' clamps to %.0f", v);
		CHECK(parse(kLowcut, L"OFF", v) && v == 0, "parse lowcut 'OFF' -> %.0f", v);
		CHECK(parse(kLowcut, L"120 Hz", v) && v == 120, "parse lowcut '120 Hz' -> %.0f", v);
		CHECK(parse(kPredelay, L"0.05s", v) && std::fabs(v - 50) < 0.01f, "parse predelay '0.05s' -> %.1f", v);
		CHECK(parse(kMix, L"35%", v) && v == 35, "parse mix '35%%' -> %.0f", v);
		CHECK(!parse(kMix, L"abc", v) && !parse(kMix, L"", v) && !parse(kMix, L"12abc", v), "parse rejects junk");

		CHECK(format(kSpeed, 85) == L"85%" && format(kSpeed, 84.09f) == L"84.1%", "format speed");
		CHECK(detail(kSpeed, 85) == L"−2.8 semitones" && detail(kSpeed, 84.09f) == L"−3 semitones", "speed detail");
		CHECK(detail(kSpeed, 94.39f) == L"−1 semitone" && detail(kSpeed, 100) == L"original pitch" && detail(kMix, 50).empty(), "speed detail: singular, original, none");
		CHECK(format(kLowcut, 0) == L"Off" && format(kLowcut, 120) == L"120 Hz", "format lowcut");
		CHECK(format(kPredelay, 20) == L"20 ms" && format(kWidth, 100) == L"100%", "format predelay/width");
		CHECK(format(kHighcut, 20000) == L"Off" && format(kHighcut, 8000) == L"8 kHz" && format(kHighcut, 6500) == L"6.5 kHz", "format highcut");
		CHECK(parse(kHighcut, L"off", v) && v == 20000, "parse highcut 'off' -> %.0f", v);
		CHECK(parse(kHighcut, L"8", v) && v == 8000 && parse(kHighcut, L"6.5k", v) && v == 6500 && parse(kHighcut, L"9000hz", v) && v == 9000, "parse highcut '8', '6.5k', '9000hz'");
		CHECK(editText(kHighcut, 8000) == L"8k" && editText(kHighcut, 20000) == L"Off", "highcut edit text");
		CHECK(tickCount(kHighcut) == 19 && quantize(kHighcut, 7400, false) == 7000, "highcut ticks 1 kHz");
		{
			PresetStore old = PresetStore::parse("Old\t85;35;80;50;20;120;100\n");
			CHECK(old.items.size() == 1 && get(old.items[0].params, kHighcut) == 20000, "7-value user preset loads with high cut off");
			Preset g2;
			CHECK(parsePresetFile("[Slowed & Reverb preset]\nname=Old\nspeed=80\n", g2) && get(g2.params, kHighcut) == 20000, "old preset file: high cut off");
		}

		CHECK(quantize(kSpeed, 83.2f, false) == 85 && quantize(kSpeed, 82.4f, false) == 80, "drag snaps speed to 5%% ticks");
		CHECK(quantize(kLowcut, 137, false) == 125 && quantize(kPredelay, 14.9f, false) == 10, "drag snaps lowcut 25 Hz / predelay 10 ms");
		CHECK(std::fabs(quantize(kSpeed, 83.2f, true) - 83.0f) < 1e-4f, "Shift: fine grid -> %.2f", quantize(kSpeed, 83.2f, true));
		CHECK(tickCount(kSpeed) == 11 && tickCount(kMix) == 21 && tickCount(kLowcut) == 21 && tickCount(kPredelay) == 21, "tick counts");
		CHECK(nudge(kMix, 35, 1, false) == 40 && nudge(kMix, 37, 1, false) == 40 && nudge(kMix, 37, -1, false) == 35, "nudge to next tick");
		CHECK(nudge(kLowcut, 120, -1, true) == 115 && nudge(kSpeed, 100, 1, false) == 100, "nudge fine; clamps at max");

		sr::Params prm; set(prm, kSpeed, 77); set(prm, kLowcut, 200);
		CHECK(std::fabs(prm.speed - 0.77f) < 1e-6f && prm.lowcut == 200 && get(prm, kSpeed) == 77, "get/set scale");
		CHECK(sameParams(factoryPresets()[1].params, sr::Params()), "Classic == defaults");

		PresetStore st;
		CHECK(!st.put("classic", prm) && !st.put("   ", prm), "store refuses factory and empty names");
		CHECK(st.put("  Late Night ", prm) && st.items[0].name == "Late Night", "store trims names");
		CHECK(st.put("late night", factoryPresets()[0].params) && st.items.size() == 1, "same name (any case) overwrites");
		st.put(reinterpret_cast<const char*>(u8"새벽 드라이브"), prm);
		PresetStore st2 = PresetStore::parse(st.serialize());
		CHECK(st2.items.size() == 2 && st2.items[1].name == st.items[1].name && sameParams(st2.items[1].params, prm), "store round-trip (UTF-8 name)");
		CHECK(st2.rename("Late Night", "Dawn") && st2.find("dawn") && !st2.rename("Dawn", "Dreamy"), "rename; refuses factory name");
		CHECK(st2.remove("DAWN") && st2.items.size() == 1, "remove");
		CHECK(PresetStore::parse("garbage\nno tab here\nx\t1;2\n").items.empty(), "store ignores bad lines");

		Preset f; f.name = "Share Me"; f.params = prm;
		Preset g;
		CHECK(parsePresetFile(formatPresetFile(f), g) && g.name == "Share Me" && sameParams(g.params, prm), "preset file round-trip");
		CHECK(parsePresetFile("[Slowed & Reverb preset]\r\nname=X\r\nspeed=70\r\nfuture=1\r\n", g) && get(g.params, kSpeed) == 70 && get(g.params, kMix) == 35, "preset file: CRLF, unknown keys, defaults");
		CHECK(!parsePresetFile("name=x", g), "preset file needs the header");

		const Rgb bg = rgb(0x20, 0x20, 0x20);
		CHECK(readableAccent({ rgb(0x4C, 0xC2, 0xFF) }, bg) == rgb(0x4C, 0xC2, 0xFF), "accent: Windows default dark shade kept");
		const Rgb darkRed = rgb(0x68, 0x00, 0x00);
		CHECK(contrast(readableAccent({ darkRed }, bg), bg) >= 3.0, "accent: dark red lightened to %.2f:1", contrast(readableAccent({ darkRed }, bg), bg));
		CHECK(textOn(rgb(0x4C, 0xC2, 0xFF)) == rgb(0, 0, 0) && textOn(rgb(0x00, 0x3E, 0x92)) == rgb(255, 255, 255), "text on accent");
	}

	if (argc >= 3) {
		FILE* f = fopen(argv[1], "rb");
		if (f) {
			std::vector<float> in; float buf[4096]; size_t n;
			while ((n = fread(buf, 4, 4096, f)) > 0) in.insert(in.end(), buf, buf + n);
			fclose(f);
			if (argc >= 4 && strcmp(argv[3], "legacy") == 0) p.setLegacyReverbShape();
			auto out = runChunked(in, 2, 44100, p, 4410);
			writeWavFloat(argv[2], out, 2, 44100);
			float pk = 0; for (float v : out) pk = std::max(pk, std::fabs(v));
			float ipk = 0; for (float v : in) ipk = std::max(ipk, std::fabs(v));
			printf("file: in %.2fs peak %.3f -> out %.2fs peak %.3f\n", in.size() / 2.0 / 44100, ipk, out.size() / 2.0 / 44100, pk);
		}
	}
	printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
	return fails;
}
