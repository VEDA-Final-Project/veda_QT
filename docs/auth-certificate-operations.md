# RPi Auth 서버 인증서 운영 가이드

이 문서는 `RpiAuthClient`가 접속하는 라즈베리파이 로그인(auth) 서버의 TLS 인증서를
운영하는 절차를 정리합니다.

## 목표

- 서버 개인키는 서버에만 보관합니다.
- 클라이언트 앱은 서버 인증서의 SHA-256 fingerprint만 신뢰합니다.
- 인증서 교체 시 구 fingerprint와 신 fingerprint를 일정 기간 함께 허용합니다.
- 인증서 만료/교체/유출 대응을 사람이 헷갈리지 않게 단순한 절차로 유지합니다.

## 권장 서버 경로

라즈베리파이 auth 서버에서 아래 경로를 권장합니다.

- 인증서: `/opt/veda/auth/certs/server.crt`
- 개인키: `/opt/veda/auth/certs/server.key`

권한 권장값:

- 디렉토리: `chmod 700 /opt/veda/auth/certs`
- 개인키: `chmod 600 /opt/veda/auth/certs/server.key`
- 인증서: `chmod 644 /opt/veda/auth/certs/server.crt`

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

- 하나만 넣으면 단일 인증서만 허용합니다.
- 두 개 이상 넣으면 인증서 교체(rollover) 중에도 끊김 없이 운영할 수 있습니다.
- 환경변수 `VEDA_AUTH_TLS_PINNED_SHA256`로도 덮어쓸 수 있습니다.

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

## 인증서 교체 절차

1. 서버에서 새 인증서를 생성합니다.
2. 새 인증서 fingerprint를 추출합니다.
3. 앱 설정의 `auth.pinnedSha256`에 기존 fingerprint와 새 fingerprint를 함께 넣습니다.
4. 앱을 배포합니다.
5. 서버 인증서를 새 인증서로 교체하고 auth 서버를 재시작합니다.
6. 접속이 정상인지 확인합니다.
7. 안정화가 끝나면 앱 설정에서 기존 fingerprint를 제거합니다.

## 긴급 폐기 절차

개인키 유출 의심 시:

1. 즉시 새 인증서/개인키를 생성합니다.
2. 앱 설정에서 새 fingerprint만 남기고 구 fingerprint는 제거합니다.
3. 서버 인증서를 교체하고 재시작합니다.
4. 구 `server.key`는 안전하게 삭제합니다.

## 운영 원칙

- `server.key`는 Git에 올리지 않습니다.
- 인증서와 개인키는 빌드 폴더가 아니라 고정 운영 경로에 둡니다.
- fingerprint는 문서나 설정 파일에 남겨도 되지만, 개인키는 절대 공유하지 않습니다.
- 인증서 교체 시에는 반드시 다중 fingerprint 허용 상태를 먼저 배포합니다.
