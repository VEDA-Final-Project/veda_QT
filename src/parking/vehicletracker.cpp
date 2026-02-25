#include "parking/vehicletracker.h"
#include <QDateTime>
#include <QPainterPath>
#include <QPolygonF>

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
    // ReID 현황판에서는 모든 인식된 객체를 관리하는 것이 좋으므로
    // 타입 필터링을 완화하거나 제거합니다.
    if (obj.type == "Unknown") {
      continue;
    }

    // 기존 차량인지 확인, 없으면 새로 등록
    VehicleState &vs = m_vehicles[obj.id];
    if (vs.objectId < 0) {
      vs.objectId = obj.id;
      vs.firstSeenMs = nowMs;
      // 초기 등록 시에는 무조건 데이터 반영
      vs.type = obj.type;
      vs.plateNumber = obj.plate.isEmpty() ? obj.extraInfo : obj.plate;
      vs.score = obj.score;
    } else {
      // 이미 존재하는 차량 update
      // 수동 오버라이드가 아닐 때만 AI 값으로 덮어씀
      if (!vs.manualOverride) {
        vs.type = obj.type;
        if (!obj.plate.isEmpty())
          vs.plateNumber = obj.plate;
        else if (!obj.extraInfo.isEmpty())
          vs.plateNumber = obj.extraInfo;
        // score는 변동이 심하므로 계속 업데이트? 혹은 최고점 유지? -> 계속
        // 업데이트
        vs.score = obj.score;
      }
    }

    // BBox는 이동 추적을 위해 오버라이드 여부 상관없이(또는 별도 처리) 업데이트
    // 단, 강제 지정된 프레임에서는 덮어쓰지 않아야 하는데,
    // forceTrackState가 호출된 직후의 프레임인지 알기 어려움.
    // 여기서는 BBox는 AI를 따르되, 사용자가 수동 입력한 순간은
    // forceTrackState에서 처리됨.
    vs.boundingBox = obj.rect;
    vs.lastSeenMs = nowMs;

    // 각 ROI에 대해 점유율 확인
    int prevRoi = vs.occupiedRoiIndex;
    vs.occupiedRoiIndex = -1;

    double maxRatio = 0.0;
    int bestRoiIndex = -1;

    for (int i = 0; i < m_roiPolygons.size(); ++i) {
      // AI 메타데이터 좌표(obj.rect)를 [0.0, 1.0] 정규화된 좌표로 변환합니다.
      QRectF normRect((obj.rect.x() - cropOffsetX) /
                          static_cast<double>(effectiveWidth),
                      obj.rect.y() / static_cast<double>(sourceHeight),
                      obj.rect.width() / static_cast<double>(effectiveWidth),
                      obj.rect.height() / static_cast<double>(sourceHeight));

      double ratio = computeOccupancyRatio(normRect, m_roiPolygons[i]);
      if (ratio > maxRatio) {
        maxRatio = ratio;
        bestRoiIndex = i;
      }
    }

    // 0.4 이상 겹치면서 가장 많이 겹치는 ROI를 최종 선택 (경계선 이슈 방지)
    if (maxRatio >= 0.4 && bestRoiIndex >= 0) {
      vs.occupiedRoiIndex = bestRoiIndex;
    }

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
  // 트래킹 중인 객체가 없으면 새로 생성 (실험용)
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
  vs.manualOverride = true; // 강제 업데이트됨 표시

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

double
VehicleTracker::computeOccupancyRatio(const QRectF &vehicleRect,
                                      const QPolygonF &roiPolygon) const {
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

  // 차체 하단 삼각형의 총 면적 (단순 삼각형 공식 0.5 * base * height)
  // base = width, height = half height
  const double triangleArea =
      0.5 * vehicleRect.width() * (vehicleRect.height() / 2.0);

  if (triangleArea <= 0) {
    return 0.0;
  }

  // 교집합 면적 계산 (QPainterPath::intersected 사용)
  QPainterPath intersection = roiPath.intersected(vehiclePath);

  // QPainterPath의 교집합 면적을 폴리곤 분할을 통해 근사 계산
  double interArea = 0.0;
  const QList<QPolygonF> polygons = intersection.toSubpathPolygons();
  for (const QPolygonF &poly : polygons) {
    // 다각형 면적 공식 (Shoelace formula)
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

  // 차량 하단부가 주차 구역에 포함된 비율 반환
  return interArea / triangleArea;
}
