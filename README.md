# Hanwha Vision Parking Management System

한화비전 AI 카메라(P/X 시리즈)와 연동하여 차량 번호판을 인식하고 메타데이터를 시각화하는 주차 관제 시스템 프로젝트입니다.
최근 대규모 리팩토링을 통해 아키텍처가 개선되었으며, 설정 외부화를 통해 재컴파일 없이 환경 설정을 변경할 수 있습니다.

## 🚀 주요 기능

- **RTSP 영상 스트리밍**: OpenCV 및 FFmpeg를 활용한 저지연 비디오 재생
- **AI 메타데이터 시각화**: 카메라에서 수신한 AI 객체(차량, 번호판 등) 정보를 영상 위에 오버레이
- **번호판 OCR**: Tesseract OCR을 통한 번호판 텍스트 추출 (비동기 처리)
- **설정 외부화**: `settings.json`을 통해 카메라 IP, 해상도, 싱크 조절 등을 간편하게 관리
- **싱크 조절**: 영상과 메타데이터 간의 시간차를 UI에서 실시간으로 보정 가능

## 🏗️ 프로젝트 구조

프로젝트는 기능별로 명확하게 분리된 디렉토리 구조를 가지고 있습니다.

```
veda_QT/
├── CMakeLists.txt          # Qt6(CMake) 빌드 설정, OpenCV/Tesseract/Leptonica 링크
├── config/
│   └── settings.json       # 런타임 설정 (빌드 시 실행 파일 옆 config/ 로 복사됨)
└── src/
    ├── main.cpp            # 앱 진입점: Config 로드 후 MainWindow 실행
    ├── core/
    │   ├── config.h/.cpp           # settings.json 로드 및 기본값 관리 (싱글톤)
    │   └── cameramanager.h/.cpp    # 카메라/스트림 관련 핵심 관리
    ├── ui/
    │   ├── mainwindow.h/.cpp       # 메인 윈도우 및 UI 컨트롤
    │   └── videowidget.h/.cpp      # 영상 표시 위젯 (오버레이 등)
    ├── video/
    │   └── videothread.h/.cpp      # RTSP 영상 스트리밍 스레드
    ├── metadata/
    │   └── metadatathread.h/.cpp   # 카메라 AI 메타데이터 수신 스레드
    └── ocr/
        └── ocrmanager.h/.cpp       # Tesseract 기반 OCR 비동기 처리
```

> **참고**: Tesseract 언어 데이터(`tessdata/`, `kor.traineddata` 등)는 `settings.json`의 `tessdataPath`에 지정한 경로에 두면 됩니다.

## 🛠️ 개발 환경 및 요구 사항

- **OS**: Windows 10/11 64-bit
- **Compiler**: MSVC 2019/2022 (또는 MinGW)
- **Framework**: Qt 6.5+ (Core, Widgets, Network, Concurrent)
- **Dependencies** (vcpkg 권장):
  - OpenCV 4.x
  - Tesseract 5.x
  - Leptonica
  - FFmpeg (런타임 필요)

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
.\vcpkg\vcpkg install opencv tesseract leptonica --triplet x64-windows
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

> **참고**: FFmpeg 실행 파일(`ffmpeg.exe`)이 시스템 PATH에 있거나 실행 파일과 같은 폴더에 있어야 메타데이터 수신 기능이 작동합니다.

## ⚙️ 설정 방법 (settings.json)

빌드 후 `config/settings.json` 파일을 수정하여 재컴파일 없이 설정을 변경할 수 있습니다.

```json
{
  "camera": {
    "ip": "192.168.0.100",       // 카메라 IP
    "username": "admin",         // RTSP 계정
    "password": "********",      // RTSP 비밀번호 (실제 값으로 교체)
    "profile": "profile2/media.smp" // RTSP 프로파일
  },
  "video": {
    "sourceWidth": 3840,         // 카메라 원본 해상도 (Width)
    "sourceHeight": 2160,        // 카메라 원본 해상도 (Height)
    "effectiveWidth": 2880,      // 실제 유효 영상 폭 (Crop 고려)
    "cropOffsetX": 480           // X축 Crop 오프셋
  },
  "ocr": {
    "language": "kor",           // OCR 언어 (kor/eng)
    "tessdataPath": "C:/path/to/tessdata/" // traineddata 파일 경로
  },
  "sync": {
    "defaultDelayMs": 0          // 초기 싱크 딜레이 (ms)
  }
}
```

## ⚠️ 트러블슈팅

**Q. OCR 초기화 실패 오류가 발생합니다.**
A. `settings.json`의 `tessdataPath`가 올바른지 확인하세요. 또한 해당 경로에 `kor.traineddata` 또는 `eng.traineddata` 파일이 존재하는지 확인해야 합니다.

**Q. 메타데이터가 화면에 표시되지 않습니다.**
A. `ffmpeg`가 설치되어 있는지 확인하고, 시스템 환경 변수 `PATH`에 추가되어 있는지 확인하세요. 또한 카메라 시간과 PC 시간이 동기화되어 있는지 확인이 필요할 수 있습니다.
