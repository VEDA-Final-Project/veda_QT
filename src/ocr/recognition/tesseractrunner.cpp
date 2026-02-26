#include "ocr/recognition/tesseractrunner.h"
#include <limits>

namespace ocr::recognition
{

  postprocess::OcrCandidate runOcrOnBinary(tesseract::TessBaseAPI *api,
                                           const cv::Mat &binary,
                                           const QString &sourceTag,
                                           const tesseract::PageSegMode psm)
  {
    postprocess::OcrCandidate out;
    out.sourceTag = sourceTag;
    // 유효한 후보가 없을 때를 구분하기 위해 매우 낮은 초기 점수를 둔다.
    out.score = std::numeric_limits<int>::min() / 4;

    if (!api || binary.empty())
    {
      return out;
    }

    api->SetPageSegMode(psm);
    // 전처리된 단일 채널 이진 영상을 Tesseract 입력으로 전달한다.
    api->SetImage(binary.data, binary.cols, binary.rows, 1,
                  static_cast<int>(binary.step));

    char *outText = api->GetUTF8Text();
    // 텍스트가 비어도 confidence는 진단에 쓸 수 있어 먼저 읽는다.
    out.confidence = api->MeanTextConf();
    if (!outText)
    {
      return out;
    }

    out.rawText = QString::fromUtf8(outText).trimmed();
    delete[] outText;
    // 후보 비교를 위해 정규화 텍스트와 번호판 유사도 점수를 계산한다.
    out.normalizedText = postprocess::normalizePlateText(out.rawText);
    out.score = postprocess::platePlausibilityScore(out.normalizedText);
    return out;
  }

  std::vector<postprocess::OcrCandidate> collectCandidates(
      tesseract::TessBaseAPI *api, const cv::Mat &binary, const cv::Mat &binaryInv,
      const cv::Mat &adaptiveBinary, const bool adaptiveUsed)
  {
    std::vector<postprocess::OcrCandidate> candidates;
    // adaptive 사용 시 입력 종류가 3개(binary/binary_inv/adaptive)여서 6개 후보.
    candidates.reserve(adaptiveUsed ? 6 : 4);

    auto appendCandidates = [&](const cv::Mat &bin, const QString &baseTag)
    {
      if (bin.empty())
      {
        return;
      }

      const tesseract::PageSegMode modes[] = {
          postprocess::primaryPageSegMode(), postprocess::secondaryPageSegMode()};

      // 동일 입력에 대해 line/word 두 모드를 모두 실행해 후보 다양성을 확보한다.
      for (const tesseract::PageSegMode psm : modes)
      {
        const QString sourceTag =
            QString("%1_%2").arg(baseTag, postprocess::psmTag(psm));
        candidates.push_back(runOcrOnBinary(api, bin, sourceTag, psm));
      }
    };

    // 기본 이진화 + 반전 이진화는 항상 시도한다.
    appendCandidates(binary, QStringLiteral("binary"));
    appendCandidates(binaryInv, QStringLiteral("binary_inv"));
    if (adaptiveUsed)
    {
      // adaptive 이진화가 생성된 경우에만 추가 후보로 사용한다.
      appendCandidates(adaptiveBinary, QStringLiteral("adaptive"));
    }

    return candidates;
  }

} // namespace ocr::recognition
