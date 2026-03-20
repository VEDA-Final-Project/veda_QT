#ifndef RTSPURL_H
#define RTSPURL_H

#include <QString>

QString buildRtspUrl(const QString &ip, const QString &username,
                     const QString &password, const QString &profile);
QString maskedRtspUrl(const QString &rtspUrl);

#endif // RTSPURL_H
