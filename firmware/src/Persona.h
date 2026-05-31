#ifndef _PERSONA_H
#define _PERSONA_H

#include <Arduino.h>

// Persona presets: a small library of selectable character prompts (role).
// The ACTIVE preset's role is what the LLM uses (kept in sync with /data.json
// messages[USER_ROLE]). Long-term memory (User Info) is shared across presets.
//
// Storage: SPIFFS /personas.json  {"active":N,"presets":[{"name","role"},...]}
// On first boot the "기본" preset is seeded from the current /data.json role so the
// existing (carefully tuned) family persona is preserved; the others are drafts.

void   persona_init();
String persona_get_json();                      // {"active":N,"presets":[...]}
bool   persona_set_json(const String& json);    // {"active":N,"presets":[...],"apply":bool}

#endif  // _PERSONA_H
