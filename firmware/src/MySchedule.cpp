#include <Arduino.h>
#include <time.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "Scheduler.h"
#include "MySchedule.h"
#include "Robot.h"
#if defined(REALTIME_API)
#include "llm/RealtimeLLMBase.h"
#endif
#include "Expression.h"
#include "Sfx.h"          // 알람 슬롯: 시간 되면 알람 소리 직접 재생
#include <Avatar.h>
using namespace m5avatar;

extern Avatar avatar;

#define SCHEDULES_SPIFFS_PATH  "/schedules.json"

// In-memory schedule slots. Defaults are written here; load_schedules_from_spiffs()
// overrides any slots present in the saved JSON. The schedule callbacks read
// .prompt directly from this array each time they fire, so prompt edits via the
// web UI take effect immediately on the next fire.
struct ScheduleItem {
    int hour;
    int min;
    bool weekdayOnly;
    String prompt;
    bool alarm;           // true 면 그 시간에 알람 소리(sfx event "alarm") 직접 재생 (하위호환)
    String sound;         // 그 시간에 먼저 재생할 SD sfx mp3 파일명("" = 없음). 멘트보다 먼저 재생.
};

static ScheduleItem g_schedules[SCHEDULE_SLOT_COUNT] = {
    { 0,  0, false, "지금 자정이야. 가족에게 잘 시간이라고 알려줘. 이렇게 말해: '이제 잘 시간인데 다들 잘 준비 하고 있어?'", false, "" },
    { 7,  0, true,  "지금 평일 아침 7시야. 밝게 이렇게 말해: '아침이야 이제 일어날 준비를 하자~!!'", false, "" },
    { 8, 10, true,  "지금 평일 아침 8시 10분이야. 가족에게 '좋은 아침이야~!' 하고 밝게 인사한 뒤, 오늘 날씨와 급식을 알려주고, 마지막에 따뜻한 응원 한마디를 해줘. 각 항목은 한 줄로 아주 짧게.", false, "" },
    { 7,  0, false, "", false, "" },   // 자유 슬롯(알람 등) — 기본 비활성(멘트 없음/사운드 없음)
    { 7, 30, false, "", false, "" },   // 자유 슬롯
};

static bool is_weekday_now()
{
    struct tm now;
    if (!getLocalTime(&now)) return false;
    return (now.tm_wday >= 1 && now.tm_wday <= 5);
}

static void scheduled_speak(const String& prompt)
{
#if defined(REALTIME_API)
    if (robot == nullptr || robot->llm == nullptr) return;
    RealtimeLLMBase* rt = (RealtimeLLMBase*)robot->llm;
    rt->pushUserText(prompt);
#else
    if (robot && robot->llm) robot->llm->chat(prompt);
#endif
}

// ScheduleEveryDay's callback is void(void); we need one per slot so the
// callback knows which g_schedules[] entry to read.
static void sched_fire(int i) {
    if (g_schedules[i].weekdayOnly && !is_weekday_now()) { Serial.printf("[sched] slot %d skipped (weekend)\n", i); return; }
    Serial.printf("[sched] slot %d fire (%02d:%02d) alarm=%d\n", i,
                  g_schedules[i].hour, g_schedules[i].min, (int)g_schedules[i].alarm);
    // 사운드 먼저(블로킹) → 그 다음 멘트. sound 가 지정되면 그 파일을, 없고 alarm 이면 알람 이벤트를 재생.
    if (g_schedules[i].sound.length())      sfx_play_file(g_schedules[i].sound.c_str());   // 선택한 SD sfx mp3 재생(블로킹)
    else if (g_schedules[i].alarm)          sfx_play_event("alarm");                       // 하위호환: 알람 소리
    if (g_schedules[i].prompt.length()) scheduled_speak(g_schedules[i].prompt);   // 멘트 있으면 발화(사운드 끝난 뒤)
}
static void sched_slot_0() { sched_fire(0); }
static void sched_slot_1() { sched_fire(1); }
static void sched_slot_2() { sched_fire(2); }
static void sched_slot_3() { sched_fire(3); }
static void sched_slot_4() { sched_fire(4); }

static void load_schedules_from_spiffs()
{
    if (!SPIFFS.exists(SCHEDULES_SPIFFS_PATH)) {
        Serial.println("[sched] no SPIFFS schedules.json — using compiled-in defaults");
        return;
    }
    File f = SPIFFS.open(SCHEDULES_SPIFFS_PATH, "r");
    if (!f) return;
    String body = f.readString();
    f.close();
    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc, body)) {
        Serial.println("[sched] schedules.json parse error — using defaults");
        return;
    }
    JsonArray items = doc["items"].as<JsonArray>();
    int n = (int)items.size();
    if (n > SCHEDULE_SLOT_COUNT) n = SCHEDULE_SLOT_COUNT;
    for (int i = 0; i < n; i++) {
        JsonObject o = items[i];
        if (o.containsKey("hour"))        g_schedules[i].hour        = o["hour"];
        if (o.containsKey("min"))         g_schedules[i].min         = o["min"];
        if (o.containsKey("weekdayOnly")) g_schedules[i].weekdayOnly = o["weekdayOnly"];
        if (o.containsKey("alarm"))       g_schedules[i].alarm       = o["alarm"];
        if (o.containsKey("sound"))       g_schedules[i].sound       = String((const char*)(o["sound"] | ""));    // 먼저 재생할 sfx mp3 파일명
        if (o.containsKey("prompt"))      g_schedules[i].prompt      = String((const char*)(o["prompt"] | ""));   // 빈값=멘트 없음(사운드 전용)
    }
    Serial.println("[sched] loaded schedules from SPIFFS");
}

void init_schedule(void)
{
    load_schedules_from_spiffs();
    Serial.println("[sched] init_schedule:");
    for (int i = 0; i < SCHEDULE_SLOT_COUNT; i++) {
        Serial.printf("  slot %d: %02d:%02d weekdayOnly=%d prompt=%s\n",
                      i, g_schedules[i].hour, g_schedules[i].min,
                      (int)g_schedules[i].weekdayOnly,
                      g_schedules[i].prompt.c_str());
    }
    add_schedule(new ScheduleEveryDay(g_schedules[0].hour, g_schedules[0].min, sched_slot_0));
    add_schedule(new ScheduleEveryDay(g_schedules[1].hour, g_schedules[1].min, sched_slot_1));
    add_schedule(new ScheduleEveryDay(g_schedules[2].hour, g_schedules[2].min, sched_slot_2));
    add_schedule(new ScheduleEveryDay(g_schedules[3].hour, g_schedules[3].min, sched_slot_3));
    add_schedule(new ScheduleEveryDay(g_schedules[4].hour, g_schedules[4].min, sched_slot_4));
}

void speak_now(const String& text)
{
    Serial.printf("[sched] speak_now: %s\n", text.c_str());
    scheduled_speak(text);
}

String get_schedules_json()
{
    DynamicJsonDocument doc(4096);
    doc["version"] = 1;
    JsonArray items = doc.createNestedArray("items");
    for (int i = 0; i < SCHEDULE_SLOT_COUNT; i++) {
        JsonObject o = items.createNestedObject();
        o["hour"]        = g_schedules[i].hour;
        o["min"]         = g_schedules[i].min;
        o["weekdayOnly"] = g_schedules[i].weekdayOnly;
        o["alarm"]       = g_schedules[i].alarm;
        o["sound"]       = g_schedules[i].sound;
        o["prompt"]      = g_schedules[i].prompt;
    }
    String result;
    serializeJson(doc, result);
    return result;
}

bool set_schedules_json(const String& json)
{
    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc, json)) return false;
    JsonArray items = doc["items"].as<JsonArray>();
    if (items.isNull()) return false;
    int n = (int)items.size();
    if (n > SCHEDULE_SLOT_COUNT) n = SCHEDULE_SLOT_COUNT;
    for (int i = 0; i < n; i++) {
        JsonObject o = items[i];
        if (o.containsKey("hour"))        g_schedules[i].hour        = o["hour"];
        if (o.containsKey("min"))         g_schedules[i].min         = o["min"];
        if (o.containsKey("weekdayOnly")) g_schedules[i].weekdayOnly = o["weekdayOnly"];
        if (o.containsKey("alarm"))       g_schedules[i].alarm       = o["alarm"];
        if (o.containsKey("sound"))       g_schedules[i].sound       = String((const char*)(o["sound"] | ""));    // 먼저 재생할 sfx mp3 파일명
        if (o.containsKey("prompt"))      g_schedules[i].prompt      = String((const char*)(o["prompt"] | ""));   // 빈값=멘트 없음(사운드 전용)
    }
    File f = SPIFFS.open(SCHEDULES_SPIFFS_PATH, "w");
    if (!f) {
        Serial.println("[sched] SPIFFS open(w) failed");
        return false;
    }
    String out = get_schedules_json();
    size_t written = f.print(out);
    f.close();
    Serial.printf("[sched] saved schedules to SPIFFS (%u bytes)\n", (unsigned)written);
    return written > 0;
}
