#ifndef LOGINPAGEVIEW_H
#define LOGINPAGEVIEW_H

class QLabel;
class QLineEdit;
class QPushButton;
class QWidget;

struct LoginPageUiRefs {
  QLabel *leftImageLabel = nullptr;
  QLineEdit *idInput = nullptr;
  QLineEdit *passwordInput = nullptr;
  QLabel *otpHintLabel = nullptr;
  QLineEdit *otpInput = nullptr;
  QPushButton *loginButton = nullptr;
  QPushButton *backButton = nullptr;
  QPushButton *closeButton = nullptr;
  QLabel *loginStatusLabel = nullptr;
};

LoginPageUiRefs buildLoginPageUi(QWidget *page);
void updateLoginPageLeftImage(QLabel *leftImageLabel);

#endif // LOGINPAGEVIEW_H
