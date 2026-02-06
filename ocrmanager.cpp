#include "ocrmanager.h"
#include <QDebug>

OcrManager::OcrManager(QObject *parent) : QObject(parent), m_tessApi(nullptr) {
  m_tessApi = new tesseract::TessBaseAPI();
}

OcrManager::~OcrManager() {
  if (m_tessApi) {
    m_tessApi->End();
    delete m_tessApi;
  }
}

bool OcrManager::init(const char *datapath, const char *language) {
  // 데이터 경로가 null이면 기본 vcpkg 경로 시도
  const char *path = datapath;
  if (!path) {
    path = "C:/vcpkg/installed/x64-windows/share/tessdata/";
  }

  // 언어 데이터 파일 존재 여부 확인 로직은 tessApi->Init 내부에서 처리됨
  // 하지만 "eng.traineddata"가 없으면 실패하므로 사용자에게 안내 필요

  if (m_tessApi->Init(path, language)) {
    qDebug() << "Could not initialize tesseract. Path:" << path
             << "Lang:" << language;
    return false;
  }

  // 화이트리스트 적용: 숫자 + 한국 자동차 번호판에 사용되는 표준 한글
  // 0-9,
  // 가나다라마거너더러머버서어저고노도로모보소오조구누두루무부수우주아바사자하허호
  m_tessApi->SetVariable("tessedit_char_whitelist",
                         "0123456789가나다라마거너더러머버서어저고노도로모보소"
                         "오조구누두루무부수우주아바사자하허호");

  return true;
}

QString OcrManager::performOcr(const QImage &image) {
  if (image.isNull())
    return QString();

  // 1. QImage -> cv::Mat 변환 (Format_RGB888 강제 변환)
  // 입력 이미지가 RGB888이 아닐 경우(예: RGB32) CV_8UC3와 호환되지 않아 메모리
  // 참조 오류 발생 가능
  QImage formattedImage = image.convertToFormat(QImage::Format_RGB888);

  cv::Mat matRoi(formattedImage.height(), formattedImage.width(), CV_8UC3,
                 (uchar *)formattedImage.bits(), formattedImage.bytesPerLine());

  // 2. 전처리 (RGB -> Gray -> Binary)
  // 주의: 입력 이미지는 RGB라고 가정 (QImage::Format_RGB888)
  cv::Mat gray, binary;
  cv::cvtColor(matRoi, gray, cv::COLOR_RGB2GRAY);
  cv::threshold(gray, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

  // 3. OCR 수행
  // Leptonica/Tesseract는 내부적으로 데이터 복사를 수행하므로,
  // binary.data 포인터를 넘기는 것이 thread-safe한지 확인 필요하지만,
  // 여기서는 동기적으로 처리하므로 괜찮음.
  m_tessApi->SetImage(binary.data, binary.cols, binary.rows, 1, binary.step);

  char *outText = m_tessApi->GetUTF8Text();
  QString result = QString::fromUtf8(outText).trimmed();

  delete[] outText;
  return result;
}
