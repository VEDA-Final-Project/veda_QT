#ifndef CHANNELRUNTIMECONTROLLER_H
#define CHANNELRUNTIMECONTROLLER_H

#include <QObject>
#include <functional>

class CameraChannelRuntime;
class QCheckBox;
class QLabel;
class QSpinBox;
class VideoWidget;
class QFrame;
class QEvent;

class ChannelRuntimeController : public QObject {
  Q_OBJECT

public:
  struct UiRefs {
    VideoWidget *videoWidgets[4] = {nullptr, nullptr, nullptr, nullptr};
    VideoWidget *recordVideoWidget = nullptr;
    QFrame *channelCards[4] = {nullptr, nullptr, nullptr, nullptr};
    QLabel *channelNameLabels[4] = {nullptr, nullptr, nullptr, nullptr};
    QLabel *thumbnailLabels[4] = {nullptr, nullptr, nullptr, nullptr};
    QSpinBox *staleTimeoutInput = nullptr;
    QSpinBox *pruneTimeoutInput = nullptr;
    QCheckBox *chkShowStaleObjects = nullptr;
    QCheckBox *chkShowFps = nullptr;
    QLabel *avgFpsLabel = nullptr;
  };

  struct Context {
    std::function<bool(QObject *)> handleMousePress;
    std::function<void()> onRecordPreviewResize;
  };

  explicit ChannelRuntimeController(const UiRefs &uiRefs, Context context,
                                    QObject *parent = nullptr);

  void connectSignals();
  void shutdown();
  void setReidPanelActiveForAll(bool active);
  void resetAllChannelZoom();
  CameraChannelRuntime *channelAt(int index) const;
  CameraChannelRuntime *channelForCardIndex(int cardIndex) const;
  int cardIndexForVideoWidget(const VideoWidget *videoWidget) const;

signals:
  void primaryVideoReady();
  void zoneStateChanged();

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
  void onVideoWidgetResizedSlot();

private:
  UiRefs m_ui;
  Context m_context;
  CameraChannelRuntime *m_channels[4] = {nullptr, nullptr, nullptr, nullptr};
  class QTimer *m_resizeDebounceTimer = nullptr;
  bool m_signalsConnected = false;
};

#endif // CHANNELRUNTIMECONTROLLER_H
