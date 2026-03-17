#include "vehicletracker.h"
#include <QDateTime>
#include <QMap>
#include <QPainterPath>
#include <QPolygonF>
#include <QString>
#include <QHash>
#include <algorithm>
#include <mutex>
#include <QList>
#include <QDebug> 

// Member initialization is handled in the header or constructor
void VehicleTracker::setIdPrefix(const QString &prefix) { m_idPrefix = prefix; }

namespace {
bool isTrackableVehicleType(const QString &type) {
  return type == QStringLiteral("Vehical") ||
         type == QStringLiteral("Vehicle") ||
         type == QStringLiteral("Car") || type == QStringLiteral("Truck") ||
         type == QStringLiteral("Bus") ||
         type == QStringLiteral("Motorcycle");
}
} // namespace

void VehicleTracker::setRoiPolygons(const QList<QPolygonF> &polygons) {
  m_roiPolygons = polygons;
}

QList<VehicleState> VehicleTracker::update(const QList<ObjectInfo> &objects,
                                           int cropOffsetX, int effectiveWidth,
                                           int sourceHeight, qint64 nowMs,
                                           qint64 pruneTimeoutMs) {
  // 방어 코드
  if (effectiveWidth <= 0)
    effectiveWidth = 1;
  if (sourceHeight <= 0)
    sourceHeight = 1;

  QList<VehicleState> newEntries;

  // 1. Spatial Matching (Stage 1: IoU)
  QHash<int, int> objectToTrackMap; // Key: WiseAI object.id, Value: Existing track key
  QSet<int> matchedTrackKeys;

  for (const ObjectInfo &obj : objects) {
    // 0x0 크기 객체는 노이즈로 간주하여 완전히 무시
    if (obj.type == "Unknown" || obj.rect.width() <= 0 || obj.rect.height() <= 0) continue;

    double maxIoU = 0.0;
    int bestTrackKey = -1;

    for (auto it = m_vehicles.begin(); it != m_vehicles.end(); ++it) {
      if (matchedTrackKeys.contains(it.key())) continue;

      QRectF inter = it.value().boundingBox.intersected(obj.rect);
      if (inter.isEmpty()) continue;

      double interArea = inter.width() * inter.height();
      double unionArea = (it.value().boundingBox.width() * it.value().boundingBox.height()) +
                         (obj.rect.width() * obj.rect.height()) - interArea;
      double iou = (unionArea > 0) ? (interArea / unionArea) : 0;

      if (iou > 0.45 && iou > maxIoU) {
        maxIoU = iou;
        bestTrackKey = it.key();
      }
    }

    if (bestTrackKey != -1) {
      objectToTrackMap.insert(obj.id, bestTrackKey);
      matchedTrackKeys.insert(bestTrackKey);
    }
  }

  // 2. ID Fallback (Stage 2: Semantic ID)
  // IoU로는 못 찾았지만, Wise AI ID가 동일하다면 기존 상태를 계승하여 V--- 방지
  for (const ObjectInfo &obj : objects) {
    if (obj.type == "Unknown" || obj.rect.width() <= 0 || obj.rect.height() <= 0) continue;
    if (objectToTrackMap.contains(obj.id)) continue;

    if (m_vehicles.contains(obj.id) && !matchedTrackKeys.contains(obj.id)) {
        objectToTrackMap.insert(obj.id, obj.id);
        matchedTrackKeys.insert(obj.id);
    }
  }

  // 2. 실제 업데이트 수행
  QHash<int, VehicleState> nextVehicles;
  for (const ObjectInfo &obj : objects) {
    if (obj.type == "Unknown" || obj.rect.width() <= 0 || obj.rect.height() <= 0) continue;
    
    VehicleState vs;
    if (objectToTrackMap.contains(obj.id)) {
      // 기존 트랙을 새 ID(혹은 같은 ID)로 계승
      vs = m_vehicles.take(objectToTrackMap[obj.id]);
      vs.objectId = obj.id; // 최신 Wise AI ID 업데이트
    } else {
      // 신규 등록
      vs.objectId = obj.id;
      vs.firstSeenMs = nowMs;
      vs.reidId = ""; 
    }

    // 공통 정보 업데이트
    if (!vs.manualOverride) {
      vs.type = obj.type;
      vs.plateNumber = obj.plate.isEmpty() ? obj.extraInfo : obj.plate;
    }
    vs.score = obj.score;
    vs.boundingBox = obj.rect;
    vs.lastSeenMs = nowMs;

    // ReID ID Assignment / Matching
    if (!obj.reidFeatures.empty()) vs.reidFeatures = obj.reidFeatures;
    
    QString lowerType = vs.type.toLower();
    bool isVehicle = (lowerType == "vehicle" || lowerType == "car" || 
                      lowerType == "vehical" || lowerType == "truck" || 
                      lowerType == "bus");

    if (!vs.reidFeatures.empty()) {
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
    }

    int prevRoi = vs.occupiedRoiIndex;

    // 1. 타입 검사: 차량 계열인 경우에만 ROI 점유 로직 수행
    int currentFrameRoiIndex = -1;
    if (isVehicle) {
      // AI 메타데이터 좌표(obj.rect)를 [0.0, 1.0] 정규화된 좌표로 변환
      QRectF normRect((obj.rect.x() - cropOffsetX) /
                          static_cast<double>(effectiveWidth),
                      obj.rect.y() / static_cast<double>(sourceHeight),
                      obj.rect.width() / static_cast<double>(effectiveWidth),
                      obj.rect.height() / static_cast<double>(sourceHeight));

      // 각 ROI에 대해 점유율 확인 (Max Ratio 방식)
      double maxRatio = 0.0;
      int bestRoiIndex = -1;

      for (int i = 0; i < m_roiPolygons.size(); ++i) {
        double dynamicThres = 0.35; // Default
        double ratio =
            computeOccupancyRatio(normRect, m_roiPolygons[i], &dynamicThres);

        if (ratio > dynamicThres && ratio > maxRatio) {
          maxRatio = ratio;
          bestRoiIndex = i;
        }
      }
      currentFrameRoiIndex = bestRoiIndex;
    }

    // 2. 시계열 큐 필터링 (Temporal Hysteresis Queue)
    vs.roiHistory.append(currentFrameRoiIndex);
    const int HISTORY_SIZE = 15;
    if (vs.roiHistory.size() > HISTORY_SIZE) {
      vs.roiHistory.removeFirst();
    }

    QMap<int, int> roiFreqMap;
    const QList<int> &roiHistory = vs.roiHistory;
    for (int roi : roiHistory) {
      roiFreqMap[roi]++;
    }

    int requiredVotes =
        static_cast<int>(vs.roiHistory.size() * 0.8); // 80% 동의
    int stableRoi = vs.occupiedRoiIndex; // 기본 상태 유지
    for (auto it = roiFreqMap.begin(); it != roiFreqMap.end(); ++it) {
      if (it.value() >= requiredVotes) {
        stableRoi = it.key();
        break;
      }
    }

    vs.occupiedRoiIndex = stableRoi;
    
    // 신규 진입 감지
    if (isVehicle && prevRoi < 0 && vs.occupiedRoiIndex >= 0) {
      vs.roiEntryMs = nowMs;
      newEntries.append(vs);
    }
    
    // 최종 상태를 nextVehicles에 저장 (Wise AI ID 기반 키 관리)
    nextVehicles.insert(obj.id, vs);
  }

  // 3. 이번 프레임에 나타나지 않은 차량들 보존 (타임아웃은 pruning에서 처리)
  // matchedTrackKeys에 포함되지 않은 녀석들 중 아직 살아있는 것들을 합침
  for (auto it = m_vehicles.begin(); it != m_vehicles.end(); ++it) {
      if (!matchedTrackKeys.contains(it.key())) {
          // 중복 방지 (Wise AI가 예전 ID를 다른 객체에 재활용했을 경우 대비)
          if (!nextVehicles.contains(it.key())) {
              nextVehicles.insert(it.key(), it.value());
          }
      }
  }

  m_vehicles = nextVehicles;
  return newEntries;
}

void VehicleTracker::setPlateNumber(int objectId, const QString &plate) {
  if (m_vehicles.contains(objectId)) {
    m_vehicles[objectId].plateNumber = plate;
  }
}

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

QList<VehicleState> VehicleTracker::pruneStale(qint64 nowMs, qint64 timeoutMs) 
{
  QList<VehicleState> departed;
  auto it = m_vehicles.begin();
  while (it != m_vehicles.end()) {
    if ((nowMs - it.value().lastSeenMs) > timeoutMs) {
      departed.append(it.value());
      it = m_vehicles.erase(it);
    } else {
      ++it;
    }
  }
  return departed;
}

double VehicleTracker::computeOccupancyRatio(const QRectF &vehicleRect,
                                             const QPolygonF &roiPolygon,
                                             double *dynamicThreshold) const {
  // AABB 사전 필터링: 바운딩 박스가 겹치지 않으면 즉시 0 반환
  if (!vehicleRect.intersects(roiPolygon.boundingRect()))
    return 0.0;

  // 차량 바운딩 박스의 하단 삼각형 생성 (좌하단, 우하단, 중심점)
  QPolygonF vehicleTriangle;
  vehicleTriangle << QPointF(vehicleRect.left(), vehicleRect.bottom())
                  << QPointF(vehicleRect.right(), vehicleRect.bottom())
                  << QPointF(vehicleRect.center().x(),
                             vehicleRect.center().y());

  // QPainterPath를 사용하여 정확한 부동소수점 다각형 교집합 계산
  QPainterPath roiPath;
  roiPath.addPolygon(roiPolygon);

  QPainterPath vehiclePath;
  vehiclePath.addPolygon(vehicleTriangle);

  // 차체 하단 삼각형의 총 면적 (0.5 * base * height)
  const double triangleArea =
      0.5 * vehicleRect.width() * (vehicleRect.height() / 2.0);

  if (triangleArea <= 0) {
    return 0.0;
  }

  // 교집합 면적 계산 (QPainterPath::intersected 사용)
  QPainterPath intersection = roiPath.intersected(vehiclePath);

  // Shoelace formula로 정확한 면적 계산
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

  // 동적 임계값: ROI가 화면 위쪽(먼 곳, Y값이 작음)일수록 투영에 의해 작아
  // 보이므로 thres를 낮춤.
  if (dynamicThreshold) {
    double centerV = roiPolygon.boundingRect().center().y();
    // y=0(상단) -> 0.25, y=1(하단) -> 0.50
    *dynamicThreshold = std::max(0.25, std::min(0.5, 0.25 + centerV * 0.25));
  }

  // 차량 하단부가 주차 구역에 포함된 비율 반환
  return interArea / triangleArea;
}

QString VehicleTracker::findMatchInGallery(const std::vector<float> &features,
                                           float threshold) {
  if (features.empty())
    return QString();

  QString bestId;
  float maxSim = -1.0f;

  std::lock_guard<std::mutex> lock(m_galleryMutex);
  for (const auto &entry : m_reidGallery) {
    if (entry.features.size() != features.size())
      continue;

    // Cosine Similarity (Assuming L2 normalized features)
    float sim = 0.0f;
    for (size_t i = 0; i < features.size(); ++i) {
      sim += features[i] * entry.features[i];
    }

    if (sim > threshold && sim > maxSim) {
      maxSim = sim;
      bestId = entry.persistentId;
    }
  }

  // No logging needed for production

  return (maxSim >= threshold) ? bestId : QString();
}

void VehicleTracker::updateReidFeatures(const QList<ObjectInfo> &objects,
                                        qint64 nowMs) {
  int matchedCount = 0;
  int fallbackCount = 0;
  int lostCount = 0;

  for (const auto &obj : objects) {
    if (obj.reidFeatures.empty()) continue;

    int targetTrackId = -1;

    // 1. Direct ID Match
    if (m_vehicles.contains(obj.id)) {
        targetTrackId = obj.id;
        matchedCount++;
    } else {
        // 2. Spatial Fallback: 연산 도중 ID가 바뀌었을 가능성 대비 (IoU 매칭)
        double maxIoU = 0.0;
        for (auto it = m_vehicles.begin(); it != m_vehicles.end(); ++it) {
            // ReID ID가 아직 없는 녀석들 우선
            if (!it.value().reidId.isEmpty() && it.value().reidId != "V---") continue;

            QRectF inter = it.value().boundingBox.intersected(obj.rect);
            if (inter.isEmpty()) continue;

            double interArea = inter.width() * inter.height();
            double unionArea = (it.value().boundingBox.width() * it.value().boundingBox.height()) +
                               (obj.rect.width() * obj.rect.height()) - interArea;
            double iou = (unionArea > 0) ? (interArea / unionArea) : 0;

            if (iou > 0.45 && iou > maxIoU) {
                maxIoU = iou;
                targetTrackId = it.key();
            }
        }
        if (targetTrackId != -1) {
            fallbackCount++;
        } else {
            lostCount++;
        }
    }

    if (targetTrackId != -1) {
      VehicleState &vs = m_vehicles[targetTrackId];
      vs.reidFeatures = obj.reidFeatures;

      QString lowerType = vs.type.toLower();
      bool isVehicle = (lowerType == "vehicle" || lowerType == "car" ||
                        lowerType == "vehical" ||
                        lowerType == "truck" || lowerType == "bus");

      // If ID is not assigned yet, try to match or assign now!
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
    }
  }

  // No logging
}

void VehicleTracker::updateGallery(const std::vector<float> &features,
                                   const QString &id, qint64 nowMs) {
  std::lock_guard<std::mutex> lock(m_galleryMutex);
  // EMA (Exponential Moving Average) disabled to prevent feature drift across channels.
  const float alpha = 0.0f;

  for (auto &entry : m_reidGallery) {
    if (entry.persistentId == id) {
      if (entry.features.size() == features.size()) {
        float normSq = 0.0f;
        for (size_t i = 0; i < features.size(); ++i) {
          entry.features[i] =
              (1.0f - alpha) * entry.features[i] + alpha * features[i];
          normSq += entry.features[i] * entry.features[i];
        }
        // Re-normalize to maintain Cosine Similarity properties
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

  // Not in gallery? (This case usually handled in update() new id assignment,
  // but adding as safety or if features arrived late for a manual ID)
  m_reidGallery.append({features, id, nowMs});
  if (m_reidGallery.size() > 50)
    m_reidGallery.removeFirst();
}
