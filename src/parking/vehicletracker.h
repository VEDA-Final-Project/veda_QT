#ifndef VEHICLETRACKER_H
#define VEHICLETRACKER_H

#include "metadata/metadatathread.h"
#include <QHash>
#include <QList>
#include <QPolygon>
#include <QRectF>
#include <QString>

/**
 * @brief 개별 차량의 현재 상태를 나타내는 구조체
 */
struct VehicleState {
  int objectId = -1;           // AI 메타데이터 Object ID
  QString plateNumber;         // 인식된 번호판 (아직 미인식이면 빈 문자열)
  QString type;                // 객체 타입 (Vehicle, Person...)
  float score = 0.0f;          // 신뢰도 점수
  QRectF boundingBox;          // 현재 바운딩 박스 (원본 좌표)
  int occupiedRoiIndex = -1;   // 점유 중인 ROI 인덱스 (-1이면 미점유)
  bool ocrRequested = false;   // OCR 요청을 이미 보냈는지 여부
  bool notified = false;       // 텔레그램 알림을 이미 보냈는지 여부
  bool manualOverride = false; // 수동으로 정보가 수정되었는지 여부
  qint64 firstSeenMs = 0;      // 최초 감지 시각 (ms)
  qint64 lastSeenMs = 0;       // 마지막 감지 시각 (ms)
  QList<int> roiHistory; // 최근 N프레임 동안의 점유 상태 (히스테리시스 필터용)
};

/**
 * @brief 차량 객체를 추적하고 상태를 관리하는 클래스
 *
 * AI 메타데이터를 받아 차량별 상태(위치, ROI 점유, OCR/알림 여부)를 유지합니다.
 * 향후 ReID 특징점 기반 추적으로 확장 가능합니다.
 */
class VehicleTracker {
public:
  /**
   * @brief ROI 폴리곤 목록을 설정합니다 (주차 구역 정보).
   */
  void setRoiPolygons(const QList<QPolygonF> &polygons);

  /**
   * @brief 새로운 메타데이터 프레임을 처리하여 차량 상태를 업데이트합니다.
   * @param objects AI가 감지한 객체 목록
   * @param cropOffsetX AI 인식 좌표 보정값
   * @param effectiveWidth 정규화를 위한 프레임 너비
   * @param sourceHeight 정규화를 위한 프레임 높이
   * @param nowMs 현재 시각 (ms)
   * @return 이번 프레임에서 새로 ROI에 진입한 차량 목록
   */
  QList<VehicleState> update(const QList<ObjectInfo> &objects, int cropOffsetX,
                             int effectiveWidth, int sourceHeight,
                             qint64 nowMs);

  /**
   * @brief 특정 차량의 OCR 결과를 반영합니다.
   */
  void setPlateNumber(int objectId, const QString &plate);

  /**
   * @brief 특정 차량의 상태(메타데이터)를 강제로 업데이트합니다.
   * 실험용 상세 제어 기능을 위해 사용됩니다.
   */
  void forceTrackState(int objectId, const ObjectInfo &info);

  /**
   * @brief 특정 차량의 알림 완료를 표시합니다.
   */
  void markNotified(int objectId);

  /**
   * @brief 현재 추적 중인 모든 차량 상태를 반환합니다.
   */
  const QHash<int, VehicleState> &vehicles() const;

  /**
   * @brief 일정 시간 이상 감지되지 않은 차량을 정리합니다.
   * @param nowMs 현재 시각
   * @param timeoutMs 타임아웃 (기본 5초)
   * @return 사라진(출차된) 차량 목록
   */
  QList<VehicleState> pruneStale(qint64 nowMs, qint64 timeoutMs = 5000);

private:
  double computeOccupancyRatio(const QRectF &vehicleRect,
                               const QPolygonF &roiPolygon) const;

  QHash<int, VehicleState> m_vehicles;
  QList<QPolygonF> m_roiPolygons;
};

#endif // VEHICLETRACKER_H
