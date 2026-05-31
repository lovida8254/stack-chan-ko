# NOTICE — 서드파티 출처 및 라이선스

`stack-chan-ko`는 여러 오픈소스 프로젝트를 기반·통합한 결과물입니다. 아래 구성요소들의 저작권과 라이선스를 명기합니다.

## 프로젝트 전체 라이선스

이 저장소에는 **GPL-3.0-or-later** 라이선스의 구성요소(FluxGarage RoboEyes)가 포함되어 있고, 펌웨어 바이너리는 이를 포함한 결합 저작물이므로 **저장소 전체를 GPL-3.0-or-later로 배포**합니다 (루트 `LICENSE` 참조). MIT 라이선스 구성요소들은 GPL과 호환되며, 아래에 원 저작권 표시를 그대로 보존합니다.

## 기반 프로젝트 (Upstream)

| 구성요소 | 라이선스 | 저작권 | 출처 |
|---|---|---|---|
| **Stack-chan** (원본 프로젝트) | MIT | Naoki Kosaka (@meganetaaan) 외 | https://github.com/meganetaaan/stack-chan |
| **AI_StackChan_Ex** (베이스 펌웨어) | MIT | Copyright (c) 2024 motoh | https://github.com/ronron-gh/AI_StackChan_Ex |
| **m5stack-avatar** (얼굴/아바타) | MIT | Copyright (c) 2018 Shinya Ishikawa | https://github.com/meganetaaan/m5stack-avatar |
| **FluxGarage RoboEyes** (도형 눈) | **GPL-3.0-or-later** | Copyright (C) 2024-2025 Dennis Hoelscher | https://github.com/FluxGarage/RoboEyes |

이 저장소는 `AI_StackChan_Ex`를 베이스로 포크/커스터마이즈했습니다. 업스트림과의 관계 및 동기화 방법은 `UPSTREAM.md`를 참조하세요.

## 주요 라이브러리 (PlatformIO 의존성)

`firmware/platformio.ini`에 명시된 라이브러리들은 각자의 라이선스를 따릅니다 (M5Unified, M5GFX, ArduinoJson, ESP8266Audio, WebSockets 등). 각 라이브러리의 라이선스 전문은 해당 패키지에 포함되어 있습니다. 벤더링된 라이브러리의 라이선스 원문:

- `firmware/lib/m5stack-avatar/LICENSE.txt` (MIT)
- `firmware/lib/RoboEyes/src/FluxGarage_RoboEyes.h` 헤더 주석 (GPL-3.0-or-later)

## 라이선스 원문 보존

### AI_StackChan_Ex (베이스 펌웨어) — MIT

```
MIT License

Copyright (c) 2024 motoh

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### m5stack-avatar — MIT

```
MIT License

Copyright (c) 2018 Shinya Ishikawa
```
(전문: `firmware/lib/m5stack-avatar/LICENSE.txt`)

### FluxGarage RoboEyes — GPL-3.0-or-later

```
Copyright (C) 2024-2025 Dennis Hoelscher
www.fluxgarage.com

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.
```
(전문: 루트 `LICENSE` 및 RoboEyes 헤더 주석)
