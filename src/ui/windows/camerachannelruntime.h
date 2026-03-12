#ifndef CAMERACHANNELRUNTIME_H
#define CAMERACHANNELRUNTIME_H

#include "metadata/metadatathread.h"
#include "parking/vehicletracker.h"
#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QString>
#include <QVector>

class CameraSource;
class ParkingService;
class RoiService;
class VideoWidget;
class QCheckBox;
class QLabel;
class QSpinBox;
class QTableWidget;

class CameraChannelRuntime : public QObject {
  Q_OBJECT

public:
  enum class Slot { Ch1 = 0, Ch2 = 1, Ch3 = 2, Ch4 = 3 };

  struct SharedUiRefs {
    QTableWidget *reidTable = nullptr;
    QSpinBox *staleTimeoutInput = nullptr;
    QSpinBox *pruneTimeoutInput = nullptr;
    QCheckBox *chkShowStaleObjects = nullptr;
    QLabel *avgFpsLabel = nullptr;
  };

  CameraChannelRuntime(Slot slot, const QString &channelLabel,
                       VideoWidget *videoWidget, const SharedUiRefs &sharedUi,
                       QObject *parent = nullptr);

  void connectSignals();
  void shutdown();

  bool activate(CameraSource *source, int cardIndex);
  void deactivate();
  void selectCardWithoutStream(int cardIndex);
  void handleResizeProfileChange();
  void updateObjectFilter(const QSet<QString> &disabledTypes);
  void setShowFps(bool show);
  bool reloadRoi(bool writeLog = true);
  void syncEnabledRoiPolygons();
  void setReidPanelActive(bool active);

  int selectedCardIndex() const;
  QString cameraKey() const;
  QString channelLabel() const;
  QString displayProfile() const;
  QString ocrProfile() const;
  VideoWidget *videoWidget() const;
  bool isRunning() const;
  ParkingService *parkingService();
  const ParkingService *parkingService() const;
  RoiService *roiService();
  const RoiService *roiService() const;
  const QVector<QJsonObject> &roiRecords() const;
  QList<VehicleState> activeVehicles() const;
  CameraSource *source() const;

signals:
  void logMessage(const QString &msg);
  void videoReady();
  void zoneStateChanged();

private slots:
  void onSourceDisplayFrameReady(const QImage &image,
                                 const QList<ObjectInfo> &objects);
  void onSourceRoiDataChanged();
  void onSourceVideoReady();

private:
  static void populateReidTable(QTableWidget *table,
                                const QList<VehicleState> &vehicleStates,
                                int staleTimeoutMs, bool showStaleObjects);
  void bindSource(CameraSource *source);
  void applyRoiDataToWidget();
  void refreshReidTable();
  void clearWidgetState();
  int slotId() const;

  Slot m_slot;
  QString m_channelLabel;
  VideoWidget *m_videoWidget = nullptr;
  SharedUiRefs m_sharedUi;
  CameraSource *m_source = nullptr;
  int m_selectedCardIndex = -1;
  bool m_signalsConnected = false;
  bool m_videoReadyNotified = false;
  bool m_reidPanelActive = false;
  QElapsedTimer m_renderTimer;
  QElapsedTimer m_reidTimer;
};

#endif // CAMERACHANNELRUNTIME_H
