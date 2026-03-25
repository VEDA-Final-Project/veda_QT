# PR: RTSP 메타데이터 수신을 FFmpeg 외부 프로세스에서 라이브러리 기반으로 전환

## 배경

기존 일반 RTSP 메타데이터 수신은 `MetadataThread` 내부에서 `QProcess + ffmpeg.exe`를 실행하는 방식이었습니다.

- 실행 환경에 `ffmpeg.exe`가 PATH에 있거나 별도로 배치되어 있어야 했음
- 메타데이터 수신 로직이 외부 프로세스 lifecycle에 의존해 배포와 운영이 번거로웠음
- 일반 RTSP 메타데이터 파싱과 SRTP 메타데이터 파싱이 거의 같은 일을 각각 따로 구현하고 있었음
- `ObjectInfo` 타입이 `MetadataThread`에 묶여 있어 다른 메타데이터 구성요소가 구현 세부사항에 의존하고 있었음

이번 변경은 메타데이터 수신을 FFmpeg 라이브러리 기반으로 옮기고, ONVIF 메타데이터 파서를 일반 RTSP/SRTP 경로에서 공통으로 사용하도록 정리하는 데 목적이 있습니다.

## 주요 변경 사항

### 1. 일반 RTSP 메타데이터 수신을 `ffmpeg.exe` subprocess에서 FFmpeg 라이브러리로 전환

- `MetadataThread`가 더 이상 `QProcess`로 `ffmpeg.exe`를 실행하지 않음
- 새 `FfmpegMetadataStreamReader`를 추가해 `libavformat` 기반으로 RTSP 입력을 직접 엶
- `AVMEDIA_TYPE_DATA` 스트림을 찾아 `av_read_frame()`으로 메타데이터 패킷을 읽음
- RTSP transport는 기존 동작과 맞춰 TCP 우선, 짧은 timeout 중심으로 유지

### 2. ONVIF 메타데이터 파서를 공통 컴포넌트로 추출

- `OnvifMetadataParser`를 추가해 XML 버퍼링, 프레임 재조립, 객체 파싱 로직을 공통화
- 일반 RTSP 메타데이터 경로와 SRTP 메타데이터 경로가 같은 파서를 사용하도록 정리
- 빈 객체 프레임도 그대로 전달해 ghost box 방지 동작을 유지
- 객체 type 필터링, score 파싱, plate 파싱의 기존 의미를 유지

### 3. `ObjectInfo`를 독립 타입으로 분리

- `ObjectInfo`를 `MetadataThread` 헤더에서 분리해 `objectinfo.h`로 이동
- 파서, 동기화기, UI, 주차 추적, SRTP 경로가 더 이상 `MetadataThread` 구현에 직접 묶이지 않음

### 4. SRTP 메타데이터 파서를 공통 파서 어댑터로 축소

- `SrtpMetadataParser`는 자체 파싱 구현 대신 공통 `OnvifMetadataParser`에 위임
- SRTP 상위 계약인 `metadataReceived()` 시그널과 `SrtpOrchestrator` 연결 방식은 유지
- start/stop 시 parser buffer를 명시적으로 reset하도록 정리

### 5. 빌드/문서 정책 정리

- CMake에서 FFmpeg 헤더와 `avcodec`, `avformat`, `avutil`, `swscale` 라이브러리를 필수로 요구
- README와 아키텍처 문서에서 `ffmpeg.exe` 기반 안내를 제거하고 라이브러리 기반 설명으로 갱신
- 메타데이터 경로에는 runtime fallback을 두지 않고, FFmpeg 라이브러리 전제를 명확히 함

## 영향 범위

- 일반 RTSP 메타데이터 수신 경로
- SRTP 메타데이터 파싱 구현 구조
- 메타데이터 타입 의존 관계
- 빌드 시 FFmpeg 의존성 검사
- 운영/배포 시 `ffmpeg.exe` 필요 여부

일반 RTSP 비디오 경로는 이번 변경에서 유지하며, 여전히 OpenCV `VideoCapture` 기반입니다.

## 관련 파일

- `src/infrastructure/metadata/metadatathread.*`
- `src/infrastructure/metadata/ffmpegmetadatastreamreader.*`
- `src/infrastructure/metadata/onvifmetadataparser.*`
- `src/infrastructure/metadata/objectinfo.h`
- `src/srtp/srtpmetadataparser.*`
- `src/srtp/srtporchestrator.*`
- `CMakeLists.txt`
- `README.md`

## 확인 방법

1. 메타데이터가 있는 일반 RTSP 카메라 연결 시 `ffmpeg.exe` 없이 객체 박스/plate 정보가 계속 수신되는지 확인
2. 메타데이터가 없는 RTSP 카메라 연결 시 앱이 죽지 않고 analytics 경로만 실패 로그를 남기는지 확인
3. 일반 RTSP 메타데이터와 SRTP 메타데이터 모두에서 disabled type 필터가 동일하게 적용되는지 확인
4. 객체가 사라진 프레임에서 빈 metadata가 전달되어 이전 박스가 화면에 남지 않는지 확인
5. start/stop/restart 반복 시 메타데이터 스레드가 중복 실행되거나 종료 지연이 없는지 확인
6. 기존 UI overlay, ROI 연동, 주차 추적, OCR 트리거 흐름이 회귀 없이 유지되는지 확인

## 리뷰 포인트

- `MetadataThread`가 subprocess 책임을 완전히 제거하고 reader/parser 조합만 담당하는지
- `OnvifMetadataParser`가 일반 RTSP와 SRTP에서 동일한 파싱 결과를 내도록 공통화되었는지
- `ObjectInfo` 분리 후 의존 방향이 자연스러워졌는지
- `FfmpegMetadataStreamReader`의 open/read/interrupt/cleanup 경로가 stop 요청에 안전하게 반응하는지
- CMake에서 FFmpeg 의존성 실패를 조기에 명확하게 드러내는지

## 비고

- 이번 변경은 메타데이터 경로만 FFmpeg 라이브러리 기반으로 전환하며, 일반 RTSP 비디오 디코드 경로는 포함하지 않습니다.
- 현재 환경에서는 Windows 툴체인 문제로 전체 빌드를 끝까지 검증하지 못했고, 실제 실패 로그는 표준 라이브러리 헤더(`type_traits`, `utility`) 탐색 실패였습니다. 소스 변경 자체 외에 로컬 빌드 환경 점검이 추가로 필요합니다.
