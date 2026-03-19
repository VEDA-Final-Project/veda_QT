#include "ocr/plateocrcoordinator.h"
#include "config/config.h"
#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>

namespace {
constexpr int kStabilizationWindow = 5;
constexpr int kMinimumStableCount = 2;
constexpr qint64 kHistoryTtlMs = 15000;
constexpr int kRuntimeLogInterval = 10;

const QRegularExpression
    kPlatePattern(QStringLiteral("^(?:\\d{2}|\\d{3})[가-힣]\\d{4}$"));

bool isDigitOnlyPlateCandidate(const QString &text) {
  if (text.isEmpty()) {
    return false;
  }

  for (const QChar ch : text) {
    if (!ch.isDigit()) {
      return false;
    }
  }

  return text.size() == 7 || text.size() == 8;
}

bool extractPlateFormat(const QString &text, int *hangulIndexOut,
                        QChar *hangulOut) {
  if (!hangulIndexOut || !hangulOut || !kPlatePattern.match(text).hasMatch()) {
    return false;
  }

  for (int i = 0; i < text.size(); ++i) {
    if (text.at(i).isDigit()) {
      continue;
    }

    *hangulIndexOut = i;
    *hangulOut = text.at(i);
    return true;
  }

  return false;
}

QString restoreHangulFromTemplate(const QString &digitOnly,
                                  const QString &templatePlate) {
  int hangulIndex = -1;
  QChar hangul;
  if (!extractPlateFormat(templatePlate, &hangulIndex, &hangul)) {
    return QString();
  }

  QString withoutHangul = templatePlate;
  withoutHangul.remove(hangulIndex, 1);
  if (digitOnly == withoutHangul) {
    QString restored = digitOnly;
    restored.insert(hangulIndex, hangul);
    return restored;
  }

  if (digitOnly.size() != templatePlate.size() ||
      hangulIndex >= digitOnly.size()) {
    return QString();
  }

  if (digitOnly.left(hangulIndex) != templatePlate.left(hangulIndex)) {
    return QString();
  }

  if (digitOnly.mid(hangulIndex + 1) != templatePlate.mid(hangulIndex + 1)) {
    return QString();
  }

  QString restored = digitOnly;
  restored[hangulIndex] = hangul;
  return restored;
}

} // namespace

PlateOcrCoordinator::PlateOcrCoordinator(QObject *parent) : QObject(parent) {
  qRegisterMetaType<OcrFullResult>("OcrFullResult");
  const auto &cfg = Config::instance();
  for (size_t i = 0; i < m_workers.size(); ++i) {
    WorkerState &worker = m_workers[i];
    if (!worker.ocrManager.init()) {
      qDebug() << "Could not initialize OCR Manager for worker" << i;
    }


    connect(&worker.watcher, &QFutureWatcher<OcrFullResult>::finished, this,
            [this, i]() { onWorkerFinished(i); });
  }
}

PlateOcrCoordinator::~PlateOcrCoordinator() {
  m_shuttingDown = true;
  m_pending.clear();
  m_inflightObjectIds.clear();
  m_pendingObjectIds.clear();
  m_histories.clear();
  for (WorkerState &worker : m_workers) {
    if (worker.watcher.isRunning()) {
      worker.watcher.cancel();
      worker.watcher.waitForFinished();
    }
  }
}

void PlateOcrCoordinator::resetRuntimeState() {
  m_pending.clear();
  m_pendingObjectIds.clear();
  m_histories.clear();

  m_inflightObjectIds.clear();
  for (WorkerState &worker : m_workers) {
    if (!worker.watcher.isRunning()) {
      worker.runningObjectId = -1;
      worker.queuedAtMs = 0;
      worker.startedAtMs = 0;
    } else if (worker.runningObjectId >= 0) {
      m_inflightObjectIds.insert(worker.runningObjectId);
    }
  }
}

void PlateOcrCoordinator::requestOcr(int objectId, const QImage &crop) {
  if (m_shuttingDown) {
    return;
  }

  if (objectId < 0 || crop.isNull()) {
    return;
  }

  if (m_inflightObjectIds.contains(objectId) ||
      m_pendingObjectIds.contains(objectId)) {
    return;
  }

  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();

  // API 요청 최적화 로직
  if (Config::instance().ocrType() == "LLM") {
    OcrHistory &history = m_histories[objectId];
    
    // 1. 이미 인식이 성공적으로 완료된 경우 중단
    if (history.isFinalized) {
      return;
    }

    // 2. 시도 횟수 및 재시도 로직 제어
    if (history.attemptCount >= 2) {
      return;
    }

    if (history.attemptCount == 1) {
      // 패턴 불일치로 인한 재시도 요청인 경우
      if (!history.isRetryScheduled) {
        return;
      }
      // 3초 대기 체크 (다른 각도/프레임 확보를 위해 대기)
      if (nowMs - history.lastFailedMs < 3000) {
        return;
      }
      // 재시도 시작
      history.isRetryScheduled = false;
      qDebug() << "[OCR] Starting retry for objectId=" << objectId << "after 3s wait.";
    }

    history.lastRequestMs = nowMs;
    history.attemptCount++;
    emit ocrStarted(objectId);
  }
  m_pending.enqueue(PendingOcr{objectId, crop, nowMs});

  m_pendingObjectIds.insert(objectId);
  qDebug() << "[OCR][Enter] objectId=" << objectId
           << "pending=" << m_pending.size()
           << "inflight=" << m_inflightObjectIds.size();
  startNext();
}

void PlateOcrCoordinator::onWorkerFinished(const size_t workerIndex) {
  if (m_shuttingDown) {
    return;
  }

  if (workerIndex >= m_workers.size()) {
    return;
  }

  WorkerState &worker = m_workers[workerIndex];
  const OcrFullResult result = worker.watcher.result();
  const int objectId = worker.runningObjectId;
  const qint64 finishedAtMs = QDateTime::currentMSecsSinceEpoch();
  const qint64 processMs =
      (worker.startedAtMs > 0 && finishedAtMs > worker.startedAtMs)
          ? (finishedAtMs - worker.startedAtMs)
          : 0;
  const qint64 endToEndMs =
      (worker.queuedAtMs > 0 && finishedAtMs > worker.queuedAtMs)
          ? (finishedAtMs - worker.queuedAtMs)
          : processMs;
  const double speedHz =
      (processMs > 0) ? (1000.0 / static_cast<double>(processMs)) : 0.0;

  qDebug() << "[OCR][Done] objectId=" << objectId
           << "filtered=" << result.filtered << "raw=" << result.raw
           << "processMs=" << processMs << "endToEndMs=" << endToEndMs;

  m_inflightObjectIds.remove(objectId);
  worker.runningObjectId = -1;
  worker.queuedAtMs = 0;
  worker.startedAtMs = 0;

  const bool isLlmMode = (Config::instance().ocrType() == "LLM");

  if (!result.filtered.isEmpty()) {
    const bool isPatternMatch = kPlatePattern.match(result.filtered).hasMatch();

    QString finalizedText;
    if (isLlmMode && isPatternMatch) {
      // LLM 모드에서는 유효한 패턴이 나오면 즉시 확정 (API 비용 절감 및 신뢰도 우선)
      finalizedText = result.filtered;
      m_histories[objectId].isFinalized = true;
      m_histories[objectId].lastEmitted = finalizedText;
      qDebug() << "[OCR] LLM recognition finalized immediately for objectId=" << objectId << "text=" << finalizedText;
    } else {
      // 그 외(Paddle 모드 또는 패턴 불일치)에는 기존 안정화 로직 사용
      finalizedText = stabilizeResult(objectId, result.filtered);
      
      // LLM 모드에서 안정화 로직을 통과한 경우에도 확정 처리
      if (isLlmMode && !finalizedText.isEmpty()) {
        m_histories[objectId].isFinalized = true;
      }
    }

    if (!finalizedText.isEmpty()) {
      OcrFullResult finalRes = result;
      finalRes.filtered = finalizedText;
      emit ocrReady(objectId, finalRes);
    } else {
      qDebug() << "[OCR] Result filtered out by stabilization or pattern check. filtered="
               << result.filtered;
    }
  } else if (!result.raw.isEmpty()) {
    qDebug() << "[OCR] Raw result exists but filtered is empty. raw="
             << result.raw;
    if (m_emitPartialResults) {
      emit ocrReady(objectId, result);
    }
  }

  // LLM 모드 재시도 로직: 1회차 실패 시 (패턴 불일치 혹은 비어있음) 3초 후 재시도 예약
  if (isLlmMode && m_histories.contains(objectId)) {
    OcrHistory &history = m_histories[objectId];
    if (history.attemptCount == 1 && !history.isFinalized && !history.isRetryScheduled) {
      history.isRetryScheduled = true;
      history.lastFailedMs = finishedAtMs;
      history.lastUpdatedMs = finishedAtMs; // 히스토리 TTL 연장
      qDebug() << "[OCR] Failed to get valid pattern. Scheduling retry in 3s for objectId=" << objectId;
    }
  }

  startNext();
}

QString PlateOcrCoordinator::stabilizeResult(int objectId,
                                             const QString &result) {
  if (!m_stabilizationEnabled) {
    return result;
  }
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  pruneHistory(nowMs);

  OcrHistory &history = m_histories[objectId];
  const bool shouldLogThisFrame =
      ((++history.logFrameCount % kRuntimeLogInterval) == 0);
  const QString stabilizedInput = restoreDigitOnlyResult(history, result);
  if (shouldLogThisFrame && stabilizedInput != result) {
    qDebug() << "[OCR] Digit-only result restored from history. objectId="
             << objectId << "raw=" << result << "restored=" << stabilizedInput;
  }

  if (!kPlatePattern.match(stabilizedInput).hasMatch() &&
      !isDigitOnlyPlateCandidate(stabilizedInput)) {
    if (shouldLogThisFrame) {
      qDebug() << "[OCR] Skipping non-matching/non-digit OCR result. objectId="
               << objectId << "raw=" << result
               << "candidate=" << stabilizedInput;
    }
    history.lastUpdatedMs = nowMs;
    return QString();
  }

  history.lastUpdatedMs = nowMs;
  history.recentResults.enqueue(stabilizedInput);
  while (history.recentResults.size() > kStabilizationWindow) {
    history.recentResults.dequeue();
  }

  QHash<QString, int> counts;
  QHash<QString, int> lastIndexByText;
  for (int i = 0; i < history.recentResults.size(); ++i) {
    const QString &text = history.recentResults[i];
    counts[text] += 1;
    lastIndexByText[text] = i;
  }

  QString bestText;
  int bestCount = -1;
  int bestLastIndex = -1;
  for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
    const QString &text = it.key();
    const int count = it.value();
    const int lastIndex = lastIndexByText.value(text, -1);
    if (count > bestCount ||
        (count == bestCount && lastIndex > bestLastIndex)) {
      bestText = text;
      bestCount = count;
      bestLastIndex = lastIndex;
    }
  }

  if (bestText.isEmpty()) {
    return QString();
  }

  if (bestCount < kMinimumStableCount) {
    return QString();
  }

  if (history.lastEmitted == bestText) {
    return QString();
  }

  history.lastEmitted = bestText;
  return bestText;
}

QString
PlateOcrCoordinator::restoreDigitOnlyResult(const OcrHistory &history,
                                            const QString &result) const {
  if (!isDigitOnlyPlateCandidate(result)) {
    return result;
  }

  for (int i = history.recentResults.size() - 1; i >= 0; --i) {
    const QString restored =
        restoreHangulFromTemplate(result, history.recentResults.at(i));
    if (!restored.isEmpty()) {
      return restored;
    }
  }

  if (!history.lastEmitted.isEmpty()) {
    const QString restored =
        restoreHangulFromTemplate(result, history.lastEmitted);
    if (!restored.isEmpty()) {
      return restored;
    }
  }

  return result;
}

void PlateOcrCoordinator::pruneHistory(const qint64 nowMs) {
  for (auto it = m_histories.begin(); it != m_histories.end();) {
    if ((nowMs - it.value().lastUpdatedMs) > kHistoryTtlMs) {
      it = m_histories.erase(it);
    } else {
      ++it;
    }
  }
}

void PlateOcrCoordinator::startNext() {
  if (m_shuttingDown) {
    return;
  }

  for (WorkerState &worker : m_workers) {
    if (m_pending.isEmpty()) {
      return;
    }
    if (worker.watcher.isRunning()) {
      continue;
    }

    const PendingOcr next = m_pending.dequeue();
    m_pendingObjectIds.remove(next.objectId);
    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();
    const qint64 queueWaitMs =
        (next.enqueuedAtMs > 0 && startMs > next.enqueuedAtMs)
            ? (startMs - next.enqueuedAtMs)
            : 0;
    worker.runningObjectId = next.objectId;
    worker.queuedAtMs = next.enqueuedAtMs;
    worker.startedAtMs = startMs;
    m_inflightObjectIds.insert(next.objectId);
    WorkerState *workerPtr = &worker;

    qDebug() << "[OCR][Start] objectId=" << next.objectId
             << "queueWaitMs=" << queueWaitMs << "pending=" << m_pending.size();

    QFuture<OcrFullResult> future = QtConcurrent::run(
        [workerPtr, objectId = next.objectId, crop = next.crop]() {
          return workerPtr->ocrManager.performOcr(crop, objectId);
        });
    worker.watcher.setFuture(future);
  }
}
