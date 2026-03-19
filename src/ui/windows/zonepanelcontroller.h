#ifndef ZONEPANELCONTROLLER_H
#define ZONEPANELCONTROLLER_H

#include <QJsonObject>
#include <QObject>
#include <QVector>
#include <functional>

class QPushButton;
class QTableWidget;

class ZonePanelController : public QObject {
  Q_OBJECT

public:
  struct UiRefs {
    QTableWidget *zoneTable = nullptr;
    QPushButton *btnRefreshZone = nullptr;
  };

  struct Context {
    std::function<QVector<QJsonObject>()> allZoneRecordsProvider;
    std::function<void(const QString &)> logMessage;
  };

  explicit ZonePanelController(const UiRefs &uiRefs, Context context,
                               QObject *parent = nullptr);

  void connectSignals();

public slots:
  void refreshZoneTable();

private:
  void appendLog(const QString &message) const;

  UiRefs m_ui;
  Context m_context;
  bool m_signalsConnected = false;
};

#endif // ZONEPANELCONTROLLER_H
