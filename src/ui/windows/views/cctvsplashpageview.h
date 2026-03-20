#ifndef CCTVSPLASHPAGEVIEW_H
#define CCTVSPLASHPAGEVIEW_H

#include "ui/windows/mainwindowuirefs.h"
#include <QWidget>

class CctvSplashPageView : public QWidget {
  Q_OBJECT

public:
  explicit CctvSplashPageView(QWidget *parent = nullptr);
  const SplashUiRefs &uiRefs() const;

private:
  void setupUi();

  SplashUiRefs m_ui;
};

#endif // CCTVSPLASHPAGEVIEW_H
