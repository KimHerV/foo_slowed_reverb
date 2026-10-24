// SPDX-License-Identifier: MIT
// Copyright (c) 2026 KimHerV

#pragma once
// Slowed & Reverb DSP core. No foobar2000 dependencies, so it can be unit-tested standalone.
//  - "Slowed": varispeed resampling (tempo and pitch drop together, like a slowed-down record).
//  - "Reverb": Freeverb (Jezar at Dreampoint, public domain), delay lengths scaled to the sample rate,
//    plus pre-delay, a low cut and a high cut on the reverb send, and stereo width on the return.

#include <vector>
#include <cmath>
#include <cstddef>
#include <algorithm>

#ifndef SR_SAMPLE
#define SR_SAMPLE float
#endif

namespace sr {

using Sample = SR_SAMPLE; // audio I/O type (foobar2000 x64 uses double); reverb state stays float

struct Params {
	float speed = 0.85f;   // playback rate, 0.50 .. 1.00
	float wet = 0.35f;     // reverb amount, 0 .. 1
	float room = 0.80f;    // room size, 0 .. 1
	float damp = 0.50f;    // high-frequency damping, 0 .. 1
	float predelay = 20.0f; // ms before the reverb starts, 0 .. kMaxPredelayMs
	float lowcut = 120.0f;  // Hz, high-pass on the reverb send (0 = off), 0 .. kMaxLowcutHz
	float highcut = 10000.0f; // Hz, low-pass on the reverb send (kMaxHighcutHz = off), kMinHighcutHz .. kMaxHighcutHz
	float width = 1.0f;     // stereo width of the reverb, 0 (mono) .. 1

	static constexpr float kMaxPredelayMs = 200.0f, kMaxLowcutHz = 500.0f, kMinHighcutHz = 2000.0f, kMaxHighcutHz = 20000.0f;

	// Settings saved before pre-delay / low cut / width existed: these values reproduce the old sound exactly.
	void setLegacyReverbShape() { predelay = 0; lowcut = 0; width = 1; highcut = kMaxHighcutHz; }

	static float clamp(float v, float lo, float hi) { return v >= lo ? (v <= hi ? v : hi) : lo; } // NaN -> lo
	static float clamp01(float v) { return clamp(v, 0, 1); }
	void sanitize() {
		speed = clamp(speed, 0.5f, 1.0f);
		wet = clamp01(wet); room = clamp01(room); damp = clamp01(damp);
		predelay = clamp(predelay, 0, kMaxPredelayMs); lowcut = clamp(lowcut, 0, kMaxLowcutHz); width = clamp01(width);
		highcut = clamp(highcut, kMinHighcutHz, kMaxHighcutHz);
	}
	bool operator==(const Params& o) const {
		return speed == o.speed && wet == o.wet && room == o.room && damp == o.damp
			&& predelay == o.predelay && lowcut == o.lowcut && width == o.width && highcut == o.highcut;
	}
	bool operator!=(const Params& o) const { return !(*this == o); }
};

// Kaiser-windowed sinc interpolator. speed <= 1 means we only ever upsample, so nothing aliases; the cutoff just
// below Nyquist leaves the filter room to roll off, which keeps interpolation images down (about -120 dB measured
// at 44.1 kHz, flat to 20 kHz).
class Resampler {
public:
	static constexpr int kHalf = 64;      // taps on each side
	static constexpr int kPhases = 1024;  // table resolution between input samples
	static constexpr double kCutoff = 0.97; // fraction of the input Nyquist frequency
	static constexpr double kBeta = 12.0;   // Kaiser window shape

	void setup(unsigned channels) { m_ch = channels; reset(); }
	void reset() {
		m_buf.assign((size_t)kHalf * m_ch, Sample(0)); // leading silence so the first output lands on input sample 0
		m_pos = kHalf;
	}

	// Appends interleaved input; emits as many output frames as the buffered input allows.
	void process(const Sample* in, size_t frames, double speed, std::vector<Sample>& out) {
		m_buf.insert(m_buf.end(), in, in + frames * m_ch);
		run(speed, out);
	}
	// Pushes trailing silence so the last real input samples get emitted.
	void drain(double speed, std::vector<Sample>& out) {
		m_buf.insert(m_buf.end(), (size_t)(kHalf + 1) * m_ch, Sample(0));
		run(speed, out);
	}
	size_t bufferedFrames() const { return m_ch ? m_buf.size() / m_ch : 0; }

private:
	// Built once and shared by all instances (about 0.5 MB).
	static const std::vector<float>& table() {
		static const std::vector<float> t = buildTable();
		return t;
	}
	static std::vector<float> buildTable() {
		const int n = 2 * kHalf;
		std::vector<float> t((size_t)(kPhases + 1) * n);
		std::vector<double> v(n);
		for (int p = 0; p <= kPhases; ++p) {
			const double frac = (double)p / kPhases;
			double sum = 0;
			for (int k = 0; k < n; ++k) {
				const double x = (k - (kHalf - 1)) - frac; // tap offset relative to the interpolation point
				const double y = kCutoff * x;
				const double sinc = y == 0 ? 1.0 : std::sin(M_PI_ * y) / (M_PI_ * y);
				v[k] = sinc * kaiser(x / kHalf);
				sum += v[k];
			}
			for (int k = 0; k < n; ++k) t[(size_t)p * n + k] = (float)(v[k] / sum); // unity gain at DC
		}
		return t;
	}
	static double kaiser(double t) { // t in [-1, 1]
		if (t <= -1 || t >= 1) return 0;
		return besselI0(kBeta * std::sqrt(1 - t * t)) / besselI0(kBeta);
	}
	static double besselI0(double x) {
		double sum = 1, term = 1;
		for (int k = 1; k < 50; ++k) { term *= (x / (2 * k)) * (x / (2 * k)); sum += term; }
		return sum;
	}

	void run(double speed, std::vector<Sample>& out) {
		const size_t avail = bufferedFrames();
		const int n = 2 * kHalf;
		const std::vector<float>& tab = table();
		for (;;) {
			const size_t base = (size_t)m_pos;
			if (base + kHalf >= avail) break;
			const double frac = m_pos - (double)base;
			const double fp = frac * kPhases;
			const int p = (int)fp;
			const float t = (float)(fp - p);
			const float* c0 = &tab[(size_t)p * n];
			const float* c1 = c0 + n;
			float coef[2 * kHalf]; // interpolated once per frame, shared by all channels
			for (int k = 0; k < n; ++k) coef[k] = c0[k] + (c1[k] - c0[k]) * t;
			const Sample* src = &m_buf[(base - (kHalf - 1)) * m_ch];
			for (unsigned c = 0; c < m_ch; ++c) {
				Sample acc = 0;
				for (int k = 0; k < n; ++k) acc += coef[k] * src[(size_t)k * m_ch + c];
				out.push_back(acc);
			}
			m_pos += speed;
		}
		// Drop input that no future output frame can reach.
		const size_t keepFrom = (size_t)m_pos - (kHalf - 1);
		if (keepFrom > 0) {
			m_buf.erase(m_buf.begin(), m_buf.begin() + keepFrom * m_ch);
			m_pos -= (double)keepFrom;
		}
	}

	static constexpr double M_PI_ = 3.14159265358979323846;
	unsigned m_ch = 0;
	std::vector<Sample> m_buf;
	double m_pos = 0;
};

class Freeverb {
public:
	void setup(unsigned srate, unsigned channels) {
		m_ch = channels; m_srate = srate;
		const double scale = srate / 44100.0;
		m_banks.assign(channels, Bank());
		// One send per bank input: stereo feeds both banks from a single summed send.
		m_sends.assign(channels == 2 ? 1 : channels, Send());
		const size_t maxDelay = (size_t)std::ceil(Params::kMaxPredelayMs / 1000.0 * srate) + 1;
		for (Send& s : m_sends) s.delay.assign(maxDelay, 0.0f);
		for (unsigned c = 0; c < channels; ++c) {
			// Odd channels get the classic Freeverb stereo spread.
			const int spread = (c & 1) ? 23 : 0;
			Bank& b = m_banks[c];
			for (int i = 0; i < kCombs; ++i) b.comb[i].init((size_t)std::lround((kCombTuning[i] + spread) * scale));
			for (int i = 0; i < kAllpasses; ++i) b.allpass[i].init((size_t)std::lround((kAllpassTuning[i] + spread) * scale));
		}
		update();
	}
	void reset() {
		for (Bank& b : m_banks) {
			for (Comb& c : b.comb) c.clear();
			for (Allpass& a : b.allpass) a.clear();
		}
		for (Send& s : m_sends) s.clear();
	}
	void setParams(const Params& p) {
		m_room = p.room; m_damp = p.damp; m_width = p.width;
		const size_t maxDelay = m_sends.empty() ? 1 : m_sends[0].delay.size();
		m_predelay = (std::min)((size_t)std::lround(p.predelay / 1000.0 * m_srate), maxDelay - 1);
		for (Send& s : m_sends) { s.hp.design(p.lowcut, m_srate); s.lp.design(p.highcut < Params::kMaxHighcutHz ? p.highcut : 0, m_srate); }
		update();
	}

	// In-place on interleaved frames; returns the wet signal mixed with dry according to wet.
	void process(Sample* data, size_t frames, float wet) {
		const float wetGain = 2.0f * wet;
		const float dryGain = 1.0f - 0.4f * wet;
		const bool stereo = m_ch == 2;
		// Freeverb width: each side gets its own bank plus some of the other; width 1 keeps them fully separate.
		const float wet1 = wetGain * (m_width / 2 + 0.5f), wet2 = wetGain * ((1 - m_width) / 2);
		const bool fullWidth = m_width >= 1.0f;
		for (size_t f = 0; f < frames; ++f) {
			Sample* fr = data + f * m_ch;
			if (stereo) {
				// Classic Freeverb: both banks are fed the summed input; their spread tunings give the stereo image.
				const float in = m_sends[0].tick((float)(fr[0] + fr[1]) * kFixedGain, m_predelay);
				const float l = m_banks[0].tick(in);
				const float r = m_banks[1].tick(in);
				if (fullWidth) {
					fr[0] = fr[0] * dryGain + l * wetGain;
					fr[1] = fr[1] * dryGain + r * wetGain;
				} else {
					fr[0] = fr[0] * dryGain + l * wet1 + r * wet2;
					fr[1] = fr[1] * dryGain + r * wet1 + l * wet2;
				}
			} else {
				for (unsigned c = 0; c < m_ch; ++c) {
					const float w = m_banks[c].tick(m_sends[c].tick((float)fr[c] * kFixedGain * 2.0f, m_predelay));
					fr[c] = fr[c] * dryGain + w * wetGain;
				}
			}
		}
	}

private:
	static constexpr int kCombs = 8, kAllpasses = 4;
	static constexpr float kFixedGain = 0.015f, kScaleRoom = 0.28f, kOffsetRoom = 0.7f, kScaleDamp = 0.4f;
	static constexpr int kCombTuning[kCombs] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
	static constexpr int kAllpassTuning[kAllpasses] = { 556, 441, 341, 225 };

	static float undenormal(float v) { return std::fabs(v) < 1e-20f ? 0.0f : v; }

	struct Comb {
		std::vector<float> buf; size_t idx = 0; float store = 0, feedback = 0, damp1 = 0, damp2 = 0;
		void init(size_t len) { buf.assign((std::max<size_t>)(len, 1), 0.0f); idx = 0; store = 0; }
		void clear() { std::fill(buf.begin(), buf.end(), 0.0f); store = 0; }
		float tick(float in) {
			const float out = buf[idx];
			store = undenormal(out * damp2 + store * damp1);
			buf[idx] = undenormal(in + store * feedback);
			if (++idx >= buf.size()) idx = 0;
			return out;
		}
	};
	struct Allpass {
		std::vector<float> buf; size_t idx = 0;
		void init(size_t len) { buf.assign((std::max<size_t>)(len, 1), 0.0f); idx = 0; }
		void clear() { std::fill(buf.begin(), buf.end(), 0.0f); }
		float tick(float in) {
			const float b = buf[idx];
			const float out = b - in;
			buf[idx] = undenormal(in + b * 0.5f);
			if (++idx >= buf.size()) idx = 0;
			return out;
		}
	};
	// 2nd-order Butterworth high-pass or low-pass (RBJ cookbook). Off when the cutoff is 0.
	template <bool kLowPass> struct Biquad {
		float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; bool on = false;
		void design(float hz, unsigned srate) {
			on = hz > 0 && hz < srate * 0.45f;
			if (!on) return;
			const double w = 2 * 3.14159265358979323846 * hz / srate, cw = std::cos(w);
			const double alpha = std::sin(w) / (2 * 0.70710678118654752), a0 = 1 + alpha;
			if (kLowPass) { b0 = (float)((1 - cw) / 2 / a0); b1 = (float)((1 - cw) / a0); }
			else { b0 = (float)((1 + cw) / 2 / a0); b1 = (float)(-(1 + cw) / a0); }
			b2 = b0;
			a1 = (float)(-2 * cw / a0); a2 = (float)((1 - alpha) / a0);
		}
		void clear() { z1 = z2 = 0; }
		float tick(float in) { // transposed direct form II
			if (!on) return in;
			const float out = b0 * in + z1;
			z1 = undenormal(b1 * in - a1 * out + z2);
			z2 = undenormal(b2 * in - a2 * out);
			return out;
		}
	};
	// What goes into the reverb: low cut and high cut, then pre-delay.
	struct Send {
		Biquad<false> hp; Biquad<true> lp; std::vector<float> delay; size_t idx = 0;
		void clear() { hp.clear(); lp.clear(); std::fill(delay.begin(), delay.end(), 0.0f); }
		float tick(float in, size_t predelay) {
			const float x = lp.tick(hp.tick(in));
			if (predelay == 0) return x;
			delay[idx] = x;
			size_t r = idx + delay.size() - predelay;
			if (r >= delay.size()) r -= delay.size();
			if (++idx >= delay.size()) idx = 0;
			return delay[r];
		}
	};

	struct Bank {
		Comb comb[kCombs]; Allpass allpass[kAllpasses];
		float tick(float in) {
			float out = 0;
			for (Comb& c : comb) out += c.tick(in);
			for (Allpass& a : allpass) out = a.tick(out);
			return out;
		}
	};

	void update() {
		const float fb = m_room * kScaleRoom + kOffsetRoom;
		const float d1 = m_damp * kScaleDamp;
		for (Bank& b : m_banks) for (Comb& c : b.comb) { c.feedback = fb; c.damp1 = d1; c.damp2 = 1 - d1; }
	}

	unsigned m_ch = 0, m_srate = 0;
	float m_room = 0.8f, m_damp = 0.5f, m_width = 1.0f;
	size_t m_predelay = 0; // frames
	std::vector<Bank> m_banks;
	std::vector<Send> m_sends;
};

// Full chain: slow down, then reverb.
class Processor {
public:
	void setup(unsigned srate, unsigned channels, const Params& p) {
		m_srate = srate; m_ch = channels; m_params = p; m_params.sanitize();
		m_resampler.setup(channels);
		m_reverb.setup(srate, channels);
		m_reverb.setParams(m_params);
	}
	bool isSetup(unsigned srate, unsigned channels) const { return m_srate == srate && m_ch == channels; }
	void setParams(const Params& p) {
		m_params = p; m_params.sanitize();
		m_reverb.setParams(m_params);
	}
	const Params& params() const { return m_params; }
	void reset() { m_resampler.reset(); m_reverb.reset(); }

	// Interleaved input -> interleaved output appended to out (out is cleared first).
	void process(const Sample* in, size_t frames, std::vector<Sample>& out) {
		out.clear();
		m_resampler.process(in, frames, m_params.speed, out);
		m_reverb.process(out.data(), out.size() / m_ch, m_params.wet);
	}

	// End of stream: flush the resampler, then let the reverb ring out (stops once the tail is inaudible).
	void drain(std::vector<Sample>& out, double maxTailSeconds = 8.0) {
		out.clear();
		m_resampler.drain(m_params.speed, out);
		m_reverb.process(out.data(), out.size() / m_ch, m_params.wet);
		if (m_params.wet <= 0) return;

		const size_t block = 1024;
		const size_t maxFrames = (size_t)(maxTailSeconds * m_srate);
		std::vector<Sample> tail(block * m_ch);
		for (size_t done = 0; done < maxFrames; done += block) {
			std::fill(tail.begin(), tail.end(), Sample(0));
			m_reverb.process(tail.data(), block, m_params.wet);
			Sample peak = 0;
			for (Sample v : tail) { const Sample a = std::fabs(v); if (a > peak) peak = a; }
			out.insert(out.end(), tail.begin(), tail.end());
			if (peak < 1e-4f) break; // about -80 dBFS
		}
	}

	double latencySeconds() const { return m_srate ? (double)m_resampler.bufferedFrames() / m_srate : 0; }

private:
	unsigned m_srate = 0, m_ch = 0;
	Params m_params;
	Resampler m_resampler;
	Freeverb m_reverb;
};

} // namespace sr
