#include "parking/vehicletracker.h"
#include <QDateTime>
#include <QRegion>

void VehicleTracker::setRoiPolygons(const QList<QPolygon> &polygons) {
  m_roiPolygons = polygons;
}

QList<VehicleState> VehicleTracker::update(const QList<ObjectInfo> &objects,
                                           int frameWidth, int frameHeight,
                                           qint64 nowMs) {
  Q_UNUSED(frameWidth);
  Q_UNUSED(frameHeight);

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

    for (int i = 0; i < m_roiPolygons.size(); ++i) {
      const QRect vehicleRect = obj.rect.toRect();
      double ratio = computeOccupancyRatio(vehicleRect, m_roiPolygons[i]);
      if (ratio >= 0.5) {
        vs.occupiedRoiIndex = i;
        break;
      }
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

double VehicleTracker::computeOccupancyRatio(const QRect &vehicleRect,
                                             const QPolygon &roiPolygon) const {
  const QRegion roiRegion(roiPolygon, Qt::WindingFill);
  const double roiArea = [&]() {
    double a = 0.0;
    for (const QRect &r : roiRegion) {
      a += static_cast<double>(r.width()) * r.height();
    }
    return a;
  }();

  if (roiArea <= 0) {
    return 0.0;
  }

  const QRegion intersection = roiRegion.intersected(QRegion(vehicleRect));
  double interArea = 0.0;
  for (const QRect &r : intersection) {
    interArea += static_cast<double>(r.width()) * r.height();
  }

  return interArea / roiArea;
}
