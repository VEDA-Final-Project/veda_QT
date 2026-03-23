#ifndef SRTPDECRYPTOR_H
#define SRTPDECRYPTOR_H

#include <QObject>
#include <QByteArray>
// libsrtp 헤더 (vcpkg 경로)
#include <srtp2/srtp.h>

/**
 * @brief libsrtp 기반 SRTP 복호화 클래스 (Step 4)
 * - 암호화된 SRTP 패킷을 수신하여 복호화된 RTP 패킷으로 변환합니다.
 */
class SrtpDecryptor : public QObject {
  Q_OBJECT
public:
  explicit SrtpDecryptor(QObject *parent = nullptr);
  ~SrtpDecryptor();

  /**
   * @brief SRTP 세션 초기화
   * @param masterKey 128-bit 마스터 키
   * @param masterSalt 112-bit 마스터 솔트
   * @param mkiId 32-bit MKI identifier (optional)
   */
  bool init(const QByteArray &masterKey,
            const QByteArray &masterSalt,
            const QByteArray &mkiId = QByteArray());

  /**
   * @brief 패킷 복호화
   * @param srtpPacket 암호화된 패킷 (수정 가능해야 하므로 사본 사용 권장)
   * @return 복호화된 RTP 패킷 (헤더 포함), 실패 시 빈 QByteArray
   */
  QByteArray decrypt(const QByteArray &srtpPacket);

private:
  bool configureSession(const QByteArray &mkiId);
  QByteArray rewriteVendorTrailerOrder(const QByteArray &packet,
                                       bool *rewritten = nullptr) const;
  QString statusToString(srtp_err_status_t status) const;
  void logPacketFailure(const QByteArray &packet, srtp_err_status_t status) const;

  srtp_t m_session;
  bool m_initialized;
  bool m_useMki = false;
  QByteArray m_keyMaterial;
  QByteArray m_mkiId;
  srtp_master_key_t m_masterKeyInfo{};
  srtp_master_key_t *m_masterKeyList[1] = {nullptr};
  mutable int m_failureLogBudget = 8;
  mutable bool m_failureSuppressedLogged = false;
};

#endif // SRTPDECRYPTOR_H
