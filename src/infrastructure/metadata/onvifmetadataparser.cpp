#include "infrastructure/metadata/onvifmetadataparser.h"

#include <cstring>
#include <QRegularExpression>
#include <utility>

namespace {
constexpr int kMaxMetadataBufferBytes = 4 * 1024 * 1024;
constexpr int kTrimThresholdBytes = 1024 * 1024;
}

OnvifMetadataParser::OnvifMetadataParser(QObject *parent) : QObject(parent) {}

void OnvifMetadataParser::pushData(const QByteArray &xmlData) {
  if (xmlData.isEmpty()) {
    return;
  }

  m_buffer.append(xmlData);
  processBuffer();
}

void OnvifMetadataParser::reset() {
  m_buffer.clear();
}

void OnvifMetadataParser::setTypeFilter(TypeFilter filter) {
  m_typeFilter = std::move(filter);
}

void OnvifMetadataParser::processBuffer() {
  if (m_buffer.size() > kMaxMetadataBufferBytes) {
    emit logMessage(QStringLiteral("Metadata buffer too large, clearing."));
    m_buffer.clear();
    return;
  }

  while (true) {
    const int startTagIndex = m_buffer.indexOf("<tt:MetadataStream");
    if (startTagIndex == -1) {
      if (m_buffer.size() > kTrimThresholdBytes) {
        m_buffer.clear();
      }
      return;
    }

    if (startTagIndex > 0) {
      m_buffer.remove(0, startTagIndex);
    }

    const int frameEndIndex = m_buffer.indexOf("</tt:Frame>");
    const int streamEndIndex = m_buffer.indexOf("</tt:MetadataStream>");

    if (frameEndIndex == -1 && streamEndIndex == -1) {
      return;
    }

    int frameLength = -1;
    if (frameEndIndex != -1) {
      frameLength = frameEndIndex + int(strlen("</tt:Frame>"));
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

void OnvifMetadataParser::parseFrame(const QString &frameXml) {
  QList<ObjectInfo> objects;

  QRegularExpression objectRe(
      "<tt:Object\\b[^>]*ObjectId=\"(\\d+)\"[^>]*>(.*?)</tt:Object>",
      QRegularExpression::DotMatchesEverythingOption);
  QRegularExpressionMatchIterator it = objectRe.globalMatch(frameXml);

  while (it.hasNext()) {
    const QRegularExpressionMatch match = it.next();

    ObjectInfo info{};
    info.id = match.captured(1).toInt();
    const QString objectContent = match.captured(2);

    QRegularExpression bboxRe(
        "<tt:BoundingBox\\b[^>]*left=\"([\\d\\.]+)\"[^>]*top=\"([\\d\\.]+)\""
        "[^>]*right=\"([\\d\\.]+)\"[^>]*bottom=\"([\\d\\.]+)\"",
        QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch bboxMatch = bboxRe.match(objectContent);
    if (!bboxMatch.hasMatch()) {
      continue;
    }

    const float left = bboxMatch.captured(1).toFloat();
    const float top = bboxMatch.captured(2).toFloat();
    const float right = bboxMatch.captured(3).toFloat();
    const float bottom = bboxMatch.captured(4).toFloat();
    info.rect = QRectF(left, top, right - left, bottom - top);

    QRegularExpression typeRe("<tt:Type[^>]*>([^<]+)</tt:Type>");
    const QRegularExpressionMatch typeMatch = typeRe.match(objectContent);
    info.type = typeMatch.hasMatch() ? typeMatch.captured(1)
                                     : QStringLiteral("Unknown");

    if (isTypeDisabled(info.type)) {
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

    if (!info.plate.isEmpty()) {
      emit logMessage(QStringLiteral("Plate Detected! ID: %1, Num: %2")
                          .arg(info.id)
                          .arg(info.plate));
    }

    objects.append(info);
  }

  // Keep empty frames flowing so stale metadata overlays disappear promptly.
  emit metadataReceived(objects);
}

bool OnvifMetadataParser::isTypeDisabled(const QString &type) const {
  return m_typeFilter && m_typeFilter(type);
}
