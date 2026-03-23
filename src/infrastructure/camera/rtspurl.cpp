#include "infrastructure/camera/rtspurl.h"

#include <QByteArray>
#include <QUrl>

namespace {
QString normalizedProfilePath(QString profile) {
  if (profile.startsWith('/')) {
    profile.remove(0, 1);
  }
  return profile;
}

QString decodePercentEncodedCredential(const QString &value) {
  if (!value.contains('%')) {
    return value;
  }
  return QString::fromUtf8(QByteArray::fromPercentEncoding(value.toUtf8()));
}
} // namespace

QString buildRtspUrl(const QString &ip, const QString &username,
                     const QString &password, const QString &profile) {
  QUrl url;
  url.setScheme(QStringLiteral("rtsp"));
  url.setHost(ip.trimmed());
  url.setUserName(username.trimmed());
  // settings.json에 URL 인코딩(%21 등)된 값이 들어와도 실제 비밀번호로 보정
  url.setPassword(decodePercentEncodedCredential(password));
  url.setPath(QStringLiteral("/") + normalizedProfilePath(profile.trimmed()));
  return url.toString(QUrl::FullyEncoded);
}

QString maskedRtspUrl(const QString &rtspUrl) {
  QUrl url(rtspUrl);
  if (!url.isValid()) {
    return QStringLiteral("rtsp://***:***@***");
  }
  if (!url.userName().isEmpty()) {
    url.setUserName(QStringLiteral("***"));
  }
  if (!url.password().isEmpty()) {
    url.setPassword(QStringLiteral("***"));
  }
  return url.toString(QUrl::FullyEncoded);
}
