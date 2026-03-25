#ifndef NOTIFICATIONCONTROLLER_H
#define NOTIFICATIONCONTROLLER_H

#include <QObject>
#include "presentation/widgets/toastoverlay.h"

class QWidget;

class NotificationController : public QObject {
  Q_OBJECT

public:
  explicit NotificationController(QWidget *hostWidget, QObject *parent = nullptr);

  static QStringList getHistory();

  void showRoiCreated(const QString &name);
  void showRoiDeleted(const QString &name);
  void showVehicleEntered(const QString &zoneName);
  void showVehicleDeparted(const QString &zoneName);

private:
  void showToast(const QString &message, ToastOverlay::Level level);
  static QString normalizedLabel(const QString &value);

  ToastOverlay *m_overlay = nullptr;
  static QStringList s_history;
};

#endif // NOTIFICATIONCONTROLLER_H
