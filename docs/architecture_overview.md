# VEDA 프로젝트 아키텍처 개요

이 문서는 현재 `veda_QT` 저장소의 실제 코드 구조를 기준으로 프로젝트 아키텍처를 빠르게 이해하기 위한 개요 문서입니다.

핵심 목표는 다음 세 가지입니다.

- 어떤 폴더가 어떤 책임을 가지는지 한눈에 설명한다.
- 런타임에서 객체가 어떻게 조립되고 연결되는지 정리한다.
- 카메라 영상, 메타데이터, OCR, 녹화가 어떤 흐름으로 이어지는지 현재 구현 기준으로 요약한다.

자세한 레이어 규칙은 `docs/layered-architecture-guidelines.md`를 기준 문서로 사용합니다. 이 문서는 그 규칙이 현재 코드에 어떻게 반영되어 있는지 설명하는 "현황 문서"에 가깝습니다.

## 1. 전체 구조 요약

현재 프로젝트는 크게 아래 축으로 나뉩니다.

- `presentation`: Qt 위젯, 페이지, 화면 이벤트 처리, UI 상태 연결
- `application`: 화면이나 외부 입력을 하나의 유스케이스로 묶는 서비스 계층
- `domain`: 주차 규칙, 차량 추적 같은 핵심 비즈니스 규칙
- `infrastructure`: 카메라, RTSP, 메타데이터, OCR, DB, Telegram, RPi, 파일 저장 등 외부 시스템 연동
- `config`, `logging`, `shared`: 전역 설정, 로그 필터링, 공용 타입
- `srtp`: SRTP 관련 저수준 전송 모듈

현재 빌드는 계층별 라이브러리로 분리된 상태는 아니고, 하나의 Qt 실행 파일 타깃 안에 이 구조가 논리적으로 정리되어 있습니다.

## 2. 소스 트리 기준 책임 분리

```text
src/
├── main.cpp
├── application/
│   ├── db/
│   ├── parking/
│   └── roi/
├── config/
├── domain/
│   └── parking/
├── infrastructure/
│   ├── camera/
│   ├── metadata/
│   ├── ocr/
│   ├── persistence/
│   ├── rpi/
│   ├── telegram/
│   ├── video/
│   └── vision/
├── logging/
├── presentation/
│   ├── controllers/
│   ├── pages/
│   ├── shell/
│   └── widgets/
├── shared/
└── srtp/
```

### `main.cpp`

- Qt 앱 시작점
- 로그 필터 설치
- 설정 로드
- 폰트/스타일시트 적용
- `MainWindow`, `LoginPage`, `MainWindowController` 조립

즉, 현재 앱의 가장 바깥 composition root 역할을 합니다.

### `presentation/`

사용자와 직접 맞닿는 계층입니다.

- `shell/`: `MainWindow`, `HeaderBarView`, 전체 화면 골격
- `pages/`: 로그인, CCTV, DB, 녹화, Telegram 페이지 단위 뷰
- `widgets/`: `VideoWidget`, `CameraChannelRuntime`, ROI 인터랙션, 렌더링 보조
- `controllers/`: UI 이벤트 처리와 화면 오케스트레이션

특징:

- 화면 책임은 controller/view/widget 조합으로 분리되어 있습니다.
- `MainWindowController`는 직접 모든 기능을 처리하지 않고 specialized controller를 조립하는 coordinator 역할을 맡습니다.

### `application/`

화면과 도메인/인프라 사이에서 "하나의 작업 흐름"을 구성하는 계층입니다.

- `application/parking/ParkingService`
  - 메타데이터, OCR 결과, ROI 상태를 받아 입출차 로직을 실행
- `application/roi/RoiService`
  - ROI 저장/조회/동기화 처리
- `application/db/*`
  - DB 화면에서 필요한 조회/수정/삭제를 UI 친화적 흐름으로 묶음

특징:

- `ParkingService`는 현재 구조에서 핵심 유스케이스 서비스입니다.
- DB 화면은 repository를 직접 다루기보다 application service를 통해 접근하도록 정리되어 있습니다.

### `domain/`

외부 시스템과 무관한 핵심 규칙이 위치합니다.

- `domain/parking/VehicleTracker`
- `domain/parking/ParkingFeePolicy`

현재는 주차 도메인 중심으로 구성되어 있으며, 차량 상태 추적과 요금 정책이 여기에 있습니다.

### `infrastructure/`

외부 시스템 연결 구현을 담당합니다.

- `camera/`: `CameraManager`, `CameraSource`, `CameraSessionService`, RTSP URL 조립
- `video/`: `VideoThread`, `VideoBufferManager`, `MediaRecorderWorker`, `SharedVideoFrame`
- `metadata/`: `MetadataThread`, `MetadataSynchronizer`, ONVIF 파서, FFmpeg 메타데이터 리더
- `ocr/`: `PlateOcrCoordinator`, `OcrManager`, LLM OCR 러너
- `vision/`: `ReidSession`, `SharedReidRuntime`
- `persistence/`: `DatabaseContext`, 각종 repository
- `telegram/`: Telegram API 연동
- `rpi/`: RPi 인증/제어 TCP 클라이언트

특징:

- 카메라 수신, 메타데이터 파싱, OCR, 영상 저장, DB 저장은 모두 인프라 계층에 모여 있습니다.
- 현재 프로젝트에서 `CameraSource`는 인프라 계층에 있지만, 단순 I/O 래퍼를 넘어서 여러 런타임 흐름의 허브 역할도 함께 수행합니다.

### `config/`, `logging/`, `shared/`

- `config/`: `settings.json` 기반 설정 로드
- `logging/`: 카테고리 로그 필터링, 로그 중복 억제
- `shared/`: 최소한의 공용 결과 타입

### `srtp/`

SRTP 카메라 연결을 위한 별도 저수준 모듈입니다.

- `SrtpSession`
- `SrtpRtspClient`
- `SrtpVideoThread`
- `SrtpOrchestrator`

현재 폴더 위치상 `infrastructure/` 밖에 있지만, 역할 자체는 인프라 성격에 가깝습니다.

## 3. 런타임 조립 구조

앱 시작 후 객체가 연결되는 큰 흐름은 아래와 같습니다.

```text
main.cpp
  -> LoginPage 표시
  -> 로그인 성공 시 MainWindowController 생성
  -> MainWindowController가 각 specialized controller 조립
  -> CameraSessionController가 카메라 소스 초기화
  -> CctvController / ChannelRuntimeController / RecordingWorkflowController 등과 연결
```

### 핵심 조립 포인트

#### `MainWindowController`

현재 UI 계층의 중심 coordinator입니다.

주요 책임:

- 페이지 전반의 controller 조립
- 컨트롤러 간 연결선 관리
- 공통 로그/알림/초기화 흐름 전달
- 카메라 raw frame을 녹화 워크플로우로 위임

직접 담당하지 않고 하위 컨트롤러로 위임하는 영역:

- 카메라 세션 시작/정지: `CameraSessionController`
- CCTV 레이아웃/채널 선택/ROI: `CctvController`
- 채널별 런타임 바인딩: `ChannelRuntimeController`
- 녹화 및 캡처: `RecordingWorkflowController`
- 녹화 패널 재생/목록: `RecordPanelController`
- DB 패널 조회/수정: `DbPanelController`
- Telegram 패널: `TelegramPanelController`
- 하드웨어 연동: `HardwareController`
- 알림 표시: `NotificationController`
- ReID 연동: `ReidController`

즉, 현재 구조는 "거대한 단일 MainWindow"에서 벗어나 "MainWindowController + specialized controllers" 형태로 정리된 상태입니다.

#### `CameraSessionController`

채널별 `CameraSource`를 생성하고 관리하는 상위 컨트롤러입니다.

주요 책임:

- 최대 4개 채널용 `CameraSource` 보유
- 썸네일 갱신
- 공용 ReID 런타임 초기화
- `rawFrameReady`를 상위 coordinator로 전달

#### `ChannelRuntimeController`

화면 위젯과 채널 런타임 객체를 연결하는 binder 역할입니다.

주요 책임:

- `VideoWidget`와 `CameraChannelRuntime` 연결
- 메인 CCTV 화면과 녹화 탭 프리뷰의 표시 소비자 크기 반영
- 화면 resize, FPS 표시, stale object 표시 옵션 반영

#### `CctvController`

CCTV 페이지에서 사용자가 직접 만지는 화면 동작을 담당합니다.

주요 책임:

- 싱글/듀얼/쿼드 레이아웃 전환
- 채널 카드 선택 상태 관리
- ROI 대상 채널 선택
- ROI 생성/완료/삭제 처리

#### `RecordingWorkflowController`

저장 워크플로우를 담당하는 실질적인 녹화 coordinator입니다.

주요 책임:

- 실시간 raw frame 수집
- 채널별 버퍼 적재
- 수동 캡처/수동 녹화/이벤트 녹화 처리
- 상시 녹화 타이머 운용
- `MediaRecorderWorker` 백그라운드 저장 요청
- 녹화 탭 라이브 프리뷰 바인딩

#### `DbPanelController`

DB 페이지의 여러 하위 패널을 묶는 UI 조정자입니다.

주요 책임:

- 주차 로그, 사용자, 차량, 구역 테이블 갱신
- `ParkingLogApplicationService`, `UserAdminApplicationService`, `ZoneQueryApplicationService` 연결

## 4. 카메라 런타임 구조

현재 카메라 처리의 중심은 `CameraSource`입니다.

### `CameraManager`

- 채널별 실제 스트림 시작/중지 관리
- 비디오 스레드와 메타데이터 스레드 관리
- 필요 시 SRTP 경로 orchestration

일반적으로 한 채널은 다음 자원을 가집니다.

- `VideoThread` 1개
- `MetadataThread` 1개

### `CameraSessionService`

`CameraManager`와 `MetadataSynchronizer`를 묶어 프레임-메타데이터 타이밍을 맞춥니다.

주요 책임:

- 카메라 재생/재시작 요청 위임
- 메타데이터 버퍼 적재
- 특정 프레임 시각에 맞는 메타데이터 소비

### `CameraSource`

현재 런타임에서 가장 중요한 허브 객체입니다.

입력:

- `VideoThread`가 전달한 최신 비디오 프레임
- `MetadataThread`가 전달한 객체 메타데이터

내부 책임:

- latest-frame 보관
- 메타데이터 동기화
- display / thumbnail / OCR / ReID 타이머 분리
- `ParkingService`, `RoiService`와 연결
- 상태 관리, 헬스체크, reconnect 제어

출력:

- 메인 CCTV 화면용 `displayFrameReady`
- 썸네일용 `thumbnailFrameReady`
- 녹화용 `rawFrameReady`
- ROI/zone 상태 변경 신호

현재 구조상 `CameraSource`는 "카메라별 런타임 컨테이너"라고 보는 편이 가장 이해하기 쉽습니다.

## 5. 현재 데이터 흐름

### 5-1. 라이브 표시 흐름

```text
Camera
  -> VideoThread
  -> CameraSource
  -> displayFrameReady / thumbnailFrameReady
  -> CameraChannelRuntime / VideoWidget / thumbnail QLabel
```

핵심 포인트:

- 채널당 비디오 스트림은 기본적으로 1개만 유지합니다.
- `CameraSource`는 최신 프레임 한 장을 중심으로 display 소비자를 갱신합니다.
- 녹화 탭 프리뷰도 별도 RTSP를 다시 열지 않고 같은 `CameraSource`를 공유합니다.

### 5-2. 메타데이터 및 OCR 흐름

```text
Camera metadata stream
  -> MetadataThread
  -> CameraSessionService / MetadataSynchronizer
  -> CameraSource
  -> PlateOcrCoordinator
  -> ParkingService
```

핵심 포인트:

- 메타데이터는 비디오와 별도 스트림으로 수신합니다.
- `CameraSource`가 최신 프레임과 메타데이터를 맞춰 OCR crop과 주차 판단에 사용합니다.
- OCR은 별도 비디오 스트림을 열지 않고 메인 비디오 프레임을 재사용합니다.
- OCR 결과는 `ParkingService`로 전달되어 차량 상태와 입출차 기록에 반영됩니다.

### 5-3. 녹화 및 저장 흐름

```text
CameraSource
  -> rawFrameReady
  -> RecordingWorkflowController
  -> VideoBufferManager
  -> MediaRecorderWorker
  -> MediaRepository
  -> DB / file system
```

핵심 포인트:

- 저장 경로는 화면 표시 경로와 분리되어 있습니다.
- 실시간 프레임은 채널별 버퍼에 적재됩니다.
- 실제 파일 저장은 `MediaRecorderWorker`가 백그라운드에서 수행합니다.
- 저장 메타데이터와 조회는 `MediaRepository`가 담당합니다.

## 6. 현재 아키텍처의 장점

- 화면 계층이 `MainWindowController` 하나에 몰리지 않고 specialized controller로 분리되어 있습니다.
- 카메라, 메타데이터, OCR, 녹화, DB, Telegram 책임이 폴더 구조상 비교적 명확합니다.
- 채널당 비디오 스트림 수를 줄이고 프레임 재사용을 늘려 중복 연결을 줄였습니다.
- latest-frame 기반 처리로 프레임 적체를 완화하고 UI 응답성을 유지하기 쉽습니다.
- DB 화면 흐름을 application service로 분리해 presentation과 persistence 결합을 낮췄습니다.

## 7. 현재 구조에서 이해하고 넘어가야 할 점

- 프로젝트는 계층형 구조로 정리되었지만, 아직 빌드 타깃 단위까지 분리된 것은 아닙니다.
- `ParkingService`는 이름상 application 계층이지만 repository와 Telegram 연동까지 함께 다루고 있어 역할 폭이 넓은 편입니다.
- `CameraSource`도 인프라 객체이면서 런타임 허브 성격이 강해 책임이 큰 편입니다.
- `srtp/`는 역할상 인프라이지만 현재 별도 최상위 폴더에 있습니다.
- 문서상 목표 구조와 실제 구조 사이에 일부 과도기 흔적이 남아 있습니다.

## 8. 지금 이 프로젝트를 읽는 추천 순서

처음 구조를 파악할 때는 아래 순서가 가장 빠릅니다.

1. `src/main.cpp`
2. `src/presentation/controllers/mainwindowcontroller.*`
3. `src/presentation/controllers/camerasessioncontroller.*`
4. `src/infrastructure/camera/camerasource.*`
5. `src/application/parking/parkingservice.*`
6. `src/presentation/controllers/recordingworkflowcontroller.*`
7. `src/infrastructure/persistence/*`

이 순서로 보면 "앱 조립 -> 화면 orchestration -> 카메라 런타임 -> 비즈니스 처리 -> 저장/DB" 흐름이 자연스럽게 이어집니다.

## 9. 관련 문서

- `docs/layered-architecture-guidelines.md`
- `docs/frame-optimization-progress.md`
- `README.md`
