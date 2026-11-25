/*
 * Mechanical hard drive emulation for directory (virtual) drives:
 * access timing, and the events harddrive_sound plays.
 */
#ifndef UAE_HARDDRIVE_EMU_H
#define UAE_HARDDRIVE_EMU_H

#include <cstdint>

/* "off", "fast" (A1200 IDE era) or "slow" (A590/early SCSI era). */
void hdemu_set_profile(const char *name);
bool hdemu_enabled();

/* Emulation thread, once per frame. */
void hdemu_vsync(double frame_us);
void hdemu_reset();
/* Releases waiting filesystem threads until the next hdemu_reset(). */
void hdemu_abort();
/* A directory drive is mounted; cleared by hdemu_reset(). */
void hdemu_spinup();

/* path identifies the object on the host; NULL means the volume root. */
void hdemu_transfer(int unit, const char *path, uint64_t pos, uint64_t bytes);
void hdemu_metadata(int unit, const char *path, bool cacheable);

/* Filesystem thread: blocks until the drive has caught up with its work. */
void hdemu_wait(int unit);
/* Whether any drive was busy during the last frame; -1 before any work. */
int hdemu_activity();
double hdemu_now_us();
/* When the busiest drive finishes its queued work. */
double hdemu_busy_until_us();
bool hdemu_spinning();
/* Removes the seek start times before until_us, in emulated microseconds. */
int hdemu_take_seeks(double until_us, double *out, int max);

/* Microseconds of queued work, for tests. */
double hdemu_pending_us(int unit);

#endif
