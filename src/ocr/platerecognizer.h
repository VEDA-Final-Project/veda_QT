#ifndef OCR_PLATERECOGNIZER_H
#define OCR_PLATERECOGNIZER_H

#include <QRectF>
#include <QString>
#include <memory>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>


// Forward declare ONNX Runtime types (avoids heavy header in .h)
namespace Ort {
class Env;
class Session;
class SessionOptions;
} // namespace Ort

/**
 * @brief PaddleOCR PP-OCRv5 ONNX 기반 한국어 번호판 텍스트 인식기
 *
 * Wise AI가 제공한 BoundingBox로 프레임을 crop한 뒤,
 * rec.onnx 모델로 CTC 추론하여 번호판 문자열을 반환합니다.
 *
 * Detection은 사용하지 않습니다 (Wise AI가 담당).
 */
class PlateRecognizer {
public:
  PlateRecognizer();
  ~PlateRecognizer();

  /**
   * @brief ONNX 모델과 문자 사전을 로드합니다.
   * @param modelPath  rec.onnx 경로
   * @param dictPath   dict.txt 경로
   * @return 로드 성공 여부
   */
  bool load(const QString &modelPath, const QString &dictPath);

  /** @return 모델이 로드되어 있는지 여부 */
  bool isLoaded() const { return m_loaded; }

  /**
   * @brief 프레임에서 bbox 영역을 crop하여 OCR을 수행합니다.
   * @param frame     전체 비디오 프레임 (BGR, cv::Mat)
   * @param bbox      0~1 정규화 좌표 (QRectF) 또는 픽셀 좌표
   * @param normalized bbox가 0~1 정규화 좌표인지 여부 (기본: true)
   * @return 인식된 번호판 문자열 (실패 시 빈 문자열)
   */
  QString recognize(const cv::Mat &frame, const QRectF &bbox,
                    bool normalized = true);

private:
  // 번호판 이미지 전처리: 고정 높이 48px 리사이즈 후 float 텐서 변환
  std::vector<float> preprocess(const cv::Mat &plateCrop, int &outWidth);

  // CTC 디코딩: argmax per timestep → 반복/blank 제거 → dict 변환
  QString ctcDecode(const std::vector<float> &logits, int timeSteps,
                    int vocabSize);

  // 번호판 후처리: 숫자+허용한글만 남기기
  QString normalizePlate(const QString &raw);

private:
  bool m_loaded = false;
  std::vector<std::string> m_dict; // dict.txt 문자 목록 (index 0 = blank)

  // ONNX Env/Session은 포인터로 관리 (헤더에 onnxruntime 불포함)
  std::unique_ptr<Ort::Env> m_env;
  std::unique_ptr<Ort::Session> m_session;

  static constexpr int TARGET_HEIGHT = 48;
  static constexpr float PIXEL_MEAN = 0.5f;
  static constexpr float PIXEL_STD = 0.5f;
};

#endif // OCR_PLATERECOGNIZER_H
