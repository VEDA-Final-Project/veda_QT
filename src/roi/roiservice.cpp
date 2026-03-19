#include "roi/roiservice.h"

#include <QTimeZone>
#include <QDateTime>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QtGlobal>

namespace
{
  const QString kDefaultCameraKey = QStringLiteral("camera");

  QString normalizedCameraKey(const QString &cameraKey)
  {
    const QString trimmed = cameraKey.trimmed();
    return trimmed.isEmpty() ? kDefaultCameraKey : trimmed;
  }
} // namespace

Result<RoiService::RoiInitData> RoiService::init(const QString &cameraKey)
{
  Result<RoiInitData> result;
  m_cameraKey = normalizedCameraKey(cameraKey);

  // 통합 DB 사용, 경로 인자 제거
  if (auto err = m_repository.init(); err.has_value())
  {
    result.error = err.value();
    return result;
  }

  auto loadResult = m_repository.loadByCameraKey(m_cameraKey);
  if (!loadResult.isOk())
  {
    result.error = loadResult.error;
    return result;
  }
  m_records = loadResult.data;

  result.data.normalizedPolygons = toNormalizedPolygons(m_records);
  result.data.loadedCount = m_records.size();
  return result;
}

std::optional<QString> RoiService::isValidName(const QString &name) const
{
  if (name.isEmpty())
  {
    return QStringLiteral("ROI 이름은 필수입니다.");
  }

  constexpr int kMinNameLen = 1;
  constexpr int kMaxNameLen = 20;
  if (name.size() < kMinNameLen || name.size() > kMaxNameLen)
  {
    return QStringLiteral("ROI 이름은 1~20자로 입력해주세요.");
  }

  static const QRegularExpression kAllowedNamePattern(
      QStringLiteral("^[A-Za-z0-9가-힣 _-]+$"));
  if (!kAllowedNamePattern.match(name).hasMatch())
  {
    return QStringLiteral("ROI 이름은 한글/영문/숫자/공백/밑줄(_) / "
                          "하이픈(-)만 사용할 수 있습니다.");
  }
  return std::nullopt;
}

bool RoiService::isDuplicateName(const QString &name) const
{
  for (const QJsonObject &record : m_records)
  {
    if (record["zone_name"].toString().compare(name, Qt::CaseInsensitive) ==
        0)
    {
      return true;
    }
  }
  return false;
}

Result<QJsonObject> RoiService::createFromPolygon(const QPolygon &polygon,
                                                  const QSize &frameSize,
                                                  const QString &name)
{
  Result<QJsonObject> result;
  if (frameSize.isEmpty())
  {
    result.error = QStringLiteral("프레임 크기가 유효하지 않습니다.");
    return result;
  }

  if (auto nameError = isValidName(name); nameError.has_value())
  {
    result.error = nameError.value();
    return result;
  }
  if (isDuplicateName(name))
  {
    result.error = QString("이름 '%1' 이(가) 이미 존재합니다.").arg(name);
    return result;
  }

  const double frameW = static_cast<double>(frameSize.width());
  const double frameH = static_cast<double>(frameSize.height());
  auto normX = [frameW](int x)
  {
    return qBound(0.0, static_cast<double>(x) / frameW, 1.0);
  };
  auto normY = [frameH](int y)
  {
    return qBound(0.0, static_cast<double>(y) / frameH, 1.0);
  };

  QJsonArray points;
  for (const QPoint &pt : polygon)
  {
    points.append(QJsonObject{{"x", normX(pt.x())}, {"y", normY(pt.y())}});
  }

  const int nextSequence = nextAvailableSequence();
  const QString zoneId = QString("%1-%2")
                             .arg(m_cameraKey)
                             .arg(nextSequence, 3, 10, QLatin1Char('0'));
  const QRect bbox = polygon.boundingRect();
  const QString ts = QDateTime::currentDateTimeUtc()
                         .toTimeZone(QTimeZone("Asia/Seoul"))
                         .toString(Qt::ISODate);

  QJsonObject roiData{
      {"zone_id", zoneId},
      {"zone_name", name},
      {"camera_key", m_cameraKey},
      {"zone_enable", true},
      {"zone_points", points},
      {"bbox",
       QJsonObject{
           {"x", normX(bbox.x())},
           {"y", normY(bbox.y())},
           {"w", qBound(0.0, static_cast<double>(bbox.width()) / frameW, 1.0)},
           {"h", qBound(0.0, static_cast<double>(bbox.height()) / frameH, 1.0)},
       }},
      {"created_at", ts},
  };

  if (auto dbError = m_repository.upsert(roiData); dbError.has_value())
  {
    result.error = dbError.value();
    return result;
  }

  m_records.append(roiData);
  result.data = roiData;
  return result;
}

Result<QJsonObject> RoiService::setZoneEnabled(const QString &zoneId,
                                               bool enabled)
{
  Result<QJsonObject> result;
  if (zoneId.trimmed().isEmpty())
  {
    result.error = QStringLiteral("zone_id가 비어 있습니다.");
    return result;
  }

  for (int i = 0; i < m_records.size(); ++i)
  {
    const QString currentZoneId = m_records[i]["zone_id"].toString();
    if (currentZoneId != zoneId)
    {
      continue;
    }

    QJsonObject updated = m_records[i];
    updated["zone_enable"] = enabled;
    if (auto dbError = m_repository.upsert(updated); dbError.has_value())
    {
      result.error = dbError.value();
      return result;
    }

    m_records[i] = updated;
    result.data = updated;
    return result;
  }

  result.error = QString("zone_id '%1'를 찾을 수 없습니다.").arg(zoneId);
  return result;
}

Result<QString> RoiService::removeAt(int index)
{
  Result<QString> result;
  if (index < 0 || index >= m_records.size())
  {
    result.error = QStringLiteral("ROI를 선택해주세요.");
    return result;
  }

  const QString removedId = m_records[index]["zone_id"].toString();
  if (auto dbError = m_repository.removeById(removedId); dbError.has_value())
  {
    result.error = dbError.value();
    return result;
  }

  result.data = m_records[index]["zone_name"].toString();
  m_records.removeAt(index);
  return result;
}

const QVector<QJsonObject> &RoiService::records() const { return m_records; }

int RoiService::count() const { return m_records.size(); }
QString RoiService::cameraKey() const { return m_cameraKey; }

QList<QPolygonF>
RoiService::toNormalizedPolygons(const QVector<QJsonObject> &records)
{
  QList<QPolygonF> normalizedPolygons;
  normalizedPolygons.reserve(records.size());

  for (const QJsonObject &record : records)
  {
    const QJsonArray points = record["zone_points"].toArray();
    if (points.size() < 3)
    {
      continue;
    }

    QPolygonF polygon;
    for (const QJsonValue &pointValue : points)
    {
      const QJsonObject pointObj = pointValue.toObject();
      polygon << QPointF(pointObj["x"].toDouble(), pointObj["y"].toDouble());
    }
    if (polygon.size() >= 3)
    {
      normalizedPolygons.append(polygon);
    }
  }
  return normalizedPolygons;
}

int RoiService::nextAvailableSequence() const
{
  const QRegularExpression zoneIdPattern(
      QStringLiteral("^zone-%1-(\\d+)$")
          .arg(QRegularExpression::escape(m_cameraKey)));
  QSet<int> usedSequences;

  for (const QJsonObject &record : m_records)
  {
    const QString zoneId = record["zone_id"].toString();
    const QRegularExpressionMatch match = zoneIdPattern.match(zoneId);
    if (!match.hasMatch())
    {
      continue;
    }

    bool ok = false;
    const int seq = match.captured(1).toInt(&ok);
    if (ok && seq > 0)
    {
      usedSequences.insert(seq);
    }
  }

  int next = 1;
  while (usedSequences.contains(next))
  {
    ++next;
  }
  return next;
}
