#include "harddrive_emu.h"
#include "harddrive_sound.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <vector>

static int failures;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void put32(FILE *f, uint32_t v) { for (int i = 0; i < 4; i++) fputc((v >> (i * 8)) & 0xff, f); }
static void put16(FILE *f, uint16_t v) { fputc(v & 0xff, f); fputc(v >> 8, f); }

static void write_wav(const std::string &path, int16_t value, int frames, int channels)
{
	FILE *f = fopen(path.c_str(), "wb");
	const uint32_t data = frames * channels * 2;
	fwrite("RIFF", 1, 4, f); put32(f, 36 + data); fwrite("WAVE", 1, 4, f);
	fwrite("fmt ", 1, 4, f); put32(f, 16); put16(f, 1); put16(f, channels);
	put32(f, 48000); put32(f, 48000 * channels * 2); put16(f, channels * 2); put16(f, 16);
	fwrite("data", 1, 4, f); put32(f, data);
	for (int i = 0; i < frames * channels; i++)
		put16(f, (uint16_t)value);
	fclose(f);
}

static std::vector<int16_t> mix(unsigned frames)
{
	std::vector<int16_t> buf(frames * 2, 0);
	hdsnd_mix(buf.data(), frames, 48000);
	return buf;
}

int main()
{
	char tmpl[] = "/tmp/hdsnd_test.XXXXXX";
	const std::string dir = mkdtemp(tmpl);
	mkdir((dir + "/fast").c_str(), 0755);
	write_wav(dir + "/fast/spinup.wav", 1000, 480, 2);
	write_wav(dir + "/fast/running.wav", 100, 4800, 1);
	write_wav(dir + "/fast/seek.wav", 10000, 480, 1);

	CHECK(!hdsnd_configure(dir.c_str(), "slow") || !hdsnd_enabled());
	CHECK(hdsnd_configure(dir.c_str(), "fast"));
	CHECK(hdsnd_enabled());

	hdemu_set_profile("fast");
	hdemu_reset();
	CHECK(mix(960)[0] == 0);

	hdemu_spinup();
	std::vector<int16_t> b = mix(960);
	CHECK(b[0] == 1000 && b[1] == 1000);
	CHECK(b[700 * 2] == 30);

	hdemu_metadata(0, "/x", false);
	hdemu_vsync(20000);
	int loud = 0;
	for (int f = 0; f < 5; f++) {
		for (int16_t s : mix(960))
			loud += s > 1000;
	}
	CHECK(loud > 0);
	CHECK(mix(960)[0] == 30);

	hdsnd_set_volume(2);
	CHECK(mix(960)[0] == 60);
	hdsnd_set_volume(1);

	hdsnd_set_idle_off(0.1);
	for (int f = 0; f < 30; f++) {
		hdemu_vsync(20000);
		mix(960);
	}
	b = mix(960);
	CHECK(b[0] == 0);
	hdemu_metadata(0, "/y", false);
	b = mix(960);
	CHECK(b[0] == 1000 + 3000);
	hdsnd_set_idle_off(0);

	hdsnd_configure(dir.c_str(), nullptr);
	CHECK(!hdsnd_enabled());
	CHECK(mix(960)[0] == 0);

	std::system(("rm -rf " + dir).c_str());
	if (failures)
		return 1;
	std::puts("harddrive_sound: ok");
	return 0;
}
