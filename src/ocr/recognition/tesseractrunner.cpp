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
    out.score = std::numeric_limits<int>::min() / 4;

    if (!api || binary.empty())
    {
      return out;
    }

    api->SetPageSegMode(psm);
    api->SetImage(binary.data, binary.cols, binary.rows, 1,
                  static_cast<int>(binary.step));

    char *outText = api->GetUTF8Text();
    out.confidence = api->MeanTextConf();
    if (!outText)
    {
      return out;
    }

    out.rawText = QString::fromUtf8(outText).trimmed();
    delete[] outText;
    out.normalizedText = postprocess::normalizePlateText(out.rawText);
    out.score = postprocess::platePlausibilityScore(out.normalizedText);
    return out;
  }

  std::vector<postprocess::OcrCandidate> collectCandidates(
      tesseract::TessBaseAPI *api, const cv::Mat &binary, const cv::Mat &binaryInv,
      const cv::Mat &adaptiveBinary, const bool adaptiveUsed)
  {
    std::vector<postprocess::OcrCandidate> candidates;
    candidates.reserve(adaptiveUsed ? 6 : 4);

    auto appendCandidates = [&](const cv::Mat &bin, const QString &baseTag)
    {
      if (bin.empty())
      {
        return;
      }

      const tesseract::PageSegMode modes[] = {
          postprocess::primaryPageSegMode(), postprocess::secondaryPageSegMode()};

      for (const tesseract::PageSegMode psm : modes)
      {
        const QString sourceTag =
            QString("%1_%2").arg(baseTag, postprocess::psmTag(psm));
        candidates.push_back(runOcrOnBinary(api, bin, sourceTag, psm));
      }
    };

    appendCandidates(binary, QStringLiteral("binary"));
    appendCandidates(binaryInv, QStringLiteral("binary_inv"));
    if (adaptiveUsed)
    {
      appendCandidates(adaptiveBinary, QStringLiteral("adaptive"));
    }

    return candidates;
  }

} // namespace ocr::recognition
