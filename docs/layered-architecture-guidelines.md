# Layered Architecture Guidelines

이 문서는 VEDA Qt 프로젝트의 계층형 아키텍처 기준 문서입니다.

현재 단계의 목표는 다음 두 가지입니다.

- 현재 구조와 의존 규칙을 일관된 기준으로 유지한다.
- 남은 리팩토링이 같은 방향을 따르도록 다음 단계 기준을 문서화한다.

현재는 주요 코드 이동이 대부분 완료되었고, 일부 과도기 파일만 남아 있습니다.

## Target Layout

```text
src/
├── app/                  # 진입점, 부트스트랩, DI 조립, 전역 설정 연결
├── presentation/         # QWidget/View, Presenter/Controller, UI 상태
│   ├── shell/            # MainWindow, HeaderBar, 페이지 전환
│   ├── pages/            # CCTV/DB/Record/Telegram 등 화면 단위
│   ├── widgets/          # VideoWidget, ROI interaction, 공용 UI 위젯
│   └── controllers/      # UI 이벤트 -> application 호출
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
- DB, RTSP, FFmpeg 라이브러리 연동, OCR 엔진, Telegram API, RPi TCP, 파일 저장, 설정 파일 입출력이 여기에 속합니다.
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

- `src/presentation/shell`, `src/presentation/pages`, `src/presentation/widgets`, `src/presentation/controllers`
- `MainWindow`는 page/view 조립과 상위 orchestration 위주로 유지
- `CameraChannelRuntime`는 `src/presentation/widgets/`로 통합되었고, `src/ui/`는 아이콘 자산 중심으로 유지

2. 유스케이스/오케스트레이션

- `src/application/db/*`, `src/application/parking/*`, `src/application/roi/*`
- DB 화면의 조회/수정/삭제 흐름은 application service로 이동 완료
- 복잡한 기능 흐름은 specialized controller와 application service 조합으로 정리

3. 인프라 구현

- `src/infrastructure/persistence`, `src/infrastructure/video`, `src/infrastructure/metadata`
- `src/infrastructure/camera`, `src/infrastructure/ocr`, `src/infrastructure/telegram`, `src/infrastructure/rpi`, `src/infrastructure/vision`
- RTSP, OCR, FFmpeg, Telegram, DB, RPi 연동은 `infrastructure/` 아래에 둔다

4. 도메인 규칙

- `src/domain/parking/*`
- 주차 정책과 추적 규칙처럼 외부 시스템과 무관한 로직은 `domain/`에 둔다

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

## Next Cleanup Priorities

1. 실제 빌드 기준 검증

- 폴더 이동 이후 남은 include 누락, forward declaration 문제, CMake 소스 누락을 계속 정리
- 현재 확인된 빌드 차단 원인은 프로젝트 경로보다 MSVC 표준 라이브러리 include path 환경 문제다

2. `presentation` 정리 지속

- controller 조립 책임이 과도하게 커지지 않도록 composition root 분리 여부 검토
- widget/runtime binder와 controller 경계가 더 단순해질 수 있는지 점검

3. CMake 타깃 분리

- 현재는 계층형 폴더 구조지만 단일 executable 중심 빌드
- 이후 `presentation/application/domain/infrastructure` 라이브러리로 쪼개 의존 방향을 빌드 레벨에서도 강제

4. 문서와 구조 동기화

- README, 아키텍처 문서, PR 문서의 경로 예시를 현재 구조 기준으로 갱신

## Review Checklist

새 코드를 추가할 때는 아래 질문으로 확인합니다.

- 이 코드는 화면 표시 책임인가?
- 이 코드는 유스케이스 orchestration인가?
- 이 코드는 외부 시스템 구현인가?
- 이 코드는 핵심 규칙인가?
- 이 코드를 `shared`에 두지 않아도 되는가?
- 이 파일이 금지된 의존 방향을 만들고 있지 않은가?

## Current Scope

현재까지 수행한 일:

- `presentation/`, `application/`, `domain/`, `infrastructure/`, `shared/` 기준으로 실제 코드 이동 완료
- `MainWindowController`의 큰 책임을 specialized controller로 분리
- DB 화면용 application service 도입
- repository, RTSP, video, metadata, OCR, Telegram, RPi, ReID extractor를 `infrastructure/` 아래로 이동
- 주차 정책/추적 규칙을 `domain/parking`으로 이동

현재 남아 있는 일:

- 실제 Windows/Qt Creator 빌드에서 남는 경로/타입 문제 추가 정리
- MSVC toolchain include path 문제 해결
- CMake 타깃 분리
- composition root 및 DI 구조 보강
