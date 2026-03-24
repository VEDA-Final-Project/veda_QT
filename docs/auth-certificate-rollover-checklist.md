# Auth 인증서 교체 체크리스트

이 문서는 RPi auth 서버 인증서를 교체할 때 실제 작업자가 순서대로 확인할 수 있도록 만든 짧은 체크리스트입니다.

## 사전 준비

- [ ] 현재 운영 중인 auth 서버 경로를 확인했다.
- [ ] 현재 운영 중인 fingerprint를 기록했다.
- [ ] 새 인증서를 생성할 서버 호스트/IP를 확인했다.
- [ ] 앱 설정을 배포할 대상 PC 목록을 확인했다.

## 새 인증서 생성

- [ ] 새 인증서를 생성했다.
- [ ] 새 fingerprint를 추출했다.
- [ ] `server.crt`, `server.key` 권한을 확인했다.

예시:

```bash
./tools/auth/generate_auth_cert.sh /opt/veda/auth/certs 192.168.0.67
./tools/auth/print_cert_fingerprint.sh /opt/veda/auth/certs/server.crt
```

## 클라이언트 선반영

- [ ] `config/settings.json`의 `auth.pinnedSha256`에 기존 fingerprint를 유지했다.
- [ ] `config/settings.json`의 `auth.pinnedSha256`에 새 fingerprint를 추가했다.
- [ ] old/new fingerprint가 함께 들어간 앱 또는 설정을 운영 PC에 먼저 배포했다.

예시:

```json
"pinnedSha256": [
  "oldfingerprint...",
  "newfingerprint..."
]
```

## 서버 교체

- [ ] auth 서버를 중지했다.
- [ ] 새 인증서/개인키를 운영 경로에 반영했다.
- [ ] auth 서버를 재시작했다.
- [ ] 서버 기동 에러가 없는지 확인했다.

## 교체 후 확인

- [ ] 현재 PC에서 로그인 접속이 정상인지 확인했다.
- [ ] 다른 운영 PC에서도 로그인 접속이 정상인지 확인했다.
- [ ] TLS 연결 실패나 pinning 에러가 없는지 확인했다.

## 안정화 후 정리

- [ ] 일정 기간 이상 정상 운영을 확인했다.
- [ ] 앱 설정에서 구 fingerprint를 제거했다.
- [ ] 폐기된 구 인증서/개인키를 보관 또는 삭제 정책에 맞게 처리했다.

## 긴급 폐기 시 예외

다음 상황이면 일반 rollover 절차 대신 긴급 폐기를 사용합니다.

- [ ] 개인키 유출 의심
- [ ] 서버 파일 무단 복사 의심
- [ ] 인증서 위조/교체 의심

이 경우:

- [ ] 새 인증서/개인키를 즉시 생성했다.
- [ ] 클라이언트 설정에서 구 fingerprint를 제거했다.
- [ ] 새 fingerprint만 남긴 상태로 배포했다.
