#include "mikeybuilder.h"
#include <QRandomGenerator>
#include <QDateTime>
#include <QDebug>
#include <QByteArray>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QMessageAuthenticationCode>
#include <QCryptographicHash>
#include <cstdint>

namespace {
constexpr quint32 kSrtpMapSsrc = 0xC20F551C;
constexpr quint8 kPayloadT = 5;
constexpr quint8 kPayloadSp = 10;
constexpr quint8 kPayloadKemac = 1;
constexpr quint8 kNextLast = 0;
constexpr quint8 kDataTypePreShared = 0;
constexpr quint8 kSrtpProtType = 0;
constexpr quint8 kKemacNullEncr = 0;
constexpr quint8 kKemacHmacSha1 = 1;
constexpr quint8 kKeyTypeTekWithSalt = 3;
constexpr quint8 kKvTypeSpi = 1;

QByteArray hmacSha1(const QByteArray &key, const QByteArray &message) {
  return QMessageAuthenticationCode::hash(message, key, QCryptographicHash::Sha1);
}

QByteArray mikeyPrf(const QByteArray &inkey, const QByteArray &label, int outBytes) {
  if (inkey.isEmpty() || outBytes <= 0) {
    return QByteArray();
  }

  const int blockCount = (inkey.size() + 31) / 32;
  const int m = (outBytes + 19) / 20;
  QByteArray output(outBytes, 0);

  for (int block = 0; block < blockCount; ++block) {
    const QByteArray s = inkey.mid(block * 32, 32);
    QByteArray a = label;
    QByteArray stream;
    for (int i = 0; i < m; ++i) {
      a = hmacSha1(s, a);
      stream += hmacSha1(s, a + label);
    }
    stream.truncate(outBytes);
    for (int i = 0; i < outBytes; ++i) {
      output[i] = static_cast<char>(static_cast<quint8>(output[i]) ^
                                    static_cast<quint8>(stream[i]));
    }
  }

  return output;
}

void appendU16(QByteArray &out, quint16 value) {
  out.append(static_cast<char>((value >> 8) & 0xff));
  out.append(static_cast<char>(value & 0xff));
}

void appendU32(QByteArray &out, quint32 value) {
  out.append(static_cast<char>((value >> 24) & 0xff));
  out.append(static_cast<char>((value >> 16) & 0xff));
  out.append(static_cast<char>((value >> 8) & 0xff));
  out.append(static_cast<char>(value & 0xff));
}

QByteArray ntpTimestamp64() {
  QByteArray out;
  out.reserve(8);
  const quint64 unixTimeMs = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
  const quint32 seconds = static_cast<quint32>(unixTimeMs / 1000) + 2208988800U;
  const quint32 fraction =
      static_cast<quint32>((unixTimeMs % 1000) * 0x100000000ULL / 1000);
  appendU32(out, seconds);
  appendU32(out, fraction);
  return out;
}

QByteArray sampleSrtpPolicyParams() {
  return QByteArray::fromHex(
      "0001010101100201010301140081010801010381010b010a");
}

QByteArray buildSrtpPolicyPayload() {
  const QByteArray params = sampleSrtpPolicyParams();
  QByteArray payload;
  payload.reserve(5 + params.size());
  payload.append(static_cast<char>(kPayloadKemac));
  payload.append('\x00'); // policy number 0
  payload.append(static_cast<char>(kSrtpProtType));
  appendU16(payload, static_cast<quint16>(params.size()));
  payload += params;
  return payload;
}

QByteArray buildKeyDataSubPayload(const QByteArray &key,
                                  const QByteArray &salt,
                                  const QByteArray &mkiId) {
  QByteArray payload;
  payload.reserve(4 + key.size() + 2 + salt.size() + 1 + mkiId.size());
  payload.append(static_cast<char>(kNextLast));
  payload.append(static_cast<char>((kKeyTypeTekWithSalt << 4) | kKvTypeSpi));
  appendU16(payload, static_cast<quint16>(key.size()));
  payload += key;
  appendU16(payload, static_cast<quint16>(salt.size()));
  payload += salt;
  payload.append(static_cast<char>(mkiId.size()));
  payload += mkiId;
  return payload;
}

QByteArray deriveMikeyAuthKey(const QByteArray &preSharedSecret,
                              const QByteArray &csbId,
                              const QByteArray &surrogateRand) {
  const QByteArray label = QByteArray::fromHex("2d22ac75ff") + csbId + surrogateRand;
  return mikeyPrf(preSharedSecret, label, 20);
}

QByteArray hanwhaMikeyTemplate() {
  return QByteArray::fromBase64(
      "AQAFAP1td9ABAADCD1UcAAAAAAoAAdOOGc75XD0BAAAAGAABAQEBEAIBAQMBFAcBAQgBAQoBAQsBCgAAACcAIQAe30C59UrClE0e27UP5h/Wty9UL8+dfzg+2ttmmo3kBAAAAC8A");
}

void loadTemplateKeyMaterial(MikeyBuilder::MikeyKeys &keys) {
  const QByteArray packet = hanwhaMikeyTemplate();
  constexpr int kKemacOffset = 58;
  if (packet.size() < kKemacOffset + 4) {
    return;
  }

  const quint16 encrLen =
      (static_cast<quint8>(packet[kKemacOffset + 2]) << 8) |
      static_cast<quint8>(packet[kKemacOffset + 3]);
  const QByteArray encrData = packet.mid(kKemacOffset + 4, encrLen);
  if (encrData.size() < 39) {
    return;
  }

  const quint16 keyDataLen =
      (static_cast<quint8>(encrData[2]) << 8) |
      static_cast<quint8>(encrData[3]);
  if (keyDataLen != 30 || encrData.size() < 4 + keyDataLen + 1) {
    return;
  }

  const QByteArray keyMaterial = encrData.mid(4, keyDataLen);
  keys.masterKey = keyMaterial.left(16);
  keys.masterSalt = keyMaterial.mid(16, 14);

  const quint8 spiLen = static_cast<quint8>(encrData[4 + keyDataLen]);
  if (spiLen > 0 && encrData.size() >= 5 + keyDataLen + spiLen) {
    keys.mkiId = encrData.mid(5 + keyDataLen, spiLen);
  }
}

bool useOfficialSampleUnchanged() {
  const QString value = QProcessEnvironment::systemEnvironment()
                            .value("VEDA_SRTP_USE_OFFICIAL_MIKEY_SAMPLE")
                            .trimmed()
                            .toLower();
  if (value.isEmpty()) {
    return true;
  }
  return value == QStringLiteral("1") || value == QStringLiteral("true") ||
         value == QStringLiteral("yes");
}

} // namespace

MikeyBuilder::MikeyKeys MikeyBuilder::generate(const QByteArray &preSharedSecret) {
  MikeyKeys keys;

  if (useOfficialSampleUnchanged()) {
    keys.mikeyBlob = hanwhaMikeyTemplate();
    loadTemplateKeyMaterial(keys);
    keys.base64Data = QString::fromLatin1(keys.mikeyBlob.toBase64());
    return keys;
  }
  
  // 1. 128-bit Master Key (16 bytes) 생성
  keys.masterKey.resize(16);
  for (int i = 0; i < keys.masterKey.size(); ++i) {
    keys.masterKey[i] = static_cast<char>(QRandomGenerator::global()->generate() & 0xFF);
  }

  // 2. 112-bit Master Salt (14 bytes) 생성
  keys.masterSalt.resize(14);
  for (int i = 0; i < keys.masterSalt.size(); ++i) {
    keys.masterSalt[i] = static_cast<char>(QRandomGenerator::global()->generate() & 0xFF);
  }

  keys.mkiId.resize(4);
  for (int i = 0; i < keys.mkiId.size(); ++i) {
    keys.mkiId[i] = static_cast<char>(QRandomGenerator::global()->generate() & 0xFF);
  }

  // 3. MIKEY 바이너리 패킷 조립 (RFC 3830 - PSK 기반 단순 프로파일 약식 구현)
  // Hanwha sample does not advertise a RAND payload, so we keep the same
  // T->SP->KEMAC layout and use the NTP timestamp twice as a RAND surrogate
  // for PSK auth-key derivation.
  keys.mikeyBlob = buildMikeyPacket(keys.masterKey, keys.masterSalt, keys.mkiId,
                                    preSharedSecret);
  
  // 4. Base64 인코딩
  keys.base64Data = QString::fromLatin1(keys.mikeyBlob.toBase64());

  return keys;
}

QByteArray MikeyBuilder::buildMikeyPacket(const QByteArray &key,
                                          const QByteArray &salt,
                                          const QByteArray &mkiId,
                                          const QByteArray &preSharedSecret) {
  if (key.size() != 16 || salt.size() != 14 || mkiId.size() != 4) {
    qWarning() << "[SRTP][Step3] Invalid MIKEY material size:"
               << key.size() << salt.size() << mkiId.size();
    return QByteArray();
  }
  if (preSharedSecret.isEmpty()) {
    qWarning() << "[SRTP][Step3] Pre-shared secret is empty. Cannot build PSK MIKEY.";
    return QByteArray();
  }

  const uint32_t csbId = QRandomGenerator::global()->generate();
  QByteArray packet;
  packet.reserve(128);

  // Common header with one SRTP map entry.
  packet.append('\x01'); // version
  packet.append(static_cast<char>(kDataTypePreShared));
  packet.append(static_cast<char>(kPayloadT));
  packet.append('\x00'); // v/prf
  appendU32(packet, csbId);
  packet.append('\x01'); // #CS
  packet.append('\x00'); // CS ID map type = SRTP-ID
  packet.append('\x00'); // policy no
  appendU32(packet, kSrtpMapSsrc);
  appendU32(packet, 0);  // ROC

  const QByteArray timestamp = ntpTimestamp64();
  QByteArray timestampPayload;
  timestampPayload.append(static_cast<char>(kPayloadSp));
  timestampPayload.append('\x00'); // NTP-UTC
  timestampPayload += timestamp;
  packet += timestampPayload;

  packet += buildSrtpPolicyPayload();

  const QByteArray keyData = buildKeyDataSubPayload(key, salt, mkiId);
  QByteArray kemac;
  kemac.append(static_cast<char>(kNextLast));
  kemac.append(static_cast<char>(kKemacNullEncr));
  appendU16(kemac, static_cast<quint16>(keyData.size()));
  kemac += keyData;
  kemac.append(static_cast<char>(kKemacHmacSha1));

  const QByteArray csbBytes = packet.mid(4, 4);
  const QByteArray surrogateRand = timestamp + timestamp;
  const QByteArray authKey =
      deriveMikeyAuthKey(preSharedSecret, csbBytes, surrogateRand);
  if (authKey.size() != 20) {
    qWarning() << "[SRTP][Step3] Failed to derive MIKEY auth key.";
    return QByteArray();
  }

  QByteArray macInput = packet + kemac;
  macInput += QByteArray(20, 0);
  const QByteArray mac = hmacSha1(authKey, macInput);
  kemac += mac.left(20);
  packet += kemac;
  return packet;
}
