#include "ServoCustom.h"
#include "ServoTrim.h"   // g_servoTrimX/Y — 홈 보정 오프셋(모든 이동에 더함)

void ServoCustom::moveToOrigin(){
  moveXY(_init_param.servo[AXIS_X].start_degree + g_servoTrimX,
         _init_param.servo[AXIS_Y].start_degree + g_servoTrimY, 1000);
}

void ServoCustom::moveTo(int degX, int degY){
  moveXY(_init_param.servo[AXIS_X].start_degree + g_servoTrimX + degX,
         _init_param.servo[AXIS_Y].start_degree + g_servoTrimY + degY, 1000);
}

void ServoCustom::moveTo(int degX, int degY, uint32_t millis_for_move){
  moveXY(_init_param.servo[AXIS_X].start_degree + g_servoTrimX + degX,
         _init_param.servo[AXIS_Y].start_degree + g_servoTrimY + degY, millis_for_move);
}
