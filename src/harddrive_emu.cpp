/*
 * Mechanical hard drive emulation for directory (virtual) drives.
 *
 * A directory drive has no geometry, so every host path gets a home cylinder
 * from its hash and file offsets move on from there. Each request adds its
 * cost to the drive's busy time, counted in emulated time, and the filesystem
 * thread holds back its reply once the drive is more than a frame behind.
 */
#include "harddrive_emu.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace {

struct Profile {
	const char *name;
	double track_to_track_us;
	double full_stroke_us;
	double rpm;
	double bytes_per_us;
	double command_us;
	int cylinders;
	uint64_t bytes_per_cylinder;
};

constexpr Profile profiles[] = {
	/* ~1993 2.5" IDE drive on the A600/A1200 Gayle port */
	{ "fast", 3000, 25000, 3600, 1.5, 500, 1000, 128 * 1024 },
	/* ~1989 SCSI drive on an A590/A2091 */
	{ "slow", 8000, 60000, 3600, 0.6, 1500, 800, 64 * 1024 },
};

constexpr int MAX_UNITS = 64;
constexpr int CACHE_SIZE = 32;
constexpr double BLOCK_BYTES = 512;

struct Drive {
	int cylinder;
	uint64_t last_file;
	uint64_t last_end;
	double busy_until;
	uint64_t cache[CACHE_SIZE];
	int cache_next;
};

std::mutex mutex;
std::condition_variable wake;
const Profile *profile;
double now_us;
double frame_us = 20000;
bool aborted;
Drive drives[MAX_UNITS];

uint64_t hash_path(const char *path)
{
	uint64_t h = 1469598103934665603ull;
	for (const char *p = path ? path : ""; *p; p++)
		h = (h ^ (unsigned char)*p) * 1099511628211ull;
	return h ? h : 1;
}

Drive *drive_for(int unit)
{
	return unit >= 0 && unit < MAX_UNITS ? &drives[unit] : nullptr;
}

double seek_to(Drive &d, int cylinder)
{
	const int distance = std::abs(cylinder - d.cylinder);
	d.cylinder = cylinder;
	if (!distance)
		return 0;
	const double fraction = (double)distance / profile->cylinders;
	return profile->track_to_track_us
		+ (profile->full_stroke_us - profile->track_to_track_us) * std::sqrt(fraction);
}

double half_revolution()
{
	return 30e6 / profile->rpm;
}

int cylinder_of(uint64_t file, uint64_t pos)
{
	return (int)((file + pos / profile->bytes_per_cylinder) % (uint64_t)profile->cylinders);
}

void charge(Drive &d, double us)
{
	d.busy_until = std::max(d.busy_until, now_us) + us;
}

bool cache_hit(Drive &d, uint64_t h)
{
	for (uint64_t c : d.cache) {
		if (c == h)
			return true;
	}
	d.cache[d.cache_next] = h;
	d.cache_next = (d.cache_next + 1) % CACHE_SIZE;
	return false;
}

}

void hdemu_set_profile(const char *name)
{
	std::lock_guard<std::mutex> l(mutex);
	profile = nullptr;
	for (const Profile &p : profiles) {
		if (name && !strcmp(name, p.name))
			profile = &p;
	}
	wake.notify_all();
}

bool hdemu_enabled()
{
	std::lock_guard<std::mutex> l(mutex);
	return profile != nullptr;
}

void hdemu_vsync(double frame_duration_us)
{
	std::lock_guard<std::mutex> l(mutex);
	if (frame_duration_us > 0)
		frame_us = frame_duration_us;
	now_us += frame_us;
	wake.notify_all();
}

void hdemu_reset()
{
	std::lock_guard<std::mutex> l(mutex);
	aborted = false;
	now_us = 0;
	memset(drives, 0, sizeof drives);
}

void hdemu_abort()
{
	std::lock_guard<std::mutex> l(mutex);
	aborted = true;
	wake.notify_all();
}

void hdemu_transfer(int unit, const char *path, uint64_t pos, uint64_t bytes)
{
	std::lock_guard<std::mutex> l(mutex);
	Drive *d = drive_for(unit);
	if (!profile || !d || !bytes)
		return;
	const uint64_t file = hash_path(path);
	const uint64_t first = pos / profile->bytes_per_cylinder;
	const uint64_t last = (pos + bytes - 1) / profile->bytes_per_cylinder;
	double us = profile->command_us + seek_to(*d, cylinder_of(file, pos));
	if (file != d->last_file || pos != d->last_end)
		us += half_revolution();
	us += bytes / profile->bytes_per_us;
	us += (last - first) * profile->track_to_track_us;
	d->cylinder = cylinder_of(file, pos + bytes - 1);
	d->last_file = file;
	d->last_end = pos + bytes;
	charge(*d, us);
}

void hdemu_metadata(int unit, const char *path, bool cacheable)
{
	std::lock_guard<std::mutex> l(mutex);
	Drive *d = drive_for(unit);
	if (!profile || !d)
		return;
	const uint64_t h = hash_path(path);
	if (cacheable && cache_hit(*d, h))
		return;
	d->last_file = 0;
	charge(*d, profile->command_us + seek_to(*d, cylinder_of(h, 0))
		+ half_revolution() + BLOCK_BYTES / profile->bytes_per_us);
}

void hdemu_wait(int unit)
{
	std::unique_lock<std::mutex> l(mutex);
	Drive *d = drive_for(unit);
	if (!d)
		return;
	wake.wait(l, [d] { return aborted || !profile || d->busy_until <= now_us + frame_us; });
}

int hdemu_activity()
{
	std::lock_guard<std::mutex> l(mutex);
	if (!profile)
		return -1;
	int activity = -1;
	for (const Drive &d : drives) {
		if (d.busy_until <= 0)
			continue;
		if (d.busy_until > now_us - frame_us)
			return 1;
		activity = 0;
	}
	return activity;
}

double hdemu_pending_us(int unit)
{
	std::lock_guard<std::mutex> l(mutex);
	Drive *d = drive_for(unit);
	return d ? std::max(0.0, d->busy_until - now_us) : 0;
}
