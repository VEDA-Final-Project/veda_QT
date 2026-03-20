#include "srtpdecryptor.h"
#include <QDebug>

namespace {
QString hexSlice(const QByteArray &data) {
  return QString::fromLatin1(data.toHex(' '));
}
}

SrtpDecryptor::SrtpDecryptor(QObject *parent)
    : QObject(parent), m_session(nullptr), m_initialized(false) {
  // libsrtp 전역 초기화
  static bool s_srtpInit = false;
  if (!s_srtpInit) {
    srtp_err_status_t status = srtp_init();
    if (status == srtp_err_status_ok) {
        s_srtpInit = true;
    } else {
        qWarning() << "[SRTP][Step4] libsrtp global init failed:" << status;
    }
  }
}

SrtpDecryptor::~SrtpDecryptor() {
  if (m_session) {
    srtp_dealloc(m_session);
    m_session = nullptr;
  }
}

bool SrtpDecryptor::init(const QByteArray &masterKey,
                        const QByteArray &masterSalt,
                        const QByteArray &mkiId) {
  if (m_session) {
    srtp_dealloc(m_session);
    m_session = nullptr;
  }

  // 키와 솔트 결합 (16 + 14 = 30 bytes)
  m_keyMaterial = masterKey + masterSalt;
  if (m_keyMaterial.size() != 30) {
    qWarning() << "[SRTP][Step4] Invalid key/salt length:" << m_keyMaterial.size();
    return false;
  }

  m_failureLogBudget = 8;
  m_failureSuppressedLogged = false;
  return configureSession(mkiId);
}

bool SrtpDecryptor::configureSession(const QByteArray &mkiId) {
  if (m_session) {
    srtp_dealloc(m_session);
    m_session = nullptr;
  }

  srtp_policy_t policy;
  memset(&policy, 0, sizeof(policy));

  // Hanwha 문서 기준: AES-CM + HMAC-SHA1, auth tag 80-bit(10 bytes)
  srtp_crypto_policy_set_rtp_default(&policy.rtp);
  srtp_crypto_policy_set_rtcp_default(&policy.rtcp);
  policy.rtp.auth_tag_len = 10;
  policy.rtcp.auth_tag_len = 10;
  policy.ssrc.type = ssrc_any_inbound;
  policy.key = reinterpret_cast<uint8_t *>(m_keyMaterial.data());
  policy.keys = nullptr;
  policy.num_master_keys = 0;

  m_useMki = (mkiId.size() == 4);
  m_mkiId = mkiId;
  if (m_useMki) {
    // libsrtp ignores policy.keys when policy.key is non-null. Clear the legacy
    // single-key field so the MKI-aware key list is actually used.
    policy.key = nullptr;
    memset(&m_masterKeyInfo, 0, sizeof(m_masterKeyInfo));
    m_masterKeyInfo.key = reinterpret_cast<unsigned char *>(m_keyMaterial.data());
    m_masterKeyInfo.mki_id = reinterpret_cast<unsigned char *>(m_mkiId.data());
    m_masterKeyInfo.mki_size = static_cast<unsigned int>(m_mkiId.size());
    m_masterKeyList[0] = &m_masterKeyInfo;
    policy.keys = m_masterKeyList;
    policy.num_master_keys = 1;
  }
  policy.next = nullptr;

  srtp_err_status_t status = srtp_create(&m_session, &policy);
  if (status != srtp_err_status_ok) {
    qWarning() << "[SRTP][Step4] srtp_create failed:" << status;
    return false;
  }

  m_initialized = true;
  return true;
}

QByteArray SrtpDecryptor::rewriteVendorTrailerOrder(const QByteArray &packet,
                                                    bool *rewritten) const {
  if (rewritten) {
    *rewritten = false;
  }
  if (!m_useMki || m_mkiId.size() != 4 || packet.size() < 14) {
    return packet;
  }

  const QByteArray trailer = packet.right(14);
  const QByteArray trailingMki = trailer.right(4);
  if (trailingMki != m_mkiId) {
    return packet;
  }

  QByteArray reordered = packet.left(packet.size() - 14);
  reordered += trailingMki;
  reordered += trailer.left(10);
  if (rewritten) {
    *rewritten = true;
  }
  return reordered;
}

QByteArray SrtpDecryptor::decrypt(const QByteArray &srtpPacket) {
  if (!m_initialized || !m_session) return QByteArray();

  // libsrtp는 버퍼 내에서 직접 복호화하므로 가변 패킷 데이터가 필요함
  bool rewrittenTrailer = false;
  QByteArray rtpBuffer = rewriteVendorTrailerOrder(srtpPacket, &rewrittenTrailer);
  if (rewrittenTrailer) {
    qDebug() << "[SRTP][Step4] Reordered trailer from auth-tag+MKI to MKI+auth-tag"
             << "for mkiId:" << m_mkiId.toHex();
  }
  int length = rtpBuffer.size();

  srtp_err_status_t status =
      m_useMki ? srtp_unprotect_mki(m_session, rtpBuffer.data(), &length, 1)
               : srtp_unprotect(m_session, rtpBuffer.data(), &length);
  
  if (status != srtp_err_status_ok) {
    // 실서비스에서는 패킷 드랍 처리
    if (status != srtp_err_status_replay_old && status != srtp_err_status_replay_fail) {
        if (m_failureLogBudget > 0) {
          qWarning() << "[SRTP][Step4] srtp_unprotect failed:" << status
                     << statusToString(status);
        } else if (!m_failureSuppressedLogged) {
          m_failureSuppressedLogged = true;
          qWarning() << "[SRTP][Step4] Additional SRTP decrypt failures suppressed.";
        }
        logPacketFailure(srtpPacket, status);
    }
    return QByteArray();
  }

  // 복호화된 결과물(RTP 패킷)의 길이로 조정
  rtpBuffer.resize(length);
  return rtpBuffer;
}

QString SrtpDecryptor::statusToString(srtp_err_status_t status) const {
  switch (status) {
    case srtp_err_status_ok:
      return QStringLiteral("ok");
    case srtp_err_status_fail:
      return QStringLiteral("fail");
    case srtp_err_status_bad_param:
      return QStringLiteral("bad_param");
    case srtp_err_status_alloc_fail:
      return QStringLiteral("alloc_fail");
    case srtp_err_status_dealloc_fail:
      return QStringLiteral("dealloc_fail");
    case srtp_err_status_init_fail:
      return QStringLiteral("init_fail");
    case srtp_err_status_terminus:
      return QStringLiteral("terminus");
    case srtp_err_status_auth_fail:
      return QStringLiteral("auth_fail");
    case srtp_err_status_cipher_fail:
      return QStringLiteral("cipher_fail");
    case srtp_err_status_replay_fail:
      return QStringLiteral("replay_fail");
    case srtp_err_status_replay_old:
      return QStringLiteral("replay_old");
    case srtp_err_status_algo_fail:
      return QStringLiteral("algo_fail");
    case srtp_err_status_no_such_op:
      return QStringLiteral("no_such_op");
    case srtp_err_status_no_ctx:
      return QStringLiteral("no_ctx");
    case srtp_err_status_cant_check:
      return QStringLiteral("cant_check");
    case srtp_err_status_key_expired:
      return QStringLiteral("key_expired");
    case srtp_err_status_socket_err:
      return QStringLiteral("socket_err");
    case srtp_err_status_signal_err:
      return QStringLiteral("signal_err");
    case srtp_err_status_nonce_bad:
      return QStringLiteral("nonce_bad");
    case srtp_err_status_read_fail:
      return QStringLiteral("read_fail");
    case srtp_err_status_write_fail:
      return QStringLiteral("write_fail");
    case srtp_err_status_parse_err:
      return QStringLiteral("parse_err");
    case srtp_err_status_encode_err:
      return QStringLiteral("encode_err");
    case srtp_err_status_semaphore_err:
      return QStringLiteral("semaphore_err");
    case srtp_err_status_pfkey_err:
      return QStringLiteral("pfkey_err");
    case srtp_err_status_bad_mki:
      return QStringLiteral("bad_mki");
    case srtp_err_status_pkt_idx_old:
      return QStringLiteral("pkt_idx_old");
    case srtp_err_status_pkt_idx_adv:
      return QStringLiteral("pkt_idx_adv");
    default:
      return QStringLiteral("unknown");
  }
}

void SrtpDecryptor::logPacketFailure(const QByteArray &packet,
                                     srtp_err_status_t status) const {
  if (m_failureLogBudget <= 0 || packet.size() < 12) {
    return;
  }
  --m_failureLogBudget;

  const quint8 version = (static_cast<quint8>(packet[0]) >> 6) & 0x03;
  const quint8 payloadType = static_cast<quint8>(packet[1]) & 0x7f;
  const quint16 seq =
      (static_cast<quint8>(packet[2]) << 8) | static_cast<quint8>(packet[3]);
  const quint32 ssrc =
      (static_cast<quint8>(packet[8]) << 24) |
      (static_cast<quint8>(packet[9]) << 16) |
      (static_cast<quint8>(packet[10]) << 8) |
      static_cast<quint8>(packet[11]);

  const QByteArray tail = packet.right(qMin(20, packet.size()));
  qWarning().noquote()
      << QString("[SRTP][Step4] Packet failure detail: status=%1 len=%2 "
                 "v=%3 pt=%4 seq=%5 ssrc=0x%6 tail=%7")
             .arg(statusToString(status))
             .arg(packet.size())
             .arg(version)
             .arg(payloadType)
             .arg(seq)
             .arg(QString::number(ssrc, 16))
             .arg(hexSlice(tail));
}
