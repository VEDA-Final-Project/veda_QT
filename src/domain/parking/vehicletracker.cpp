#include "vehicletracker.h"
#include <QDateTime>
#include <QMap>
#include <QPainterPath>
#include <QPolygonF>
#include <QString>
#include <QHash>
#include <mutex>
#include <QList>
#include <QDebug>
#include <algorithm>

void VehicleTracker::setIdPrefix(const QString &prefix) { m_idPrefix = prefix; }

void VehicleTracker::setRoiPolygons(const QList<QPolygonF> &polygons) {
  m_roiPolygons = polygons;
}

// ===== IoU 헬퍼 함수 =====
namespace {
double computeIoU(const QRectF &a, const QRectF &b) {
  QRectF inter = a.intersected(b);
  if (inter.isEmpty()) return 0.0;

  double interArea = inter.width() * inter.height();
  double unionArea = (a.width() * a.height()) +
                     (b.width() * b.height()) - interArea;
  return (unionArea > 0) ? (interArea / unionArea) : 0.0;
}

constexpr double kIoUMatchThreshold = 0.45;
} // namespace

// ===== update() =====
QList<VehicleState> VehicleTracker::update(const QList<ObjectInfo> &objects,
                                           int cropOffsetX, int effectiveWidth,
                                           int sourceHeight, qint64 nowMs,
                                           qint64 /*pruneTimeoutMs*/) {
  if (effectiveWidth <= 0) effectiveWidth = 1;
  if (sourceHeight <= 0) sourceHeight = 1;

  QList<VehicleState> newEntries;
  m_pendingDepartures.clear();

  // Stage 1: IoU 기반 공간 매칭
  QHash<int, int> objectToTrackMap;
  QSet<int> matchedTrackKeys;

  for (const ObjectInfo &obj : objects) {
    if (obj.type == "Unknown" || obj.rect.width() <= 0 || obj.rect.height() <= 0) continue;

    double maxIoU = 0.0;
    int bestTrackKey = -1;

    for (auto it = m_vehicles.begin(); it != m_vehicles.end(); ++it) {
      if (matchedTrackKeys.contains(it.key())) continue;

      double iou = computeIoU(it.value().boundingBox, obj.rect);
      if (iou > kIoUMatchThreshold && iou > maxIoU) {
        maxIoU = iou;
        bestTrackKey = it.key();
      }
    }

    if (bestTrackKey != -1) {
      objectToTrackMap.insert(obj.id, bestTrackKey);
      matchedTrackKeys.insert(bestTrackKey);
    }
  }

  // Stage 2: Semantic ID Fallback
  for (const ObjectInfo &obj : objects) {
    if (obj.type == "Unknown" || obj.rect.width() <= 0 || obj.rect.height() <= 0) continue;
    if (objectToTrackMap.contains(obj.id)) continue;

    if (m_vehicles.contains(obj.id) && !matchedTrackKeys.contains(obj.id)) {
        objectToTrackMap.insert(obj.id, obj.id);
        matchedTrackKeys.insert(obj.id);
    }
  }

  // Stage 3: 상태 업데이트 수행
  QHash<int, VehicleState> nextVehicles;
  for (const ObjectInfo &obj : objects) {
    if (obj.type == "Unknown" || obj.rect.width() <= 0 || obj.rect.height() <= 0) continue;
    
    VehicleState vs;
    if (objectToTrackMap.contains(obj.id)) {
      vs = m_vehicles.take(objectToTrackMap[obj.id]);
      vs.objectId = obj.id;
    } else {
      vs.objectId = obj.id;
      vs.firstSeenMs = nowMs;
      vs.reidId = "V---";
    }

    // 공통 정보 업데이트
    if (!vs.manualOverride) {
      vs.type = obj.type;
      const QString newPlate = obj.plate.isEmpty() ? obj.extraInfo : obj.plate;
      if (!newPlate.isEmpty()) {
        vs.plateNumber = newPlate;
      }
    }
    vs.score = obj.score;
    vs.boundingBox = obj.rect;
    vs.lastSeenMs = nowMs;

    // ReID 특징이 이미 있으면 보존 (실제 매칭은 updateReidFeatures()에서 수행)
    if (!obj.reidFeatures.empty()) vs.reidFeatures = obj.reidFeatures;

    // 번호판 캐시 조회 (ReID ID가 이미 할당된 경우만)
    if (!vs.reidId.isEmpty() && vs.reidId != "V---") {
      if (vs.plateNumber.isEmpty()) {
        const QString cachedPlate = m_plateByReid.value(vs.reidId);
        if (!cachedPlate.isEmpty()) {
          vs.plateNumber = cachedPlate;
        }
      } else {
        m_plateByReid.insert(vs.reidId, vs.plateNumber);
      }
    }

    int prevRoi = vs.occupiedRoiIndex;
    bool isVehicle = isVehicleType(vs.type);

    // ROI 점유 판정 (차량만)
    int currentFrameRoiIndex = -1;
    if (isVehicle) {
      QRectF normRect((obj.rect.x() - cropOffsetX) /
                          static_cast<double>(effectiveWidth),
                      obj.rect.y() / static_cast<double>(sourceHeight),
                      obj.rect.width() / static_cast<double>(effectiveWidth),
                      obj.rect.height() / static_cast<double>(sourceHeight));

      double maxRatio = 0.0;
      int bestRoiIndex = -1;

      for (int i = 0; i < m_roiPolygons.size(); ++i) {
        double dynamicThres = 0.35;
        double ratio =
            computeOccupancyRatio(normRect, m_roiPolygons[i], &dynamicThres);

        if (ratio > dynamicThres && ratio > maxRatio) {
          maxRatio = ratio;
          bestRoiIndex = i;
        }
      }
      currentFrameRoiIndex = bestRoiIndex;
    }

    // Temporal Hysteresis Queue
    vs.roiHistory.append(currentFrameRoiIndex);
    const int HISTORY_SIZE = 15;
    if (vs.roiHistory.size() > HISTORY_SIZE) {
      vs.roiHistory.removeFirst();
    }

    QMap<int, int> roiFreqMap;
    for (int roi : vs.roiHistory) {
      roiFreqMap[roi]++;
    }

    int requiredVotes = static_cast<int>(vs.roiHistory.size() * 0.8);
    int stableRoi = vs.occupiedRoiIndex;
    for (auto it = roiFreqMap.begin(); it != roiFreqMap.end(); ++it) {
      if (it.value() >= requiredVotes) {
        stableRoi = it.key();
        break;
      }
    }

    vs.occupiedRoiIndex = stableRoi;

    if (isVehicle) {
      if (prevRoi >= 0 && vs.occupiedRoiIndex < 0) {
        if (vs.notified) {
          VehicleState departedState = vs;
          departedState.occupiedRoiIndex = prevRoi;
          m_pendingDepartures.append(departedState);
        }

        vs.roiEntryMs = 0;
        vs.notified = false;
        vs.roiHistory.clear();
        vs.roiHistory.append(-1);
      }
    }
    
    // 신규 진입 감지
    if (isVehicle && prevRoi < 0 && vs.occupiedRoiIndex >= 0) {
      vs.roiEntryMs = nowMs;
      newEntries.append(vs);
    }
    
    nextVehicles.insert(obj.id, vs);
  }

  // Stage 4: 미매칭 차량 보존
  for (auto it = m_vehicles.begin(); it != m_vehicles.end(); ++it) {
      if (!matchedTrackKeys.contains(it.key())) {
          if (!nextVehicles.contains(it.key())) {
              nextVehicles.insert(it.key(), it.value());
          }
      }
  }

  m_vehicles = nextVehicles;
  return newEntries;
}

QList<VehicleState> VehicleTracker::takePendingDepartures() {
  QList<VehicleState> departures = m_pendingDepartures;
  m_pendingDepartures.clear();
  return departures;
}

// ===== 번호판 관련 =====
void VehicleTracker::setPlateNumber(int objectId, const QString &plate) {
  if (m_vehicles.contains(objectId)) {
    VehicleState &vs = m_vehicles[objectId];
    vs.plateNumber = plate;
    if (!vs.reidId.isEmpty() && vs.reidId != "V---" && !plate.isEmpty()) {
      m_plateByReid.insert(vs.reidId, plate);
    }
  }
}

void VehicleTracker::setPlateNumberForReid(const QString &reidId,
                                           const QString &plate) {
  if (reidId.isEmpty() || reidId == "V---" || plate.isEmpty()) return;

  m_plateByReid.insert(reidId, plate);

  for (auto it = m_vehicles.begin(); it != m_vehicles.end(); ++it) {
    if (it.value().reidId == reidId && it.value().plateNumber.isEmpty()) {
      it.value().plateNumber = plate;
    }
  }
}

// ===== 상태 제어 =====
void VehicleTracker::forceTrackState(int objectId, const ObjectInfo &info) {
  if (!m_vehicles.contains(objectId)) {
    VehicleState vs;
    vs.objectId = objectId;
    vs.firstSeenMs = QDateTime::currentMSecsSinceEpoch();
    m_vehicles.insert(objectId, vs);
  }

  VehicleState &vs = m_vehicles[objectId];
  vs.type = info.type;
  vs.plateNumber = info.plate;
  vs.score = info.score;
  vs.boundingBox = info.rect;
  vs.manualOverride = true;
  vs.lastSeenMs = QDateTime::currentMSecsSinceEpoch();
}

void VehicleTracker::markNotified(int objectId) {
  if (m_vehicles.contains(objectId)) {
    m_vehicles[objectId].notified = true;
  }
}

const QHash<int, VehicleState> &VehicleTracker::vehicles() const {
  return m_vehicles;
}

// ===== pruneStale (갤러리 만료 포함) =====
QList<VehicleState> VehicleTracker::pruneStale(qint64 nowMs, qint64 timeoutMs) 
{
  QList<VehicleState> departed;
  auto it = m_vehicles.begin();
  while (it != m_vehicles.end()) {
    if ((nowMs - it.value().lastSeenMs) > timeoutMs) {
      const VehicleState &vs = it.value();

      // 주차하지 않고 지나간 차량(ROI 미점유)의 갤러리 엔트리 정리
      if (vs.occupiedRoiIndex < 0 && !vs.reidId.isEmpty() && vs.reidId != "V---") {
        std::lock_guard<std::mutex> lock(m_galleryMutex);
        for (int i = m_reidGallery.size() - 1; i >= 0; --i) {
          if (m_reidGallery[i].persistentId == vs.reidId) {
            m_reidGallery.removeAt(i);
            break;
          }
        }
        m_plateByReid.remove(vs.reidId);
      }

      departed.append(vs);
      it = m_vehicles.erase(it);
    } else {
      ++it;
    }
  }
  return departed;
}

// ===== ROI 점유 비율 계산 =====
double VehicleTracker::computeOccupancyRatio(const QRectF &vehicleRect,
                                             const QPolygonF &roiPolygon,
                                             double *dynamicThreshold) const {
  if (!vehicleRect.intersects(roiPolygon.boundingRect()))
    return 0.0;

  QPolygonF vehicleTriangle;
  vehicleTriangle << QPointF(vehicleRect.left(), vehicleRect.bottom())
                  << QPointF(vehicleRect.right(), vehicleRect.bottom())
                  << QPointF(vehicleRect.center().x(),
                             vehicleRect.center().y());

  QPainterPath roiPath;
  roiPath.addPolygon(roiPolygon);

  QPainterPath vehiclePath;
  vehiclePath.addPolygon(vehicleTriangle);

  const double triangleArea =
      0.5 * vehicleRect.width() * (vehicleRect.height() / 2.0);

  if (triangleArea <= 0) return 0.0;

  QPainterPath intersection = roiPath.intersected(vehiclePath);

  double interArea = 0.0;
  const QList<QPolygonF> polygons = intersection.toSubpathPolygons();
  for (const QPolygonF &poly : polygons) {
    double polyArea = 0.0;
    int n = poly.size();
    if (n >= 3) {
      for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        polyArea += (poly[i].x() * poly[j].y()) - (poly[j].x() * poly[i].y());
      }
      interArea += std::abs(polyArea) / 2.0;
    }
  }

  if (dynamicThreshold) {
    double centerV = roiPolygon.boundingRect().center().y();
    *dynamicThreshold = std::max(0.25, std::min(0.5, 0.25 + centerV * 0.25));
  }

  return interArea / triangleArea;
}

// ===== ReID 갤러리 검색 =====
QString VehicleTracker::findMatchInGallery(const std::vector<float> &features,
                                           float threshold) {
  if (features.empty()) return QString();

  QString bestId;
  float maxSim = -1.0f;

  std::lock_guard<std::mutex> lock(m_galleryMutex);
  for (const auto &entry : m_reidGallery) {
    if (entry.features.size() != features.size()) continue;

    float sim = 0.0f;
    for (size_t i = 0; i < features.size(); ++i) {
      sim += features[i] * entry.features[i];
    }

    if (sim > threshold && sim > maxSim) {
      maxSim = sim;
      bestId = entry.persistentId;
    }
  }

  return (maxSim >= threshold) ? bestId : QString();
}

// ===== ReID 특징 비동기 업데이트 (ReID 로직의 유일한 중심점) =====
void VehicleTracker::updateReidFeatures(const QList<ObjectInfo> &objects,
                                        qint64 nowMs) {
  for (const auto &obj : objects) {
    if (obj.reidFeatures.empty()) continue;

    int targetTrackId = -1;

    // 1. Direct ID Match
    if (m_vehicles.contains(obj.id)) {
      targetTrackId = obj.id;
    } else {
      // 2. Spatial Fallback (IoU)
      double maxIoU = 0.0;
      for (auto it = m_vehicles.begin(); it != m_vehicles.end(); ++it) {
        if (!it.value().reidId.isEmpty() && it.value().reidId != "V---") continue;

        double iou = computeIoU(it.value().boundingBox, obj.rect);
        if (iou > kIoUMatchThreshold && iou > maxIoU) {
          maxIoU = iou;
          targetTrackId = it.key();
        }
      }
    }

    if (targetTrackId == -1) continue;

    VehicleState &vs = m_vehicles[targetTrackId];
    vs.reidFeatures = obj.reidFeatures;

    bool isVehicle = isVehicleType(vs.type);

    // ID 할당 또는 갤러리 갱신
    if (vs.reidId.isEmpty() || vs.reidId == "V---") {
      QString matchedId = findMatchInGallery(vs.reidFeatures);
      if (!matchedId.isEmpty()) {
        vs.reidId = matchedId;
        updateGallery(vs.reidFeatures, vs.reidId, nowMs);
      } else if (isVehicle) {
        std::lock_guard<std::mutex> lock(m_galleryMutex);
        vs.reidId = QString("%1-V%2").arg(m_idPrefix).arg(m_nextPersistentId++);
        m_reidGallery.append({vs.reidFeatures, vs.reidId, nowMs});
        if (m_reidGallery.size() > 50) m_reidGallery.removeFirst();
      }
    } else {
      updateGallery(vs.reidFeatures, vs.reidId, nowMs);
    }

    // 번호판 캐시 동기화
    if (!vs.reidId.isEmpty() && vs.reidId != "V---") {
      if (vs.plateNumber.isEmpty()) {
        const QString cachedPlate = m_plateByReid.value(vs.reidId);
        if (!cachedPlate.isEmpty()) {
          vs.plateNumber = cachedPlate;
        }
      } else {
        m_plateByReid.insert(vs.reidId, vs.plateNumber);
      }
    }
  }
}

// ===== 갤러리 EMA 갱신 =====
void VehicleTracker::updateGallery(const std::vector<float> &features,
                                   const QString &id, qint64 nowMs) {
  std::lock_guard<std::mutex> lock(m_galleryMutex);
  const float alpha = 0.1f;

  for (auto &entry : m_reidGallery) {
    if (entry.persistentId == id) {
      if (entry.features.size() == features.size()) {
        float normSq = 0.0f;
        for (size_t i = 0; i < features.size(); ++i) {
          entry.features[i] =
              (1.0f - alpha) * entry.features[i] + alpha * features[i];
          normSq += entry.features[i] * entry.features[i];
        }
        float norm = std::sqrt(normSq);
        if (norm > 1e-6) {
          for (float &f : entry.features)
            f /= norm;
        }
      } else {
        entry.features = features;
      }
      entry.lastSeenMs = nowMs;
      return;
    }
  }

  m_reidGallery.append({features, id, nowMs});
  if (m_reidGallery.size() > 50)
    m_reidGallery.removeFirst();
}
