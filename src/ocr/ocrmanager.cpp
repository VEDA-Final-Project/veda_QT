#include "ocrmanager.h"
#include "config/config.h"
#include <QDebug>
#include <QElapsedTimer>

OcrManager::OcrManager() = default;

OcrManager::~OcrManager() {
  delete m_llmRunner;
}

bool OcrManager::init() {
  // 항상 LLM 러너를 초기화함 (다른 로컬 모델 경로는 무시)
  if (!m_llmRunner) {
    m_llmRunner = new ocr::recognition::LlmOcrRunner();
  }
  qDebug() << "[OCR] Initialized with LLM mode (Gemini/Cloud)";
  return true;
}

OcrFullResult OcrManager::performOcr(const QImage &image, const int objectId) {
  QElapsedTimer timer;
  timer.start();

  OcrFullResult out;
  if (!m_llmRunner) {
    qWarning() << "[OCR] LlmOcrRunner not initialized!";
    return out;
  }

  // LLM 모드: 전처리 없이 원본 크롭(AABB+padding) 이미지 그대로 전달
  OcrResult res = m_llmRunner->runSingleCandidate(image, objectId);

  out.raw = res.selectedRawText;
  out.filtered = res.text;
  out.latencyMs = static_cast<int>(timer.elapsed());

  if (out.latencyMs > 0) {
    qDebug() << "[OCR][Done] objectId=" << objectId
             << "result=" << out.filtered
             << "latency=" << out.latencyMs << "ms";
  }

  return out;
}
