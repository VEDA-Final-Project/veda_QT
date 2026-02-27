#include "ocr/platerecognizer.h"

#include <QDebug>
#include <QFile>
#include <QTextStream>

// ONNX Runtime 헤더는 .cpp에서만 포함
#include <onnxruntime_cxx_api.h>

// ─────────────────────────────────────────────────────
// 생성자 / 소멸자
// ─────────────────────────────────────────────────────
PlateRecognizer::PlateRecognizer() = default;
PlateRecognizer::~PlateRecognizer() = default;

// ─────────────────────────────────────────────────────
// load(): 모델 + 사전 로드
// ─────────────────────────────────────────────────────
bool PlateRecognizer::load(const QString &modelPath, const QString &dictPath) {
  m_loaded = false;

  // 1) dict.txt 로드 (index 0 = blank)
  QFile dictFile(dictPath);
  if (!dictFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qWarning() << "[OCR] dict.txt 열기 실패:" << dictPath;
    return false;
  }
  m_dict.clear();
  m_dict.push_back(""); // index 0 = CTC blank
  QTextStream ts(&dictFile);
  ts.setEncoding(QStringConverter::Utf8);
  while (!ts.atEnd()) {
    QString line = ts.readLine();
    m_dict.push_back(line.toStdString());
  }
  dictFile.close();
  qDebug() << "[OCR] 사전 로드 완료, 문자 수:" << m_dict.size();

  // 2) ONNX Runtime 환경 + 세션 생성
  try {
    m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "PlateOCR");

    Ort::SessionOptions opts;
    opts.SetIntraOpNumThreads(2);
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    // Windows: wstring 경로 필요
    const std::wstring wpath = modelPath.toStdWString();
    m_session = std::make_unique<Ort::Session>(*m_env, wpath.c_str(), opts);

    qDebug() << "[OCR] 모델 로드 완료:" << modelPath;
    m_loaded = true;
    return true;
  } catch (const Ort::Exception &e) {
    qWarning() << "[OCR] 모델 로드 실패:" << e.what();
    return false;
  }
}

// ─────────────────────────────────────────────────────
// recognize(): 프레임 → crop → 추론 → 텍스트
// ─────────────────────────────────────────────────────
QString PlateRecognizer::recognize(const cv::Mat &frame, const QRectF &bbox,
                                   bool normalized) {
  if (!m_loaded || frame.empty())
    return {};

  // 1) bbox → 픽셀 좌표로 변환
  int x, y, w, h;
  if (normalized) {
    // Wise AI는 0~1000 정규화 좌표를 쓰는 경우가 많으므로 범위 감지
    double scale = (bbox.right() <= 1.0 && bbox.bottom() <= 1.0) ? 1.0 : 1000.0;
    x = static_cast<int>(bbox.x() / scale * frame.cols);
    y = static_cast<int>(bbox.y() / scale * frame.rows);
    w = static_cast<int>(bbox.width() / scale * frame.cols);
    h = static_cast<int>(bbox.height() / scale * frame.rows);
  } else {
    x = static_cast<int>(bbox.x());
    y = static_cast<int>(bbox.y());
    w = static_cast<int>(bbox.width());
    h = static_cast<int>(bbox.height());
  }

  // 범위 클램핑
  x = std::max(0, std::min(x, frame.cols - 1));
  y = std::max(0, std::min(y, frame.rows - 1));
  w = std::max(1, std::min(w, frame.cols - x));
  h = std::max(1, std::min(h, frame.rows - y));

  cv::Mat crop = frame(cv::Rect(x, y, w, h));
  if (crop.empty())
    return {};

  // 2) 전처리 → 텐서
  int outWidth = 0;
  std::vector<float> inputData = preprocess(crop, outWidth);

  // 3) 추론
  try {
    Ort::MemoryInfo memInfo =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    std::array<int64_t, 4> inputShape = {1, 3, TARGET_HEIGHT, outWidth};
    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
        memInfo, inputData.data(), inputData.size(), inputShape.data(),
        inputShape.size());

    const char *inputNames[] = {"x"};
    const char *outputNames[] = {"softmax_0.tmp_0"};

    auto outputs = m_session->Run(Ort::RunOptions{nullptr}, inputNames,
                                  &inputTensor, 1, outputNames, 1);

    // output shape: [1, T, vocab_size]
    auto &outTensor = outputs[0];
    auto shape = outTensor.GetTensorTypeAndShapeInfo().GetShape();
    int timeSteps = static_cast<int>(shape[1]);
    int vocabSize = static_cast<int>(shape[2]);

    const float *logits = outTensor.GetTensorMutableData<float>();
    std::vector<float> logitVec(logits, logits + timeSteps * vocabSize);

    // 4) CTC 디코딩
    QString raw = ctcDecode(logitVec, timeSteps, vocabSize);
    QString result = normalizePlate(raw);

    qDebug() << "[OCR] raw:" << raw << "→ normalized:" << result;
    return result;

  } catch (const Ort::Exception &e) {
    qWarning() << "[OCR] 추론 실패:" << e.what();
    return {};
  }
}

// ─────────────────────────────────────────────────────
// preprocess(): crop → 48px 높이 리사이즈 → float CHW 텐서
// ─────────────────────────────────────────────────────
std::vector<float> PlateRecognizer::preprocess(const cv::Mat &plateCrop,
                                               int &outWidth) {
  // 비율 유지하며 TARGET_HEIGHT로 리사이즈
  float ratio = static_cast<float>(TARGET_HEIGHT) / plateCrop.rows;
  int newW = std::max(1, static_cast<int>(plateCrop.cols * ratio));
  outWidth = newW;

  cv::Mat resized;
  cv::resize(plateCrop, resized, cv::Size(newW, TARGET_HEIGHT));

  // BGR → RGB
  cv::Mat rgb;
  cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);

  // float32 변환 + normalize: (pixel/255 - 0.5) / 0.5
  rgb.convertTo(rgb, CV_32FC3, 1.0 / 255.0);

  // CHW 배열로 변환
  std::vector<float> tensor(3 * TARGET_HEIGHT * newW);
  for (int c = 0; c < 3; ++c) {
    for (int r = 0; r < TARGET_HEIGHT; ++r) {
      for (int col = 0; col < newW; ++col) {
        float val = rgb.at<cv::Vec3f>(r, col)[c];
        val = (val - PIXEL_MEAN) / PIXEL_STD;
        tensor[c * TARGET_HEIGHT * newW + r * newW + col] = val;
      }
    }
  }
  return tensor;
}

// ─────────────────────────────────────────────────────
// ctcDecode(): argmax → blank/반복 제거 → 문자열
// ─────────────────────────────────────────────────────
QString PlateRecognizer::ctcDecode(const std::vector<float> &logits,
                                   int timeSteps, int vocabSize) {
  QString result;
  int prevIdx = 0; // blank

  for (int t = 0; t < timeSteps; ++t) {
    const float *row = logits.data() + t * vocabSize;

    // argmax
    int bestIdx = 0;
    float bestVal = row[0];
    for (int v = 1; v < vocabSize; ++v) {
      if (row[v] > bestVal) {
        bestVal = row[v];
        bestIdx = v;
      }
    }

    // CTC 규칙: blank(0)이 아니고, 이전과 다를 때만 추가
    if (bestIdx != 0 && bestIdx != prevIdx) {
      if (bestIdx < static_cast<int>(m_dict.size())) {
        result += QString::fromStdString(m_dict[bestIdx]);
      }
    }
    prevIdx = bestIdx;
  }
  return result;
}

// ─────────────────────────────────────────────────────
// normalizePlate(): 한국 번호판 유효 문자만 남기기
// ─────────────────────────────────────────────────────
QString PlateRecognizer::normalizePlate(const QString &raw) {
  // 허용 한글: 한국 번호판에 사용되는 지역명 + 용도 한글
  // 숫자 + 한글 음절만 남긴다
  QString result;
  for (const QChar &ch : raw) {
    if (ch.isDigit()) {
      result += ch;
    } else if (ch.unicode() >= 0xAC00 && ch.unicode() <= 0xD7A3) {
      // 한글 완성형 음절
      result += ch;
    }
  }
  return result;
}
