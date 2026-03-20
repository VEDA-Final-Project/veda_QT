#ifndef MIKEYBUILDER_H
#define MIKEYBUILDER_H

#include <QByteArray>
#include <QString>

/**
 * @brief MIKEY (RFC 3830) 페이로드 생성 클래스 (Step 3)
 * - SRTP 세션 설정을 위한 마스터 키와 솔트를 생성하고 MIKEY 바이너리 블록을 조립합니다.
 */
class MikeyBuilder {
public:
  struct MikeyKeys {
    QByteArray masterKey;   // 16 bytes
    QByteArray masterSalt;  // 14 bytes
    QByteArray mkiId;       // 4 bytes (Hanwha docs: MKI length = 32 bits)
    QByteArray mikeyBlob;   // 전체 MIKEY 페이로드
    QString base64Data;     // RTSP 헤더용 Base64 문자열
  };

  /**
   * @brief 새로운 MIKEY 키 세트를 생성합니다.
   */
  static MikeyKeys generate(const QByteArray &preSharedSecret);

private:
  static QByteArray buildMikeyPacket(const QByteArray &key,
                                     const QByteArray &salt,
                                     const QByteArray &mkiId,
                                     const QByteArray &preSharedSecret);
};

#endif // MIKEYBUILDER_H
