#ifndef DATAPROTECTION_H
#define DATAPROTECTION_H

#include <QByteArray>
#include <QString>

class DataProtection {
public:
  static DataProtection &instance();

  QString encryptString(const QString &plainText);
  QString decryptString(const QString &storedValue);
  bool looksEncrypted(const QString &storedValue) const;
  QString lookupToken(const QString &purpose, const QString &plainText);

private:
  DataProtection() = default;
  DataProtection(const DataProtection &) = delete;
  DataProtection &operator=(const DataProtection &) = delete;

  QByteArray loadOrCreateDataKey();
  QString keyFilePath() const;
  QByteArray protectKeyForStorage(const QByteArray &key) const;
  QByteArray unprotectStoredKey(const QByteArray &protectedKey) const;
  QString encryptWithAesGcm(const QString &plainText, const QByteArray &key) const;
  QString decryptWithAesGcm(const QString &storedValue, const QByteArray &key) const;
  QString normalizedLookupText(const QString &plainText) const;

  QByteArray m_cachedKey;
};

#endif // DATAPROTECTION_H
