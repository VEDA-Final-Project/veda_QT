#ifndef CCTVSPLASHPAGEVIEW_H
#define CCTVSPLASHPAGEVIEW_H

#include "presentation/shell/mainwindowuirefs.h"
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
