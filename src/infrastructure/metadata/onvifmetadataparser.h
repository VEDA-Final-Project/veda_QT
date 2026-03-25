#ifndef ONVIFMETADATAPARSER_H
#define ONVIFMETADATAPARSER_H

#include "infrastructure/metadata/objectinfo.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <functional>

class OnvifMetadataParser : public QObject {
  Q_OBJECT
public:
  using TypeFilter = std::function<bool(const QString &)>;

  explicit OnvifMetadataParser(QObject *parent = nullptr);

  void pushData(const QByteArray &xmlData);
  void reset();
  void setTypeFilter(TypeFilter filter);

signals:
  void metadataReceived(const QList<ObjectInfo> &objects, qint64 timestampMs);
  void logMessage(const QString &msg);

private:
  void processBuffer();
  void parseFrame(const QString &frameXml);
  bool isTypeDisabled(const QString &type) const;

  QByteArray m_buffer;
  QByteArray m_currentStreamStartTag;
  QByteArray m_currentStreamClosingTag;
  TypeFilter m_typeFilter;
};

#endif // ONVIFMETADATAPARSER_H
