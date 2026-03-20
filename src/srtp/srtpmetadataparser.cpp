#include "srtpmetadataparser.h"

#include <cstring>
#include <QDebug>
#include <QRegularExpression>

SrtpMetadataParser::SrtpMetadataParser(QObject *parent) : QObject(parent) {}

void SrtpMetadataParser::parse(const QByteArray &xmlData) {
  if (xmlData.isEmpty()) {
    return;
  }

  m_buffer.append(xmlData);
  processBuffer();
}

void SrtpMetadataParser::processBuffer() {
  if (m_buffer.size() > 4 * 1024 * 1024) {
    qWarning() << "[SRTP][Meta] Metadata buffer too large. Clearing buffered data.";
    m_buffer.clear();
    return;
  }

  while (true) {
    int startTagIndex = m_buffer.indexOf("<tt:MetadataStream");
    if (startTagIndex == -1) {
      if (m_buffer.size() > 1024 * 1024) {
        m_buffer.clear();
      }
      return;
    }

    if (startTagIndex > 0) {
      m_buffer.remove(0, startTagIndex);
    }

    int endTagIndex = m_buffer.indexOf("</tt:Frame>");
    const int streamEndIndex = m_buffer.indexOf("</tt:MetadataStream>");

    if (endTagIndex == -1 && streamEndIndex == -1) {
      return;
    }

    int frameLength = -1;
    if (endTagIndex != -1) {
      frameLength = endTagIndex + int(strlen("</tt:Frame>"));
    }
    if (streamEndIndex != -1) {
      const int streamLength =
          streamEndIndex + int(strlen("</tt:MetadataStream>"));
      if (frameLength == -1 || streamLength < frameLength) {
        frameLength = streamLength;
      }
    }

    if (frameLength <= 0 || frameLength > m_buffer.size()) {
      return;
    }

    const QString frameXml = QString::fromUtf8(m_buffer.left(frameLength));
    parseFrame(frameXml);
    m_buffer.remove(0, frameLength);
  }
}

void SrtpMetadataParser::parseFrame(const QString &frameXml) {
  QList<ObjectInfo> objects;

  QRegularExpression objectRe(
      "<tt:Object\\b[^>]*ObjectId=\"(\\d+)\"[^>]*>(.*?)</tt:Object>",
      QRegularExpression::DotMatchesEverythingOption);
  QRegularExpressionMatchIterator i = objectRe.globalMatch(frameXml);

  while (i.hasNext()) {
    const QRegularExpressionMatch match = i.next();

    ObjectInfo info{};
    info.id = match.captured(1).toInt();
    const QString objectContent = match.captured(2);

    QRegularExpression bboxRe(
        "<tt:BoundingBox\\b[^>]*left=\"([\\d\\.]+)\"[^>]*top=\"([\\d\\.]+)\""
        "[^>]*right=\"([\\d\\.]+)\"[^>]*bottom=\"([\\d\\.]+)\"",
        QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch bboxMatch = bboxRe.match(objectContent);
    if (bboxMatch.hasMatch()) {
      const float left = bboxMatch.captured(1).toFloat();
      const float top = bboxMatch.captured(2).toFloat();
      const float right = bboxMatch.captured(3).toFloat();
      const float bottom = bboxMatch.captured(4).toFloat();
      info.rect = QRectF(left, top, right - left, bottom - top);
    }

    QRegularExpression typeRe("<tt:Type[^>]*>([^<]+)</tt:Type>");
    const QRegularExpressionMatch typeMatch = typeRe.match(objectContent);
    info.type = typeMatch.hasMatch() ? typeMatch.captured(1) : "Unknown";

    if (m_disabledTypes.contains(info.type)) {
      continue;
    }

    QRegularExpression scoreAttrRe(
        "(Likelihood|likelihood|Confidence|confidence|Score|score)=\"([\\d\\.]+)\"");
    QRegularExpressionMatch scoreMatch = scoreAttrRe.match(objectContent);
    if (!scoreMatch.hasMatch()) {
      QRegularExpression scoreTagRe(
          "<tt:(Likelihood|Confidence|Score)[^>]*>([\\d\\.]+)</tt:(Likelihood|Confidence|Score)>");
      scoreMatch = scoreTagRe.match(objectContent);
    }
    info.score = scoreMatch.hasMatch() ? scoreMatch.captured(2).toFloat() : 0.0f;

    info.plate.clear();
    info.extraInfo.clear();

    QRegularExpression plateRe(
        "<tt:(PlateNumber|Plate)[^>]*>([^<]+)</tt:(PlateNumber|Plate)>");
    QRegularExpressionMatch plateMatch = plateRe.match(objectContent);
    if (plateMatch.hasMatch()) {
      info.plate = plateMatch.captured(2).trimmed();
      info.extraInfo = info.plate;
    } else {
      QRegularExpression plateAttrRe("Plate=\"([^\"]+)\"");
      const QRegularExpressionMatch plateAttrMatch =
          plateAttrRe.match(objectContent);
      if (plateAttrMatch.hasMatch()) {
        info.plate = plateAttrMatch.captured(1).trimmed();
        info.extraInfo = info.plate;
      }
    }

    objects.append(info);
  }

  emit metadataReceived(objects);
}
