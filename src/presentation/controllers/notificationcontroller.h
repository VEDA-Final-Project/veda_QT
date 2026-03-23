#ifndef NOTIFICATIONCONTROLLER_H
#define NOTIFICATIONCONTROLLER_H

#include <QObject>
#include <QString>

class ToastOverlayWidget;

class NotificationController : public QObject {
  Q_OBJECT

public:
  struct UiRefs {
    ToastOverlayWidget *toastOverlay = nullptr;
  };

  explicit NotificationController(const UiRefs &uiRefs,
                                  QObject *parent = nullptr);

  void showRoiCreated(int cardIndex, const QString &zoneName);
  void showRoiDeleted(int cardIndex, const QString &zoneName);
  void showVehicleEntered(int cardIndex, const QString &zoneName);
  void showVehicleDeparted(int cardIndex, const QString &zoneName);

private:
  bool shouldSuppressDuplicate(const QString &key);
  QString channelLabel(int cardIndex) const;
  QString zoneLabel(const QString &zoneName) const;
  void showToast(const QString &dedupeKey, const QString &title,
                 const QString &body);

  UiRefs m_ui;
};

#endif // NOTIFICATIONCONTROLLER_H
