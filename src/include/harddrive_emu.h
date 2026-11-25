/*
 * Mechanical hard drive emulation for directory (virtual) drives:
 * access timing now, drive sounds later.
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

/* path identifies the object on the host; NULL means the volume root. */
void hdemu_transfer(int unit, const char *path, uint64_t pos, uint64_t bytes);
void hdemu_metadata(int unit, const char *path, bool cacheable);

/* Filesystem thread: blocks until the drive has caught up with its work. */
void hdemu_wait(int unit);
/* Whether any drive was busy during the last frame; -1 before any work. */
int hdemu_activity();
/* Microseconds of queued work, for tests. */
double hdemu_pending_us(int unit);

#endif
