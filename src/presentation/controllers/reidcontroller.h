#ifndef REIDCONTROLLER_H
#define REIDCONTROLLER_H

#include <QElapsedTimer>
#include <QObject>
#include <functional>

class CameraSource;
class QCheckBox;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

class ReidController : public QObject {
  Q_OBJECT

public:
  struct UiRefs {
    QTableWidget *reidTable = nullptr;
    QSpinBox *staleTimeoutInput = nullptr;
    QSpinBox *pruneTimeoutInput = nullptr;
    QCheckBox *chkShowStaleObjects = nullptr;
    QLineEdit *forcePlateInput = nullptr;
    QSpinBox *forceObjectIdInput = nullptr;
    QPushButton *btnForcePlate = nullptr;
  };

  struct Context {
    std::function<CameraSource *(int)> sourceAt;
    std::function<int()> sourceCount;
  };

  explicit ReidController(const UiRefs &uiRefs, Context context,
                          QObject *parent = nullptr);

  void connectSignals();
  void refresh(bool force = false);
  void shutdown();

private slots:
  void onReidTableCellClicked(int row, int column);
  void onRefreshRequested();

private:
  UiRefs m_ui;
  Context m_context;
  QElapsedTimer m_refreshTimer;
  QTimer *m_pollTimer = nullptr;
  bool m_signalsConnected = false;
};

#endif // REIDCONTROLLER_H
