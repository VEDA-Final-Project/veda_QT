#include "srtpmetadataparser.h"

#include "infrastructure/metadata/onvifmetadataparser.h"

SrtpMetadataParser::SrtpMetadataParser(QObject *parent)
    : QObject(parent), m_parser(new OnvifMetadataParser(this)) {
  m_parser->setTypeFilter(
      [this](const QString &type) { return m_disabledTypes.contains(type); });
  connect(m_parser, &OnvifMetadataParser::metadataReceived, this,
          &SrtpMetadataParser::metadataReceived);
  connect(m_parser, &OnvifMetadataParser::logMessage, this,
          &SrtpMetadataParser::logMessage);
}

void SrtpMetadataParser::setDisabledTypes(const QSet<QString> &types) {
  m_disabledTypes = types;
}

void SrtpMetadataParser::parse(const QByteArray &xmlData) {
  if (m_parser) {
    m_parser->pushData(xmlData);
  }
}

void SrtpMetadataParser::reset() {
  if (m_parser) {
    m_parser->reset();
  }
}
