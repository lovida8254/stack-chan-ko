#ifndef _GESTURE_H
#define _GESTURE_H

#include <Avatar.h>

extern volatile uint32_t gesture_suppress_until;

void gesture_init();
void gesture_play(m5avatar::Expression e);

#endif  //_GESTURE_H
