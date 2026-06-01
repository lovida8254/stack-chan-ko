#include "FunctionCall.h"
#include "Sfx.h"
#include "NightMode.h"
#include "DiagLog.h"   // play_sound 진단 (FTP /diag.log)
#include <Avatar.h>
#include "Robot.h"
#include <AudioGeneratorMP3.h>
#include "driver/AudioOutputM5Speaker.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoYaml.h>
#include <time.h>
#include <SD.h>
#include <SPIFFS.h>
#include "driver/WakeWord.h"
#include "Scheduler.h"
#include "StackchanExConfig.h" 
#include "share/SDUtil.h"
#include "MCPClient.h"
#include "llm/LLMBase.h"
#include "Gesture.h"
using namespace m5avatar;



// 外部参照
extern Avatar avatar;
extern AudioGeneratorMP3 *mp3;
extern bool servo_home;
extern void sw_tone();

static String avatarText;

// タイマー機能関連
TimerHandle_t xAlarmTimer;
bool alarmTimerCallbacked = false;
bool alarmTimerCanceled = false;
void alarmTimerCallback(TimerHandle_t xTimer);
void powerOffTimerCallback(TimerHandle_t xTimer);



const String json_Functions =
"["
  "{"
    "\"name\": \"update_memory\","
    "\"description\": \"Update long-term memory.\","
    "\"parameters\": {"
      "\"type\":\"object\","
      "\"properties\": {"
        "\"memory\":{"
          "\"type\": \"string\","
          "\"description\": \"Summary of user attributes and memorable conversations.\""
        "}"
      "},"
      "\"required\": [\"memory\"]"
    "}"
  "},"
#if defined(REALTIME_API)
  "{"
    "\"name\": \"set_avatar_expression\","
    "\"description\": \"Change Stack-chan's facial expression. CALL THIS TOOL FREQUENTLY — every time the emotional tone of the conversation shifts (joy, sadness, anger, doubt, sleepiness, calm). Calling this often is REQUIRED and expected; not calling it makes the robot feel lifeless. Each call is silent visual feedback: never speak the expression value (neutral/happy/angry/sad/doubt/sleepy) aloud, and never mention this tool or the function call in your spoken or transcribed response. The user sees the expression change visually, never hears it.\","
    "\"parameters\": {"
      "\"type\":\"object\","
      "\"properties\": {"
        "\"expression\":{"
          "\"type\": \"string\","
          "\"description\": \"Facial expression to set. Internal enum value — never speak this aloud.\","
          "\"enum\": [\"neutral\", \"happy\", \"angry\", \"sad\", \"doubt\", \"sleepy\"]"
        "}"
      "},"
      "\"required\": [\"expression\"]"
    "}"
  "},"
#endif
  "{"
    "\"name\": \"timer\","
    "\"description\": \"指定した時間が経過したら、指定した動作を実行する。指定できる動作はalarmとshutdown。\","
    "\"parameters\": {"
      "\"type\":\"object\","
      "\"properties\": {"
        "\"time\":{"
          "\"type\": \"integer\","
          "\"description\": \"指定したい時間。単位は秒。\""
        "},"
        "\"action\":{"
          "\"type\": \"string\","
          "\"description\": \"指定したい動作。alarmは音で通知。shutdownは電源OFF。\","
          "\"enum\": [\"alarm\", \"shutdown\"]"
        "}"
      "},"
      "\"required\": [\"time\", \"action\"]"
    "}"
  "},"
  "{"
    "\"name\": \"timer_change\","
    "\"description\": \"実行中のタイマーの設定時間を変更する。\","
    "\"parameters\": {"
      "\"type\":\"object\","
      "\"properties\": {"
        "\"time\":{"
          "\"type\": \"integer\","
          "\"description\": \"変更後の時間。単位は秒。0の場合はタイマーをキャンセルする。\""
        "}"
      "},"
      "\"required\": [\"time\"]"
    "}"
  "},"
#if defined(ARDUINO_M5STACK_CORES3)
#if defined(ENABLE_WAKEWORD)
  "{"
    "\"name\": \"register_wakeword\","
    "\"description\": \"ウェイクワードを登録する。\","
    "\"parameters\": {"
      "\"type\":\"object\","
      "\"properties\": {}"
    "}"
  "},"
  "{"
    "\"name\": \"wakeword_enable\","
    "\"description\": \"ウェイクワードを有効化する。\","
    "\"parameters\": {"
      "\"type\":\"object\","
      "\"properties\": {}"
    "}"
  "},"
  "{"
    "\"name\": \"delete_wakeword\","
    "\"description\": \"ウェイクワードを削除する。\","
    "\"parameters\": {"
      "\"type\":\"object\","
      "\"properties\": {"
        "\"idx\":{"
          "\"type\": \"integer\","
          "\"description\": \"削除するウェイクワードの番号。\""
        "}"
      "},"
      "\"required\": [\"idx\"]"
    "}"
  "},"
#endif  //defined(ENABLE_WAKEWORD)
#endif  //defined(ARDUINO_M5STACK_CORES3)
  // === Custom: Korean external services ===
  "{"
    "\"name\": \"get_weather\","
    "\"description\": \"설정한 지역의 현재 날씨(상태, 기온, 체감온도, 습도)를 가져온다. 사용자가 날씨를 물으면 호출한다.\","
    "\"parameters\": { \"type\":\"object\", \"properties\": {} }"
  "},"
  "{"
    "\"name\": \"get_air_quality\","
    "\"description\": \"설정한 지역의 미세먼지(PM10/PM2.5)와 통합대기등급을 가져온다. 사용자가 미세먼지/공기질을 물으면 호출한다.\","
    "\"parameters\": { \"type\":\"object\", \"properties\": {} }"
  "},"
  "{"
    "\"name\": \"get_school_meal\","
    "\"description\": \"등록한 학교의 급식 메뉴를 가져온다. when=today/tomorrow/week 중 선택.\","
    "\"parameters\": {"
      "\"type\":\"object\","
      "\"properties\": {"
        "\"when\":{"
          "\"type\": \"string\","
          "\"description\": \"가져올 기간\","
          "\"enum\": [\"today\", \"tomorrow\", \"week\"]"
        "}"
      "},"
      "\"required\": [\"when\"]"
    "}"
  "},"
  "{"
    "\"name\": \"get_todos\","
    "\"description\": \"사용자의 활성 할일 목록(마감일/우선순위)을 가져온다. 사용자가 할일/투두/해야할일을 물으면 호출한다.\","
    "\"parameters\": { \"type\":\"object\", \"properties\": {} }"
  "},"
  "{"
    "\"name\": \"get_schedules\","
    "\"description\": \"사용자의 일정(오늘부터 다음달까지, KST 기준)을 가져온다. 사용자가 일정/스케줄/약속/캘린더를 물으면 호출한다.\","
    "\"parameters\": { \"type\":\"object\", \"properties\": {} }"
  "},"
  "{"
    "\"name\": \"play_sound\","
    "\"description\": \"사용자가 효과음·소리·노래를 재생/들려/틀어 달라고 하면(예: '생일 노래 틀어줘', '박수 소리 들려줘', 'OO 들려줘', 'OO 재생해줘', '효과음 내줘') 반드시 이 도구를 호출한다. 설정에 등록된 사운드 이름을 사용자가 말하면 그대로 name 에 넣어 호출한다. name 에는 사용자가 말한 이름을 그대로 넣는다(부분 일치로 찾음). 중요: 소리는 네 말이 끝난 '직후'에 재생된다(아직 안 들려준 상태). 그러니 '자, 들어봐!', '소리 나간다~', '들려줄게~' 처럼 곧 들려줄 것처럼 짧게 말하고, '잘 들었지?', '어땠어?' 처럼 이미 들려준 척은 절대 하지 마라.\","
    "\"parameters\": {"
      "\"type\":\"object\","
      "\"properties\": {"
        "\"name\":{ \"type\": \"string\", \"description\": \"재생할 사운드 이름 (예: 생일축하, 박수)\" }"
      "},"
      "\"required\": [\"name\"]"
    "}"
  "},"
  "{"
    "\"name\": \"set_sleep_mode\","
    "\"description\": \"사용자가 '잘자', '굿나잇', '이제 잘게' 처럼 자러 간다고 하면 sleep=true로 호출해 취침 모드(화면 어둡게+졸린 분위기)에 들어간다. '일어나', '굿모닝', '깨어나'처럼 깨우면 sleep=false로 호출해 평소 모드로 돌아온다. 호출 후엔 짧게 한국어로 다정하게 반응한다(예: '잘 자, 좋은 꿈 꿔').\","
    "\"parameters\": {"
      "\"type\":\"object\","
      "\"properties\": {"
        "\"sleep\":{ \"type\": \"boolean\", \"description\": \"true=취침 모드 진입, false=기상\" }"
      "},"
      "\"required\": [\"sleep\"]"
    "}"
  "},"
  // === end custom ===
  "{"
    "\"name\": \"get_date\","
    "\"description\": \"今日の日付を取得する。\","
    "\"parameters\": {"
      "\"type\":\"object\","
      "\"properties\": {}"
    "}"
  "},"
  "{"
    "\"name\": \"get_time\","
    "\"description\": \"現在の時刻を取得する。\","
    "\"parameters\": {"
      "\"type\":\"object\","
      "\"properties\": {}"
    "}"
  "},"
  "{"
    "\"name\": \"get_week\","
    "\"description\": \"今日が何曜日かを取得する。\","
    "\"parameters\": {"
      "\"type\":\"object\","
      "\"properties\": {}"
    "}"
#if !defined(USE_EXTENSION_FUNCTIONS)
  "}"
#else
  "},"
  "{"
    "\"name\": \"reminder\","
    "\"description\": \"指定した時間にリマインドする。\","
    "\"parameters\": {"
      "\"type\":\"object\","
      "\"properties\": {"
        "\"hour\":{"
          "\"type\": \"integer\","
          "\"description\": \"hour (0-23)\""
        "},"
        "\"min\":{"
          "\"type\": \"integer\","
          "\"description\": \"minute\""
        "},"
        "\"text\":{"
          "\"type\": \"string\","
          "\"description\": \"リマインドする内容\""
        "}"
      "},"
      "\"required\": [\"hour\",\"min\",\"text\"]"
    "}"
  "},"
  "{"
    "\"name\": \"ask\","
    "\"description\": \"依頼を実行するのに必要な情報を会話の相手に質問する。\","
    "\"parameters\": {"
      "\"type\":\"object\","
      "\"properties\": {"
        "\"text\":{"
          "\"type\": \"string\","
          "\"description\": \"質問の内容。\""
        "}"
      "}"
    "}"
  "},"
#endif //if defined(USE_EXTENSION_FUNCTIONS)
"]";


void alarmTimerCallback(TimerHandle_t _xTimer){
  xAlarmTimer = NULL;
  //Serial.println("時間になりました。");
  alarmTimerCallbacked = true;
}

void powerOffTimerCallback(TimerHandle_t _xTimer){
  xAlarmTimer = NULL;
  //Serial.println("おやすみなさい。");
  avatar.setSpeechText("おやすみなさい。");
  delay(2000);
  avatar.setSpeechText("");
  M5.Power.powerOff();
}


FunctionCall::FunctionCall(llm_param_t param, LLMBase* llm, MCPClient** mcpClient)
  : _param(param),
    _llm(llm),
    _mcpClient(mcpClient)
{

}

// Function Call関連の初期化
void FunctionCall::init_func_call_settings(StackchanExConfig& system_config)
{

}


String FunctionCall::exec_calledFunc(const char* name, const char* args){
  String response = "";

  Serial.println(name);
  Serial.println(args);

  DynamicJsonDocument argsDoc(256);
  DeserializationError error = deserializeJson(argsDoc, args);
  if (error) {
    Serial.print(F("deserializeJson(arguments) failed: "));
    Serial.println(error.f_str());
    avatar.setExpression(Expression::Sad);
    avatar.setSpeechText("エラーです");
    response = "エラーです";
    delay(1000);
    avatar.setSpeechText("");
    avatar.setExpression(Expression::Neutral);
  }else{

    //関数名がいずれかのMCPサーバに属するかを検索し、ヒットしたらリクエストを送信する
    for(int s=0; s < _param.llm_conf.nMcpServers; s++){
      if(true == _param.llm_conf.mcpServer[s].disabled){
          continue;
      }
      if(_mcpClient[s]->search_tool(String(name))){
        DynamicJsonDocument tool_params(512);
        tool_params["name"] = String(name);
        tool_params["arguments"] = argsDoc;
        response = _mcpClient[s]->mcp_call_tool(tool_params);
        goto END;
      }
    }

    if(strcmp(name, "update_memory") == 0){
      const char* memory = argsDoc["memory"];
      Serial.println(memory);
      response = fn_update_memory(_llm, memory);
    }
#if defined(REALTIME_API)
    else if(strcmp(name, "set_avatar_expression") == 0){
      const char* expression = argsDoc["expression"];
      response = set_avatar_expression(expression);
    }
#endif
    else if(strcmp(name, "timer") == 0){
      const int time = argsDoc["time"];
      const char* action = argsDoc["action"];
      Serial.printf("time:%d\n",time);
      Serial.println(action);
      response = timer(time, action);
    }
    else if(strcmp(name, "timer_change") == 0){
      const int time = argsDoc["time"];
      response = timer_change(time);    
    }
#if defined(ARDUINO_M5STACK_CORES3)
#if defined(ENABLE_WAKEWORD)
    else if(strcmp(name, "register_wakeword") == 0){
      response = register_wakeword();    
    }
    else if(strcmp(name, "wakeword_enable") == 0){
      response = wakeword_enable();    
    }
    else if(strcmp(name, "delete_wakeword") == 0){
      const int idx = argsDoc["idx"];
      Serial.printf("idx:%d\n",idx);   
      response = delete_wakeword(idx);    
    }
#endif  //defined(ENABLE_WAKEWORD)
#endif  //defined(ARDUINO_M5STACK_CORES3)
    else if(strcmp(name, "get_date") == 0){
      response = get_date();    
    }
    else if(strcmp(name, "get_time") == 0){
      response = get_time();    
    }
    else if(strcmp(name, "get_week") == 0){
      response = get_week();
    }
    // === Custom: Korean external services ===
    else if(strcmp(name, "get_weather") == 0){
      response = get_weather();
    }
    else if(strcmp(name, "get_air_quality") == 0){
      response = get_air_quality();
    }
    else if(strcmp(name, "get_school_meal") == 0){
      const char* when = argsDoc["when"];
      response = get_school_meal(when ? when : "today");
    }
    else if(strcmp(name, "get_todos") == 0){
      response = get_todos();
    }
    else if(strcmp(name, "get_schedules") == 0){
      response = get_schedules();
    }
    else if(strcmp(name, "play_sound") == 0){
      const char* sname = argsDoc["name"];
      bool ok = (sname && sname[0]) ? sfx_request_name(sname) : false;   // 큐 적재 → 발화 끝난 뒤 sfx_pump 가 재생
      diag_log("[sfx] play_sound tool called name='%s' matched=%d", sname ? sname : "", (int)ok);
      response = ok ? "{\"result\":\"재생할게요.\"}" : "{\"result\":\"그 이름의 사운드를 찾지 못했어요. 등록된 효과음이 없거나 이름이 달라요.\"}";
    }
    else if(strcmp(name, "set_sleep_mode") == 0){
      bool sleep = argsDoc["sleep"] | true;
      night_mode_force_sleep(sleep);
      response = sleep ? "{\"result\":\"취침 모드로 전환했어요.\"}" : "{\"result\":\"평소 모드로 돌아왔어요.\"}";
    }
    // === end custom ===
#if defined(USE_EXTENSION_FUNCTIONS)
    else if(strcmp(name, "reminder") == 0){
      const int hour = argsDoc["hour"];
      const int min = argsDoc["min"];
      const char* text = argsDoc["text"];
      response = reminder(hour, min, text);
    }
    else if(strcmp(name, "ask") == 0){
      const char* text = argsDoc["text"];
      Serial.println(text);
      response = ask(text);
    }
#endif  //if defined(USE_EXTENSION_FUNCTIONS)

  }

END:
  return response;
}


String FunctionCall::fn_update_memory(LLMBase* llm, const char* memory){
  String response = "";
  if(llm->enableMemory()){
    if(llm->save_userInfo(memory)){
      response = "Memory update successful.";
    }else{
      response = "Memory update failure.";
    }
  }
  else{
    response = "Memory is disabled.";
  }

  Serial.println(response);
  return response;
}

#if defined(REALTIME_API)
String FunctionCall::set_avatar_expression(const char* expression){
  if(expression == NULL){
    return "Avatar expression is missing.";
  }

  String expressionName = String(expression);
  expressionName.toLowerCase();

  Expression avatarExpression = Expression::Neutral;
  if(expressionName.equals("neutral")){
    avatarExpression = Expression::Neutral;
  }
  else if(expressionName.equals("happy")){
    avatarExpression = Expression::Happy;
  }
  else if(expressionName.equals("angry")){
    avatarExpression = Expression::Angry;
  }
  else if(expressionName.equals("sad")){
    avatarExpression = Expression::Sad;
  }
  else if(expressionName.equals("doubt")){
    avatarExpression = Expression::Doubt;
  }
  else if(expressionName.equals("sleepy")){
    avatarExpression = Expression::Sleepy;
  }
  else{
    return "Unknown avatar expression: " + expressionName;
  }

  avatar.setExpression(avatarExpression);
  gesture_play(avatarExpression);
  return "Avatar expression set to " + expressionName + ".";
}
#endif


String FunctionCall::timer(int32_t time, const char* action){
  String response = "";

  if(xAlarmTimer != NULL){
    response = "別のタイマーを実行中です。";
  }
  else{
    if(strcmp(action, "alarm") == 0){
      xAlarmTimer = xTimerCreate("Timer", time * 1000, pdFALSE, 0, alarmTimerCallback);
      if(xAlarmTimer != NULL){
        xTimerStart(xAlarmTimer, 0);
        response = String("アラーム設定成功。") ;
      }
      else{
        response = "タイマーの設定が失敗しました。";
      }
    }
    else if(strcmp(action, "shutdown") == 0){
      xAlarmTimer = xTimerCreate("Timer", time * 1000, pdFALSE, 0, powerOffTimerCallback);
      if(xAlarmTimer != NULL){
        xTimerStart(xAlarmTimer, 0);
        response = String(time) + "秒後にパワーオフします。";
      }
      else{
        response = "タイマー設定が失敗しました。";
      }
    }
  }

  Serial.println(response);
  return response;
}

String FunctionCall::timer_change(int32_t time){
  String response = "";
  if(time == 0){
    xTimerDelete(xAlarmTimer, 0);
    xAlarmTimer = NULL;
    response = "タイマーをキャンセルしました。";
    alarmTimerCanceled = true;
  }
  else{
    xTimerChangePeriod(xAlarmTimer, time * 1000, 0);
    response = "タイマーの設定時間を" + String(time) + "秒に変更しました。";
  }

  return response;
}


String FunctionCall::get_date(){
  String response = "";
  struct tm timeInfo; 

  if (getLocalTime(&timeInfo)) {                            // timeinfoに現在時刻を格納
    response = String(timeInfo.tm_year + 1900) + "年"
               + String(timeInfo.tm_mon + 1) + "月"
               + String(timeInfo.tm_mday) + "日";
  }
  else{
    response = "時刻取得に失敗しました。";
  }
  return response;
}

String FunctionCall::get_time(){
  String response = "";
  struct tm timeInfo; 

  if (getLocalTime(&timeInfo)) {                            // timeinfoに現在時刻を格納
    response = String(timeInfo.tm_hour) + "時" + String(timeInfo.tm_min) + "分";
  }
  else{
    response = "時刻取得に失敗しました。";
  }
  return response;
}


String FunctionCall::get_week(){
  String response = "";
  struct tm timeInfo; 
  const char week[][8] = {"日", "月", "火", "水", "木", "金", "土"};

  if (getLocalTime(&timeInfo)) {                            // timeinfoに現在時刻を格納
    response = String(week[timeInfo.tm_wday]) + "曜日";
  }
  else{
    response = "時刻取得に失敗しました。";
  }
  return response;
}

#if defined(ARDUINO_M5STACK_CORES3)
#if defined(ENABLE_WAKEWORD)
bool register_wakeword_required = false;
String FunctionCall::register_wakeword(void){
  String response = "ウェイクワードを登録します。合図の後にウェイクワードを発声してください。";
  register_wakeword_required = true;
  return response;
}

bool wakeword_enable_required = false;
String FunctionCall::wakeword_enable(void){
  String response = "ウェイクワードを有効化しました。";
  wakeword_enable_required = true;
  return response;
}

String FunctionCall::delete_wakeword(int idx){
  SPIFFS.begin(true);
  String filename = filename_base + String(idx) + String(".bin");
  if (SPIFFS.exists(filename.c_str()))
  {
    SPIFFS.remove(filename.c_str());
    delete_mfcc(idx);
  }
  String response = String("ウェイクワード#") + String(idx) + String("を削除しました。");
  return response;
}
#endif  //defined(ENABLE_WAKEWORD)
#endif  //defined(ARDUINO_M5STACK_CORES3)


#if defined(USE_EXTENSION_FUNCTIONS)

String FunctionCall::reminder(int hour, int min, const char* text){
  String response = "";
  int ret;
  
  Serial.println("reminder");
  Serial.printf("%d:%d\n", hour, min);
  Serial.println(text);

  add_schedule(new ScheduleReminder(hour, min, String(text)));
  
  //response = String("Reminder setting successful");
  response = String(String("リマインドの設定成功。")
                    + String(hour) + ":" + String(min) + " "
                    + String(text));
  
  avatarText = String(hour) + ":" + String(min) + String("に設定しました");
  avatar.setSpeechText(avatarText.c_str());
  delay(2000);
  return response;
}


String FunctionCall::ask(const char* text){

  bool prev_servo_home = servo_home;
//#ifdef USE_SERVO
  servo_home = true;
//#endif
  avatar.setExpression(Expression::Happy);
  robot->speech(String(text));
  sw_tone();
  avatar.setSpeechText("どうぞ話してください");
  String ret = robot->listen();
//#ifdef USE_SERVO
  servo_home = prev_servo_home;
//#endif
  Serial.println("音声認識終了");
  if(ret != "") {
    Serial.println(ret);

  } else {
    Serial.println("音声認識失敗");
    avatar.setExpression(Expression::Sad);
    avatar.setSpeechText("聞き取れませんでした");
    delay(2000);

  } 

  avatar.setSpeechText("");
  avatar.setExpression(Expression::Neutral);
  //M5.Speaker.begin();

  return ret;
}


String FunctionCall::save_note(const char* text){
  String response = "";
  String filename = String(APP_DATA_PATH) + String(FNAME_NOTEPAD);
  
  if (SD.begin(GPIO_NUM_4, SPI, 25000000)) {
    auto fs = SD.open(filename.c_str(), FILE_WRITE, true);

    if(fs) {
      if(note == ""){
        note = String(text);
      }
      else{
        note = note + "\n" + String(text);
      }
      Serial.println(note);
      fs.write((uint8_t*)note.c_str(), note.length());
      fs.close();
      // SD.end() しない: CoreS3 では SD と LCD が SPI バスを共有しており、ここで
      // アンマウントすると以後の写真表示(PhotoFrame)等が「ファイルなし」で全滅する。
      // fs.close() でハンドルは閉じているのでマウントは維持してよい。
      //response = "Note saved successfully";
      response = String("メモの保存成功。メモの内容：" + String(text));
    }
    else{
      response = "メモの保存に失敗しました";
    }

  }
  else{
    response = "メモの保存に失敗しました";
  }
  return response;
}

String FunctionCall::read_note(){
  String response = "";
  if(note == ""){
    response = "メモはありません";
  }
  else{
    response = "メモの内容は次の通り。" + note;
  }
  return response;
}

String FunctionCall::delete_note(){
  String response = "";
  String filename = String(APP_DATA_PATH) + String(FNAME_NOTEPAD);


  if (SD.begin(GPIO_NUM_4, SPI, 25000000)) {
    auto fs = SD.open(filename.c_str(), FILE_WRITE, true);

    if(fs) {
      note = "";
      fs.write((uint8_t*)note.c_str(), note.length());
      fs.close();
      // SD.end() しない(save_note と同じ理由: 共有 SPI バスのアンマウントで写真が全滅)。
      response = "メモを消去しました";
    }
    else{
      response = "メモの消去に失敗しました";
    }
  }
  else{
    response = "メモの消去に失敗しました";
  }
  return response;
}


String FunctionCall::get_bus_time(int nNext){
  String response = "";
  String filename = "";
  int now;
  int nNextCnt = 0;
  struct tm timeInfo;

  if (getLocalTime(&timeInfo)) {                            // timeinfoに現在時刻を格納
    Serial.printf("現在時刻 %02d:%02d  曜日 %d\n", timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_wday);
    now = timeInfo.tm_hour * 60 + timeInfo.tm_min;
    
    switch(timeInfo.tm_wday){
      case 0:   //日
        filename = String(APP_DATA_PATH) + (FNAME_BUS_TIMETABLE_HOLIDAY);
        break;
      case 1:   //月
      case 2:   //火
      case 3:   //水
      case 4:   //木
      case 5:   //金
        filename = String(APP_DATA_PATH) + (FNAME_BUS_TIMETABLE);
        break;
      case 6:   //土
        filename = String(APP_DATA_PATH) + (FNAME_BUS_TIMETABLE_SAT);
        break;
    }

    if (SD.begin(GPIO_NUM_4, SPI, 25000000)) {
      auto fs = SD.open(filename.c_str(), FILE_READ);

      if(fs) {
        int hour, min;
        size_t max = 8;
        char buf[max];

        for(int line=0; line<200; line++){
          int len = fs.readBytesUntil(0x0a, (uint8_t*)buf, max);
          if(len == 0){
            Serial.printf("End of file. total line : %d\n", line);
            response = "最後の発車時刻を過ぎました。";
            break;
          }
          else{
            sscanf(buf, "%d:%d", &hour, &min);
            Serial.printf("%03d %02d:%02d\n", line, hour, min);

            int table = hour * 60 + min;
            if(now < table){
              if(nNextCnt == nNext){
                response = String(hour) + "時" + String(min) + "分";
                break;
              }
              else{
                nNextCnt ++;
              }
            }

          }
        }
        fs.close();

      } else {
        Serial.println("Failed to SD.open().");
        response = "時刻表の読み取りに失敗しました。";
      }

      // SD.end() しない(save_note と同じ理由: 共有 SPI バスのアンマウントで写真が全滅)。
    }
    else{
      response = "時刻表の読み取りに失敗しました。";
    }
  }
  else{
    response = "現在時刻の取得に失敗しました。";
  }

  return response;
}


//メッセージをメールで送信する関数
// ※EMailSenderライブラリのインクルード、及びplatformio.iniのlib_depsでの宣言を有効化してください。
String FunctionCall::send_mail(String msg) {
  String response = "";
  EMailSender::EMailMessage message;

  if (authMailAdr != "") {

    EMailSender emailSend(authMailAdr.c_str(), authAppPass.c_str());

    message.subject = "スタックチャンからの通知";
    message.message = msg;
    EMailSender::Response resp = emailSend.send(toMailAdr.c_str(), message);

    if(resp.status == true){
      response = "メール送信成功";
    }
    else{
      response = "メール送信失敗";
    }

  }
  else{
    response = "メールアカウント情報のエラー";
  }


  return response;
}

//受信したメールを読み上げる
String FunctionCall::read_mail(void) {
  String response = "";

  if(recvMessages.size() > 0){
    response = String(recvMessages[0]);
    recvMessages.pop_front();
    prev_nMail = recvMessages.size();
  }
  else{
    response = "受信メールはありません。";
  }

  return response;
}


#endif  //if defined(USE_EXTENSION_FUNCTIONS)


// =====================================================================================
// Custom Korean external service tools
// =====================================================================================

// Lazy-load Nexusive Bearer from SD's SC_SecConfig.yaml (custom apikey.nexusive).
static String& nexusive_key() {
  static String key;
  static bool loaded = false;
  if (loaded) return key;
  loaded = true;
  File f = SD.open("/yaml/SC_SecConfig.yaml", "r");
  if (!f) return key;
  String body = f.readString();
  f.close();
  DynamicJsonDocument doc(8192);
  if (deserializeYml(doc, body.c_str())) return key;
  const char* k = doc["apikey"]["nexusive"] | "";
  key = String(k);
  return key;
}

// HTTPS GET with insecure mode (skips cert validation — acceptable for personal/dev).
// Watch for transient WS audio dropouts: this competes with the Realtime WebSocket.
static String fc_https_get(const String& url, const String& bearer = String()) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(8);
  HTTPClient http;
  if (!http.begin(client, url)) return String();
  if (bearer.length() > 0) http.addHeader("Authorization", "Bearer " + bearer);
  int code = http.GET();
  String body;
  if (code == 200) body = http.getString();
  else Serial.printf("[fn] GET %s -> http %d\n", url.c_str(), code);
  http.end();
  return body;
}

static String fc_http_get(const String& url) {
  WiFiClient client;
  client.setTimeout(8);
  HTTPClient http;
  if (!http.begin(client, url)) return String();
  int code = http.GET();
  String body;
  if (code == 200) body = http.getString();
  else Serial.printf("[fn] GET %s -> http %d\n", url.c_str(), code);
  http.end();
  return body;
}

// NTP is synced to KST via GMT_OFFSET, so localtime_r() yields KST directly.
static String fc_ymd_kst(int dayOffset) {
  time_t now = time(nullptr) + (time_t)dayOffset * 86400;
  struct tm t;
  localtime_r(&now, &t);
  char buf[16];
  snprintf(buf, sizeof(buf), "%04d%02d%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  return String(buf);
}

static String fc_ymd_dash_kst(int dayOffset) {
  String s = fc_ymd_kst(dayOffset);
  return s.substring(0, 4) + "-" + s.substring(4, 6) + "-" + s.substring(6, 8);
}

// ----- Background-refreshed caches (avoid HTTPS during live Realtime WebSocket) -----
// Tools read these instantly; data_refresh_loop fills them every ~5 minutes.
static String g_cache_weather;
static String g_cache_air;
static String g_cache_meals_week;   // entire current week; get_school_meal filters by 'when'
static String g_cache_todos;
static String g_cache_schedules;
static volatile bool g_refresh_task_started = false;

// ----- Internal fetchers (called only from the refresh task, never on the WS path) -----
static String do_fetch_weather() {
  // 날씨 지역: 본인 도시명으로 변경하세요 (wttr.in 도시명. 예: Seoul, Busan, Daegu).
  String body = fc_http_get("http://wttr.in/Seoul?format=%l|%C|%t|%f|%h&lang=ko");
  if (body.length() == 0) return String();
  body.trim();
  int p1 = body.indexOf('|');
  int p2 = body.indexOf('|', p1 + 1);
  int p3 = body.indexOf('|', p2 + 1);
  int p4 = body.indexOf('|', p3 + 1);
  if (p4 < 0) return String();
  return String("{\"location\":\"") + body.substring(0, p1) +
         "\",\"condition\":\"" + body.substring(p1 + 1, p2) +
         "\",\"temperature\":\"" + body.substring(p2 + 1, p3) +
         "\",\"feelsLike\":\"" + body.substring(p3 + 1, p4) +
         "\",\"humidity\":\"" + body.substring(p4 + 1) + "\"}";
}

static String do_fetch_air() {
  // 미세먼지 측정소: stationName 을 본인 측정소명(URL 인코딩)으로 변경. 예: 종로구=%EC%A2%85%EB%A1%9C%EA%B5%AC
  String body = fc_https_get("https://k-skill-proxy.nomadamas.org/v1/fine-dust/report?stationName=%EC%A2%85%EB%A1%9C%EA%B5%AC");
  if (body.length() == 0) return String();
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, body)) return String();
  if (doc.containsKey("error")) return String();
  DynamicJsonDocument out(1024);
  out["station"] = doc["station_name"].as<const char*>();
  out["measuredAt"] = doc["measured_at"].as<const char*>();
  out["pm10"] = doc["pm10"]["value"];
  out["pm10grade"] = doc["pm10"]["grade"].as<const char*>();
  out["pm25"] = doc["pm25"]["value"];
  out["pm25grade"] = doc["pm25"]["grade"].as<const char*>();
  out["airGrade"] = doc["khai_grade"].as<const char*>();
  String result;
  serializeJson(out, result);
  return result;
}

static String do_fetch_meals_week() {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  int dow = t.tm_wday;                          // 0=Sun .. 6=Sat
  int toMonday = (dow == 0) ? -6 : (1 - dow);   // Sunday → previous Monday
  // 급식: 본인 학교의 NEIS 코드로 변경하세요. ATPT_OFCDC_SC_CODE=시도교육청코드, SD_SCHUL_CODE=학교코드.
  // 코드 조회: https://open.neis.go.kr (학교기본정보 API) 또는 학교알리미. 아래는 예시값이라 그대로면 동작하지 않습니다.
  String url = "https://open.neis.go.kr/hub/mealServiceDietInfo?Type=json&ATPT_OFCDC_SC_CODE=B10&SD_SCHUL_CODE=0000000&MLSV_FROM_YMD=" +
               fc_ymd_kst(toMonday) + "&MLSV_TO_YMD=" + fc_ymd_kst(toMonday + 4);
  String body = fc_https_get(url);
  if (body.length() == 0) return String();
  DynamicJsonDocument doc(16384);
  if (deserializeJson(doc, body)) return String();
  JsonVariant rowsVar = doc["mealServiceDietInfo"][1]["row"];
  if (rowsVar.isNull()) return String("{\"meals\":[]}");
  DynamicJsonDocument out(8192);
  JsonArray meals = out.createNestedArray("meals");
  for (JsonObject row : rowsVar.as<JsonArray>()) {
    JsonObject m = meals.createNestedObject();
    m["date"] = row["MLSV_YMD"].as<const char*>();
    String dish = row["DDISH_NM"].as<String>();
    dish.replace("<br/>", ", ");
    dish.replace("<br>", ", ");
    String cleaned;
    cleaned.reserve(dish.length());
    int depth = 0;
    for (size_t i = 0; i < dish.length(); i++) {
      char c = dish.charAt(i);
      if (c == '(') depth++;
      else if (c == ')') { if (depth > 0) depth--; }
      else if (depth == 0) cleaned += c;
    }
    cleaned.trim();
    m["dishes"] = cleaned;
  }
  String result;
  serializeJson(out, result);
  return result;
}

static String do_fetch_todos() {
  String key = nexusive_key();
  if (key.length() == 0) return String();
  String body = fc_https_get("https://nexusive.com/api/v1/todos?status=active", key);
  if (body.length() == 0) return String();
  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, body)) return String();
  if (!doc["success"].as<bool>()) return String();
  DynamicJsonDocument out(4096);
  out["today"] = fc_ymd_dash_kst(0);
  JsonArray arr = out.createNestedArray("todos");
  for (JsonObject t : doc["data"]["todos"].as<JsonArray>()) {
    JsonObject o = arr.createNestedObject();
    o["title"] = t["title"].as<const char*>();
    if (!t["due_date"].isNull()) {
      String due = t["due_date"].as<String>();
      o["due"] = due.substring(0, 10);
    }
    o["priority"] = t["priority"].as<const char*>();
    o["category"] = t["category"].as<const char*>();
  }
  String result;
  serializeJson(out, result);
  return result;
}

static String do_fetch_schedules() {
  String key = nexusive_key();
  if (key.length() == 0) return String();
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  int y = t.tm_year + 1900, m = t.tm_mon + 1;
  int ny = (m == 12) ? y + 1 : y;
  int nm = (m == 12) ? 1 : m + 1;

  DynamicJsonDocument out(8192);
  out["today"] = fc_ymd_dash_kst(0);
  JsonArray arr = out.createNestedArray("schedules");
  String todayStr = fc_ymd_dash_kst(0);

  for (int pass = 0; pass < 2; pass++) {
    int qy = (pass == 0) ? y : ny;
    int qm = (pass == 0) ? m : nm;
    char url[128];
    snprintf(url, sizeof(url), "https://nexusive.com/api/v1/schedules?year=%d&month=%d", qy, qm);
    String body = fc_https_get(url, key);
    if (body.length() == 0) continue;
    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, body)) continue;
    if (!doc["success"].as<bool>()) continue;
    for (JsonObject s : doc["data"]["schedules"].as<JsonArray>()) {
      String start = s["start_datetime"].as<String>();
      String date = start.substring(0, 10);
      if (date < todayStr) continue;
      JsonObject o = arr.createNestedObject();
      o["title"] = s["title"].as<const char*>();
      o["date"] = date;
      if (!s["all_day"].as<bool>()) {
        o["time"] = start.substring(11, 16);
      }
    }
  }
  String result;
  serializeJson(out, result);
  return result;
}

// Filter the cached weekly meal data down to today / tomorrow / week.
static String filter_meals_by_when(const char* when) {
  if (g_cache_meals_week.length() == 0) {
    return "{\"status\":\"meal info not ready, ask again in a moment\"}";
  }
  if (strcmp(when, "week") == 0) return g_cache_meals_week;
  DynamicJsonDocument week(16384);
  if (deserializeJson(week, g_cache_meals_week)) return g_cache_meals_week;
  String targetYmd = fc_ymd_kst(strcmp(when, "tomorrow") == 0 ? 1 : 0);
  DynamicJsonDocument out(4096);
  JsonArray meals = out.createNestedArray("meals");
  for (JsonObject m : week["meals"].as<JsonArray>()) {
    if (String(m["date"].as<const char*>()) == targetYmd) {
      JsonObject o = meals.createNestedObject();
      o["date"] = m["date"].as<const char*>();
      o["dishes"] = m["dishes"].as<const char*>();
    }
  }
  if (meals.size() == 0) {
    out["status"] = "no meal info for that day";
  }
  String result;
  serializeJson(out, result);
  return result;
}

// FreeRTOS task: refresh all 5 caches every 5 minutes. Pinned to APP_CPU so it
// doesn't fight the WiFi/audio tasks on PRO_CPU.
static void data_refresh_loop(void* arg) {
  // Initial delay so WiFi connect + NTP sync are complete before the first fetch.
  vTaskDelay(pdMS_TO_TICKS(12000));
  for (;;) {
    Serial.println("[fn] data refresh: starting");
    String s;
    s = do_fetch_weather();    if (s.length() > 0) g_cache_weather = s;
    s = do_fetch_air();        if (s.length() > 0) g_cache_air = s;
    s = do_fetch_meals_week(); if (s.length() > 0) g_cache_meals_week = s;
    s = do_fetch_todos();      if (s.length() > 0) g_cache_todos = s;
    s = do_fetch_schedules();  if (s.length() > 0) g_cache_schedules = s;
    Serial.printf("[fn] data refresh done: w=%u air=%u meals=%u todos=%u sched=%u\n",
                  (unsigned)g_cache_weather.length(),
                  (unsigned)g_cache_air.length(),
                  (unsigned)g_cache_meals_week.length(),
                  (unsigned)g_cache_todos.length(),
                  (unsigned)g_cache_schedules.length());
    vTaskDelay(pdMS_TO_TICKS(5 * 60 * 1000));
  }
}

void start_external_data_prefetch() {
  if (g_refresh_task_started) return;
  g_refresh_task_started = true;
  xTaskCreatePinnedToCore(data_refresh_loop, "data_refresh",
                          16384, nullptr, 1, nullptr, APP_CPU_NUM);
  Serial.println("[fn] background data_refresh task started");
}

// ----- Public tool entry points: return cached data (no network I/O here) -----
String FunctionCall::get_weather() {
  if (g_cache_weather.length() == 0) return "{\"status\":\"weather not ready, ask again in a moment\"}";
  return g_cache_weather;
}
String FunctionCall::get_air_quality() {
  if (g_cache_air.length() == 0) return "{\"status\":\"air quality not ready, ask again in a moment\"}";
  return g_cache_air;
}
String FunctionCall::get_school_meal(const char* when) {
  return filter_meals_by_when(when ? when : "today");
}
String FunctionCall::get_todos() {
  if (g_cache_todos.length() == 0) return "{\"status\":\"todos not ready, ask again in a moment\"}";
  return g_cache_todos;
}
String FunctionCall::get_schedules() {
  if (g_cache_schedules.length() == 0) return "{\"status\":\"schedules not ready, ask again in a moment\"}";
  return g_cache_schedules;
}


