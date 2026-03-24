#ifndef CCTVCONTROLLER_H
#define CCTVCONTROLLER_H

#include <QJsonObject>
#include <QObject>
#include <QPolygon>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>
#include <functional>

class CameraChannelRuntime;
class CameraSource;
class QComboBox;
class QFrame;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class RoiService;
class VideoWidget;

class CctvController : public QObject {
  Q_OBJECT

public:
  struct UiRefs {
    VideoWidget *videoWidgets[4] = {nullptr, nullptr, nullptr, nullptr};
    QFrame *channelCards[4] = {nullptr, nullptr, nullptr, nullptr};
    QLabel *channelStatusDots[4] = {nullptr, nullptr, nullptr, nullptr};
    QLabel *channelNameLabels[4] = {nullptr, nullptr, nullptr, nullptr};
    QLabel *thumbnailLabels[4] = {nullptr, nullptr, nullptr, nullptr};
    QGridLayout *videoGridLayout = nullptr;
    QComboBox *roiTargetCombo = nullptr;
    QLineEdit *roiNameEdit = nullptr;
    QComboBox *roiSelectorCombo = nullptr;
    QPushButton *btnApplyRoi = nullptr;
    QPushButton *btnFinishRoi = nullptr;
    QPushButton *btnDeleteRoi = nullptr;
  };

  struct Context {
    std::function<void(const QString &)> logMessage;
    std::function<void()> refreshZoneTable;
    std::function<void()> refreshParkingLogs;
    std::function<void(const QJsonObject &)> appendRoiStructuredLog;
    std::function<void(const QString &)> notifyRoiCreated;
    std::function<void(const QString &)> notifyRoiDeleted;
    std::function<CameraChannelRuntime *(int)> channelAt;
    std::function<CameraChannelRuntime *(int)> channelForCardIndex;
    std::function<CameraSource *(int)> sourceAt;
    std::function<int(const VideoWidget *)> cardIndexForVideoWidget;
  };

  explicit CctvController(const UiRefs &uiRefs, Context context,
                          QObject *parent = nullptr);

  void initializeViewState();
  void connectSignals();
  void startInitialCctv();
  void onSystemConfigChanged();
  bool handleMousePress(QObject *obj);
  void updateChannelCardSelection();
  void refreshRoiSelectorForTarget();

  int currentRoiTargetIndex() const;
  int selectedChannelIndex() const;
  int selectedChannelCount() const;
  int primarySelectedChannelIndex() const;
  void selectSingleChannel(int index);

private slots:
  void onStartRoiDraw();
  void onCompleteRoiDraw();
  void onDeleteSelectedRoi();
  void onRoiChanged(const QRect &roi);
  void onRoiPolygonChanged(const QPolygon &polygon, const QSize &frameSize);
  void onRoiTargetChanged(int index);
  void onChannelCardClicked(int index);

private:
  enum class LiveLayoutMode { Single, Dual, Quad };

  void initChannelCards();
  void ensureChannelSelected(int index);
  void rebuildLiveLayout();
  void applyLiveGridLayout(LiveLayoutMode mode);
  bool isChannelSelected(int index) const;
  LiveLayoutMode liveLayoutMode() const;
  VideoWidget *videoWidgetForTarget(int targetIndex) const;
  RoiService *roiServiceForTarget(int targetIndex) const;
  QString roiTargetLabel(int targetIndex) const;
  void appendLog(const QString &message) const;

  UiRefs m_ui;
  Context m_context;
  int m_roiTargetIndex = 0;
  int m_selectedChannelIndex = 0;
  QVector<int> m_selectedChannelIndices;
  bool m_signalsConnected = false;
};

#endif // CCTVCONTROLLER_H
