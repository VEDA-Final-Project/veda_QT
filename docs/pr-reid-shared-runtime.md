# PR: ReID 모델 1회 로드 + 채널별 추론 세션 분리

## 배경

기존에는 4채널 카메라가 각각 ReID 모델을 따로 로드하고 초기화했습니다.

- 같은 ReID 모델 파일을 채널 수만큼 반복 로드함
- 로그인 직후 CCTV 진입 시 초기화 비용이 크게 누적됨
- 모델 로드/compile 비용이 중복되어 메모리와 시작 시간이 비효율적이었음
- 반면 채널별 ReID 정체성 분리(`C1-*`, `C2-*`, gallery, tracker 상태)는 유지가 필요했음

이번 변경은 채널별 ReID 상태는 그대로 유지하면서, 무거운 모델 로드와 OpenVINO compile 비용만 1회로 줄이는 데 목적이 있습니다.

## 주요 변경 사항

### 1. ReID 구조를 공용 runtime + 채널별 session으로 분리

- `SharedReidRuntime`를 추가해 모델 로드와 compile을 앱 전체에서 1회만 수행
- `ReidSession`을 추가해 각 카메라는 자기 `InferRequest` 기반 세션만 사용
- 공용 모델은 공유하지만, 추론 실행 컨텍스트는 채널별로 분리

## 2. CameraSource의 모델 직접 로드 제거

- `CameraSource`가 더 이상 채널별 `loadModel()`를 호출하지 않음
- 대신 외부에서 주입받은 `ReidSession`으로 feature 추출만 수행
- ReID 비활성/미준비 상태일 때는 기존처럼 빈 feature 처리로 안전하게 동작

### 3. CameraSessionController가 shared runtime을 관리

- `CameraSessionController` 시작 시 shared runtime을 준비
- 준비된 runtime에서 채널별 `createSession()`으로 ReID 세션 생성
- 각 `CameraSource`에 동일한 모델 기반 세션을 전달

### 4. 앱 시작 시 preload 연결

- 로그인 전 단계에서 shared ReID runtime을 미리 준비할 수 있도록 경로 정리
- 로그인 후 CCTV 시작 시에는 이미 로드된 runtime을 재사용
- 로그인 직후 사용자 체감 지연을 줄이도록 구조 개선

## 영향 범위

- ReID 모델 초기화 방식
- 카메라 세션 시작 시점의 초기화 비용
- 채널별 추론 세션 생성 방식

채널별 ReID ID 체계, gallery, tracker, 주차 판정 기준, DB 스키마는 변경하지 않습니다.

## 관련 파일

- `src/infrastructure/vision/reidextractor.*`
- `src/presentation/controllers/camerasessioncontroller.*`
- `src/infrastructure/camera/camerasource.*`
- `src/main.cpp`

## 확인 방법

1. 앱 시작 후 ReID 모델 로드 로그가 채널 수와 관계없이 1회만 출력되는지 확인
2. 4채널 모두 활성화했을 때 각 채널이 계속 자기 prefix(`C1-*`, `C2-*`, `C3-*`, `C4-*`)로 ReID를 발급하는지 확인
3. 여러 채널에서 동시에 차량이 보일 때 gallery/state가 채널 간 섞이지 않는지 확인
4. 채널 재시작 시 모델 전체를 다시 로드하지 않고 세션만 재구성되는지 확인
5. 로그인 후 CCTV 진입 체감 속도가 기존보다 개선되는지 확인
6. 모델 로드 실패 시 앱이 죽지 않고 ReID만 비활성화된 상태로 동작하는지 확인

## 리뷰 포인트

- 공용 모델 로드와 채널별 추론 세션의 책임 분리가 명확한지
- `CameraSource`가 더 이상 모델 로드 책임을 갖지 않는지
- `CameraSessionController`가 shared runtime lifecycle을 안정적으로 관리하는지
- 채널별 ReID 상태가 여전히 독립적으로 유지되는지
- preload 경로와 로그인 이후 runtime 재사용 경로가 자연스럽게 이어지는지

## 비고

- 목적은 처리량 최적화보다 초기화 비용 절감과 구조 단순화에 있습니다.
- 현재 환경에서는 전체 Windows 툴체인 빌드를 끝까지 확인하지 못해 컴파일 검증은 별도로 필요합니다.
