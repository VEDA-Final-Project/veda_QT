# 보안 운영 가이드

이 문서는 현재 프로젝트에 적용된 보안 구조를 운영 관점에서 한 번에 볼 수 있도록 정리한 문서입니다.

목표는 다음과 같습니다.

- 어떤 민감값이 어디에 저장되는지 빠르게 확인할 수 있어야 합니다.
- 인증서와 fingerprint를 어떻게 관리하는지 운영자가 헷갈리지 않아야 합니다.
- 서버 교체, 인증서 교체, 설정 반영 시 순서를 명확히 따라갈 수 있어야 합니다.

## 1. 전체 보안 구조 요약

현재 프로젝트는 아래 4개 축으로 보안을 운영합니다.

1. 카메라 스트리밍 보안
   - `RTSPS/TLS + SRTP`
   - 카메라 인증서는 fingerprint pinning으로 검증
2. 로그인(auth) 보안
   - `TLS + 서버 인증서 fingerprint pinning`
3. 로컬 DB 보안
   - `Windows DPAPI + AES-256-GCM`
   - 검색 키는 `lookup token(HMAC-SHA256)` 사용
4. 로그 보안
   - raw payload, 전문 응답, 전체 경로/URL 출력 최소화

## 2. 민감값 저장 위치

### 2.1 앱 설정 파일

파일:

- [settings.json](/mnt/d/veda_QT/config/settings.json)

현재 여기에는 다음 값들이 들어 있습니다.

- 카메라 IP
- 카메라 계정 ID/PW
- 카메라별 `pinnedSha256`
- auth 서버 주소/포트
- auth 서버 `pinnedSha256`

주의:

- 카메라 ID/PW는 아직 설정 파일 평문 저장 구조입니다.
- `pinnedSha256`는 비밀값이 아니라 신뢰 기준값입니다.

### 2.2 로컬 DB

DB 파일은 실행 환경의 `config/veda.db`를 사용합니다.

현재 보호 구조:

- `telegram_users.name` 암호화
- `telegram_users.phone` 암호화
- `telegram_users.payment_info` 암호화
- `telegram_users.plate_number`는 lookup token 저장
- `telegram_users.plate_number_enc`는 암호문 저장
- `parking_logs.plate_number`는 lookup token 저장
- `parking_logs.plate_number_enc`는 암호문 저장
- `vehicles.plate_number`는 lookup token 저장
- `vehicles.plate_number_enc`는 암호문 저장

### 2.3 DB 보호 키

파일:

- `config/db_protected_key.bin`

의미:

- AES-256-GCM에 쓰는 데이터 키(DEK)를 Windows DPAPI로 보호한 파일입니다.
- 평문 키를 `settings.json`이나 DB에 직접 저장하지 않습니다.

## 3. 카메라 RTSPS 인증서 운영

### 3.1 운영 방식

카메라는 장비 자체가 TLS 인증서를 제공하고, 앱은 카메라별 fingerprint pinning으로 검증합니다.

즉 역할은 다음과 같습니다.

- 카메라 장비: 인증서 제공자
- 앱: 카메라별 fingerprint 신뢰자

### 3.2 현재 설정 위치

파일:

- [settings.json](/mnt/d/veda_QT/config/settings.json)

예시:

```json
"camera": {
  "ip": "192.168.0.23",
  "username": "admin",
  "password": "...",
  "pinnedSha256": [
    "c14cfa33c9a713649c52dab20e5de0b75ba2a62522f3a42e6341a3b08572df13"
  ]
}
```

현재 등록된 카메라:

- `camera`
- `camera2`
- `camera3`
- `camera4`

현재 확인된 카메라 fingerprint:

- `c14cfa33c9a713649c52dab20e5de0b75ba2a62522f3a42e6341a3b08572df13`

현재 4대 카메라는 동일한 인증서를 사용하고 있으므로 같은 fingerprint가 들어 있습니다.

### 3.3 카메라 인증서 교체 원칙

1. 카메라 새 인증서 fingerprint를 먼저 추출합니다.
2. 해당 카메라의 `pinnedSha256`에 기존 지문과 새 지문을 함께 추가합니다.
3. 이 설정이 반영된 앱을 먼저 배포합니다.
4. 카메라 인증서를 교체합니다.
5. 정상 접속 확인 후 기존 fingerprint를 제거합니다.

핵심 원칙:

- 카메라 인증서를 먼저 바꾸지 않습니다.
- 앱에 old/new fingerprint를 먼저 배포합니다.

## 4. Auth 서버 인증서 운영

### 4.1 현재 운영 경로

실제 RPi auth 서버 운영 경로:

- 인증서: `/opt/veda/auth/certs/server.crt`
- 개인키: `/opt/veda/auth/certs/server.key`

권한:

- 디렉토리: `700`
- 인증서: `644`
- 개인키: `600`

### 4.2 클라이언트 설정 위치

파일:

- [settings.json](/mnt/d/veda_QT/config/settings.json)

항목:

- `auth.host`
- `auth.port`
- `auth.tlsEnabled`
- `auth.pinnedSha256`

현재 auth 서버 fingerprint:

- `2dd8db661dad205a291ce0dd545a9db2329e01a479250f19338d96f06b0e29ea`

### 4.3 Auth 인증서 교체 원칙

1. 새 인증서를 생성합니다.
2. 새 fingerprint를 추출합니다.
3. 앱의 `auth.pinnedSha256`에 기존/새 지문을 함께 넣습니다.
4. 앱 또는 설정을 먼저 배포합니다.
5. 서버 인증서를 교체하고 auth 서버를 재시작합니다.
6. 접속 확인 후 기존 fingerprint를 제거합니다.

상세 절차:

- [auth-certificate-operations.md](/mnt/d/veda_QT/docs/auth-certificate-operations.md)
- [auth-certificate-rollover-checklist.md](/mnt/d/veda_QT/docs/auth-certificate-rollover-checklist.md)

## 5. 로그 운영 원칙

운영 로그는 유지하되, raw payload/debug dump는 최소화합니다.

현재 줄인 대상:

- RPi control DB sync JSON 전문
- OCR API 응답 body 전문
- RTSP URL 전체 문자열
- 미디어 저장 전체 경로

운영자가 확인해야 하는 정보는 남기되, 다음 값은 가능한 raw 그대로 남기지 않습니다.

- 전체 JSON payload
- 외부 API 응답 전문
- 전체 파일 시스템 경로
- 전체 URL

## 6. 운영자가 기억해야 할 원칙

### 해도 되는 것

- fingerprint를 설정 파일/운영 문서에 기록
- old/new fingerprint 동시 허용으로 점진 교체
- 서버 인증서를 운영 경로에 고정 배치

### 하면 안 되는 것

- `server.key`를 Git에 올리기
- 개인키를 메신저/문서로 공유하기
- 서버 인증서 교체 전에 클라이언트 반영 없이 바로 교체하기
- 카메라 fingerprint를 추출하지 않고 RTSPS 신뢰 대상으로 등록하기

## 7. 현재 남은 운영 과제

아직 남아 있는 항목은 다음입니다.

1. 하드웨어 제어 RPi 서버 TLS 및 인증서 운영 체계 적용
2. 카메라 계정 ID/PW 평문 저장 제거
3. 카메라 인증서 교체 절차 문서 별도 분리

## 8. 관련 파일

- auth 인증서 운영: [auth-certificate-operations.md](/mnt/d/veda_QT/docs/auth-certificate-operations.md)
- auth 교체 체크리스트: [auth-certificate-rollover-checklist.md](/mnt/d/veda_QT/docs/auth-certificate-rollover-checklist.md)
- 앱 설정: [settings.json](/mnt/d/veda_QT/config/settings.json)
- DB 보호 유틸: [dataprotection.cpp](/mnt/d/veda_QT/src/infrastructure/security/dataprotection.cpp)
- auth 클라이언트: [rpiauthclient.cpp](/mnt/d/veda_QT/src/infrastructure/rpi/rpiauthclient.cpp)
- 카메라 TLS 세션: [srtpsession.cpp](/mnt/d/veda_QT/src/srtp/srtpsession.cpp)
