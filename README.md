# stack-chan-ko — 한국판 스택짱

M5Stack **CoreS3** 위에서 동작하는 **한국어 가족용 AI 로봇** 펌웨어입니다. OpenAI Realtime API로 자연스러운 한국어 음성 대화를 하고, 도형 기반의 부드러운 눈(RoboEyes)으로 감정을 표현하며, 날씨·미세먼지·급식·할일·일정 같은 실생활 정보를 음성으로 알려줍니다.

[Stack-chan](https://github.com/meganetaaan/stack-chan) 프로젝트와 [AI_StackChan_Ex](https://github.com/ronron-gh/AI_StackChan_Ex)(motoh) 펌웨어를 기반으로, 한국어 사용과 가족용 생활 기능에 맞춰 포크·커스터마이즈했습니다.

> 개인용 가족 프로젝트를 일반화해 공개한 것입니다. 가족 이름·학교·지역 등 개인정보는 모두 일반 템플릿(설정값)으로 대체되어 있으니, 아래 안내대로 본인 환경에 맞게 채워 사용하세요.

---

## 주요 기능

- **한국어 음성 대화** — OpenAI Realtime API. 항상 한국어로만 응답하도록 페르소나가 고정.
- **RoboEyes 도형 눈** — 카툰 캐릭터가 아니라 [FluxGarage RoboEyes](https://github.com/FluxGarage/RoboEyes) 기반의 부드러운 도형 눈(EMO/Luna 스타일). 6감정 표정 + 말하는 눈 + 시선 + 수면 눈.
- **감정 합주** — 표정 변화에 맞춰 서보(목)가 제스처. 기쁨/슬픔/화남/의심/졸림/평온.
- **페르소나 프리셋** — 기본/여자친구/친구/비서. 웹 설정에서 즉시 전환·편집.
- **장기 기억** — `update_memory` 도구로 새로 알게 된 사실을 누적 저장, 다음 대화에서도 기억.
- **외부 데이터 도구** — 날씨(wttr.in), 미세먼지, 학교 급식(NEIS), 할일·일정([Nexusive](https://nexusive.com)). 아래 *외부 연동* 참조.
- **머리 모션 레코더** — 웹 조이스틱으로 목을 실시간 조작·녹화·재생, 효과음과 동시 재생 연동.
- **효과음 라이브러리** — SD의 mp3를 이름/이벤트(감정·쓰담·근접·부팅·알람 등)에 매핑. 음성으로도 재생.
- **생명감 기능** — 혼자 움직임, 근접센서(가까이 가면 눈 커짐), 먼저 말 걸기, 쓰담 반응, 밤 모드, 탭하면 깨어나기.
- **예약·알람** — 취침/기상/아침 인사 등 시간 예약 발화 + 알람 소리.
- **통합 웹 설정** — 단일 `settings.html`에서 페르소나·기억·발화·볼륨·움직임·센서·밤모드·WiFi 전부 설정.

## 하드웨어

- **M5Stack CoreS3** (ESP32-S3) — 메인 타깃.
- 팬틸트 서보(목). 서보 종류는 SD 설정에서 지정.
- (선택) 스피커/마이크는 CoreS3 내장 사용.

> 다른 보드(Core2, AtomS3R)용 빌드 환경도 `platformio.ini`에 있으나, 이 포크의 기능은 **CoreS3 기준으로 검증**되었습니다.

## 빌드 & 플래시

[PlatformIO](https://platformio.org/) 필요. `firmware/` 디렉토리에서:

```bash
# 빌드
pio run -e m5stack-cores3-realtime

# 빌드 + 플래시 (포트는 본인 환경에 맞게)
pio run -e m5stack-cores3-realtime -t upload --upload-port COM3
```

> Windows에서 긴 앱 영역 쓰기 중 USB 절전으로 플래시가 끊기면, USB 선택적 절전(selective suspend)을 끄고 다시 시도하세요.

## SD 카드 설정

`Copy-to-SD/` 폴더 내용을 SD 카드 루트에 복사한 뒤, 아래 파일을 본인 값으로 채웁니다.

- **`yaml/SC_SecConfig.yaml`** — WiFi와 API 키 (실제 키는 SD에만 두고 절대 커밋하지 마세요. 저장소의 파일은 `********` 템플릿입니다.)
  ```yaml
  wifi:
    ssid: "your-wifi"
    password: "your-password"
  apikey:
    aiservice: "sk-..."     # OpenAI API Key
    nexusive: "nex_..."     # (선택) Nexusive 연동용
  ```
- **`yaml/SC_BasicConfig.yaml`** — 서보 종류/핀 등 하드웨어 설정.
- **`app/AiStackChanEx/SC_ExConfig.yaml`** — 기능 옵션.

## 지역·학교 정보 변경 (중요)

날씨·미세먼지·급식은 **예시값**으로 들어 있어, 본인 지역/학교로 바꿔야 정상 동작합니다. `firmware/src/llm/ChatGPT/FunctionCall.cpp`:

- **날씨** — `do_fetch_weather()`의 `wttr.in/Seoul` → 본인 도시명.
- **미세먼지** — `do_fetch_air()`의 `stationName=` → 본인 측정소명(URL 인코딩).
- **급식** — `do_fetch_meals_week()`의 NEIS `ATPT_OFCDC_SC_CODE`(시도교육청)·`SD_SCHUL_CODE`(학교) → 본인 학교 코드. 코드는 [NEIS 오픈API](https://open.neis.go.kr)에서 조회.

각 위치에 변경 안내 주석이 달려 있습니다.

## 웹 설정 페이지

기기가 WiFi에 연결되면 같은 네트워크의 폰/PC에서 `http://<기기-IP>/settings.html` 접속(기기 IP는 시리얼 로그나 공유기에서 확인). 페르소나·기억·발화·예약·볼륨·움직임·근접·쓰담·밤모드·머리 모션·효과음·WiFi를 한 페이지에서 설정합니다.

## 외부 연동

- **날씨** — `wttr.in` (무료, 키 불필요).
- **미세먼지** — 공개 프록시 API.
- **급식** — [NEIS 교육행정정보 오픈 API](https://open.neis.go.kr).
- **할일·일정** — **[Nexusive](https://nexusive.com)**: 제작자가 만든 할일·일정·노트·가계부 플래너 앱. REST API(`/api/v1`, Bearer 인증)로 연동해, 로봇에게 "오늘 할일 알려줘" / "이번주 일정 뭐야" 하면 음성으로 답합니다. `SC_SecConfig.yaml`의 `apikey.nexusive`에 토큰을 넣으면 활성화됩니다.

## 라이선스

이 저장소는 GPL-3.0-or-later 구성요소([FluxGarage RoboEyes](https://github.com/FluxGarage/RoboEyes))를 포함하므로 **GPL-3.0-or-later**로 배포됩니다. 루트 `LICENSE` 참조.

기반·서드파티 프로젝트의 저작권과 라이선스는 **`NOTICE.md`** 에 명기되어 있습니다. 업스트림 관계와 동기화 방법은 **`UPSTREAM.md`** 참조.

## 감사

- [Stack-chan](https://github.com/meganetaaan/stack-chan) — Naoki Kosaka (@meganetaaan)
- [AI_StackChan_Ex](https://github.com/ronron-gh/AI_StackChan_Ex) — motoh
- [m5stack-avatar](https://github.com/meganetaaan/m5stack-avatar) — Shinya Ishikawa
- [FluxGarage RoboEyes](https://github.com/FluxGarage/RoboEyes) — Dennis Hoelscher
