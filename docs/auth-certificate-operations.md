# RPi Auth 서버 인증서 운영 가이드

이 문서는 `RpiAuthClient`가 접속하는 라즈베리파이 로그인(auth) 서버의 TLS 인증서를
어떻게 생성하고, 배치하고, 교체하고, 폐기할지에 대한 운영 기준을 정리합니다.

현재 프로젝트의 기본 철학은 다음과 같습니다.

- 서버 개인키는 서버에만 보관합니다.
- 클라이언트 앱은 서버 인증서의 SHA-256 fingerprint만 신뢰합니다.
- 인증서 교체는 `old/new fingerprint 동시 허용` 방식으로 무중단에 가깝게 진행합니다.
- 인증서 원문보다 운영 절차를 단순하고 반복 가능하게 유지합니다.

## 적용 범위

현재 우선 적용 대상은 다음입니다.

- RPi auth 서버

이 문서의 방식은 이후 하드웨어 제어 RPi 서버에도 그대로 확장할 수 있습니다.

## 역할 분리

### 서버가 보관하는 것

- `server.crt`
- `server.key`

### 클라이언트가 보관하는 것

- `auth.pinnedSha256`

즉, 클라이언트는 개인키를 가지지 않고 서버 인증서 지문만 신뢰합니다.

## 권장 서버 경로

라즈베리파이 auth 서버에서는 아래 고정 경로를 권장합니다.

- 인증서: `/opt/veda/auth/certs/server.crt`
- 개인키: `/opt/veda/auth/certs/server.key`

권한 권장값:

- 디렉토리: `chmod 700 /opt/veda/auth/certs`
- 개인키: `chmod 600 /opt/veda/auth/certs/server.key`
- 인증서: `chmod 644 /opt/veda/auth/certs/server.crt`

운영 원칙:

- 인증서는 빌드 폴더가 아니라 고정 운영 경로에 둡니다.
- auth 서버 실행 계정만 개인키를 읽을 수 있도록 합니다.
- `server.key`는 Git에 올리지 않습니다.

## 클라이언트 설정

앱은 `config/settings.json`의 `auth.pinnedSha256` 배열을 사용합니다.

예시:

```json
"auth": {
  "host": "192.168.0.67",
  "port": 9000,
  "tlsEnabled": true,
  "pinnedSha256": [
    "oldfingerprint...",
    "newfingerprint..."
  ],
  "connectTimeoutMs": 3000,
  "requestTimeoutMs": 5000
}
```

특징:

- fingerprint가 1개면 단일 인증서만 허용합니다.
- fingerprint가 2개 이상이면 인증서 rollover 중에도 접속이 유지될 수 있습니다.
- 환경변수 `VEDA_AUTH_TLS_PINNED_SHA256`로 덮어쓸 수 있습니다.

## 신규 인증서 생성

라즈베리파이에서 아래 스크립트를 사용합니다.

- [generate_auth_cert.sh](/mnt/d/veda_QT/tools/auth/generate_auth_cert.sh)

예시:

```bash
./tools/auth/generate_auth_cert.sh /opt/veda/auth/certs 192.168.0.67
```

생성 결과:

- `/opt/veda/auth/certs/server.crt`
- `/opt/veda/auth/certs/server.key`

참고:

- 현재 스크립트는 self-signed 인증서를 생성합니다.
- 현재 프로젝트 규모에서는 self-signed + pinning 방식으로 충분히 운영 가능합니다.

## fingerprint 추출

아래 스크립트를 사용합니다.

- [print_cert_fingerprint.sh](/mnt/d/veda_QT/tools/auth/print_cert_fingerprint.sh)

예시:

```bash
./tools/auth/print_cert_fingerprint.sh /opt/veda/auth/certs/server.crt
```

출력 예:

```text
2dd8db661dad205a291ce0dd545a9db2329e01a479250f19338d96f06b0e29ea
```

## 표준 교체 절차

정상적인 인증서 갱신/교체는 아래 순서를 따릅니다.

1. 서버에서 새 인증서를 생성합니다.
2. 새 인증서 fingerprint를 추출합니다.
3. 앱 설정의 `auth.pinnedSha256`에 기존 fingerprint와 새 fingerprint를 함께 넣습니다.
4. 이 설정이 반영된 앱을 먼저 배포합니다.
5. 서버 인증서를 새 인증서로 교체하고 auth 서버를 재시작합니다.
6. 접속이 정상인지 확인합니다.
7. 안정화가 끝나면 앱 설정에서 기존 fingerprint를 제거합니다.

핵심은 다음 한 줄입니다.

- **서버를 먼저 바꾸지 말고, 클라이언트에 old/new fingerprint를 먼저 배포합니다.**

## 긴급 폐기 절차

개인키 유출이 의심되거나, 기존 인증서를 더 이상 신뢰할 수 없는 경우:

1. 즉시 새 인증서/개인키를 생성합니다.
2. 새 fingerprint만 포함한 클라이언트 설정을 준비합니다.
3. 서버 인증서를 교체하고 auth 서버를 재시작합니다.
4. 가능한 한 빠르게 클라이언트 설정을 배포합니다.
5. 구 `server.key`와 폐기된 인증서 파일은 안전하게 제거합니다.

주의:

- 긴급 폐기는 일반 rollover보다 우선합니다.
- 이 경우에는 old/new 동시 허용보다 `구 fingerprint 즉시 제거`가 맞습니다.

## 적용 점검 항목

인증서 반영 후 최소한 아래를 확인합니다.

- `auth 서버 프로세스가 새 인증서로 정상 기동되는지`
- `클라이언트가 TLS handshake에 성공하는지`
- `로그인 화면에서 접속 및 응답이 정상인지`
- `다른 운영 PC에서도 동일하게 접속되는지`
- `설정 파일에 old/new fingerprint가 의도대로 반영되었는지`

## 권장 보관 방식

운영 문서에는 다음 정보만 남깁니다.

- 인증서 생성 날짜
- 서버 호스트/IP
- 현재 운영 fingerprint
- 이전 fingerprint
- 적용 시작일
- 교체 완료일

운영 문서에 남기지 말아야 하는 것:

- 개인키 원문
- 개인키 파일 내용
- 개인키를 첨부한 메신저/문서 공유

## 관련 파일

- 운영 가이드: [auth-certificate-operations.md](/mnt/d/veda_QT/docs/auth-certificate-operations.md)
- 교체 체크리스트: [auth-certificate-rollover-checklist.md](/mnt/d/veda_QT/docs/auth-certificate-rollover-checklist.md)
- 인증서 생성 스크립트: [generate_auth_cert.sh](/mnt/d/veda_QT/tools/auth/generate_auth_cert.sh)
- fingerprint 추출 스크립트: [print_cert_fingerprint.sh](/mnt/d/veda_QT/tools/auth/print_cert_fingerprint.sh)
