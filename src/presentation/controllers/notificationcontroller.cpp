#include "notificationcontroller.h"

#include "presentation/widgets/toastoverlay.h"
#include <QDateTime>
#include <QStringList>

QStringList NotificationController::s_history;

QStringList NotificationController::getHistory() {
  return s_history;
}

NotificationController::NotificationController(QWidget *hostWidget,
                                               QObject *parent)
    : QObject(parent) {
  if (hostWidget) {
    m_overlay = new ToastOverlay(hostWidget);
  }
}

void NotificationController::showRoiCreated(const QString &name) {
  showToast(QStringLiteral("ROI 생성: %1").arg(normalizedLabel(name)),
            ToastOverlay::Level::Success);
}

void NotificationController::showRoiDeleted(const QString &name) {
  showToast(QStringLiteral("ROI 삭제: %1").arg(normalizedLabel(name)),
            ToastOverlay::Level::Warning);
}

void NotificationController::showVehicleEntered(const QString &zoneName) {
  showToast(QStringLiteral("입차: %1").arg(normalizedLabel(zoneName)),
            ToastOverlay::Level::Success);
}

void NotificationController::showVehicleDeparted(const QString &zoneName) {
  showToast(QStringLiteral("출차: %1").arg(normalizedLabel(zoneName)),
            ToastOverlay::Level::Warning);
}

void NotificationController::showToast(const QString &message,
                                       ToastOverlay::Level level) {
  QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
  QString levelStr = (level == ToastOverlay::Level::Success) ? "INFO" : "WARN";
  QString logMsg = QString("[%1] [%2] %3").arg(timeStr, levelStr, message);

  s_history.prepend(logMsg);
  while (s_history.size() > 100) {
    s_history.removeLast();
  }

  if (m_overlay) {
    m_overlay->showToast(message, level);
  }
}

QString NotificationController::normalizedLabel(const QString &value) {
  const QString trimmed = value.trimmed();
  return trimmed.isEmpty() ? QStringLiteral("-") : trimmed;
}
