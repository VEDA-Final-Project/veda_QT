#include "infrastructure/metadata/onvifmetadataparser.h"

#include <QDateTime>
#include <QStringList>
#include <QXmlStreamReader>
#include <utility>

namespace {
constexpr int kMaxMetadataBufferBytes = 4 * 1024 * 1024;
constexpr int kTrimThresholdBytes = 1024 * 1024;

int findTagNameEnd(const QByteArray &buffer, int nameStart) {
  int index = nameStart;
  while (index < buffer.size()) {
    const char ch = buffer[index];
    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '/' ||
        ch == '>') {
      break;
    }
    ++index;
  }
  return index;
}

QByteArray localNameFromQualifiedName(const QByteArray &qualifiedName) {
  const int colonIndex = qualifiedName.indexOf(':');
  return colonIndex >= 0 ? qualifiedName.mid(colonIndex + 1) : qualifiedName;
}

int findOpenElement(const QByteArray &buffer, const QByteArray &localName,
                    int from = 0) {
  int index = from;
  while ((index = buffer.indexOf('<', index)) != -1) {
    if (index + 1 >= buffer.size()) {
      return -1;
    }

    const char marker = buffer[index + 1];
    if (marker == '/' || marker == '!' || marker == '?') {
      ++index;
      continue;
    }

    const int nameStart = index + 1;
    const int nameEnd = findTagNameEnd(buffer, nameStart);
    if (nameEnd <= nameStart) {
      ++index;
      continue;
    }

    if (localNameFromQualifiedName(buffer.mid(nameStart, nameEnd - nameStart)) ==
        localName) {
      return index;
    }
    ++index;
  }

  return -1;
}

int findCloseElement(const QByteArray &buffer, const QByteArray &localName,
                     int from = 0) {
  int index = from;
  while ((index = buffer.indexOf("</", index)) != -1) {
    const int nameStart = index + 2;
    if (nameStart >= buffer.size()) {
      return -1;
    }

    const int nameEnd = findTagNameEnd(buffer, nameStart);
    if (nameEnd <= nameStart) {
      index += 2;
      continue;
    }

    if (localNameFromQualifiedName(buffer.mid(nameStart, nameEnd - nameStart)) ==
        localName) {
      return index;
    }
    index += 2;
  }

  return -1;
}

int findTagEnd(const QByteArray &buffer, int tagStart) {
  bool inSingleQuote = false;
  bool inDoubleQuote = false;

  for (int index = tagStart; index < buffer.size(); ++index) {
    const char ch = buffer[index];
    if (ch == '"' && !inSingleQuote) {
      inDoubleQuote = !inDoubleQuote;
      continue;
    }
    if (ch == '\'' && !inDoubleQuote) {
      inSingleQuote = !inSingleQuote;
      continue;
    }
    if (ch == '>' && !inSingleQuote && !inDoubleQuote) {
      return index;
    }
  }

  return -1;
}

QByteArray qualifiedNameAt(const QByteArray &buffer, int tagStart,
                           bool closingTag) {
  const int nameStart = tagStart + (closingTag ? 2 : 1);
  const int nameEnd = findTagNameEnd(buffer, nameStart);
  if (nameEnd <= nameStart) {
    return QByteArray();
  }
  return buffer.mid(nameStart, nameEnd - nameStart);
}

QString attributeValueByName(const QXmlStreamAttributes &attributes,
                             const QStringList &names) {
  for (const QXmlStreamAttribute &attribute : attributes) {
    const QString localName = attribute.name().toString();
    for (const QString &candidate : names) {
      if (localName.compare(candidate, Qt::CaseInsensitive) == 0) {
        return attribute.value().toString().trimmed();
      }
    }
  }
  return QString();
}

float parseFloatOrFallback(const QString &value, float fallback) {
  bool ok = false;
  const float parsed = value.toFloat(&ok);
  return ok ? parsed : fallback;
}

qint64 parseUtcTimestampMs(const QString &value) {
  if (value.isEmpty()) {
    return 0;
  }

  QDateTime parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
  if (!parsed.isValid()) {
    parsed = QDateTime::fromString(value, Qt::ISODate);
  }
  return parsed.isValid() ? parsed.toMSecsSinceEpoch() : 0;
}

QString canonicalMetadataType(const QString &rawType) {
  const QString normalized = rawType.trimmed().toLower();
  if (normalized == QStringLiteral("vehicle") ||
      normalized == QStringLiteral("vehical") ||
      normalized == QStringLiteral("car") ||
      normalized == QStringLiteral("truck") ||
      normalized == QStringLiteral("bus") ||
      normalized == QStringLiteral("motorcycle")) {
    return QStringLiteral("Vehicle");
  }

  if (normalized == QStringLiteral("licenseplate")) {
    return QStringLiteral("LicensePlate");
  }

  return QString();
}
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
  m_currentStreamStartTag.clear();
  m_currentStreamClosingTag.clear();
}

void OnvifMetadataParser::setTypeFilter(TypeFilter filter) {
  m_typeFilter = std::move(filter);
}

void OnvifMetadataParser::processBuffer() {
  if (m_buffer.size() > kMaxMetadataBufferBytes) {
    emit logMessage(QStringLiteral("Metadata buffer too large, clearing."));
    reset();
    return;
  }

  while (true) {
    if (m_currentStreamStartTag.isEmpty()) {
      const int streamStartIndex = findOpenElement(m_buffer, QByteArray("MetadataStream"));
      if (streamStartIndex == -1) {
        if (m_buffer.size() > kTrimThresholdBytes) {
          m_buffer.clear();
        }
        return;
      }

      if (streamStartIndex > 0) {
        m_buffer.remove(0, streamStartIndex);
      }

      const int streamTagEnd = findTagEnd(m_buffer, 0);
      if (streamTagEnd == -1) {
        return;
      }

      const QByteArray qualifiedName = qualifiedNameAt(m_buffer, 0, false);
      if (qualifiedName.isEmpty()) {
        emit logMessage(QStringLiteral("Malformed MetadataStream tag, resetting parser."));
        reset();
        return;
      }

      m_currentStreamStartTag = m_buffer.left(streamTagEnd + 1);
      m_currentStreamClosingTag = QByteArray("</") + qualifiedName + ">";
      m_buffer.remove(0, streamTagEnd + 1);
    }

    const int frameStartIndex = findOpenElement(m_buffer, QByteArray("Frame"));
    const int streamEndIndex =
        findCloseElement(m_buffer, QByteArray("MetadataStream"));

    if (streamEndIndex != -1 &&
        (frameStartIndex == -1 || streamEndIndex < frameStartIndex)) {
      const int streamCloseEnd = findTagEnd(m_buffer, streamEndIndex);
      if (streamCloseEnd == -1) {
        return;
      }

      m_buffer.remove(0, streamCloseEnd + 1);
      m_currentStreamStartTag.clear();
      m_currentStreamClosingTag.clear();
      continue;
    }

    if (frameStartIndex == -1) {
      return;
    }

    const int frameEndIndex = findCloseElement(m_buffer, QByteArray("Frame"),
                                               frameStartIndex);
    if (frameEndIndex == -1) {
      return;
    }

    const int frameCloseEnd = findTagEnd(m_buffer, frameEndIndex);
    if (frameCloseEnd == -1) {
      return;
    }

    const QByteArray wrappedFrameXml =
        m_currentStreamStartTag +
        m_buffer.mid(frameStartIndex, frameCloseEnd - frameStartIndex + 1) +
        m_currentStreamClosingTag;
    parseFrame(QString::fromUtf8(wrappedFrameXml));
    m_buffer.remove(0, frameCloseEnd + 1);
  }
}

void OnvifMetadataParser::parseFrame(const QString &frameXml) {
  QList<ObjectInfo> objects;
  qint64 timestampMs = QDateTime::currentMSecsSinceEpoch();
  QXmlStreamReader xml(frameXml);

  ObjectInfo currentObject;
  bool insideObject = false;
  bool hasBoundingBox = false;
  bool typeDisabled = false;

  const auto finalizeObject = [&]() {
    if (!insideObject) {
      return;
    }

    if (hasBoundingBox && !typeDisabled) {
      if (!currentObject.plate.isEmpty()) {
        emit logMessage(QStringLiteral("Plate Detected! ID: %1, Num: %2")
                            .arg(currentObject.id)
                            .arg(currentObject.plate));
      }
      objects.append(currentObject);
    }

    insideObject = false;
    hasBoundingBox = false;
    typeDisabled = false;
    currentObject = ObjectInfo{};
  };

  while (!xml.atEnd()) {
    xml.readNext();
    if (!xml.isStartElement()) {
      if (xml.isEndElement() && xml.name() == QStringLiteral("Object")) {
        finalizeObject();
      }
      continue;
    }

    const QString elementName = xml.name().toString();
    const QXmlStreamAttributes attributes = xml.attributes();

    if (elementName == QStringLiteral("Frame")) {
      const qint64 parsedUtcMs =
          parseUtcTimestampMs(attributeValueByName(attributes, {QStringLiteral("UtcTime")}));
      if (parsedUtcMs > 0) {
        timestampMs = parsedUtcMs;
      }
      continue;
    }

    if (elementName == QStringLiteral("Object")) {
      finalizeObject();
      insideObject = true;
      currentObject = ObjectInfo{};
      currentObject.type.clear();
      currentObject.extraInfo.clear();
      currentObject.plate.clear();
      currentObject.id =
          attributeValueByName(attributes, {QStringLiteral("ObjectId")}).toInt();
    }

    if (!insideObject) {
      continue;
    }

    currentObject.score = parseFloatOrFallback(
        attributeValueByName(attributes, {QStringLiteral("Likelihood"),
                                          QStringLiteral("Confidence"),
                                          QStringLiteral("Score")}),
        currentObject.score);

    if (currentObject.plate.isEmpty()) {
      const QString plateAttribute =
          attributeValueByName(attributes, {QStringLiteral("Plate")});
      if (!plateAttribute.isEmpty()) {
        currentObject.plate = plateAttribute;
        currentObject.extraInfo = plateAttribute;
      }
    }

    if (elementName == QStringLiteral("BoundingBox")) {
      const float left = parseFloatOrFallback(
          attributeValueByName(attributes, {QStringLiteral("left")}), 0.0f);
      const float top = parseFloatOrFallback(
          attributeValueByName(attributes, {QStringLiteral("top")}), 0.0f);
      const float right = parseFloatOrFallback(
          attributeValueByName(attributes, {QStringLiteral("right")}), left);
      const float bottom = parseFloatOrFallback(
          attributeValueByName(attributes, {QStringLiteral("bottom")}), top);
      currentObject.rect = QRectF(left, top, right - left, bottom - top);
      hasBoundingBox = true;
      continue;
    }

    if (elementName == QStringLiteral("Type")) {
      const QString typeText =
          xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
      currentObject.type = canonicalMetadataType(typeText);
      typeDisabled =
          currentObject.type.isEmpty() || isTypeDisabled(currentObject.type);
      continue;
    }

    if (elementName == QStringLiteral("Likelihood") ||
        elementName == QStringLiteral("Confidence") ||
        elementName == QStringLiteral("Score")) {
      currentObject.score = parseFloatOrFallback(
          xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed(),
          currentObject.score);
      continue;
    }

    if (elementName == QStringLiteral("Plate") ||
        elementName == QStringLiteral("PlateNumber")) {
      const QString plateText =
          xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
      if (!plateText.isEmpty()) {
        currentObject.plate = plateText;
        currentObject.extraInfo = plateText;
      }
      continue;
    }
  }

  finalizeObject();

  if (xml.hasError()) {
    emit logMessage(QStringLiteral("Metadata XML parse failed: %1")
                        .arg(xml.errorString()));
    return;
  }

  // Keep empty frames flowing so stale metadata overlays disappear promptly.
  emit metadataReceived(objects, timestampMs);
}

bool OnvifMetadataParser::isTypeDisabled(const QString &type) const {
  return m_typeFilter && m_typeFilter(type);
}
