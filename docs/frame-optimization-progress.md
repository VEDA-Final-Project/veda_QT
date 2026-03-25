# 프레임 최적화 진행 문서

최종 업데이트: 2026-03-12

## 목적

라이브 카메라 화면의 끊김을 줄이고, 중복 디코드와 불필요한 프레임 복사 및 색 변환 비용을 낮추는 것이 목표입니다.

## 개선 전 상태

- 녹화 탭 프리뷰가 메인 카메라 파이프라인과 별도로 RTSP를 다시 열고 있었음
- 채널 하나가 여러 비디오 디코더를 동시에 사용할 수 있었음
  - display video
  - OCR video
  - metadata stream
- `CameraSource::onFrameCaptured()`에서 프레임마다 너무 많은 작업을 수행하고 있었음
  - metadata 동기화
  - OCR crop 수집
  - display 이미지 복사
  - thumbnail 이미지 복사
- `VideoThread`가 모든 프레임을 `BGR -> RGB`로 변환하고 있었음
- 녹화 저장 시 다시 `RGB -> BGR`로 역변환하고 있었음
- 앱 시작 시 모든 채널이 동시에 열리면서 일부 채널 연결이 늦어질 수 있었음

## 적용한 개선 사항

### 1. 녹화 탭의 중복 RTSP 프리뷰 제거

대상 파일:

- `src/presentation/controllers/recordpanelcontroller.cpp`
- `src/presentation/controllers/mainwindowcontroller.cpp`

변경 내용:

- 녹화 탭 프리뷰가 별도 `VideoThread`를 띄우지 않도록 수정
- 기존 `CameraSource::displayFrameReady`를 재사용하도록 변경

기대 효과:

- CPU 사용량 감소
- 네트워크 사용량 감소
- 메모리 사용량 감소
- 중복 프레임 버퍼링 제거

### 2. 채널당 스트림 구조 단순화

대상 파일:

- `src/infrastructure/camera/cameramanager.cpp`
- `src/infrastructure/camera/cameramanager.h`
- `src/infrastructure/camera/camerasource.cpp`
- `src/presentation/widgets/camerachannelruntime.cpp`

변경 내용:

- OCR 전용 `VideoThread` 제거
- 현재 목표 구조를 채널당 아래 형태로 정리
  - `video 1`
  - `metadata 1`
- OCR은 UI와 같은 메인 비디오 스트림을 공유하도록 변경

기대 효과:

- RTSP 연결 수 감소
- 디코더 수 감소
- 4채널 동작 시 안정성 개선

### 3. 카메라 프로파일 설정을 `settings.json` 한 곳으로 정리

대상 파일:

- `config/settings.json`
- `src/config/config.cpp`
- `src/config/config.h`
- `src/infrastructure/camera/camerasource.cpp`
- `src/infrastructure/metadata/metadatathread.cpp`

변경 내용:

- 공통 `cameraDefaults.profile`, `cameraDefaults.subProfile` 추가
- 반복되던 카메라별 profile 설정 제거
- 카메라 및 metadata fallback profile이 하드코딩이 아니라 설정값을 읽도록 변경

기대 효과:

- 스트림 화질 조정이 쉬워짐
- 설정 불일치 가능성 감소

### 4. latest-frame buffer 방식으로 프레임 처리 구조 변경

대상 파일:

- `src/infrastructure/camera/camerasource.cpp`
- `src/infrastructure/camera/camerasource.h`

변경 내용:

- `onFrameCaptured()`는 최신 프레임만 저장하고 빠르게 반환하도록 변경
- display 렌더, thumbnail 렌더, OCR dispatch를 각각 별도 타이머에서 처리하도록 분리
- 오래된 프레임을 순서대로 다 처리하는 대신 최신 프레임 기준으로 따라가도록 변경

기대 효과:

- 메인 경로의 프레임당 처리 비용 감소
- UI가 잠깐 밀려도 최신 프레임으로 빠르게 복구 가능
- OCR/thumbnail 때문에 생기던 순간적인 끊김 완화

### 5. 내부 프레임 포맷을 `BGR`로 유지

대상 파일:

- `src/infrastructure/video/videothread.cpp`
- `src/infrastructure/camera/camerasource.cpp`
- `src/infrastructure/video/mediarecorderworker.cpp`

변경 내용:

- `VideoThread`가 디코드한 `BGR` 프레임을 그대로 전달하도록 변경
- `CameraSource`에서 UI/OCR에 필요한 시점에만 `QImage::Format_BGR888`로 감싸서 사용
- 녹화 저장 시 `RGB -> BGR` 역변환 제거

기대 효과:

- 라이브 프레임당 색 변환 1회 감소
- 저장 경로의 추가 색 변환 제거
- 4채널 상시 동작 시 CPU 부담 감소

### 6. 채널 순차 시작 적용

대상 파일:

- `src/presentation/controllers/mainwindowcontroller.cpp`

변경 내용:

- 모든 카메라를 동시에 시작하지 않도록 수정
- 채널당 `350ms` 간격으로 시작하도록 변경

기대 효과:

- RTSP open 경쟁 완화
- 일부 채널만 늦게 뜨는 현상 완화
- FFmpeg/OpenCV open 시점 락 영향 감소

## 현재 기대 구조

채널당 구조:

- `video 1`
- `metadata 1`
- UI, thumbnail, OCR이 같은 video source를 공유

처리 흐름:

1. `VideoThread`가 하나의 `BGR` 스트림을 디코드
2. `CameraSource`가 최신 프레임만 저장
3. display timer가 최신 프레임으로 화면 표시
4. thumbnail timer가 최신 프레임으로 썸네일 생성
5. OCR timer가 최신 프레임에서 crop 추출 후 OCR 요청
6. `MainWindowController`가 raw frame을 녹화 버퍼에 적재

## 기대 효과 요약

- 중복 디코드 감소
- 프레임 복사 비용 감소
- 색 변환 비용 감소
- 4채널 시작 안정성 개선
- UI가 잠시 밀려도 최신 프레임 기준으로 복구 가능

## 아직 남아 있는 한계

- `setTargetFps()`는 여전히 디코드 이후 프레임을 버리는 구조임
- 하드웨어 디코드는 아직 적용하지 않음
- metadata는 FFmpeg 라이브러리 기반으로 전환되었고, 일반 RTSP 비디오만 OpenCV 경로를 유지함
- display timeout 시 reconnect 정책이 아직 다소 공격적임
- `src/architecture_overview.md`는 현재 구조를 아직 반영하지 못함

## 다음 권장 작업

1. 채널별 계측 추가
   - start 요청 시각
   - 첫 프레임 도착 시각
   - reconnect 횟수
   - 평균 frame age
2. 대상 장비 기준 하드웨어 디코드 적용 가능성 검토
3. 짧은 네트워크 hiccup에서 과도한 reconnect가 발생하지 않도록 정책 완화
4. `src/architecture_overview.md`를 현재 구조 기준으로 업데이트

## 검증 상태

- 코드 변경은 저장소에 반영됨
- 현재 환경에 `cmake`가 없어 로컬 빌드 검증은 수행하지 못함
