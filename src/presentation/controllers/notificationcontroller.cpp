#include "presentation/controllers/notificationcontroller.h"

#include "presentation/widgets/toastoverlaywidget.h"
#include <QDateTime>
#include <QHash>

namespace {
constexpr qint64 kDedupeWindowMs = 200;
}

NotificationController::NotificationController(const UiRefs &uiRefs,
                                               QObject *parent)
    : QObject(parent), m_ui(uiRefs) {}

void NotificationController::showRoiCreated(int cardIndex,
                                            const QString &zoneName) {
  const QString zone = zoneLabel(zoneName);
  showToast(QString("roi-created:%1:%2").arg(cardIndex).arg(zone),
            QStringLiteral("ROI 생성"),
            QString("%1 / %2").arg(channelLabel(cardIndex), zone));
}

void NotificationController::showRoiDeleted(int cardIndex,
                                            const QString &zoneName) {
  const QString zone = zoneLabel(zoneName);
  showToast(QString("roi-deleted:%1:%2").arg(cardIndex).arg(zone),
            QStringLiteral("ROI 삭제"),
            QString("%1 / %2").arg(channelLabel(cardIndex), zone));
}

void NotificationController::showVehicleEntered(int cardIndex,
                                                const QString &zoneName) {
  const QString zone = zoneLabel(zoneName);
  showToast(QString("vehicle-entered:%1:%2").arg(cardIndex).arg(zone),
            QStringLiteral("입차"),
            QString("%1 / %2").arg(channelLabel(cardIndex), zone));
}

void NotificationController::showVehicleDeparted(int cardIndex,
                                                 const QString &zoneName) {
  const QString zone = zoneLabel(zoneName);
  showToast(QString("vehicle-departed:%1:%2").arg(cardIndex).arg(zone),
            QStringLiteral("출차"),
            QString("%1 / %2").arg(channelLabel(cardIndex), zone));
}

bool NotificationController::shouldSuppressDuplicate(const QString &key) {
  static QHash<QString, qint64> lastShownMsByKey;

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  const auto it = lastShownMsByKey.constFind(key);
  if (it != lastShownMsByKey.constEnd() && (nowMs - it.value()) < kDedupeWindowMs) {
    return true;
  }

  lastShownMsByKey.insert(key, nowMs);
  return false;
}

QString NotificationController::channelLabel(int cardIndex) const {
  return (cardIndex >= 0) ? QString("Ch%1").arg(cardIndex + 1)
                          : QStringLiteral("채널");
}

QString NotificationController::zoneLabel(const QString &zoneName) const {
  const QString trimmed = zoneName.trimmed();
  return trimmed.isEmpty() ? QStringLiteral("이름 없는 ROI") : trimmed;
}

void NotificationController::showToast(const QString &dedupeKey,
                                       const QString &title,
                                       const QString &body) {
  if (!m_ui.toastOverlay || shouldSuppressDuplicate(dedupeKey)) {
    return;
  }

  m_ui.toastOverlay->showToast(title, body);
}
