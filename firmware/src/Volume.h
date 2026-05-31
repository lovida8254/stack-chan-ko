#ifndef _VOLUME_H
#define _VOLUME_H

// Speaker volume helpers with SPIFFS-backed persistence.
// Range: 0..255 (M5.Speaker.setVolume scale). Default 120.
//
//   volume_init()       Called once at boot, AFTER M5.Speaker.config / first setVolume.
//                       Loads /volume.txt from SPIFFS if present and applies it.
//   volume_get()        Returns the current in-memory volume.
//   volume_set(v)       Clamps to 0..255, applies via M5.Speaker.setVolume, persists.

extern int  volume_get();
extern bool volume_set(int v);
extern void volume_init();

#endif
