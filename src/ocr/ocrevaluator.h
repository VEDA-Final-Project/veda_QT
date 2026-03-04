#ifndef OCREVALUATOR_H
#define OCREVALUATOR_H

#include <QImage>
#include <QList>
#include <QObject>
#include <QString>

class OcrManager;

class OcrEvaluator : public QObject {
  Q_OBJECT

public:
  explicit OcrEvaluator(OcrManager *ocrManager, QObject *parent = nullptr);

  // 수집된 이미지 리스트와 정답 텍스트를 이용해 벤치마크 수행
  void runBenchmark(const QList<QImage> &crops, const QString &truth);
signals:
  void progressUpdated(int current, int total, const QString &msg);

private:
  OcrManager *m_ocrManager;

  // 레벤슈타인 거리(편집 거리) 계산 보조 함수
  int calculateCER(const QString &truth, const QString &pred) const;
};

#endif // OCREVALUATOR_H
