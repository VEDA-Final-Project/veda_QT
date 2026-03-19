# 🛠️ GPU 가속(OpenVINO) 환경 구축 및 실행 가이드

이 문서는 인텔 내장 GPU(Iris Xe 등)를 활용하여 시스템의 CPU 부하를 줄이고, ReID(특징 추출) 연산 프레임 속도를 최적화하는 방법을 설명합니다.

---

### 1. 하드웨어 및 기본 환경 (Prerequisites)
*   **OS**: Windows 10/11 (64-bit)
*   **CPU**: Intel Core i5 이상 권장 (인텔 11세대 이후 타이거레이크 이상 내장 그래픽 필수)
*   **GPU Driver**: [인텔 지원 페이지](https://www.intel.co.kr/content/www/kr/ko/download-center/home.html)에서 **최신 그래픽 드라이버**로 업데이트 (OpenVINO GPU 인식에 필수)
*   **Compiler**: **Visual Studio 2022** (MSVC 64-bit)

---

### 2. 필수 라이브러리 설치 (Libraries)

#### A. 인텔 OpenVINO 런타임 (Manual Install)
1.  [OpenVINO 2025.4.1 LTS Archive](https://storage.openvinotoolkit.org/repositories/openvino/packages/2025.4.1/windows/)에서 `w_openvino_toolkit_windows_2025.4.1.xxx_x86_64.zip` 파일을 다운로드합니다.
2.  `C:\openvino_2025.4.1` 폴더에 압축을 풉니다.
    *   *주의: 폴더명이 다르면 `CMakeLists.txt` 상단의 `OPENVINO_ROOT` 경로를 수정해야 합니다.*

#### B. vcpkg 의존성 설치 (Package Manager)
터미널(PowerShell)에서 다음 명령어를 실행하여 필수 패키지를 설치합니다:
```powershell
# 1. vcpkg 폴더로 이동 (예: C:\vcpkg)
cd C:\vcpkg

# 2. 필수 패키지 설치 (x64-windows 버전)
.\vcpkg.exe install opencv4[ffmpeg,dnn,opencl,directml]:x64-windows
.\vcpkg.exe install protobuf:x64-windows
```

---

### 3. 프로젝트 설정 및 빌드 (CMake & Build)

1.  **Qt Creator**에서 프로젝트(`CMakeLists.txt`)를 엽니다.
2.  **프로젝트 설정(Build Environment)**에서 다음 **CMake 변수**를 확인/추가합니다:
    *   `CMAKE_TOOLCHAIN_FILE`: `C:/vcpkg/scripts/buildsystems/vcpkg.cmake`
3.  **빌드 모드 선택**: 반드시 **`Release`** 모드를 선택하십시오. (Debug 모드는 최적화가 꺼져 있어 성능이 대폭 저하됩니다.)
4.  **`Run CMake` (프로젝트 재구성)**를 실행합니다.
    *   로그에 `[ReID][OV] OpenVINO found!` 메시지가 뜨는지 확인합니다.
5.  **Build** 실행. 빌드가 완료되면 결과 폴더(`bin/Release`)에 `openvino.dll`, `tbb12.dll` 등 필수 파일이 자동으로 복사됩니다.

---

### 4. 실행 및 확인 (Execution)
프로그램 실행 후 하단 로그 또는 콘솔 창에서 다음 메시지를 확인하세요:
*   `[ReID][OV] Available devices: GPU, CPU`: GPU가 정상 인식됨.
*   `[ReID][OV] Model loaded on GPU successfully!`: GPU 가속이 활발하게 작동 중.

---

### 💡 문제 발생 시 조치법 (Troubleshooting)

*   **GPU 인식이 안 되고 CPU만 뜰 때**: 인텔 그래픽 드라이버 버전이 너무 낮으면 OpenVINO가 GPU 플러그인을 로드하지 못합니다. 드라이버를 최신으로 업데이트해 주세요.
*   **`tbb12.dll` 관련 에러**: `OpenVINO_ROOT/runtime/bin/intel64/Release` 폴더에 있는 파일을 실행 폴더로 수동 복사하거나, `Run CMake`를 다시 수행하세요.
*   **성능이 여전히 낮을 때**: 반드시 **Release** 모드 빌드 여부를 다시 확인하세요.
