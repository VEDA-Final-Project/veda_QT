#include "parking/vehicletracker.h"
#include <QDateTime>
#include <QMap>
#include <QPainterPath>
#include <QPolygonF>
#include <algorithm>

void VehicleTracker::setRoiPolygons(const QList<QPolygonF> &polygons) {
  m_roiPolygons = polygons;
}

QList<VehicleState> VehicleTracker::update(const QList<ObjectInfo> &objects,
                                           int cropOffsetX, int effectiveWidth,
                                           int sourceHeight, qint64 nowMs) {
  // 방어 코드
  if (effectiveWidth <= 0)
    effectiveWidth = 1;
  if (sourceHeight <= 0)
    sourceHeight = 1;

  QList<VehicleState> newEntries;

  for (const ObjectInfo &obj : objects) {
    if (obj.type == "Unknown") {
      continue;
    }

    // 기존 차량인지 확인, 없으면 새로 등록
    VehicleState &vs = m_vehicles[obj.id];
    if (vs.objectId < 0) {
      vs.objectId = obj.id;
      vs.firstSeenMs = nowMs;
      vs.type = obj.type;
      vs.plateNumber = obj.plate.isEmpty() ? obj.extraInfo : obj.plate;
      vs.score = obj.score;
    } else {
      if (!vs.manualOverride) {
        vs.type = obj.type;
        if (!obj.plate.isEmpty())
          vs.plateNumber = obj.plate;
        else if (!obj.extraInfo.isEmpty())
          vs.plateNumber = obj.extraInfo;
        vs.score = obj.score;
      }
    }

    vs.boundingBox = obj.rect;
    vs.lastSeenMs = nowMs;

    int prevRoi = vs.occupiedRoiIndex;

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

    int currentFrameRoiIndex = bestRoiIndex;

    // 2. 시계열 큐 필터링 (Temporal Hysteresis Queue)
    vs.roiHistory.append(currentFrameRoiIndex);
    const int HISTORY_SIZE = 15;
    if (vs.roiHistory.size() > HISTORY_SIZE) {
      vs.roiHistory.removeFirst();
    }

    QMap<int, int> counts;
    for (int roi : vs.roiHistory) {
      counts[roi]++;
    }

    int requiredVotes =
        static_cast<int>(vs.roiHistory.size() * 0.8); // 80% 동의
    int stableRoiIndex = vs.occupiedRoiIndex;         // 기본 상태 유지

    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
      if (it.value() >= requiredVotes) {
        stableRoiIndex = it.key();
        break;
      }
    }

    vs.occupiedRoiIndex = stableRoiIndex;

    // 새로 ROI에 진입한 경우 (이전에 ROI 밖 -> 지금 ROI 안)
    if (prevRoi < 0 && vs.occupiedRoiIndex >= 0) {
      newEntries.append(vs);
    }
  }

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

QList<VehicleState> VehicleTracker::pruneStale(qint64 nowMs, qint64 timeoutMs) {
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
