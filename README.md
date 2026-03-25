# Hanwha Vision Parking Management System

한화비전 AI 카메라(P/X 시리즈)와 연동하여 차량 번호판을 인식하고 메타데이터를 시각화하는 주차 관제 시스템 프로젝트입니다.
최근 대규모 리팩토링을 통해 계층형 아키텍처로 정리되었고, 설정 외부화를 통해 재컴파일 없이 환경 설정을 변경할 수 있습니다.

## 🚀 주요 기능

- **RTSP 영상 스트리밍**: OpenCV 및 FFmpeg를 활용한 저지연 비디오 재생
- **AI 메타데이터 시각화**: 카메라에서 수신한 AI 객체(차량, 번호판 등) 정보를 영상 위에 오버레이
- **ROI(관심 구역) 관리**: 다각형 ROI 생성/저장/삭제 및 화면 라벨 표시
- **번호판 OCR**: LLM 기반 번호판 텍스트 추출과 비동기 후처리
- **ReID 기반 추적**: 차량 재식별과 번호판/객체 추적 보조
- **설정 외부화**: `settings.json`을 통해 카메라 IP, 해상도, 싱크 조절 등을 간편하게 관리
- **인증/하드웨어 연동**: 외부 인증 서버 및 RPi 제어 클라이언트 연동
- **싱크 조절**: 설정(`defaultDelayMs`) 기반으로 영상/메타데이터 시간차 보정

## 🏗️ 프로젝트 구조

현재 코드는 계층형 아키텍처 기준으로 정리되어 있습니다.
의존 규칙과 전환 기준은 `docs/layered-architecture-guidelines.md` 문서를 기준으로 관리합니다.

```text
veda_QT/
├── CMakeLists.txt
├── config/
│   └── settings.json
└── src/
    ├── main.cpp
    ├── application/
    │   ├── db/
    │   │   ├── parking/
    │   │   ├── user/
    │   │   └── zone/
    │   ├── parking/
    │   └── roi/
    ├── config/
    │   └── config.h/.cpp
    ├── domain/
    │   └── parking/
    ├── logging/
    │   └── logdeduplicator.h/.cpp
    ├── infrastructure/
    │   ├── camera/
    │   ├── metadata/
    │   ├── ocr/
    │   ├── persistence/
    │   ├── rpi/
    │   ├── telegram/
    │   ├── video/
    │   └── vision/
    ├── presentation/
    │   ├── controllers/
    │   ├── pages/
    │   ├── shell/
    │   └── widgets/
    ├── shared/
    │   └── result.h
    └── ui/
        └── icon/
```

주요 예시는 다음과 같습니다.

- `src/presentation/*`: `MainWindow`, 페이지 view, widget, UI controller
- `src/application/*`: DB 화면용 application service, `ParkingService`, `RoiService`
- `src/domain/*`: `ParkingFeePolicy`, `VehicleTracker`
- `src/infrastructure/*`: `CameraSource`, `VideoThread`, `MetadataThread`, OCR, Telegram, RPi, DB repository, ReID extractor

> **참고**: `CameraChannelRuntime`, `ControllerDialog`, `VideoWidget` 등 UI 위젯은 `src/presentation/widgets/` 아래에 있습니다. `src/ui/`에는 현재 아이콘 자산만 남아 있습니다.

> **참고**: ReID 모델 파일은 `settings.json`의 `reid.modelPath`에 지정합니다. OCR은 `ocr.type` 설정에 따라 동작 방식이 달라지며, LLM 모드에서는 `GEMINI_API_KEY` 환경변수 또는 `ocr.gemini.apiKey` 설정을 사용할 수 있습니다.

## 🛠️ 개발 환경 및 요구 사항

- **OS**: Windows 10/11 64-bit
- **Compiler**: MSVC 2019/2022 (또는 MinGW)
- **Framework**: Qt 6.5+ (Core, Widgets, Network, Concurrent)
- **Dependencies** (vcpkg 권장):
  - OpenCV 4.x
  - Protobuf
  - FFmpeg (런타임 필요)
  - OpenVINO (선택 사항, ReID 가속용)

## 📥 Qt 설치 방법 (Windows)

1. **Qt 다운로드**: [Qt Online Installer](https://www.qt.io/download-qt-installer)에서 설치 프로그램 다운로드 후 실행.
2. **선택할 구성 요소**:
   - **Qt** → **Qt 6.5.x** (이상)
     - **MSVC 2019/2022 64-bit** (Visual Studio 사용 시) 또는 **MinGW 64-bit**
   - **Developer and Designer Tools**:
     - **Qt Creator**
     - **CMake**
     - **Ninja** (권장)
3. **MSVC 사용 시**: Visual Studio 2019/2022 설치 후 **Desktop development with C++** 워크로드 포함.
4. 설치 완료 후 Qt Creator에서 이 프로젝트의 `CMakeLists.txt`를 열고, Kit을 위에서 선택한 컴파일러와 맞추면 됩니다.

## 📦 빌드 및 실행 방법

이 프로젝트는 **vcpkg**를 통한 의존성 관리를 권장합니다.

### 1. 전제 조건 설치
```powershell
# vcpkg 설치 (이미 있다면 생략)
git clone https://github.com/microsoft/vcpkg.git
.\vcpkg\bootstrap-vcpkg.bat
```

### 2. 의존성 라이브러리 설치
```powershell
# [vcpkg-root]는 vcpkg를 clone한 폴더 경로입니다.
.\vcpkg\vcpkg install opencv protobuf --triplet x64-windows
```

### 3. 프로젝트 빌드 (CMake)
```powershell
# [vcpkg-root]를 본인의 vcpkg 설치 경로로 변경하세요.
# 예: D:/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="[vcpkg-root]/scripts/buildsystems/vcpkg.cmake"

# 릴리즈 모드로 빌드
cmake --build build --config Release
```

**Qt Creator에서 빌드**: 프로젝트 열기 → `CMakeLists.txt` 선택 → Kit 선택 후, CMake 설정에서 `CMAKE_TOOLCHAIN_FILE`을 vcpkg의 `scripts/buildsystems/vcpkg.cmake` 경로로 지정한 뒤 Configure & Build.

### 4. 실행
`build/Release/rtsp_test.exe`를 실행합니다. 빌드 시 `config/` 폴더가 실행 파일 옆으로 자동 복사됩니다.

> **참고**: 메타데이터 수신은 FFmpeg 실행 파일이 아니라 FFmpeg 개발용 라이브러리(`avformat`, `avcodec`, `avutil`, `swscale`) 링크를 사용합니다. 빌드 시 vcpkg 경로가 올바르게 설정되어 있어야 합니다.

## ⚙️ 설정 방법 (settings.json)

빌드 후 `config/settings.json` 파일을 수정하여 재컴파일 없이 설정을 변경할 수 있습니다.
현재 설정은 다중 카메라, OCR, ReID, 인증 서버, RPi 제어를 포함합니다.

```json
{
  "cameraDefaults": {
    "profile": "profile6/media.smp",
    "subProfile": "profile7/media.smp"
  },
  "camera": {
    "ip": "192.168.0.23",
    "username": "admin",
    "password": "********"
  },
  "camera2": {
    "ip": "192.168.0.34",
    "username": "admin",
    "password": "********"
  },
  "video": {
    "sourceWidth": 3840,
    "sourceHeight": 2160,
    "effectiveWidth": 3840,
    "cropOffsetX": 0
  },
  "ocr": {
    "type": "LLM",
    "gemini": {
      "model": "gemini-3.1-flash-lite-preview",
      "prompt": "이 이미지는 자동차 번호판의 크롭 본이야. 번호판 숫자와 글자만 정확히 추출해줘."
    },
    "inputWidth": 320,
    "inputHeight": 48
  },
  "reid": {
    "modelPath": "model/sbs_R50-ibn.onnx",
    "inputWidth": 256,
    "inputHeight": 256
  },
  "sync": {
    "defaultDelayMs": 100
  },
  "auth": {
    "host": "192.168.0.67",
    "port": 9000
  },
  "rpiControl": {
    "host": "192.168.0.44",
    "port": 12345,
    "autoConnect": true
  }
}
```

> **주의**: 실제 운영용 IP, 비밀번호, API 키는 저장소에 직접 커밋하지 않는 것을 권장합니다.

## ⚠️ 트러블슈팅

**Q. OCR 요청이 실패하거나 번호판 인식이 비어 있습니다.**
A. `settings.json`의 `ocr.type` 설정을 확인하세요. LLM 모드라면 `GEMINI_API_KEY` 환경변수 또는 `ocr.gemini.apiKey`가 필요할 수 있습니다. 프롬프트와 네트워크 연결 상태도 함께 확인하세요.

**Q. ReID가 동작하지 않거나 성능이 낮습니다.**
A. `settings.json`의 `reid.modelPath`가 올바른 모델 파일을 가리키는지 확인하세요. OpenVINO가 없는 환경에서는 CPU/DirectML fallback으로 동작할 수 있습니다.

**Q. 메타데이터가 화면에 표시되지 않습니다.**
A. `ffmpeg`가 설치되어 있는지 확인하고, 시스템 환경 변수 `PATH`에 추가되어 있는지 확인하세요. 또한 카메라 시간과 PC 시간이 동기화되어 있는지 확인이 필요할 수 있습니다.

**Q. Qt/MSVC 빌드에서 `type_traits`, `utility` 같은 표준 헤더를 못 찾습니다.**
A. 현재 확인된 사례는 프로젝트 include 경로 문제가 아니라 MSVC 표준 라이브러리 include path가 Qt Creator Kit 또는 Visual Studio Build Tools 환경에 제대로 잡히지 않은 경우입니다. Qt Creator에서 사용 중인 Kit이 실제 MSVC 설치와 연결되어 있는지, "Desktop development with C++" 워크로드가 설치되어 있는지, 그리고 Developer Command Prompt 환경에서 `cmake --build`가 동작하는지 먼저 확인하세요.
