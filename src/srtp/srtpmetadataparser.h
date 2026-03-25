#ifndef SRTPMETADATAPARSER_H
#define SRTPMETADATAPARSER_H

#include "infrastructure/metadata/objectinfo.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QSet>

class OnvifMetadataParser;

/**
 * @brief SRTP 메타데이터 파싱 어댑터
 * - SRTP 재조립 XML 청크를 공통 ONVIF 메타데이터 파서에 위임합니다.
 */
class SrtpMetadataParser : public QObject {
  Q_OBJECT
public:
  explicit SrtpMetadataParser(QObject *parent = nullptr);
  void setDisabledTypes(const QSet<QString> &types);
  void parse(const QByteArray &xmlData);
  void reset();

signals:
  void metadataReceived(const QList<ObjectInfo> &objects);
  void logMessage(const QString &msg);

private:
  OnvifMetadataParser *m_parser = nullptr;
  QSet<QString> m_disabledTypes;
};

#endif // SRTPMETADATAPARSER_H
