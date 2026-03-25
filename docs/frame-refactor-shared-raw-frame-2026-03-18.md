# Shared Raw Frame + Consumer-side Render Cache 리팩터링 정리

작성일: 2026-03-18 19:22 KST

## 배경

기존 라이브 파이프라인은 `CameraSource`가 디코드된 프레임을 `QImage`로 바꾼 뒤, UI 표시용과 썸네일용으로 각각 전체 프레임을 `copy()` 해서 내보내는 구조였습니다.

이 구조에서는 같은 원본 프레임에 대해 아래 비용이 반복될 수 있었습니다.

- `CameraSource`에서 display용 전체 프레임 복사
- `CameraSource`에서 thumbnail용 전체 프레임 복사
- `VideoFrameRenderer`에서 다시 `scaled()` 수행

고해상도 RTSP 환경에서는 이 중복 복사와 스케일링이 CPU와 메모리 대역폭을 지속적으로 사용하게 되어, 라이브 UI 부하를 키우는 원인이 될 수 있습니다.

## 목표

- 라이브 파이프라인에서 원본 프레임을 한 번만 유지
- `CameraSource`의 전체 프레임 `QImage::copy()` 제거
- 소비자별로 필요한 크기의 축소본만 생성
- 같은 프레임과 같은 출력 크기 조합에 대해서는 축소 결과를 재사용
- 파일 재생 경로는 기존 `QImage` 기반 동작을 유지

## 이번에 적용한 구조 변경

### 1. 공용 라이브 프레임 타입 도입

새 내부 타입 `SharedVideoFrame`를 추가했습니다.

- 파일: `src/infrastructure/video/sharedvideoframe.h`
- 구성:
  - `QSharedPointer<cv::Mat> mat`
  - `qint64 timestampMs`

의미:

- 라이브 파이프라인에서는 디코드된 원본 `cv::Mat`을 공유 포인터로 넘깁니다.
- UI나 썸네일 소비자가 필요할 때만 이 원본을 `QImage` view로 감싸서 사용합니다.

### 2. 라이브 파이프라인 시그널을 `SharedVideoFrame` 기반으로 변경

변경 대상:

- `VideoThread::frameCaptured`
- `CameraManager::frameCaptured`
- `CameraSource::rawFrameReady`
- `CameraSource::displayFrameReady`
- `CameraSource::thumbnailFrameReady`

효과:

- 라이브 프레임 전달 과정에서 `QImage` 전체 복사를 기본 동작으로 하지 않게 되었습니다.
- `MainWindowController`의 녹화 버퍼 적재는 계속 원본 `cv::Mat`을 그대로 사용합니다.

### 3. `CameraSource`에서 라이브 display/thumbnail 복사 제거

변경 파일:

- `src/infrastructure/camera/camerasource.h`
- `src/infrastructure/camera/camerasource.cpp`

변경 내용:

- `CameraSource`는 최신 원본 프레임과 최신 메타데이터만 유지합니다.
- display timer tick에서는 `QImage`를 만들어 복사하지 않고 `SharedVideoFrame`만 emit 합니다.
- thumbnail timer tick에서도 마찬가지로 `SharedVideoFrame`만 emit 합니다.

즉, `CameraSource`는 더 이상 “표시용 이미지 생성기”가 아니라 “최신 원본 프레임 보관 + 배포” 역할에 가까워졌습니다.

### 4. `VideoWidget`에 live-frame 전용 캐시 추가

변경 파일:

- `src/ui/video/videowidget.h`
- `src/ui/video/videowidget.cpp`

변경 내용:

- 기존 `updateFrame(const QImage&)`는 파일/이미지 재생용으로 유지
- 새 `updateLiveFrame(const SharedVideoFrame&)` 추가
- live 경로에서는 아래 기준으로 축소 캐시를 유지
  - 마지막 프레임 포인터 identity
  - 마지막 대상 위젯 크기

동작:

1. 새 프레임이 오거나 위젯 크기가 바뀐 경우에만 `scaled()` 수행
2. 그 외에는 캐시된 축소본을 재사용
3. 원본 프레임은 필요 시 `QImage::Format_BGR888` view로만 감싸 사용

효과:

- 같은 프레임을 같은 위젯 크기로 다시 그릴 때 재스케일링을 피할 수 있습니다.

### 5. `VideoFrameRenderer`를 “축소 담당”에서 “오버레이 합성 담당”으로 분리

변경 파일:

- `src/ui/video/videoframerenderer.h`
- `src/ui/video/videoframerenderer.cpp`

변경 내용:

- `compose()`가 더 이상 내부에서 `frame.scaled(...)`를 수행하지 않도록 수정
- 대신 호출자가 준비한 `scaledBaseFrame` 위에 ROI, 객체 바운딩 박스, FPS 텍스트를 그립니다.

효과:

- 스케일링 책임이 renderer 내부에 숨어 있지 않게 되었고, 소비자 캐시 구조와 자연스럽게 연결됩니다.

### 6. 썸네일 consumer 쪽에 카드별 캐시 추가

변경 파일:

- `src/presentation/controllers/mainwindowcontroller.h`
- `src/presentation/controllers/mainwindowcontroller.cpp`

변경 내용:

- 채널 카드별 `ThumbnailCache` 추가
- 캐시 기준:
  - 마지막 프레임 포인터 identity
  - 마지막 라벨 크기
  - 마지막 `QPixmap`

동작:

- 프레임이 바뀌거나 라벨 크기가 바뀔 때만 새 `scaled()`와 `QPixmap::fromImage()` 수행
- 그 외에는 기존 pixmap 재사용

효과:

- 썸네일 라벨이 같은 크기에서 같은 프레임을 다시 받을 때 중복 스케일링을 피합니다.

### 7. 녹화 탭 라이브 프리뷰를 새 라이브 경로에 연결

변경 파일:

- `src/presentation/controllers/recordpanelcontroller.h`
- `src/presentation/controllers/recordpanelcontroller.cpp`
- `src/presentation/controllers/mainwindowcontroller.cpp`

변경 내용:

- 녹화 탭의 라이브 프리뷰는 이제 `SharedVideoFrame` 기반으로 전달
- 녹화 탭 위젯도 `VideoWidget::updateLiveFrame()` 사용
- 단, 파일/이미지 재생은 기존 `updateFrame(const QImage&)` 유지

효과:

- 라이브 프리뷰는 공유 raw frame 구조를 따라가고
- 저장된 파일 재생 경로는 기존 동작을 깨지 않도록 분리 유지합니다.

## 변경 후 흐름

1. `VideoThread`가 RTSP를 디코드하고 `SharedVideoFrame` 생성
2. `CameraManager`와 `CameraSource`가 이를 그대로 전달/보관
3. `CameraSource`는 최신 프레임과 최신 메타데이터만 유지
4. display consumer는 `SharedVideoFrame`을 받아 `VideoWidget::updateLiveFrame()` 호출
5. `VideoWidget`이 프레임 pointer + widget size 기준으로 축소 캐시 관리
6. `VideoFrameRenderer`는 캐시된 base frame 위에 오버레이만 합성
7. thumbnail consumer는 카드별 캐시를 사용해 pixmap 생성 최소화

## 기대 효과

- 라이브 display/thumbnail 경로의 전체 프레임 복사 감소
- renderer 내부 중복 스케일링 제거
- 소비자별 출력 크기에 맞춘 캐시 재사용
- 고해상도 프레임에서 CPU/메모리 대역폭 사용량 완화
- display consumer와 thumbnail consumer의 책임 분리 명확화

## 유지한 사항

- 녹화/버퍼 적재는 기존처럼 원본 `cv::Mat` 기반 유지
- 파일/이미지 재생은 `QImage` 기반 유지
- OCR crop 추출은 기존 `QImage` 기반 경로 유지

## 영향 파일

- `src/infrastructure/video/sharedvideoframe.h`
- `src/infrastructure/video/videothread.h`
- `src/infrastructure/video/videothread.cpp`
- `src/infrastructure/camera/cameramanager.h`
- `src/infrastructure/camera/camerasource.h`
- `src/infrastructure/camera/camerasource.cpp`
- `src/presentation/widgets/videowidget.h`
- `src/presentation/widgets/videowidget.cpp`
- `src/presentation/widgets/videoframerenderer.h`
- `src/presentation/widgets/videoframerenderer.cpp`
- `src/presentation/widgets/camerachannelruntime.h`
- `src/presentation/widgets/camerachannelruntime.cpp`
- `src/presentation/controllers/mainwindowcontroller.h`
- `src/presentation/controllers/mainwindowcontroller.cpp`
- `src/presentation/controllers/recordpanelcontroller.h`
- `src/presentation/controllers/recordpanelcontroller.cpp`

## 확인 포인트

아래 항목은 실제 앱에서 다시 확인이 필요합니다.

1. 라이브 메인 화면이 정상 표시되는지
2. ROI, 객체 바운딩 박스, FPS 오버레이가 기존처럼 맞는지
3. 썸네일이 정상 갱신되고 리사이즈 시 다시 맞춰지는지
4. 녹화 탭 라이브 프리뷰가 정상 표시되는지
5. 저장된 이미지/영상 재생이 기존처럼 동작하는지
6. 채널 전환, 스트림 재연결, 창 리사이즈 시 use-after-free 없이 안정적인지

## 검증 상태

- 코드 반영 완료
- 정적 점검 기준으로 라이브 `CameraSource` display/thumbnail 경로의 `QImage::copy()` 제거 확인
- `VideoFrameRenderer` 내부 `frame.scaled(...)` 제거 확인
- 다만 현재 작업 환경에서는 Windows 빌드 호출이 WSL 소켓 오류로 실패하여 실제 컴파일 검증은 아직 미완료

## 메모

이번 변경은 “디코드 수 감소”가 아니라 “디코드 이후 프레임 복사 및 소비자별 렌더 비용 감소”에 초점을 둔 리팩터링입니다.

즉, 다음 단계 최적화 후보는 여전히 별도로 남아 있습니다.

- 더 낮은 RTSP 프로파일/서브스트림 적극 사용
- `targetFps` 이전 단계에서의 디코드 비용 절감 검토
- 필요 시 하드웨어 디코드 도입 검토
