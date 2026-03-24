#ifndef TOASTOVERLAY_H
#define TOASTOVERLAY_H

#include <QString>
#include <QWidget>

class QEvent;
class QVBoxLayout;

class ToastOverlay : public QWidget {
  Q_OBJECT

public:
  enum class Level { Success, Warning };

  explicit ToastOverlay(QWidget *hostWidget);

  void showToast(const QString &message, Level level);

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void syncToHost();
  void dismissToast(QWidget *toast);

  QWidget *m_hostWidget = nullptr;
  QVBoxLayout *m_layout = nullptr;
};

#endif // TOASTOVERLAY_H
