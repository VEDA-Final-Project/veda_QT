# PR Summary: `feature/final` vs `main`

이 문서는 `main` 기준으로 `feature/final` 브랜치의 변경 사항을 정리한 PR 초안입니다.
정리 성격상 "merge 전 기준 요약"으로 보는 것이 맞습니다.

## 제목 예시

`feat: camera/parking/ocr 파이프라인 개편 및 DB 패널 확장`

## 요약

- 카메라 표시, 메타데이터, 렌더링 구조를 정리하고 live frame 처리 경로를 개선했습니다.
- OCR 파이프라인을 기존 Paddle 중심 구성에서 LLM 기반 흐름으로 재구성했습니다.
- ReID 기반 차량 추적과 주차 입출차 처리 로직을 보강했습니다.
- DB 탭에 사용자 및 주차구역 현황 패널을 추가하고 관련 UI 흐름을 정리했습니다.
- 설정, 모델, 문서 구성을 현재 구조에 맞게 정리했습니다.

## 주요 변경 사항

### 1. 카메라 및 비디오 처리 구조 개편

- `CameraSource`, `CameraManager`, `VideoThread`, `VideoBufferManager` 주변 구조를 정리했습니다.
- `SharedVideoFrame`를 도입해 raw/live frame 전달 방식을 정비했습니다.
- 멀티뷰 렌더링, display frame 전달, consumer 크기 대응 로직을 개선했습니다.
- ROI 표시와 실시간 오버레이 경로를 정리했습니다.

### 2. OCR 파이프라인 재구성

- 기존 Paddle OCR 전처리, 후처리, 디버그 덤프 경로를 제거했습니다.
- `LlmOcrRunner`, `TelegramLlmRunner`를 추가해 LLM 기반 OCR 및 질의 흐름을 붙였습니다.
- `OcrManager`, `PlateOcrCoordinator`를 새 OCR 경로에 맞게 수정했습니다.
- 기존 한국어 OCR 모델 파일은 정리하고 새 구성에 맞는 설정을 반영했습니다.

### 3. 주차, 차량 추적, ReID 강화

- `ReIDFeatureExtractor`와 ReID 모델을 추가했습니다.
- `VehicleTracker`, `ParkingService`, `ParkingRepository`를 중심으로 차량 추적 및 주차 상태 처리 로직을 확장했습니다.
- 주차 요금 정책 계산 로직을 분리했습니다.
- 텔레그램 알림, 조회와 주차 데이터 연동을 보강했습니다.

### 4. DB 및 관리 UI 확장

- DB 패널 구조를 정리하고 `UserDbPanelController`, `ZonePanelController`를 추가했습니다.
- 주차구역 현황 탭과 사용자 관리 흐름을 확장했습니다.
- `MainWindowController`, `DbPanelController`, `ParkingLogPanelController` 등 관련 UI 연결을 정리했습니다.

### 5. 설정, 문서, 빌드 정리

- `CMakeLists.txt`, `config/settings.json`, `config/config.*`를 최신 구조에 맞게 조정했습니다.
- 프레임 처리 리팩터링 문서를 추가했습니다.
- 사용하지 않는 OCR 관련 코드와 파일을 정리했습니다.

## 추가된 주요 파일

- `src/ocr/recognition/llmocrrunner.*`
- `src/ocr/recognition/telegramllmrunner.*`
- `src/tracking/reidextractor.*`
- `src/parking/parkingfeepolicy.*`
- `src/ui/windows/userdbpanelcontroller.*`
- `src/ui/windows/zonepanelcontroller.*`
- `src/video/sharedvideoframe.h`
- `model/sbs_R50-ibn.onnx`
- `docs/frame-refactor-shared-raw-frame-2026-03-18.md`

## 제거 또는 정리된 주요 항목

- 기존 Paddle OCR runner
- OCR 전처리 및 후처리 관련 코드
- OCR debug dumper
- 기존 `models/korean` 일부 모델 파일

## 확인 포인트

- 카메라 live view 및 멀티뷰 동작
- 메타데이터 수신과 ROI, 주차 상태 반영
- LLM OCR 요청 및 응답 흐름
- ReID 연동 후 입출차 처리 및 로그 저장
- DB 탭 내 사용자, 주차구역 현황 표시

## 비고

- 이 문서는 상세 changelog가 아니라 PR 설명 초안입니다.
- 실제 최종 PR에는 브랜치 기준 commit 범위와 테스트 결과를 함께 적는 것을 권장합니다.
