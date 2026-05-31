# 2026-05-29 — 통합 설정 페이지 + 랜덤 움직임 + 근접센서 + 생명감 디테일

작업 환경: `firmware_arduino/AI_StackChan_Ex/firmware`, env `m5stack-cores3-realtime`, 기기 미연결(코딩+컴파일만, 배포는 집에서). 빌드 검증: `pio run -e m5stack-cores3-realtime` → **SUCCESS** (Flash ~37%, RAM ~22%).

---

## 🏠 집에서 바로 시작 (QUICKSTART)

**상태: 모든 코드 작성 + 5회 빌드 SUCCESS, 아직 한 번도 플래시 안 함.** 집에서 아래 순서로.

**0) 준비** — 기기 USB 연결(COM3), 작업 경로 `cd V:\00.Projects_jrn\stack-chan_JRN\firmware_arduino\AI_StackChan_Ex\firmware`

**1) 플래시 — 일반 빌드부터 (모든 생명감 기능 포함, 카메라 제외)**
```
pio run -e m5stack-cores3-realtime -t upload --upload-port COM3
```
⚠️ Moddable용 `%TEMP%\xs-build-v.bat` 쓰지 말 것(그건 옛 펌웨어). 페르소나/User Info는 SPIFFS에 남아 재플래시해도 유지됨.

**2) 부팅 트레이스 확인** (근접/IMU/시간 동기 등)
```
python %TEMP%\esp_boot_capture.py COM3 25
```
- `[prox] LTR-553 part id = 0x92` → 근접센서 감지 OK. 0x92 아니면 미감지(설정페이지에 "센서 미감지" 뜸).
- `[pet] IMU ready` / `[batt] ready` / `[night] ready` / `[talk] ready` 확인.

**3) 웹 설정 페이지 열기** (폰/PC, 같은 WiFi)
- `http://<기기-IP>/settings.html` (시리얼 로그나 공유기에서 기기 IP 확인). `/` 도 같은 페이지.
- 섹션: 페르소나·기억 / 발화·예약 / 볼륨 / 혼자 움직임 / 근접센서 / 먼저 말 걸기 / 쓰담 / 배터리 / 밤 모드 / 카메라 / WiFi.

**4) 튜닝 순서** — 아래 "다음 세션 TODO" 2~8번 따라 웹UI에서 값만 조정(대부분 재부팅 불필요). 임계값은 설정페이지의 실시간 표시(근접 raw, 배터리 level)를 보며 맞춤.

**5) 카메라(GPT-4o 비전)는 별도 빌드로 실험** — 일반 기능 안정화 후:
```
pio run -e m5stack-cores3-realtime-camera -t upload --upload-port COM3
```
⚠️ **플래시하면 제일 먼저 근접/IMU/배터리/터치가 여전히 동작하는지 확인**(카메라가 내부 I2C 점유 가능 → 공존 안 되면 카메라 vs 센서 택일). 비전 테스트: 설정페이지 "카메라 둘러보기" 버튼. 자세히는 아래 "카메라 다음 단계".

**진입 신호**: "집 도착, 스택짱 이어서" → 이 문서 QUICKSTART 1번부터.

---

## 한 줄 요약

웹 설정을 단일 페이지로 통합하고, "혼자서도 귀엽게" 보이도록 랜덤 표정/행동 엔진, CoreS3 내장 근접센서(LTR-553) 기반 "다가오면 눈 커짐", 그리고 생명감 디테일 4종(먼저 말 걸기/쓰담 반응/밤 모드/엉뚱한 농담)을 추가. 전부 웹UI에서 커스터마이징 + SPIFFS 영속. **배포/튜닝은 미실시(기기 집에 있음).**

## 설계 원칙: 내부 I2C 직렬화

근접(LTR-553)·IMU(BMI270)·백라이트는 모두 **내부 I2C 공유 버스**에 있음. 별도 FreeRTOS 태스크에서 동시 접근하면 `M5.update()`(터치/전원 폴링)와 레이스 → 버스 깨짐 위험. 그래서 센서를 읽는 3종(`proximity_tick`, `pet_reaction_tick`, `night_mode_tick`)은 **`loop()`에서 `M5.update()` 직후 호출**해 메인 스레드에 직렬화. 각자 millis() 기반 self-throttle. I2C를 안 쓰는 엔진(idle motion, idle talk)만 자체 태스크.

## 신규/변경 파일

### 신규 모듈 (src/)
- **IdleMotion.cpp/.h** — 랜덤 표정/행동 엔진 (자체 태스크). 조용할 때 가중치 기반 랜덤 표정 + 매칭 제스처 + 시선 두리번 + 깜빡임. config `/idlemotion.json`. 말하는 중(getAudioLevel>200)/제스처 중/말 끝난 직후(quietAfter)엔 양보. **"대화 마지막 표정 고정" 문제를 이걸로 해소.** 밤이면 NightMode 연동으로 Sleepy 편향.
- **IdleTalk.cpp/.h** — 능동 발화 (자체 태스크). N분(랜덤) 조용하면 멘트 풀에서 랜덤으로 `pushUserText` → 먼저 말 걸기 + 엉뚱한 농담(한 풀에 통합). `idle_talk_on_approach()`로 근접 시 인사. config `/idletalk.json`. 온라인 전용.
- **PetReaction.cpp/.h** — 쓰담/들어올림 반응 (loop tick). IMU 자이로 크기를 lowpass한 "handling energy"가 임계 넘으면 HAPPY+제스처+(가끔)발화. 민감도 1~10. **제스처 중엔 서보 진동 오발동 방지로 감지 정지(`gesture_suppress_until`)**. config `/petreaction.json`.
- **NightMode.cpp/.h** — 밤 모드 (loop tick). 밤 시간대(자정 넘어가는 wrap 지원)면 `setBrightness` 어둡게 + `night_mode_is_night()`로 idle을 Sleepy 편향 + 밤 시작 시 취침 인사 1회. config `/nightmode.json`. 기본 낮 밝기 180 / 밤 25.
- **Proximity.cpp/.h** — LTR-553(0x23) 근접센서 (loop tick). PS값에 비례해 눈 반지름 스케일(`setEyeRadiusScale`) 1.0→maxEyeScale, very-near 진입 시 깜짝(Doubt+제스처)+근접 인사 트리거. 부팅 시 part id(0x92) 확인, 미감지면 graceful disable. config `/proximity.json` + 실시간 raw값 노출(튜닝용).
- **WifiConfig.cpp/.h** — 웹 편집 WiFi 목록 `/wifi.json`. 부팅 시 `tryMultiNetworkWifi()`가 SPIFFS 먼저 → 없으면 기존 SD YAML 폴백. 스캔(`/wifi_scan`), 현재 상태(ssid/ip/rssi). **적용은 재부팅**(연결 끊김 방지).

### 라이브러리 패치
- **lib/m5stack-avatar/src/Eye.h/.cpp** — `setEyeRadiusScale(float)`/`getEyeRadiusScale()` 전역 추가. `Eye::draw`에서 매 프레임 `r = this->r * scale`로 양쪽 눈 동시 확대(마스크 수식도 로컬 r 참조라 함께 스케일). 근접센서 "눈 커짐"용. (기존 Face.h protected+virtual 패치와 같은 계열.)

### 웹 UI (incbin/)
- **settings.html / settings.js** (신규) — 단일 통합 설정 페이지(접이식 `<details>` 섹션): 페르소나·기억 / 발화·예약 / 볼륨 / 혼자 움직임 / 근접센서 / 먼저 말 걸기·농담 / 쓰담 반응 / 밤 모드 / WiFi. 모바일 대응. 멘트 풀은 textarea 한 줄=한 멘트.

### 기존 파일 수정
- **WebAPI.cpp** — settings.html/js 임베드 + `/`·legacy URL(personalize/schedules)을 settings로. 신규 엔드포인트: `/idle_get|set`, `/prox_get|set`, `/talk_get|set`, `/pet_get|set`, `/night_get|set`, `/wifi_get|set`, `/wifi_scan`, `/reboot`.
- **main.cpp** — 신규 include 5종 / `tryMultiNetworkWifi()` SPIFFS 우선 분기 / setup 끝에 `idle_motion_init`,`proximity_init`,`idle_talk_init`,`pet_reaction_init`,`night_mode_init` / `loop()`의 `M5.update()` 직후 `proximity_tick`,`pet_reaction_tick`,`night_mode_tick`.

## SPIFFS 설정 파일 (전부 웹UI로 생성/수정, 부팅 시 컴파일 기본값 위에 덮어씀)
`/idlemotion.json` `/idletalk.json` `/petreaction.json` `/nightmode.json` `/proximity.json` `/wifi.json` (+ 기존 `/schedules.json` `/volume.txt` `/data.json` 페르소나).

## 다음 세션 TODO (집에서 — 기기 연결 후)

1. **플래시/배포** — `%TEMP%\xs-build-v.bat`은 Moddable용. Arduino는 `pio run -e m5stack-cores3-realtime -t upload` (또는 esptool). COM3.
2. **근접센서 검증/튜닝** — 부팅 트레이스에서 `[prox] LTR-553 part id = 0x??` 확인. `/settings.html` 근접 섹션의 실시간 raw값 보며 손 거리로 near/veryNear 임계값 맞추기. 미감지(0x92 아님)면 I2C 주소/버스 재확인.
3. **쓰담 민감도** — PetReaction sensitivity 슬라이더로 들어올림/쓰담 잘 잡히게. 서보 제스처 오발동 없는지 확인(가드 적용했지만 실측 필요).
4. **랜덤 움직임 느낌** — idle 간격/활발함/표정 가중치 "감상만 해도 귀여운" 수준으로 튜닝.
5. **능동 발화 빈도** — 먼저 말 걸기 간격, 멘트 풀 내용(농담 톤) 다듬기. STT 한계로 고유명사 주의.
6. **밤 모드** — 밝기 낮/밤 값, 시간대, 취침 인사 멘트 확인. NTP 동기 후에만 동작(콜드부트 시 시간 필요).
7. **WiFi 웹 편집 검증** — `/wifi.json` 저장 후 재부팅 → 새 망 연결되는지. SPIFFS 우선순위 동작 확인.
8. **(미적용) 페르소나 보강** — 능동 발화/쓰담/밤 멘트가 표정 메타 누설 안 하도록 기존 금지 문구 적용 여부 점검.
9. **(선택) 단계 3 얼굴 인식** — 별개 작업, [[stackchan-stage3-plan]] 참조. 근접센서로 "다가옴"은 이미 커버됨(카메라 불필요).

## 추가 세션 (2026-05-29, 기기 미연결 — 코딩+빌드만, Flash 37.8%)

사용자 선택: 하드닝 리뷰 + 터치 쓰담/시선 + 배터리 반응 + 멘트 톤. 빌드 SUCCESS.

### ① 하드닝 리뷰 (실기 blind 배포 대비)
- **WS 스레드 안전**: `pushUserText`→`webSocket.sendTXT`는 `webSocketLoopTask`도 호출 → 별도 태스크에서 부르면 동시 write 위험. 기존 스케줄러/`speak_now`는 loop()에서 호출(검증됨). **IdleTalk를 태스크→loop tick(`idle_talk_tick`)으로 전환**해 모든 pushUserText 호출을 loop 컨텍스트로 통일. (Pet/Battery/Night/Proximity도 전부 loop tick → 일관.)
- **NightMode 부팅 인사 오발동**: 밤에 부팅+NTP 늦으면 첫 유효시각에서 day→night 전이로 오인해 취침인사. `firstValidDone`로 첫 유효 읽기는 인사 없이 상태만 기록하게 수정.
- **PetReaction 서보 자가진동 오발동**: 제스처(`gesture_suppress_until`) 중 IMU 감지 정지 + energy 감쇠.

### ② 터치 반응 (신규 `TouchReaction.cpp/.h`)
- loop tick. 기존 mod 터치(탭=모드토글, 플릭=모드전환) 안 깸 — 추가만.
- **시선 따라가기**: 누르는 동안 터치 좌표→avatar.setGaze, idle_motion_hold로 idle 양보.
- **쓰담→pet**: 누른 채 드래그 경로 누적(`deltaX+deltaY`)이 임계(기본 120px) 넘고 플릭 아니면 `pet_reaction_fire()`. config `/touch.json`(lookEnabled/strokeEnabled/strokeThreshold).
- PetReaction에 **공용 `pet_reaction_fire()`** 추출(IMU+터치 공유, 쿨다운 3s 공유, file-static 타이머).

### ③ 배터리/충전 반응 (신규 `BatteryReaction.cpp/.h`)
- loop tick(30s). **낮으면**(기본 ≤20%, 비충전) N분(기본15)마다 Sad+"배고파" 발화. **충전 시작 edge**면 Happy+"냠냠" 1회. config `/battery.json` + 실시간 level/charging 노출.

### ④ 멘트 톤
- IdleTalk 기본 멘트 보강(심심/농담/혼잣말 5종 + 다가옴 인사 2종), "표정" 등 메타 narration 유발 단어 배제, 모두 "짧게/한 문장" 명시.

### loop tick 정리 (main.cpp `M5.update()` 직후, 직렬화)
`proximity_tick` → `pet_reaction_tick` → `touch_reaction_tick` → `battery_reaction_tick` → `night_mode_tick` → `idle_talk_tick`. 자체 태스크는 IdleMotion(I2C·WS 미사용)뿐.

### 신규 SPIFFS 설정파일 추가
`/touch.json` `/battery.json` (+ 기존 6종). 웹UI 섹션 2개 추가(터치 반응 / 배터리·충전 반응).

### 새 엔드포인트
`/touch_get|set`, `/batt_get|set`.

## 카메라 / GPT-4o 비전 (2026-05-29, 별도 env — 코딩+컴파일만)

사용자 요청 "카메라도 해줘 (둘 다)". 조사 결과 제약:
- **esp-dl(얼굴검출/인식 모델) 미존재** — 프로젝트/lib/.pio 어디에도 없음. `Camera.h`가 무조건 include해서 `-DENABLE_CAMERA`가 컴파일조차 안 됐음. → 얼굴 검출/인식은 esp-dl + ronron 라이브러리 통합 필요 = **별도 큰 작업, 실기 heap 측정(3-1) 선행**.
- **비전은 비-realtime `AiStackChanMod`에만** 배선돼 있었음(`ChatGPT.cpp` gpt-4o image_url). realtime엔 없음.
- **카메라 SCCB가 내부 I2C 공유** — 캡처 시 `M5.In_I2C.release()`. 근접/IMU/배터리/터치와 공존 미검증(Moddable이 막혔던 그 문제).

### 한 일 (GPT-4o 비전만, 별도 env)
- **`Camera.h`/`Camera.cpp` 가드**: esp-dl 헤더 + fb_gfx + `draw_face_boxes`를 `#if defined(ENABLE_FACE_DETECT)`로 감쌈 → **비전 전용(ENABLE_CAMERA만)이 esp-dl 없이 컴파일**.
- **신규 `CameraVision.cpp/.h`**: `camera_vision_look(hint)` = `camera_capture_base64`→`/v1/chat/completions`(gpt-4o-mini, WiFiClientSecure setInsecure, `robot->llm->param.api_key`)→응답 content를 `pushUserText`로 realtime에 주입(메모리의 Moddable 우회법). `camera_is_busy()` 플래그로 캡처 중 센서 I2C tick 양보.
- **센서 tick 4종(prox/pet/batt/touch)에 `camera_is_busy()` 가드** 추가.
- **WebAPI `/look`**(POST) + settings.html "카메라 둘러보기" 섹션(힌트 입력 + 버튼).
- **platformio.ini 신규 env `m5stack-cores3-realtime-camera`** = realtime + `-DENABLE_CAMERA`(FACE_DETECT 없음). **일반 env는 안 건드림**.
- 빌드: 일반 Flash 37.8%(불변) / 카메라 Flash 38.9% RAM 26.4%, 둘 다 SUCCESS.

### 카메라 다음 단계 (집, 실기 필수)
- ⚠️ **최우선 검증: I2C 공존** — 카메라 env 플래시 후 근접/IMU/배터리/터치가 여전히 동작하는지. 안 되면 카메라와 센서는 동시 사용 불가(택일) → 설계 재검토.
- 카메라 HW init 성공 여부(`Camera Init Failed` 트레이스), 비전 응답 품질/지연(POST 중 loop ~1-3s 정지, 오디오는 별도 태스크라 지속).
- **얼굴 인식**: esp-dl 통합 → heap 측정 → ronron-gh/M5CoreS3_FaceRecognition → 등록 웹UI. [[stackchan-stage3-plan]] 5단계. **아직 0%** (지금은 검출 헤더 가드만 해둔 상태).

## 알려진 한계/주의
- 능동 발화·취침 인사·쓰담 발화·근접 인사 모두 **온라인(realtime) 전용**. 오프라인이면 표정/제스처만.
- 제스처가 서보로 머리를 움직이면 IMU가 인식 → 쓰담 가드로 막았으나 실측 미검증.
- gpt-realtime-mini는 자율 도구 호출 약함 → 현재 풀모델(`gpt-realtime`) 사용 중([[stackchan-family-features]]).
- WiFi 변경은 의도적으로 재부팅 적용(원격에서 끊겨 못 돌아오는 사고 방지).
