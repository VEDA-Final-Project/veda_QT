#ifndef OCR_POSTPROCESS_PLATEPOSTPROCESSOR_H
#define OCR_POSTPROCESS_PLATEPOSTPROCESSOR_H

#include "ocr/ocrmanager.h"
#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>
#include <vector>

namespace ocr::postprocess
{

  // 한 번의 OCR 실행 결과(원문/정규화/점수/신뢰도)를 묶어 전달한다.
  struct OcrCandidate
  {
    QString sourceTag;
    QString rawText;
    QString normalizedText;
    int score = 0;
    int confidence = -1;
  };

  // 번호판 전체 인식용 whitelist(숫자 + 허용 한글) 문자열.
  const char *plateWhitelist();
  // 한글 복구 단계에서 사용할 한글 전용 whitelist 문자열.
  const char *hangulWhitelist();
  // 기본/보조 PSM(페이지 분할 모드) 반환.
  tesseract::PageSegMode primaryPageSegMode();
  tesseract::PageSegMode secondaryPageSegMode();
  // PSM을 디버그 태그 문자열로 변환(line/word).
  QString psmTag(tesseract::PageSegMode psm);
  // OCR 원문에서 숫자/한글 음절만 남겨 번호판 후보 텍스트로 정규화한다.
  QString normalizePlateText(const QString &raw);
  // 번호판 형태에 가까울수록 높은 점수를 주는 휴리스틱 점수 함수.
  int platePlausibilityScore(const QString &candidate);
  // 다수 OCR 후보 중 최종 결과(또는 dropReason)를 선택한다.
  OcrResult chooseBestPlateResult(const std::vector<OcrCandidate> &candidates);
  // 숫자만 인식된 후보에서 중앙 한글을 별도 탐색해 번호판을 복구한다.
  QString recoverPlateUsingCenterHangul(tesseract::TessBaseAPI *api,
                                        const cv::Mat &binary,
                                        const cv::Mat &binaryInv,
                                        const QString &selectedCandidate);

} // namespace ocr::postprocess

#endif // OCR_POSTPROCESS_PLATEPOSTPROCESSOR_H
