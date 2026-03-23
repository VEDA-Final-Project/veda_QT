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
constexpr int kTemplateLength = 102;
constexpr int kTemplateCsbIdOffset = 4;
constexpr int kTemplateTimestampOffset = 21;
constexpr int kTemplatePolicyOffset = 29;
constexpr int kTemplateKemacOffset = 58;
constexpr int kTemplateKeyDataOffset = 62;
constexpr int kTemplateKeyTypeOffset = 63;
constexpr int kTemplateKeyLenOffset = 64;
constexpr int kTemplateKeyMaterialOffset = 66;
constexpr int kTemplateKeyMaterialLength = 30;
constexpr int kTemplateMkiLenOffset = 96;
constexpr int kTemplateMkiOffset = 97;
constexpr int kTemplateMkiLength = 4;
constexpr int kTemplateMacAlgOffset = 101;

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

bool validateHanwhaTemplate(const QByteArray &packet) {
  if (packet.size() != kTemplateLength) {
    return false;
  }
  const quint16 keyLen =
      (static_cast<quint8>(packet[kTemplateKeyLenOffset]) << 8) |
      static_cast<quint8>(packet[kTemplateKeyLenOffset + 1]);
  if (keyLen != kTemplateKeyMaterialLength) {
    return false;
  }
  return static_cast<quint8>(packet[kTemplateMkiLenOffset]) == kTemplateMkiLength;
}

void loadTemplateKeyMaterial(MikeyBuilder::MikeyKeys &keys) {
  const QByteArray packet = hanwhaMikeyTemplate();
  if (!validateHanwhaTemplate(packet)) {
    return;
  }

  const QByteArray keyMaterial =
      packet.mid(kTemplateKeyMaterialOffset, kTemplateKeyMaterialLength);
  keys.masterKey = keyMaterial.left(16);
  keys.masterSalt = keyMaterial.mid(16, 14);
  keys.mkiId = packet.mid(kTemplateMkiOffset, kTemplateMkiLength);
}

QByteArray randomBytes(int size) {
  QByteArray out(size, Qt::Uninitialized);
  for (int i = 0; i < out.size(); ++i) {
    out[i] =
        static_cast<char>(QRandomGenerator::global()->generate() & 0xFF);
  }
  return out;
}

bool useOfficialSampleUnchanged() {
  const QString value = QProcessEnvironment::systemEnvironment()
                            .value("VEDA_SRTP_USE_OFFICIAL_MIKEY_SAMPLE")
                            .trimmed()
                            .toLower();
  if (value.isEmpty()) {
    return false;
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
  Q_UNUSED(preSharedSecret);

  keys.masterKey = randomBytes(16);
  keys.masterSalt = randomBytes(14);
  keys.mkiId = randomBytes(kTemplateMkiLength);

  keys.mikeyBlob =
      buildMikeyPacket(keys.masterKey, keys.masterSalt, keys.mkiId, QByteArray());
  if (keys.mikeyBlob.isEmpty()) {
    qWarning() << "[SRTP][Step3] Failed to build template-compatible MIKEY.";
    return keys;
  }

  keys.base64Data = QString::fromLatin1(keys.mikeyBlob.toBase64());

  return keys;
}

QByteArray MikeyBuilder::buildMikeyPacket(const QByteArray &key,
                                          const QByteArray &salt,
                                          const QByteArray &mkiId,
                                          const QByteArray &preSharedSecret) {
  Q_UNUSED(preSharedSecret);

  if (key.size() != 16 || salt.size() != 14 || mkiId.size() != kTemplateMkiLength) {
    qWarning() << "[SRTP][Step3] Invalid MIKEY material size:"
               << key.size() << salt.size() << mkiId.size();
    return QByteArray();
  }

  QByteArray packet = hanwhaMikeyTemplate();
  if (!validateHanwhaTemplate(packet)) {
    qWarning() << "[SRTP][Step3] Hanwha MIKEY template validation failed.";
    return QByteArray();
  }

  const quint32 csbId = QRandomGenerator::global()->generate();
  packet[kTemplateCsbIdOffset + 0] = static_cast<char>((csbId >> 24) & 0xFF);
  packet[kTemplateCsbIdOffset + 1] = static_cast<char>((csbId >> 16) & 0xFF);
  packet[kTemplateCsbIdOffset + 2] = static_cast<char>((csbId >> 8) & 0xFF);
  packet[kTemplateCsbIdOffset + 3] = static_cast<char>(csbId & 0xFF);

  const QByteArray timestamp = ntpTimestamp64();
  packet.replace(kTemplateTimestampOffset, timestamp.size(), timestamp);

  const QByteArray keyMaterial = key + salt;
  packet.replace(kTemplateKeyMaterialOffset, keyMaterial.size(), keyMaterial);
  packet.replace(kTemplateMkiOffset, mkiId.size(), mkiId);
  return packet;
}
