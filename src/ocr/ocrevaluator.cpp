#include "ocrevaluator.h"
#include "ocrmanager.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <algorithm>
#include <vector>

OcrEvaluator::OcrEvaluator(OcrManager *ocrManager, QObject *parent)
    : QObject(parent), m_ocrManager(ocrManager) {}

int OcrEvaluator::calculateCER(const QString &truth,
                               const QString &pred) const {
  int m = truth.length();
  int n = pred.length();
  if (m == 0)
    return n;
  if (n == 0)
    return m;

  std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1, 0));

  for (int i = 0; i <= m; ++i)
    dp[i][0] = i;
  for (int j = 0; j <= n; ++j)
    dp[0][j] = j;

  for (int i = 1; i <= m; ++i) {
    for (int j = 1; j <= n; ++j) {
      int cost = (truth[i - 1] == pred[j - 1]) ? 0 : 1;
      dp[i][j] = std::min({
          dp[i - 1][j] + 1,       // deletion
          dp[i][j - 1] + 1,       // insertion
          dp[i - 1][j - 1] + cost // substitution
      });
    }
  }
  return dp[m][n];
}

void OcrEvaluator::runBenchmark(const QList<QImage> &crops,
                                const QString &truth) {
  if (!m_ocrManager) {
    qWarning() << "OcrEvaluator: OcrManager is null.";
    return;
  }

  if (crops.isEmpty()) {
    qWarning() << "OcrEvaluator: No images provided.";
    return;
  }

  int limit = crops.size();

  struct ResultRec {
    QString fileName;
    QString truth;
    QString rawPred;
    qint64 rawTime;
    int rawDist;
    QString e2ePred;
    qint64 e2eTime;
    int e2eDist;
  };

  std::vector<ResultRec> results;
  results.reserve(limit);

  int totalTruthChars = 0;
  int rawExactMatches = 0;
  int rawTotalDist = 0;
  std::vector<qint64> rawLatencies;

  int e2eExactMatches = 0;
  int e2eTotalDist = 0;
  std::vector<qint64> e2eLatencies;

  qDebug() << "OcrEvaluator: Starting benchmark on" << limit
           << "images with truth:" << truth;

  for (int i = 0; i < limit; ++i) {
    QImage img = crops[i];
    QString frameName = QString("frame_%1").arg(i + 1);

    if (img.isNull()) {
      qWarning() << "OcrEvaluator: Null image at index:" << i;
      continue;
    }

    totalTruthChars += truth.length();

    QElapsedTimer timer;

    // --- Raw OCR ---
    // --- OCR Analysis ---
    OcrFullResult res = m_ocrManager->performOcr(img);
    QString e2ePred = res.filtered;
    qint64 e2eTime = res.latencyMs;

    int e2eDist = calculateCER(truth, e2ePred);
    e2eTotalDist += e2eDist;
    if (e2ePred == truth)
      e2eExactMatches++;
    e2eLatencies.push_back(e2eTime);

    // --- Raw OCR (Optional/Legacy support) ---
    QString rawPred = res.raw;
    qint64 rawTime =
        res.latencyMs; // Rough estimate since they share the same call now

    int rawDist = calculateCER(truth, rawPred);
    rawTotalDist += rawDist;
    if (rawPred == truth)
      rawExactMatches++;
    rawLatencies.push_back(rawTime);

    results.push_back({frameName, truth, rawPred, rawTime, rawDist, e2ePred,
                       e2eTime, e2eDist});

    qDebug() << "Eval [" << (i + 1) << "/" << limit << "] Truth:" << truth
             << "| Raw:" << rawPred << "(" << rawTime << "ms) | E2E:" << e2ePred
             << "(" << e2eTime << "ms)";

    emit progressUpdated(
        i + 1, limit,
        QString("[%1/%2] 분석 중: %3...").arg(i + 1).arg(limit).arg(e2ePred));
  }

  if (results.empty()) {
    qWarning() << "OcrEvaluator: No valid results collected.";
    return;
  }

  // --- Calculate Metrics ---
  auto calcMetrics = [&](std::vector<qint64> &latencies, int exactMatches,
                         int totalDist, double &exactRate, double &cer,
                         double &meanL, qint64 &p50, qint64 &p95) {
    exactRate = (double)exactMatches / results.size() * 100.0;
    cer = totalTruthChars > 0 ? ((double)totalDist / totalTruthChars * 100.0)
                              : 0.0;

    qint64 sumL = 0;
    for (auto l : latencies)
      sumL += l;
    meanL = (double)sumL / latencies.size();

    std::sort(latencies.begin(), latencies.end());
    p50 = latencies[latencies.size() * 0.50];
    p95 = latencies[latencies.size() * 0.95];
  };

  double rawExact, rawCer, rawMeanL;
  qint64 rawP50, rawP95;
  calcMetrics(rawLatencies, rawExactMatches, rawTotalDist, rawExact, rawCer,
              rawMeanL, rawP50, rawP95);

  double e2eExact, e2eCer, e2eMeanL;
  qint64 e2eP50, e2eP95;
  calcMetrics(e2eLatencies, e2eExactMatches, e2eTotalDist, e2eExact, e2eCer,
              e2eMeanL, e2eP50, e2eP95);

  // --- Write to CSV ---
  QString appDir = QCoreApplication::applicationDirPath();
  QString reportPath = appDir + "/ocr_evaluation_report.csv";
  QFile file(reportPath);
  if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8); // Avoid encoding issues

    // Header
    out << "Test Type,Metric,Value\n";

    // Raw Summary
    out << "Raw OCR,Exact Match Rate," << QString::number(rawExact, 'f', 2)
        << " %\n";
    out << "Raw OCR,CER," << QString::number(rawCer, 'f', 2) << " %\n";
    out << "Raw OCR,Latency (Mean; p50; p95),"
        << QString::number(rawMeanL, 'f', 1) << " ; " << rawP50 << " ; "
        << rawP95 << " ms\n";

    // E2E Summary
    out << "E2E OCR,Exact Match Rate," << QString::number(e2eExact, 'f', 2)
        << " %\n";
    out << "E2E OCR,CER," << QString::number(e2eCer, 'f', 2) << " %\n";
    out << "E2E OCR,Latency (Mean; p50; p95),"
        << QString::number(e2eMeanL, 'f', 1) << " ; " << e2eP50 << " ; "
        << e2eP95 << " ms\n";
    out << "\n";

    // Detailed Log Header
    out << "Image File,Ground Truth,Raw Pred,Raw Dist,Raw Time(ms),E2E "
           "Pred,E2E Dist,E2E Time(ms)\n";

    for (const auto &r : results) {
      out << r.fileName << "," << r.truth << "," << r.rawPred << ","
          << r.rawDist << "," << r.rawTime << "," << r.e2ePred << ","
          << r.e2eDist << "," << r.e2eTime << "\n";
    }

    file.close();
    qDebug() << "OcrEvaluator: Report saved to" << reportPath;
  } else {
    qWarning() << "OcrEvaluator: Could not open report file for writing.";
  }
}
