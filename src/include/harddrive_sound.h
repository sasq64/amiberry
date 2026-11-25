/*
 * Hard drive sounds for directory drives, from 86Box's hdd_audio samples.
 * Plays the spindle and the seeks that harddrive_emu reports.
 */
#ifndef UAE_HARDDRIVE_SOUND_H
#define UAE_HARDDRIVE_SOUND_H

#include <cstdint>

/* Loads <dir>/<profile>/{spinup,running,spindown,seek}.wav; NULL profile disables. */
bool hdsnd_configure(const char *dir, const char *profile);
bool hdsnd_enabled();
/* 1 is 86Box's level. */
void hdsnd_set_volume(float gain);
/* Fades the drive out after this long without drive work; 0 never does. */
void hdsnd_set_idle_off(double seconds);

/* Adds the drive to interleaved stereo frames, as they leave the emulation. */
void hdsnd_mix(int16_t *stereo, unsigned frames, int rate);

#endif
