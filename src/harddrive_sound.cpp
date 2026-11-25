/*
 * Hard drive sounds for directory drives, after 86Box's hdd_audio: a spin-up
 * sample, then the running loop, plus one head-step sample per seek. Seeks
 * carry emulated timestamps, so each starts at its place within a chunk.
 */
#include "harddrive_sound.h"
#include "harddrive_emu.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {

struct Sample {
	std::vector<int16_t> data;
	int rate = 0;
};

struct Profile {
	const char *name;
	float motor_volume;
	float running_volume;
	float seek_volume;
};

constexpr Profile profiles[] = {
	/* Seagate ST3120A, 1992 IDE */
	{ "fast", 1.0f, 0.3f, 0.3f },
	/* Miniscribe 8425, 1987 */
	{ "slow", 1.0f, 0.3f, 1.0f },
};

constexpr int MAX_VOICES = 8;
constexpr int MAX_SEEKS = 64;
constexpr double FADE_US = 300000;

enum Spindle { STOPPED, STARTING, RUNNING, STOPPING, IDLE };

std::mutex mutex;
const Profile *profile;
float gain = 1;
double idle_us;
double last_active_us;
std::string loaded;
Sample spinup, running, seek;
Spindle spindle;
double spindle_pos;
float fade;
double voices[MAX_VOICES];
int voice_count;
double clock_us;

uint32_t le32(const uint8_t *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool load_wav(const std::string &path, Sample &out)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	std::vector<uint8_t> buf;
	uint8_t tmp[65536];
	size_t n;
	while ((n = fread(tmp, 1, sizeof tmp, f)) > 0)
		buf.insert(buf.end(), tmp, tmp + n);
	fclose(f);
	if (buf.size() < 12 || memcmp(buf.data(), "RIFF", 4) || memcmp(buf.data() + 8, "WAVE", 4))
		return false;
	int channels = 0, bits = 0;
	size_t p = 12;
	while (p + 8 <= buf.size()) {
		const uint32_t len = le32(&buf[p + 4]);
		const uint8_t *body = &buf[p + 8];
		const size_t avail = std::min<size_t>(len, buf.size() - p - 8);
		if (!memcmp(&buf[p], "fmt ", 4) && avail >= 16) {
			channels = body[2] | (body[3] << 8);
			out.rate = (int)le32(body + 4);
			bits = body[14] | (body[15] << 8);
		} else if (!memcmp(&buf[p], "data", 4)) {
			if (bits != 16 || channels < 1 || out.rate <= 0)
				return false;
			const size_t frames = avail / 2 / channels;
			out.data.resize(frames);
			for (size_t i = 0; i < frames; i++) {
				int sum = 0;
				for (int c = 0; c < channels; c++) {
					const uint8_t *s = body + (i * channels + c) * 2;
					sum += (int16_t)(s[0] | (s[1] << 8));
				}
				out.data[i] = (int16_t)(sum / channels);
			}
			return !out.data.empty();
		}
		p += 8 + len + (len & 1);
	}
	return false;
}

float at(const Sample &s, double pos)
{
	const size_t i = (size_t)pos;
	if (i + 1 >= s.data.size())
		return i < s.data.size() ? s.data[i] : 0;
	const float frac = (float)(pos - i);
	return s.data[i] + (s.data[i + 1] - s.data[i]) * frac;
}

void add(int16_t *out, float v)
{
	const int x = *out + (int)v;
	*out = (int16_t)std::clamp(x, -32768, 32767);
}

}

bool hdsnd_configure(const char *dir, const char *name)
{
	std::lock_guard<std::mutex> l(mutex);
	const Profile *want = nullptr;
	for (const Profile &p : profiles) {
		if (name && dir && !strcmp(name, p.name))
			want = &p;
	}
	if (!want) {
		profile = nullptr;
		return true;
	}
	const std::string base = std::string(dir) + "/" + want->name + "/";
	if (loaded != base) {
		loaded.clear();
		spinup = running = seek = Sample();
		if (!load_wav(base + "running.wav", running) || !load_wav(base + "seek.wav", seek)) {
			profile = nullptr;
			return false;
		}
		load_wav(base + "spinup.wav", spinup);
		loaded = base;
		spindle = STOPPED;
	}
	profile = want;
	return true;
}

void hdsnd_set_volume(float g)
{
	std::lock_guard<std::mutex> l(mutex);
	gain = g;
}

void hdsnd_set_idle_off(double seconds)
{
	std::lock_guard<std::mutex> l(mutex);
	idle_us = std::max(0.0, seconds) * 1e6;
}

bool hdsnd_enabled()
{
	std::lock_guard<std::mutex> l(mutex);
	return profile != nullptr;
}

void hdsnd_mix(int16_t *stereo, unsigned frames, int rate)
{
	std::lock_guard<std::mutex> l(mutex);
	if (!profile || !frames || rate <= 0)
		return;
	if (!hdemu_enabled() || !hdemu_spinning()) {
		spindle = STOPPED;
		voice_count = 0;
		return;
	}
	const double chunk_us = frames * 1e6 / rate;
	const double now = hdemu_now_us();
	if (clock_us < now - 50000 || clock_us > now + 50000)
		clock_us = now;
	clock_us += (now - clock_us) * 0.05;
	double seeks[MAX_SEEKS];
	const int n = hdemu_take_seeks(clock_us + chunk_us, seeks, MAX_SEEKS);
	std::sort(seeks, seeks + n);

	const bool active = n > 0 || hdemu_busy_until_us() > now;
	if (active || spindle == STOPPED)
		last_active_us = clock_us;
	if (spindle == STOPPED || ((spindle == STOPPING || spindle == IDLE) && active)) {
		spindle = spinup.data.empty() ? RUNNING : STARTING;
		spindle_pos = 0;
	} else if ((spindle == RUNNING || spindle == STARTING) && idle_us > 0
		&& clock_us - last_active_us > idle_us) {
		if (spindle == STARTING)
			spindle_pos = 0;
		spindle = STOPPING;
		fade = 1;
	}

	const double spinup_step = (double)spinup.rate / rate;
	const double running_step = (double)running.rate / rate;
	const float fade_step = (float)(1e6 / rate / FADE_US);
	const double seek_step = (double)seek.rate / rate;
	int next = 0;
	for (unsigned i = 0; i < frames; i++) {
		const double t = clock_us + i * 1e6 / rate;
		while (next < n && seeks[next] <= t) {
			if (voice_count < MAX_VOICES)
				voices[voice_count++] = 0;
			next++;
		}
		float v = 0;
		if (spindle == STARTING) {
			v += at(spinup, spindle_pos) * profile->motor_volume;
			spindle_pos += spinup_step;
			if (spindle_pos >= spinup.data.size()) {
				spindle = RUNNING;
				spindle_pos = 0;
			}
		} else if (spindle == RUNNING || spindle == STOPPING) {
			v += at(running, spindle_pos) * profile->running_volume * (spindle == STOPPING ? fade : 1);
			if (spindle == STOPPING && (fade -= fade_step) <= 0)
				spindle = IDLE;
			spindle_pos += running_step;
			if (spindle_pos >= running.data.size())
				spindle_pos -= running.data.size();
		}
		for (int k = 0; k < voice_count;) {
			v += at(seek, voices[k]) * profile->seek_volume;
			voices[k] += seek_step;
			if (voices[k] >= seek.data.size())
				voices[k] = voices[--voice_count];
			else
				k++;
		}
		v *= gain;
		add(&stereo[i * 2], v);
		add(&stereo[i * 2 + 1], v);
	}
	clock_us += chunk_us;
}
