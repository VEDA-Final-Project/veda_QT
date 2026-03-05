#ifndef MAINWINDOWCONTROLLER_H
#define MAINWINDOWCONTROLLER_H

#include "camera/camerasessionservice.h"
#include "logging/logdeduplicator.h"
#include "mainwindowuirefs.h"
#include "ocr/plateocrcoordinator.h"
#include "parking/parkingservice.h"
#include "roi/roiservice.h"
#include <QComboBox>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QPushButton>
#include <QSet>
#include <QTextEdit>

#include "telegram/telegrambotapi.h"

class RpiPanelController;
class DbPanelController;

struct OcrAuditResult {
  QString timestamp;
  int objectId;
  QString truth;
  QString raw;
  QString filtered;
  int latencyMs;
  bool isRawMatch;
  bool isE2EMatch;
  int cer;
};

class MainWindowController : public QObject {
  Q_OBJECT

public:
  explicit MainWindowController(const MainWindowUiRefs &uiRefs,
                                QObject *parent = nullptr);
  void shutdown();

public slots:
  bool eventFilter(QObject *obj, QEvent *event) override;
  void onVideoWidgetResizedSlot();

  void connectSignals();
  void initRoiDb();
  void appendRoiStructuredLog(const QJsonObject &roiData);
  void updateObjectFilter(const QSet<QString> &disabledTypes);
  void onLogMessage(const QString &msg);
  void onOcrResultPrimary(int objectId, const OcrFullResult &result);
  void onOcrResultSecondary(int objectId, const OcrFullResult &result);
  void onRunBenchmark();
  void onStartRoiDraw();
  void onCompleteRoiDraw();   // Renamed from onFinishRoiDraw
  void onDeleteSelectedRoi(); // Renamed from onDeleteRoi
  void onRoiChanged(const QRect &roi);
  void onRoiPolygonChanged(const QPolygon &polygon, const QSize &frameSize);
  void onRoiTargetChanged(int index);
  void onChannelCardClicked(int index);
  void onMetadataReceivedPrimary(const QList<ObjectInfo> &objects);
  void onMetadataReceivedSecondary(const QList<ObjectInfo> &objects);
  void onFrameCapturedPrimary(QSharedPointer<cv::Mat> framePtr,
                              qint64 timestampMs);
  void onFrameCapturedSecondary(QSharedPointer<cv::Mat> framePtr,
                                qint64 timestampMs);
  void onFrameCapturedThumb(int index, QSharedPointer<cv::Mat> framePtr,
                            qint64 timestampMs);
  void onReidTableCellClicked(int row, int column);

  // Telegram Slots
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
  bool refreshCameraConnectionFromConfig(
      CameraManager *cameraManager, const QString &cameraKey,
      QString *resolvedKey = nullptr, const QString &profileSuffix = QString(),
      bool reloadConfig = true);
  QString getBestProfileForSize(const QSize &size) const;
  VideoWidget *videoWidgetForTarget(RoiTarget target) const;
  RoiService *roiServiceForTarget(RoiTarget target);
  const RoiService *roiServiceForTarget(RoiTarget target) const;
  ParkingService *parkingServiceForTarget(RoiTarget target);
  QString cameraKeyForTarget(RoiTarget target) const;
  void generateAuditReport();
  void recordAuditResult(int objectId, const OcrFullResult &result);

  MainWindowUiRefs m_ui;
  RoiTarget m_roiTarget = RoiTarget::Primary;
  int m_selectedChannelIndex = 0;
  int m_secondaryChannelIndex = -1;
  QString m_selectedCameraKeyPrimary = QStringLiteral("camera");
  QString m_selectedCameraKeySecondary = QStringLiteral("camera2");
  CameraManager *m_cameraManagerPrimary = nullptr;
  CameraManager *m_cameraManagerSecondary = nullptr;
  PlateOcrCoordinator *m_ocrCoordinatorPrimary = nullptr;
  PlateOcrCoordinator *m_ocrCoordinatorSecondary = nullptr;
  TelegramBotAPI *m_telegramApi = nullptr;
  RpiPanelController *m_rpiPanelController = nullptr;
  DbPanelController *m_dbPanelController = nullptr;
  CameraSessionService m_cameraSessionPrimary;
  CameraSessionService m_cameraSessionSecondary;
  CameraManager *m_thumbManagers[4] = {nullptr, nullptr, nullptr, nullptr};
  CameraSessionService *m_thumbSessions[4] = {nullptr, nullptr, nullptr,
                                              nullptr};
  QString m_thumbCameraKeys[4];
  RoiService m_roiServicePrimary;
  RoiService m_roiServiceSecondary;
  ParkingService *m_parkingServicePrimary = nullptr;
  ParkingService *m_parkingServiceSecondary = nullptr;
  LogDeduplicator m_logDeduplicator;
  QElapsedTimer m_renderTimerPrimary;
  QElapsedTimer m_renderTimerSecondary;
  QElapsedTimer m_renderTimerThumbs[4];
  QTimer *m_resizeDebounceTimer = nullptr;
  QString m_currentProfilePrimary;
  QString m_currentProfileSecondary;

  bool m_isBenchmarking = false;
  int m_benchmarkTargetCount = 100;
  QString m_benchmarkTruth;
  QList<OcrAuditResult> m_auditResults;

  static int calculateCER(const QString &truth, const QString &pred);
};

#endif // MAINWINDOWCONTROLLER_H
