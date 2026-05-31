# 업스트림 (Upstream) 관계

`stack-chan-ko`는 아래 업스트림 펌웨어를 베이스로 포크·커스터마이즈한 저장소입니다.

## 출처

- **베이스 펌웨어**: [ronron-gh/AI_StackChan_Ex](https://github.com/ronron-gh/AI_StackChan_Ex) (MIT, Copyright 2024 motoh)
- **포크 기준 커밋**: `74da93e` ("Add ESP-NOW remote mod Codex log", 2026-05-25)
- 베이스를 평탄화(flatten)해 가져온 뒤 한국어/가족용 기능을 추가했습니다.

상위 개념 프로젝트: [meganetaaan/stack-chan](https://github.com/meganetaaan/stack-chan).

## 우리가 추가/변경한 것

- 한국어 전용 페르소나 시스템 + 프리셋(기본/여자친구/친구/비서)
- [FluxGarage RoboEyes](https://github.com/FluxGarage/RoboEyes) 기반 도형 눈 얼굴 (GPL-3.0)
- 감정 표정 + 서보 제스처 합주
- 외부 데이터 도구(날씨·미세먼지·NEIS 급식·Nexusive 할일/일정)
- 장기 기억(update_memory)
- 머리 모션 레코더(웹 조이스틱 녹화/재생) + 효과음 연동
- 통합 웹 설정 페이지, 근접/쓰담/밤모드/먼저말걸기, 예약·알람
- CoreS3 SD+LCD 공유 SPI 버스 충돌 수정, 음성 안정화, 효과음 노이즈 수정 등

상세 구현 기록은 `firmware/doc/codex/` 에 있습니다.

## 업스트림 변경 동기화 방법

1. 업스트림을 별도로 클론: `git clone https://github.com/ronron-gh/AI_StackChan_Ex.git`
2. 기준 커밋 이후 변경 확인: `git diff 74da93e..origin/main`
3. 필요한 변경을 이 트리에 hand-merge / cherry-pick.

라이선스: 베이스는 MIT이나, RoboEyes(GPL-3.0) 포함으로 이 저장소 전체는 GPL-3.0-or-later. `NOTICE.md`·`LICENSE` 참조.
