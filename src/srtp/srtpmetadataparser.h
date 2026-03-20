#ifndef SRTPMETADATAPARSER_H
#define SRTPMETADATAPARSER_H

#include <QObject>
#include <QByteArray>
#include <QList>
#include <QSet>
#include "metadata/metadatathread.h" // ObjectInfo 구조체 재사용

/**
 * @brief SRTP 메타데이터 파싱 클래스 (Step 5)
 * - 재조립된 XML 데이터를 QXmlStreamReader를 사용하여 파싱합니다.
 * - 기존 MetadataThread의 파싱 로직을 캡슐화합니다.
 */
class SrtpMetadataParser : public QObject {
  Q_OBJECT
public:
  explicit SrtpMetadataParser(QObject *parent = nullptr);

  /**
   * @brief XML 프레임 파싱
   * @param xmlData 재조립된 XML 프레임 데이터
   */
  void parse(const QByteArray &xmlData);

signals:
  void metadataReceived(const QList<ObjectInfo> &objects);
  void logMessage(const QString &msg);

private:
  void processBuffer();
  void parseFrame(const QString &frameXml);

private:
  QByteArray m_buffer;
  QSet<QString> m_disabledTypes;
};

#endif // SRTPMETADATAPARSER_H
