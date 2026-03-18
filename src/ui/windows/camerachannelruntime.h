#ifndef CAMERACHANNELRUNTIME_H
#define CAMERACHANNELRUNTIME_H

#include "metadata/metadatathread.h"
#include "parking/vehicletracker.h"
#include "video/sharedvideoframe.h"
#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QObject>
#include <QSet>
#include <QSize>

class CameraSource;
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

  CameraChannelRuntime(Slot slot, VideoWidget *videoWidget,
                       const SharedUiRefs &sharedUi,
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
  VideoWidget *videoWidget() const;

  static void populateReidTable(QTableWidget *table, int channelId,
                                const QList<VehicleState> &vehicleStates,
                                int staleTimeoutMs, bool showStaleObjects);

signals:
  void logMessage(const QString &msg);
  void videoReady();
  void zoneStateChanged();

private slots:
  void onSourceDisplayFrameReady(SharedVideoFrame frame,
                                 const QList<ObjectInfo> &objects);
  void onSourceRoiDataChanged();
  void onSourceVideoReady();

private:

  void bindSource(CameraSource *source);
  void applyRoiDataToWidget();
  void refreshReidTable();
  void clearWidgetState();
  int slotId() const;

  Slot m_slot;
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
