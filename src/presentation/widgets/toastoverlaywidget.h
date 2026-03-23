#ifndef TOASTOVERLAYWIDGET_H
#define TOASTOVERLAYWIDGET_H

#include <QList>
#include <QWidget>

class QFrame;
class QVBoxLayout;

class ToastOverlayWidget : public QWidget {
  Q_OBJECT

public:
  explicit ToastOverlayWidget(QWidget *parent = nullptr);

  void showToast(const QString &title, const QString &body);
  void repositionInParent();

private:
  void removeToast(QFrame *card);
  void finalizeRemoval(QFrame *card);
  void trimOverflow();
  QSize overlaySizeHint() const;

  QVBoxLayout *m_layout = nullptr;
  QList<QFrame *> m_cards;
};

#endif // TOASTOVERLAYWIDGET_H
