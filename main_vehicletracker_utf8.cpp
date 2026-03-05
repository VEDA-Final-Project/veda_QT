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
    // ReID ?꾪솴?먯뿉?쒕뒗 紐⑤뱺 ?몄떇??媛앹껜瑜?愿由ы븯??寃껋씠 醫뗭쑝誘濡?    // ????꾪꽣留곸쓣 ?꾪솕?섍굅???쒓굅?⑸땲??
    if (obj.type == "Unknown") {
      continue;
    }

    // 湲곗〈 李⑤웾?몄? ?뺤씤, ?놁쑝硫??덈줈 ?깅줉
    VehicleState &vs = m_vehicles[obj.id];
    if (vs.objectId < 0) {
      vs.objectId = obj.id;
      vs.firstSeenMs = nowMs;
      // 珥덇린 ?깅줉 ?쒖뿉??臾댁“嫄??곗씠??諛섏쁺
      vs.type = obj.type;
      vs.plateNumber = obj.plate.isEmpty() ? obj.extraInfo : obj.plate;
      vs.score = obj.score;
    } else {
      // ?대? 議댁옱?섎뒗 李⑤웾 update
      // ?섎룞 ?ㅻ쾭?쇱씠?쒓? ?꾨땺 ?뚮쭔 AI 媛믪쑝濡???뼱?
      if (!vs.manualOverride) {
        vs.type = obj.type;
        if (!obj.plate.isEmpty())
          vs.plateNumber = obj.plate;
        else if (!obj.extraInfo.isEmpty())
          vs.plateNumber = obj.extraInfo;
        // score??蹂?숈씠 ?ы븯誘濡?怨꾩냽 ?낅뜲?댄듃? ?뱀? 理쒓퀬???좎?? -> 怨꾩냽
        // ?낅뜲?댄듃
        vs.score = obj.score;
      }
    }

    // BBox???대룞 異붿쟻???꾪빐 ?ㅻ쾭?쇱씠???щ? ?곴??놁씠(?먮뒗 蹂꾨룄 泥섎━) ?낅뜲?댄듃
    // ?? 媛뺤젣 吏?뺣맂 ?꾨젅?꾩뿉?쒕뒗 ??뼱?곗? ?딆븘???섎뒗??
    // forceTrackState媛 ?몄텧??吏곹썑???꾨젅?꾩씤吏 ?뚭린 ?대젮?.
    // ?ш린?쒕뒗 BBox??AI瑜??곕Ⅴ?? ?ъ슜?먭? ?섎룞 ?낅젰???쒓컙?
    // forceTrackState?먯꽌 泥섎━??
    vs.boundingBox = obj.rect;
    vs.lastSeenMs = nowMs;

    // 媛?ROI??????먯쑀???뺤씤
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

    // ?덈줈 ROI??吏꾩엯??寃쎌슦 (?댁쟾??ROI 諛?-> 吏湲?ROI ??
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
  // ?몃옒??以묒씤 媛앹껜媛 ?놁쑝硫??덈줈 ?앹꽦 (?ㅽ뿕??
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
  vs.manualOverride = true; // 媛뺤젣 ?낅뜲?댄듃???쒖떆

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
