#include "harddrive_emu.h"

#include <atomic>
#include <cstdio>
#include <thread>

static int failures;

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static double read_file(const char *path, uint64_t size, uint64_t chunk)
{
	const double before = hdemu_pending_us(0);
	for (uint64_t pos = 0; pos < size; pos += chunk)
		hdemu_transfer(0, path, pos, chunk);
	return hdemu_pending_us(0) - before;
}

static void off_costs_nothing()
{
	hdemu_set_profile("off");
	hdemu_reset();
	CHECK(!hdemu_enabled());
	read_file("/a", 1 << 20, 65536);
	hdemu_metadata(0, "/a", false);
	CHECK(hdemu_pending_us(0) == 0);
}

static void sequential_read_matches_transfer_rate()
{
	hdemu_set_profile("fast");
	hdemu_reset();
	const double us = read_file("/demo/data", 1 << 20, 65536);
	CHECK(us > 0.65e6 && us < 0.9e6);
}

static void slow_is_slower()
{
	hdemu_set_profile("fast");
	hdemu_reset();
	const double fast = read_file("/demo/data", 1 << 20, 65536);
	hdemu_set_profile("slow");
	hdemu_reset();
	const double slow = read_file("/demo/data", 1 << 20, 65536);
	CHECK(slow > fast * 2);
}

static void interleaved_files_pay_for_seeks()
{
	hdemu_set_profile("fast");
	hdemu_reset();
	const double one = read_file("/one", 8 * 4096, 4096);
	hdemu_reset();
	for (uint64_t pos = 0; pos < 4 * 4096; pos += 4096) {
		hdemu_transfer(0, "/one", pos, 4096);
		hdemu_transfer(0, "/two", pos, 4096);
	}
	CHECK(hdemu_pending_us(0) > one * 3);
}

static void cached_metadata_is_free()
{
	hdemu_set_profile("fast");
	hdemu_reset();
	hdemu_metadata(0, "/dir", true);
	const double first = hdemu_pending_us(0);
	hdemu_metadata(0, "/dir", true);
	CHECK(first > 0);
	CHECK(hdemu_pending_us(0) == first);
	hdemu_metadata(0, "/dir", false);
	CHECK(hdemu_pending_us(0) > first);
}

static void wait_follows_emulated_time()
{
	hdemu_set_profile("fast");
	hdemu_reset();
	read_file("/big", 1 << 20, 65536);
	std::atomic<bool> done { false };
	std::thread t([&] { hdemu_wait(0); done = true; });
	int frames = 0;
	while (!done && frames < 1000) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		hdemu_vsync(20000);
		frames++;
	}
	t.join();
	CHECK(frames >= 25 && frames <= 50);
	CHECK(hdemu_pending_us(0) <= 20000);
}

static void activity_follows_busy_time()
{
	hdemu_set_profile("fast");
	hdemu_reset();
	CHECK(hdemu_activity() == -1);
	hdemu_transfer(0, "/a", 0, 4096);
	CHECK(hdemu_activity() == 1);
	hdemu_vsync(20000);
	CHECK(hdemu_activity() == 1);
	for (int i = 0; i < 5; i++)
		hdemu_vsync(20000);
	CHECK(hdemu_activity() == 0);
	hdemu_set_profile("off");
	CHECK(hdemu_activity() == -1);
}

static void abort_releases_waiter()
{
	hdemu_set_profile("slow");
	hdemu_reset();
	read_file("/big", 1 << 20, 65536);
	std::thread t([] { hdemu_wait(0); });
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	hdemu_abort();
	t.join();
	hdemu_reset();
}

static void seeks_are_reported_while_spinning()
{
	hdemu_set_profile("fast");
	hdemu_reset();
	double seeks[64];
	hdemu_metadata(0, "/a", false);
	CHECK(hdemu_take_seeks(1e9, seeks, 64) == 0);
	hdemu_spinup();
	CHECK(hdemu_spinning());
	hdemu_metadata(0, "/b", false);
	hdemu_metadata(0, "/c", false);
	CHECK(hdemu_take_seeks(0, seeks, 64) == 0);
	CHECK(hdemu_take_seeks(1e9, seeks, 64) == 2);
	CHECK(seeks[0] > 0 && seeks[1] > seeks[0]);
	CHECK(hdemu_take_seeks(1e9, seeks, 64) == 0);
	hdemu_transfer(0, "/c", 0, 1 << 20);
	CHECK(hdemu_take_seeks(1e9, seeks, 64) == 7);
	hdemu_reset();
	CHECK(!hdemu_spinning());
}

int main()
{
	off_costs_nothing();
	sequential_read_matches_transfer_rate();
	slow_is_slower();
	interleaved_files_pay_for_seeks();
	cached_metadata_is_free();
	wait_follows_emulated_time();
	activity_follows_busy_time();
	abort_releases_waiter();
	seeks_are_reported_while_spinning();
	if (failures)
		return 1;
	std::puts("harddrive_emu: ok");
	return 0;
}
