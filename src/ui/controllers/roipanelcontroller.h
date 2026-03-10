#ifndef ROIPANELCONTROLLER_H
#define ROIPANELCONTROLLER_H

#include <QJsonObject>
#include <QObject>
#include <QPolygon>
#include <QRect>
#include <QSize>
#include <functional>

class CameraChannelRuntime;
class QComboBox;
class QLineEdit;
class QPushButton;
class QTextEdit;
class RoiService;
class VideoWidget;

class RoiPanelController : public QObject {
  Q_OBJECT

public:
  struct UiRefs {
    QComboBox *roiTargetCombo = nullptr;
    QLineEdit *roiNameEdit = nullptr;
    QComboBox *roiSelectorCombo = nullptr;
    QTextEdit *logView = nullptr;
    QPushButton *btnApplyRoi = nullptr;
    QPushButton *btnFinishRoi = nullptr;
    QPushButton *btnDeleteRoi = nullptr;
    VideoWidget *videoWidgetPrimary = nullptr;
    VideoWidget *videoWidgetSecondary = nullptr;
  };

  struct Context {
    std::function<CameraChannelRuntime *(int)> channelAt;
    std::function<void()> refreshZoneTable;
  };

  explicit RoiPanelController(const UiRefs &ui, Context ctx,
                              QObject *parent = nullptr);
  void connectSignals();
  void refreshSelector();
  int roiTarget() const;

public slots:
  void onStartRoiDraw();
  void onCompleteRoiDraw();
  void onDeleteSelectedRoi();
  void onRoiChanged(const QRect &roi);
  void onRoiPolygonChanged(const QPolygon &polygon, const QSize &frameSize);
  void onRoiTargetChanged(int index);

signals:
  void roiTargetChanged(int target);
  void logMessage(const QString &msg);

private:
  VideoWidget *videoWidgetForTarget() const;
  RoiService *roiServiceForTarget() const;
  RoiService *roiServiceAt(int target) const;
  void appendStructuredLog(const QJsonObject &roiData);

  UiRefs m_ui;
  Context m_ctx;
  int m_roiTarget = 0;
};

#endif // ROIPANELCONTROLLER_H
