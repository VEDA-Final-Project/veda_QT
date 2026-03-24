#include "infrastructure/security/dataprotection.h"

#include <QCoreApplication>
#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Bcrypt.lib")
#endif

namespace {
const char kEncryptedPrefix[] = "enc:v1:";
constexpr int kAes256KeySize = 32;
constexpr int kGcmNonceSize = 12;
constexpr int kGcmTagSize = 16;

QByteArray randomBytes(int size) {
  QByteArray bytes(size, Qt::Uninitialized);
  for (int i = 0; i < size; ++i) {
    bytes[i] = static_cast<char>(QRandomGenerator::system()->generate() & 0xFF);
  }
  return bytes;
}
}

DataProtection &DataProtection::instance() {
  static DataProtection instance;
  return instance;
}

QString DataProtection::encryptString(const QString &plainText) {
  if (plainText.isEmpty() || looksEncrypted(plainText)) {
    return plainText;
  }

  const QByteArray key = loadOrCreateDataKey();
  if (key.size() != kAes256KeySize) {
    return plainText;
  }
  return encryptWithAesGcm(plainText, key);
}

QString DataProtection::decryptString(const QString &storedValue) {
  if (!looksEncrypted(storedValue)) {
    return storedValue;
  }

  const QByteArray key = loadOrCreateDataKey();
  if (key.size() != kAes256KeySize) {
    return storedValue;
  }
  return decryptWithAesGcm(storedValue, key);
}

bool DataProtection::looksEncrypted(const QString &storedValue) const {
  return storedValue.startsWith(QLatin1String(kEncryptedPrefix));
}

QString DataProtection::lookupToken(const QString &purpose,
                                    const QString &plainText) {
  const QString normalized = normalizedLookupText(plainText);
  if (normalized.isEmpty()) {
    return QString();
  }

  const QByteArray key = loadOrCreateDataKey();
  if (key.size() != kAes256KeySize) {
    return QString();
  }

  QByteArray payload = purpose.toUtf8();
  payload.append(':');
  payload.append(normalized.toUtf8());
  return QString::fromLatin1(
      QMessageAuthenticationCode::hash(payload, key, QCryptographicHash::Sha256)
          .toHex());
}

QByteArray DataProtection::loadOrCreateDataKey() {
  if (!m_cachedKey.isEmpty()) {
    return m_cachedKey;
  }

  QFile keyFile(keyFilePath());
  if (keyFile.exists() && keyFile.open(QIODevice::ReadOnly)) {
    const QByteArray protectedKey = QByteArray::fromBase64(keyFile.readAll().trimmed());
    keyFile.close();
    const QByteArray unprotected = unprotectStoredKey(protectedKey);
    if (unprotected.size() == kAes256KeySize) {
      m_cachedKey = unprotected;
      return m_cachedKey;
    }
  }

  QByteArray newKey = randomBytes(kAes256KeySize);

  const QByteArray protectedKey = protectKeyForStorage(newKey);
  if (protectedKey.isEmpty()) {
    return QByteArray();
  }

  QFileInfo info(keyFilePath());
  QDir().mkpath(info.absolutePath());
  if (keyFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    keyFile.write(protectedKey.toBase64());
    keyFile.close();
  }

  m_cachedKey = newKey;
  return m_cachedKey;
}

QString DataProtection::keyFilePath() const {
  const QString appConfigDir =
      QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("config"));
  return QDir(appConfigDir).filePath(QStringLiteral("db_protected_key.bin"));
}

QByteArray DataProtection::protectKeyForStorage(const QByteArray &key) const {
#ifdef Q_OS_WIN
  DATA_BLOB inputBlob;
  inputBlob.pbData =
      reinterpret_cast<BYTE *>(const_cast<char *>(key.constData()));
  inputBlob.cbData = static_cast<DWORD>(key.size());

  DATA_BLOB outputBlob{};
  if (!CryptProtectData(&inputBlob, L"VEDA DB Key", nullptr, nullptr, nullptr,
                        CRYPTPROTECT_UI_FORBIDDEN, &outputBlob)) {
    return QByteArray();
  }

  QByteArray result(reinterpret_cast<const char *>(outputBlob.pbData),
                    static_cast<int>(outputBlob.cbData));
  LocalFree(outputBlob.pbData);
  return result;
#else
  return key;
#endif
}

QByteArray DataProtection::unprotectStoredKey(
    const QByteArray &protectedKey) const {
#ifdef Q_OS_WIN
  DATA_BLOB inputBlob;
  inputBlob.pbData = reinterpret_cast<BYTE *>(
      const_cast<char *>(protectedKey.constData()));
  inputBlob.cbData = static_cast<DWORD>(protectedKey.size());

  DATA_BLOB outputBlob{};
  if (!CryptUnprotectData(&inputBlob, nullptr, nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &outputBlob)) {
    return QByteArray();
  }

  QByteArray result(reinterpret_cast<const char *>(outputBlob.pbData),
                    static_cast<int>(outputBlob.cbData));
  LocalFree(outputBlob.pbData);
  return result;
#else
  return protectedKey;
#endif
}

QString DataProtection::encryptWithAesGcm(const QString &plainText,
                                          const QByteArray &key) const {
#ifdef Q_OS_WIN
  BCRYPT_ALG_HANDLE algHandle = nullptr;
  BCRYPT_KEY_HANDLE keyHandle = nullptr;
  PBYTE keyObject = nullptr;
  DWORD keyObjectLength = 0;
  DWORD dataLength = 0;

  if (BCryptOpenAlgorithmProvider(&algHandle, BCRYPT_AES_ALGORITHM, nullptr, 0) !=
      0) {
    return plainText;
  }

  if (BCryptSetProperty(algHandle, BCRYPT_CHAINING_MODE,
                        reinterpret_cast<PUCHAR>(
                            const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_GCM)),
                        sizeof(BCRYPT_CHAIN_MODE_GCM), 0) != 0) {
    BCryptCloseAlgorithmProvider(algHandle, 0);
    return plainText;
  }

  if (BCryptGetProperty(algHandle, BCRYPT_OBJECT_LENGTH,
                        reinterpret_cast<PUCHAR>(&keyObjectLength),
                        sizeof(keyObjectLength), &dataLength, 0) != 0) {
    BCryptCloseAlgorithmProvider(algHandle, 0);
    return plainText;
  }

  keyObject = static_cast<PBYTE>(HeapAlloc(GetProcessHeap(), 0, keyObjectLength));
  if (!keyObject) {
    BCryptCloseAlgorithmProvider(algHandle, 0);
    return plainText;
  }

  if (BCryptGenerateSymmetricKey(
          algHandle, &keyHandle, keyObject, keyObjectLength,
          reinterpret_cast<PUCHAR>(const_cast<char *>(key.constData())),
          static_cast<ULONG>(key.size()), 0) != 0) {
    HeapFree(GetProcessHeap(), 0, keyObject);
    BCryptCloseAlgorithmProvider(algHandle, 0);
    return plainText;
  }

  QByteArray nonce = randomBytes(kGcmNonceSize);

  QByteArray plainUtf8 = plainText.toUtf8();
  QByteArray cipherText(plainUtf8.size(), Qt::Uninitialized);
  QByteArray tag(kGcmTagSize, 0);

  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
  BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
  authInfo.pbNonce =
      reinterpret_cast<PUCHAR>(const_cast<char *>(nonce.constData()));
  authInfo.cbNonce = static_cast<ULONG>(nonce.size());
  authInfo.pbTag = reinterpret_cast<PUCHAR>(tag.data());
  authInfo.cbTag = static_cast<ULONG>(tag.size());

  ULONG cipherSize = 0;
  const NTSTATUS status = BCryptEncrypt(
      keyHandle,
      reinterpret_cast<PUCHAR>(plainUtf8.data()),
      static_cast<ULONG>(plainUtf8.size()), &authInfo, nullptr, 0,
      reinterpret_cast<PUCHAR>(cipherText.data()),
      static_cast<ULONG>(cipherText.size()), &cipherSize, 0);

  QString result = plainText;
  if (status == 0) {
    QByteArray packed;
    packed.reserve(nonce.size() + cipherSize + tag.size());
    packed.append(nonce);
    packed.append(cipherText.constData(), static_cast<int>(cipherSize));
    packed.append(tag);
    result = QString::fromLatin1(kEncryptedPrefix) +
             QString::fromLatin1(packed.toBase64());
  }

  BCryptDestroyKey(keyHandle);
  HeapFree(GetProcessHeap(), 0, keyObject);
  BCryptCloseAlgorithmProvider(algHandle, 0);
  return result;
#else
  return plainText;
#endif
}

QString DataProtection::decryptWithAesGcm(const QString &storedValue,
                                          const QByteArray &key) const {
#ifdef Q_OS_WIN
  const QByteArray payload = QByteArray::fromBase64(
      storedValue.mid(int(strlen(kEncryptedPrefix))).toLatin1());
  if (payload.size() < (kGcmNonceSize + kGcmTagSize)) {
    return storedValue;
  }

  const QByteArray nonce = payload.left(kGcmNonceSize);
  const QByteArray tag = payload.right(kGcmTagSize);
  const QByteArray cipherText =
      payload.mid(kGcmNonceSize, payload.size() - kGcmNonceSize - kGcmTagSize);

  BCRYPT_ALG_HANDLE algHandle = nullptr;
  BCRYPT_KEY_HANDLE keyHandle = nullptr;
  PBYTE keyObject = nullptr;
  DWORD keyObjectLength = 0;
  DWORD dataLength = 0;

  if (BCryptOpenAlgorithmProvider(&algHandle, BCRYPT_AES_ALGORITHM, nullptr, 0) !=
      0) {
    return storedValue;
  }

  if (BCryptSetProperty(algHandle, BCRYPT_CHAINING_MODE,
                        reinterpret_cast<PUCHAR>(
                            const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_GCM)),
                        sizeof(BCRYPT_CHAIN_MODE_GCM), 0) != 0) {
    BCryptCloseAlgorithmProvider(algHandle, 0);
    return storedValue;
  }

  if (BCryptGetProperty(algHandle, BCRYPT_OBJECT_LENGTH,
                        reinterpret_cast<PUCHAR>(&keyObjectLength),
                        sizeof(keyObjectLength), &dataLength, 0) != 0) {
    BCryptCloseAlgorithmProvider(algHandle, 0);
    return storedValue;
  }

  keyObject = static_cast<PBYTE>(HeapAlloc(GetProcessHeap(), 0, keyObjectLength));
  if (!keyObject) {
    BCryptCloseAlgorithmProvider(algHandle, 0);
    return storedValue;
  }

  if (BCryptGenerateSymmetricKey(
          algHandle, &keyHandle, keyObject, keyObjectLength,
          reinterpret_cast<PUCHAR>(const_cast<char *>(key.constData())),
          static_cast<ULONG>(key.size()), 0) != 0) {
    HeapFree(GetProcessHeap(), 0, keyObject);
    BCryptCloseAlgorithmProvider(algHandle, 0);
    return storedValue;
  }

  QByteArray plainText(cipherText.size(), Qt::Uninitialized);
  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
  BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
  authInfo.pbNonce =
      reinterpret_cast<PUCHAR>(const_cast<char *>(nonce.constData()));
  authInfo.cbNonce = static_cast<ULONG>(nonce.size());
  authInfo.pbTag =
      reinterpret_cast<PUCHAR>(const_cast<char *>(tag.constData()));
  authInfo.cbTag = static_cast<ULONG>(tag.size());

  ULONG plainSize = 0;
  const NTSTATUS status = BCryptDecrypt(
      keyHandle,
      reinterpret_cast<PUCHAR>(const_cast<char *>(cipherText.constData())),
      static_cast<ULONG>(cipherText.size()), &authInfo, nullptr, 0,
      reinterpret_cast<PUCHAR>(plainText.data()),
      static_cast<ULONG>(plainText.size()), &plainSize, 0);

  QString result = storedValue;
  if (status == 0) {
    result = QString::fromUtf8(plainText.constData(), static_cast<int>(plainSize));
  }

  BCryptDestroyKey(keyHandle);
  HeapFree(GetProcessHeap(), 0, keyObject);
  BCryptCloseAlgorithmProvider(algHandle, 0);
  return result;
#else
  return storedValue;
#endif
}

QString DataProtection::normalizedLookupText(const QString &plainText) const {
  QString normalized = plainText.trimmed();
  normalized.remove(QRegularExpression(QStringLiteral("\\s+")));
  return normalized.toUpper();
}
