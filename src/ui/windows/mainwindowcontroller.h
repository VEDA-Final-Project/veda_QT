#ifndef MAINWINDOWCONTROLLER_H
#define MAINWINDOWCONTROLLER_H

#include "logging/logdeduplicator.h"
#include "mainwindowuirefs.h"
#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QPolygon>
#include <QRect>
#include <QSet>
#include <QSize>
#include <array>

#include "telegram/telegrambotapi.h"

class CameraChannelRuntime;
class DbPanelController;
class ParkingService;
class RoiService;
class RpiPanelController;
class VideoWidget;
class QEvent;
class QTimer;

class MainWindowController : public QObject {
  Q_OBJECT

public:
  explicit MainWindowController(const MainWindowUiRefs &uiRefs,
                                QObject *parent = nullptr);
  void shutdown();
  void startInitialCctv();

signals:
  void primaryVideoReady();

public slots:
  bool eventFilter(QObject *obj, QEvent *event) override;
  void onVideoWidgetResizedSlot();

  void connectSignals();
  void initRoiDb();
  void appendRoiStructuredLog(const QJsonObject &roiData);
  void updateObjectFilter(const QSet<QString> &disabledTypes);
  void onLogMessage(const QString &msg);
  void onStartRoiDraw();
  void onCompleteRoiDraw();
  void onDeleteSelectedRoi();
  void onRoiChanged(const QRect &roi);
  void onRoiPolygonChanged(const QPolygon &polygon, const QSize &frameSize);
  void onRoiTargetChanged(int index);
  void onChannelCardClicked(int index);
  void onReidTableCellClicked(int row, int column);

  void onSendEntry();
  void onSendExit();
  void onTelegramLog(const QString &msg);
  void onUsersUpdated(int count);
  void onPaymentConfirmed(const QString &plate, int amount);
  void onAdminSummoned(const QString &chatId, const QString &name);

private:
  enum class RoiTarget { Primary = 0, Secondary = 1 };

  void initChannelCards();
  void initRoiDbForChannels();
  void reloadRoiForTarget(RoiTarget target, bool writeLog = true);
  void refreshRoiSelectorForTarget();
  void refreshZoneTableAllChannels();
  void updateChannelCardSelection();
  void updateThumbnailForCard(int cardIndex, const QImage &image);
  CameraChannelRuntime *channelAt(int index) const;
  VideoWidget *videoWidgetForTarget(RoiTarget target) const;
  RoiService *roiServiceForTarget(RoiTarget target);
  const RoiService *roiServiceForTarget(RoiTarget target) const;
  ParkingService *parkingServiceForTarget(RoiTarget target);
  QString cameraKeyForTarget(RoiTarget target) const;

  MainWindowUiRefs m_ui;
  RoiTarget m_roiTarget = RoiTarget::Primary;
  TelegramBotAPI *m_telegramApi = nullptr;
  RpiPanelController *m_rpiPanelController = nullptr;
  DbPanelController *m_dbPanelController = nullptr;
  std::array<CameraChannelRuntime *, 2> m_channels{{nullptr, nullptr}};
  LogDeduplicator m_logDeduplicator;
  QElapsedTimer m_renderTimerThumbs[4];
  QTimer *m_resizeDebounceTimer = nullptr;
};

#endif // MAINWINDOWCONTROLLER_H
