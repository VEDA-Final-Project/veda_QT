#ifndef LOGINPAGE_H
#define LOGINPAGE_H

#include <QWidget>

class QCheckBox;
class QCloseEvent;
class QLabel;
class QLineEdit;

class LoginPage : public QWidget {
  Q_OBJECT

public:
  explicit LoginPage(QWidget *parent = nullptr);

signals:
  void loginSucceeded();
  void loginClosed();

protected:
  void closeEvent(QCloseEvent *event) override;

private slots:
  void handleLogin();

private:
  void buildUi();

  QLineEdit *idInput_ = nullptr;
  QLineEdit *passwordInput_ = nullptr;
  QCheckBox *rememberPasswordCheck_ = nullptr;
  QCheckBox *autoLoginCheck_ = nullptr;
  QLabel *loginStatusLabel_ = nullptr;
  bool loginSucceeded_ = false;
};

#endif // LOGINPAGE_H
