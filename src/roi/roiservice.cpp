#include "roi/roiservice.h"

#include <QDateTime>
#include <QJsonArray>
#include <QRegularExpression>
#include <QtGlobal>

namespace {
const QString kDefaultCameraKey = QStringLiteral("camera");

QString normalizedCameraKey(const QString &cameraKey) {
  const QString trimmed = cameraKey.trimmed();
  return trimmed.isEmpty() ? kDefaultCameraKey : trimmed;
}
} // namespace

RoiService::InitResult RoiService::init(const QString &cameraKey) {
  InitResult result;
  QString dbError;
  m_cameraKey = normalizedCameraKey(cameraKey);
  // 통합 DB 사용, 경로 인자 제거
  if (!m_repository.init(&dbError)) {
    result.error = dbError;
    return result;
  }

  m_records = m_repository.loadByCameraKey(m_cameraKey, &dbError);
  if (!dbError.isEmpty()) {
    result.error = dbError;
    return result;
  }

  recomputeSequenceFromRecords();
  result.normalizedPolygons = toNormalizedPolygons(m_records);
  result.loadedCount = m_records.size();
  result.ok = true;
  return result;
}

bool RoiService::isValidName(const QString &name, QString *errorMessage) const {
  if (name.isEmpty()) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("ROI 이름은 필수입니다.");
    }
    return false;
  }

  constexpr int kMinNameLen = 1;
  constexpr int kMaxNameLen = 20;
  if (name.size() < kMinNameLen || name.size() > kMaxNameLen) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("ROI 이름은 1~20자로 입력해주세요.");
    }
    return false;
  }

  static const QRegularExpression kAllowedNamePattern(
      QStringLiteral("^[A-Za-z0-9가-힣 _-]+$"));
  if (!kAllowedNamePattern.match(name).hasMatch()) {
    if (errorMessage) {
      *errorMessage = QStringLiteral("ROI 이름은 한글/영문/숫자/공백/밑줄(_) / "
                                     "하이픈(-)만 사용할 수 있습니다.");
    }
    return false;
  }
  return true;
}

bool RoiService::isDuplicateName(const QString &name) const {
  for (const QJsonObject &record : m_records) {
    if (record["zone_name"].toString().compare(name, Qt::CaseInsensitive) == 0) {
      return true;
    }
  }
  return false;
}

RoiService::CreateResult RoiService::createFromPolygon(const QPolygon &polygon,
                                                       const QSize &frameSize,
                                                       const QString &name) {
  CreateResult result;
  if (frameSize.isEmpty()) {
    result.error = QStringLiteral("프레임 크기가 유효하지 않습니다.");
    return result;
  }

  QString nameError;
  if (!isValidName(name, &nameError)) {
    result.error = nameError;
    return result;
  }
  if (isDuplicateName(name)) {
    result.error = QString("이름 '%1' 이(가) 이미 존재합니다.").arg(name);
    return result;
  }

  const double frameW = static_cast<double>(frameSize.width());
  const double frameH = static_cast<double>(frameSize.height());
  auto normX = [frameW](int x) {
    return qBound(0.0, static_cast<double>(x) / frameW, 1.0);
  };
  auto normY = [frameH](int y) {
    return qBound(0.0, static_cast<double>(y) / frameH, 1.0);
  };

  QJsonArray points;
  for (const QPoint &pt : polygon) {
    points.append(QJsonObject{{"x", normX(pt.x())}, {"y", normY(pt.y())}});
  }

  ++m_roiSequence;
  const QString zoneId = QString("zone-%1-%2")
                             .arg(m_cameraKey)
                             .arg(m_roiSequence, 3, 10, QLatin1Char('0'));
  const QRect bbox = polygon.boundingRect();
  const QString ts = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

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

  QString dbError;
  if (!m_repository.upsert(roiData, &dbError)) {
    result.error = dbError;
    return result;
  }

  m_records.append(roiData);
  result.record = roiData;
  result.ok = true;
  return result;
}

RoiService::DeleteResult RoiService::removeAt(int index) {
  DeleteResult result;
  if (index < 0 || index >= m_records.size()) {
    result.error = QStringLiteral("ROI를 선택해주세요.");
    return result;
  }

  const QString removedId = m_records[index]["zone_id"].toString();
  QString dbError;
  if (!m_repository.removeById(removedId, &dbError)) {
    result.error = dbError;
    return result;
  }

  result.removedName = m_records[index]["zone_name"].toString();
  m_records.removeAt(index);
  result.ok = true;
  return result;
}

const QVector<QJsonObject> &RoiService::records() const { return m_records; }

int RoiService::count() const { return m_records.size(); }
QString RoiService::cameraKey() const { return m_cameraKey; }

QList<QPolygonF>
RoiService::toNormalizedPolygons(const QVector<QJsonObject> &records) {
  QList<QPolygonF> normalizedPolygons;
  normalizedPolygons.reserve(records.size());

  for (const QJsonObject &record : records) {
    const QJsonArray points = record["zone_points"].toArray();
    if (points.size() < 3) {
      continue;
    }

    QPolygonF polygon;
    for (const QJsonValue &pointValue : points) {
      const QJsonObject pointObj = pointValue.toObject();
      polygon << QPointF(pointObj["x"].toDouble(), pointObj["y"].toDouble());
    }
    if (polygon.size() >= 3) {
      normalizedPolygons.append(polygon);
    }
  }
  return normalizedPolygons;
}

void RoiService::recomputeSequenceFromRecords() {
  m_roiSequence = 0;
  static const QRegularExpression trailingDigitsRe(QStringLiteral("(\\d+)$"));
  for (const QJsonObject &record : m_records) {
    const QString zoneId = record["zone_id"].toString();
    const QRegularExpressionMatch match = trailingDigitsRe.match(zoneId);
    if (!match.hasMatch()) {
      continue;
    }
    const int seq = match.captured(1).toInt();
    m_roiSequence = qMax(m_roiSequence, seq);
  }
}
