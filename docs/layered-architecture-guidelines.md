# Layered Architecture Guidelines

이 문서는 VEDA Qt 프로젝트를 점진적으로 계층형 아키텍처로 정리하기 위한 기준 문서입니다.

현재 단계의 목표는 다음 두 가지입니다.

- 새 계층 구조의 폴더 뼈대를 먼저 고정한다.
- 앞으로의 코드 이동과 신규 개발이 같은 의존 규칙을 따르도록 기준을 문서화한다.

이 문서는 "지금 당장 모든 코드를 옮긴 상태"를 설명하지 않습니다. 기존 구조와 새 구조가 한동안 공존하는 과도기 기준 문서입니다.

## Target Layout

```text
src/
├── app/                  # 진입점, 부트스트랩, DI 조립, 전역 설정 연결
├── presentation/         # QWidget/View, Presenter/Controller, UI 상태
│   ├── shell/            # MainWindow, HeaderBar, 페이지 전환
│   ├── pages/            # CCTV/DB/Record/Telegram 등 화면 단위
│   ├── widgets/          # VideoWidget, ROI interaction, 공용 UI 위젯
│   └── presenters/       # UI 이벤트 -> application 호출
├── application/          # 유스케이스, orchestration, ports
│   ├── camera/
│   ├── parking/
│   ├── roi/
│   ├── recording/
│   └── telegram/
├── domain/               # 핵심 규칙, 엔티티, 값 객체, 도메인 정책
│   ├── camera/
│   ├── parking/
│   ├── roi/
│   ├── recording/
│   └── shared/
├── infrastructure/       # DB, RTSP, OCR, Telegram API, RPi, 파일/설정 IO
│   ├── persistence/
│   ├── video/
│   ├── metadata/
│   ├── ocr/
│   ├── telegram/
│   ├── rpi/
│   └── config/
└── shared/               # 진짜 공용 유틸만 배치
```

## Layer Responsibilities

### 1. `app`

- 프로그램 시작점과 조립 전용 계층입니다.
- `main.cpp`, Qt 메시지 핸들러 등록, 설정 로드, 최상위 객체 조립이 들어갑니다.
- 비즈니스 규칙이나 화면 동작을 직접 가지지 않습니다.

### 2. `presentation`

- 사용자 입력과 화면 표시를 담당합니다.
- `QWidget`, `QDialog`, `QFrame`, `UiRefs`, 페이지 전환, 버튼/테이블 이벤트 처리가 여기에 속합니다.
- Repository를 직접 호출하지 않고, application 유스케이스를 통해 기능을 요청합니다.

### 3. `application`

- 사용자 시나리오를 완성하는 유스케이스 계층입니다.
- 예: 입차 처리, ROI 저장, 이벤트 녹화, 결제 반영, DB 조회 갱신
- 흐름 제어와 orchestration을 담당하지만, 구체적인 DB/네트워크 구현은 몰라야 합니다.

### 4. `domain`

- 비즈니스 규칙과 모델의 중심 계층입니다.
- 예: 주차 요금 정책, ROI 이름 검증 규칙, 차량 상태 전이, 값 객체
- 가능하면 UI와 인프라 프레임워크에 독립적으로 유지합니다.

### 5. `infrastructure`

- 외부 시스템 연결 구현 계층입니다.
- DB, RTSP, FFmpeg subprocess, OCR 엔진, Telegram API, RPi TCP, 파일 저장, 설정 파일 입출력이 여기에 속합니다.
- application/domain에서 정의한 추상 인터페이스를 구현하는 위치로 사용합니다.

### 6. `shared`

- 여러 계층에서 재사용되지만 특정 기능 문맥에 속하지 않는 최소한의 공용 코드만 둡니다.
- "애매하면 shared"는 금지합니다.
- 특정 기능에 더 가까우면 원래 feature 계층에 둡니다.

## Dependency Rules

허용 의존 방향:

```text
presentation -> application -> domain
infrastructure -> application
infrastructure -> domain
app -> presentation
app -> application
app -> infrastructure
app -> domain
```

금지 규칙:

- `domain` -> `application`, `presentation`, `infrastructure` 금지
- `application` -> `presentation` 금지
- `presentation` -> `infrastructure` 직접 의존 금지
- `presentation`에서 `Repository`, `QSqlDatabase`, `QNetworkAccessManager` 직접 호출 금지
- `domain`에서 `QWidget`, `QDialog`, `QTableWidget`, OpenCV UI API 사용 금지
- `shared`에서 계층 역전을 만드는 범용 헬퍼 추가 금지

## Practical Rules For This Repo

이 프로젝트에 바로 적용할 때는 아래 기준을 우선합니다.

1. 화면 코드

- `src/ui/windows`, `src/ui/windows/views`, `src/ui/video`, `src/ui/roi`
- 향후 `presentation/` 아래로 이동

2. 유스케이스/오케스트레이션

- `MainWindowController`, `DbPanelController`, `RecordPanelController`, `ParkingService`
- 향후 `application/` 또는 `presentation/presenters/`로 분리

3. 인프라 구현

- `database/*`, `video/videothread.*`, `metadata/*`, `telegram/*`, `rpi/*`, OCR 엔진 연동부
- 향후 `infrastructure/` 아래로 이동

4. 도메인 규칙

- `parking/parkingfeepolicy.*`, ROI 검증 규칙, 차량 상태 모델 등
- 향후 `domain/` 아래로 이동

## Naming Rules

- `*View`: QWidget 기반 화면/위젯
- `*Presenter` 또는 `*Controller`: UI 이벤트 처리
- `*UseCase` 또는 `*Interactor`: application 유스케이스
- `*Repository`: 저장소 추상 또는 구현
- `*Client` / `*Gateway`: 외부 시스템 adapter
- `*Policy`: 도메인 정책
- `*State`, `*Entity`, `*Value`: 도메인 모델

## Transitional Rules

리팩토링 과도기에는 아래 규칙을 적용합니다.

1. 새 파일은 가능하면 새 계층 구조에만 추가합니다.
2. 기존 파일 이동은 작은 단위로 나눕니다.
3. 큰 파일은 "분리 -> 호출부 유지 -> 이동" 순서로 진행합니다.
4. 화면 계층에서 DB/네트워크 객체를 새로 생성하지 않습니다.
5. 기존 구조와 새 구조가 공존하더라도 의존 방향만큼은 먼저 맞춥니다.

## Recommended First Moves

1. `presentation`

- `MainWindow`, `HeaderBarView`, `DbPageView`, `RecordPageView`, `TelegramPageView`, `VideoWidget`

2. `infrastructure`

- `database/*`, `rpi/*`, `telegram/*`, `metadata/*`, `video/videothread.*`

3. `application`

- `MainWindowController`에서 기능별 흐름을 `parking`, `roi`, `recording` 유스케이스로 분리

4. `domain`

- 주차 요금 정책, ROI 명명/검증, 차량 상태 모델을 정리

## Review Checklist

새 코드를 추가할 때는 아래 질문으로 확인합니다.

- 이 코드는 화면 표시 책임인가?
- 이 코드는 유스케이스 orchestration인가?
- 이 코드는 외부 시스템 구현인가?
- 이 코드는 핵심 규칙인가?
- 이 코드를 `shared`에 두지 않아도 되는가?
- 이 파일이 금지된 의존 방향을 만들고 있지 않은가?

## Current Scope

이번 단계에서 수행한 일:

- 새 계층 구조의 폴더 생성
- `.gitkeep` 추가로 Git 추적 가능 상태 확보
- 의존 규칙과 전환 기준 문서화

이번 단계에서 아직 하지 않은 일:

- 기존 코드 파일 이동
- include 경로 정리
- CMake 타깃 분리
- 인터페이스 추출 및 DI 도입
