#include "roi/roiservice.h"

#include <QDateTime>
#include <QJsonArray>
#include <QRegularExpression>
#include <QtGlobal>

RoiService::InitResult RoiService::init(const QString &dbPath)
{
  InitResult result;
  QString dbError;
  if (!m_repository.init(dbPath, &dbError))
  {
    result.error = dbError;
    return result;
  }

  m_records = m_repository.loadAll(&dbError);
  if (!dbError.isEmpty())
  {
    result.error = dbError;
    return result;
  }

  recomputeSequenceFromRecords();
  result.normalizedPolygons = toNormalizedPolygons(m_records);
  result.loadedCount = m_records.size();
  result.ok = true;
  return result;
}

bool RoiService::isValidName(const QString &name, QString *errorMessage) const
{
  if (name.isEmpty())
  {
    if (errorMessage)
    {
      *errorMessage = QStringLiteral("ROI 이름은 필수입니다.");
    }
    return false;
  }

  constexpr int kMinNameLen = 1;
  constexpr int kMaxNameLen = 20;
  if (name.size() < kMinNameLen || name.size() > kMaxNameLen)
  {
    if (errorMessage)
    {
      *errorMessage = QStringLiteral("ROI 이름은 1~20자로 입력해주세요.");
    }
    return false;
  }

  static const QRegularExpression kAllowedNamePattern(
      QStringLiteral("^[A-Za-z0-9가-힣 _-]+$"));
  if (!kAllowedNamePattern.match(name).hasMatch())
  {
    if (errorMessage)
    {
      *errorMessage = QStringLiteral(
          "ROI 이름은 한글/영문/숫자/공백/밑줄(_) / 하이픈(-)만 사용할 수 있습니다.");
    }
    return false;
  }
  return true;
}

bool RoiService::isDuplicateName(const QString &name) const
{
  for (const QJsonObject &record : m_records)
  {
    if (record["rod_name"].toString().compare(name, Qt::CaseInsensitive) == 0)
    {
      return true;
    }
  }
  return false;
}

RoiService::CreateResult RoiService::createFromPolygon(const QPolygon &polygon,
                                                       const QSize &frameSize,
                                                       const QString &name,
                                                       const QString &purpose)
{
  CreateResult result;
  if (frameSize.isEmpty())
  {
    result.error = QStringLiteral("프레임 크기가 유효하지 않습니다.");
    return result;
  }

  QString nameError;
  if (!isValidName(name, &nameError))
  {
    result.error = nameError;
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

  ++m_roiSequence;
  const QString rodId =
      QString("rod-%1").arg(m_roiSequence, 3, 10, QLatin1Char('0'));
  const QRect bbox = polygon.boundingRect();
  const QString ts = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  const QString finalPurpose =
      purpose.isEmpty() ? QStringLiteral("일반 주차") : purpose;

  QJsonObject roiData{
      {"rod_id", rodId},
      {"rod_name", name},
      {"rod_enable", true},
      {"rod_purpose", finalPurpose},
      {"rod_points", points},
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
  if (!m_repository.upsert(roiData, &dbError))
  {
    result.error = dbError;
    return result;
  }

  m_records.append(roiData);
  result.record = roiData;
  result.ok = true;
  return result;
}

RoiService::DeleteResult RoiService::removeAt(int index)
{
  DeleteResult result;
  if (index < 0 || index >= m_records.size())
  {
    result.error = QStringLiteral("ROI를 선택해주세요.");
    return result;
  }

  const QString removedId = m_records[index]["rod_id"].toString();
  QString dbError;
  if (!m_repository.removeById(removedId, &dbError))
  {
    result.error = dbError;
    return result;
  }

  result.removedName = m_records[index]["rod_name"].toString();
  m_records.removeAt(index);
  result.ok = true;
  return result;
}

const QVector<QJsonObject> &RoiService::records() const
{
  return m_records;
}

int RoiService::count() const
{
  return m_records.size();
}

QList<QPolygonF> RoiService::toNormalizedPolygons(const QVector<QJsonObject> &records)
{
  QList<QPolygonF> normalizedPolygons;
  normalizedPolygons.reserve(records.size());

  for (const QJsonObject &record : records)
  {
    const QJsonArray points = record["rod_points"].toArray();
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

void RoiService::recomputeSequenceFromRecords()
{
  m_roiSequence = 0;
  for (const QJsonObject &record : m_records)
  {
    const QString rodId = record["rod_id"].toString();
    if (!rodId.startsWith("rod-"))
    {
      continue;
    }
    bool ok = false;
    const int seq = rodId.mid(4).toInt(&ok);
    if (ok)
    {
      m_roiSequence = qMax(m_roiSequence, seq);
    }
  }
}
