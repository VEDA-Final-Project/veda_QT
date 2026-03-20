#ifndef TELEGRAMPAGEVIEW_H
#define TELEGRAMPAGEVIEW_H

#include "ui/windows/mainwindowuirefs.h"
#include <QWidget>

class TelegramPageView : public QWidget {
  Q_OBJECT

public:
  explicit TelegramPageView(QWidget *parent = nullptr);
  const TelegramUiRefs &uiRefs() const;

private:
  void setupUi();

  TelegramUiRefs m_ui;
};

#endif // TELEGRAMPAGEVIEW_H
