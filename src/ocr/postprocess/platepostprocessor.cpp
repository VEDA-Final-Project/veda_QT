#include "ocr/postprocess/platepostprocessor.h"
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <limits>

namespace ocr::postprocess
{
  namespace
  {
    // 번호판 OCR은 한 줄/한 단어 두 모드를 모두 시도한다.
    constexpr tesseract::PageSegMode kPlatePageSegMode = tesseract::PSM_SINGLE_LINE;
    constexpr tesseract::PageSegMode kPlateAltPageSegMode = tesseract::PSM_SINGLE_WORD;
    // 점수 차이가 작을 때 신뢰도로 동점 해소하는 임계값.
    constexpr int kScoreTieThreshold = 30;

    // 최종 번호판 형식: 2~3자리 숫자 + 한글 1자 + 4자리 숫자
    const QRegularExpression kFinalPlatePattern(
        QStringLiteral("^(?:\\d{2}|\\d{3})[가-힣]\\d{4}$"));

    // 차량 번호판 OCR에 허용할 문자 집합.
    const char kPlateWhitelist[] =
        "0123456789"
        "가나다라마거너더러머버서어저고노도로모보소"
        "오조구누두루무부수우주아바사자하허호";

    // 중앙 글자 복구 단계에서 쓸 한글 전용 집합.
    const char kHangulWhitelist[] =
        "가나다라마거너더러머버서어저고노도로모보소"
        "오조구누두루무부수우주아바사자하허호";

    bool isHangulSyllable(const QChar ch)
    {
      // UTF-16 코드 단위가 한글 음절 범위(가~힣)인지 판별.
      const ushort u = ch.unicode();
      return (u >= 0xAC00 && u <= 0xD7A3);
    }

    bool setWhitelist(tesseract::TessBaseAPI *api, const char *whitelist)
    {
      if (!api || !whitelist)
      {
        return false;
      }
      return api->SetVariable("tessedit_char_whitelist", whitelist);
    }

    QChar firstHangulChar(const QString &text)
    {
      for (const QChar ch : text)
      {
        if (isHangulSyllable(ch))
        {
          return ch;
        }
      }
      return QChar();
    }

    QString readRawText(tesseract::TessBaseAPI *api, const cv::Mat &binary,
                        const tesseract::PageSegMode psm)
    {
      if (!api || binary.empty())
      {
        return QString();
      }

      api->SetPageSegMode(psm);
      // 단일 채널(binary) 이미지를 직접 OCR 입력으로 전달.
      api->SetImage(binary.data, binary.cols, binary.rows, 1,
                    static_cast<int>(binary.step));

      char *outText = api->GetUTF8Text();
      if (!outText)
      {
        return QString();
      }

      const QString text = QString::fromUtf8(outText).trimmed();
      delete[] outText;
      return text;
    }

    void considerBestPatternCandidate(const OcrCandidate &candidate,
                                      const OcrCandidate **bestPattern)
    {
      // 최종 번호판 정규식과 맞는 후보만 '패턴 우선 후보'로 고려한다.
      if (!kFinalPlatePattern.match(candidate.normalizedText).hasMatch())
      {
        return;
      }

      if (!(*bestPattern))
      {
        *bestPattern = &candidate;
        return;
      }

      if (candidate.score > (*bestPattern)->score)
      {
        *bestPattern = &candidate;
        return;
      }

      if (candidate.score == (*bestPattern)->score &&
          candidate.confidence > (*bestPattern)->confidence)
      {
        *bestPattern = &candidate;
        return;
      }

      if (candidate.score == (*bestPattern)->score &&
          candidate.confidence == (*bestPattern)->confidence &&
          candidate.normalizedText.size() > (*bestPattern)->normalizedText.size())
      {
        *bestPattern = &candidate;
      }
    }

    QString probeHangulFromCenter(tesseract::TessBaseAPI *api, const cv::Mat &binary,
                                  const cv::Mat &binaryInv)
    {
      if (!api || binary.empty() || binaryInv.empty() ||
          binary.size() != binaryInv.size())
      {
        return QString();
      }

      const int w = binary.cols;
      const int h = binary.rows;
      if (w <= 8 || h <= 8)
      {
        return QString();
      }

      // 번호판 중앙 영역(한글이 주로 위치하는 구간)을 잘라 별도 OCR 시도.
      const int x0 = std::clamp(static_cast<int>(std::lround(w * 0.24)), 0, w - 2);
      const int x1 = std::clamp(static_cast<int>(std::lround(w * 0.62)), x0 + 1, w);
      const int y0 = std::clamp(static_cast<int>(std::lround(h * 0.10)), 0, h - 2);
      const int y1 = std::clamp(static_cast<int>(std::lround(h * 0.90)), y0 + 1, h);
      const cv::Rect roi(x0, y0, x1 - x0, y1 - y0);

      if (roi.width < 4 || roi.height < 4)
      {
        return QString();
      }

      const cv::Mat binaryCrop = binary(roi);
      const cv::Mat binaryInvCrop = binaryInv(roi);

      if (!setWhitelist(api, hangulWhitelist()))
      {
        return QString();
      }

      QString recoveredHangul;
      // 극성이 다른 두 영상(binary/binary_inv)과 두 PSM을 모두 탐색한다.
      const cv::Mat probes[] = {binaryCrop, binaryInvCrop};
      const tesseract::PageSegMode probeModes[] = {
          tesseract::PSM_SINGLE_CHAR, tesseract::PSM_SINGLE_WORD};

      for (const cv::Mat &probe : probes)
      {
        for (const tesseract::PageSegMode mode : probeModes)
        {
          const QString raw = readRawText(api, probe, mode);
          const QChar hangul = firstHangulChar(raw);
          if (!hangul.isNull())
          {
            recoveredHangul = QString(hangul);
            break;
          }
        }
        if (!recoveredHangul.isEmpty())
        {
          break;
        }
      }

      setWhitelist(api, plateWhitelist());
      api->SetPageSegMode(primaryPageSegMode());
      return recoveredHangul;
    }

  } // namespace

  const char *plateWhitelist()
  {
    return kPlateWhitelist;
  }

  const char *hangulWhitelist()
  {
    return kHangulWhitelist;
  }

  tesseract::PageSegMode primaryPageSegMode()
  {
    return kPlatePageSegMode;
  }

  tesseract::PageSegMode secondaryPageSegMode()
  {
    return kPlateAltPageSegMode;
  }

  QString psmTag(const tesseract::PageSegMode psm)
  {
    if (psm == secondaryPageSegMode())
    {
      return QStringLiteral("word");
    }
    return QStringLiteral("line");
  }

  QString normalizePlateText(const QString &raw)
  {
    // OCR 잡문자를 제거하고 숫자/한글 음절만 유지한다.
    QString normalized;
    normalized.reserve(raw.size());
    for (const QChar ch : raw)
    {
      if (ch.isDigit() || isHangulSyllable(ch))
      {
        normalized.append(ch);
      }
    }
    return normalized;
  }

  int platePlausibilityScore(const QString &candidate)
  {
    if (candidate.isEmpty())
    {
      return std::numeric_limits<int>::min() / 4;
    }

    const int len = candidate.size();
    int score = 0;

    // 정규식 일치 여부를 가장 큰 가중치로 부여한다.
    if (kFinalPlatePattern.match(candidate).hasMatch())
    {
      score += 1000;
    }

    // 길이(7 또는 8)와 문자 구성(숫자/한글 분포)을 함께 평가한다.
    const int distanceTo7 = std::abs(len - 7);
    const int distanceTo8 = std::abs(len - 8);
    const int nearestDistance = std::min(distanceTo7, distanceTo8);
    score += std::max(0, 120 - 20 * nearestDistance);

    int digitCount = 0;
    int hangulCount = 0;
    for (int i = 0; i < len; ++i)
    {
      const QChar ch = candidate.at(i);
      if (ch.isDigit())
      {
        ++digitCount;
      }
      else if (isHangulSyllable(ch))
      {
        ++hangulCount;
      }
    }

    score += digitCount * 8;
    if (hangulCount == 1)
    {
      score += 120;
    }
    else
    {
      score -= std::abs(hangulCount - 1) * 40;
    }

    // 번호판 구조상 한글 위치(3번째 또는 4번째)에 보너스/패널티를 준다.
    const int hangulIndex = (len == 7) ? 2 : 3;
    if (len > hangulIndex)
    {
      score += isHangulSyllable(candidate.at(hangulIndex)) ? 120 : -80;
    }

    const int headEnd = std::min(3, len);
    for (int i = 0; i < headEnd; ++i)
    {
      score += candidate.at(i).isDigit() ? 20 : -20;
    }

    for (int i = 4; i < len; ++i)
    {
      score += candidate.at(i).isDigit() ? 10 : -15;
    }

    return score;
  }

  OcrResult chooseBestPlateResult(const std::vector<OcrCandidate> &candidates)
  {
    OcrResult out;
    if (candidates.empty())
    {
      out.dropReason = QStringLiteral("no OCR candidates");
      return out;
    }

    std::vector<const OcrCandidate *> ranked;
    ranked.reserve(candidates.size());
    for (const OcrCandidate &candidate : candidates)
    {
      ranked.push_back(&candidate);
    }

    // 우선 score -> confidence -> 길이 순으로 정렬한다.
    std::sort(ranked.begin(), ranked.end(),
              [](const OcrCandidate *lhs, const OcrCandidate *rhs)
              {
                if (lhs->score != rhs->score)
                {
                  return lhs->score > rhs->score;
                }
                if (lhs->confidence != rhs->confidence)
                {
                  return lhs->confidence > rhs->confidence;
                }
                return lhs->normalizedText.size() > rhs->normalizedText.size();
              });

    const OcrCandidate *primary = ranked.front();
    const OcrCandidate *secondary = (ranked.size() >= 2) ? ranked[1] : nullptr;

    // 상위 두 후보 점수가 근소하면 confidence로 최종 순서를 조정한다.
    if (secondary)
    {
      const int scoreGap = std::abs(primary->score - secondary->score);
      if (scoreGap <= kScoreTieThreshold &&
          primary->confidence != secondary->confidence)
      {
        out.confidenceTiebreakUsed = true;
        if (secondary->confidence > primary->confidence)
        {
          std::swap(primary, secondary);
        }
      }
      else if (primary->score == secondary->score &&
               primary->confidence == secondary->confidence &&
               secondary->normalizedText.size() > primary->normalizedText.size())
      {
        std::swap(primary, secondary);
      }
    }

    out.selectedCandidate = primary->normalizedText;
    out.selectedScore = primary->score;
    out.selectedConfidence = primary->confidence;

    // 정규 패턴에 맞는 후보가 하나라도 있으면 그 후보를 최우선 채택한다.
    const OcrCandidate *bestPattern = nullptr;
    for (const OcrCandidate &candidate : candidates)
    {
      considerBestPatternCandidate(candidate, &bestPattern);
    }

    if (bestPattern)
    {
      out.text = bestPattern->normalizedText;
      out.selectedCandidate = bestPattern->normalizedText;
      out.selectedScore = bestPattern->score;
      out.selectedConfidence = bestPattern->confidence;
      return out;
    }

    // 최종 실패 시 상위 후보 요약을 dropReason에 남겨 디버깅에 활용한다.
    QStringList detail;
    const int detailCount = std::min(3, static_cast<int>(ranked.size()));
    for (int i = 0; i < detailCount; ++i)
    {
      const OcrCandidate *cand = ranked[i];
      detail.append(QString("%1='%2'(score=%3,conf=%4)")
                        .arg(cand->sourceTag)
                        .arg(cand->normalizedText)
                        .arg(cand->score)
                        .arg(cand->confidence));
    }

    out.dropReason =
        QString("pattern mismatch: top=%1").arg(detail.join(QStringLiteral(" | ")));
    return out;
  }

  QString recoverPlateUsingCenterHangul(tesseract::TessBaseAPI *api,
                                        const cv::Mat &binary,
                                        const cv::Mat &binaryInv,
                                        const QString &selectedCandidate)
  {
    if (selectedCandidate.isEmpty())
    {
      return QString();
    }

    int hangulCount = 0;
    QString digitsOnly;
    digitsOnly.reserve(selectedCandidate.size());
    for (const QChar ch : selectedCandidate)
    {
      if (ch.isDigit())
      {
        digitsOnly.append(ch);
      }
      else if (isHangulSyllable(ch))
      {
        ++hangulCount;
      }
    }

    // 이미 한글이 포함되어 있으면 복구 대상이 아니다(숫자-only 후보만 복구 시도).
    if (hangulCount > 0 || digitsOnly.size() < 6)
    {
      return QString();
    }

    const QString hangul = probeHangulFromCenter(api, binary, binaryInv);
    if (hangul.isEmpty())
    {
      return QString();
    }

    // 앞자리 2/3자리 두 경우를 모두 조합해 최종 패턴에 맞는 후보를 선택한다.
    QString best;
    int bestScore = std::numeric_limits<int>::min() / 4;
    const int headLens[] = {3, 2};
    for (const int headLen : headLens)
    {
      if (digitsOnly.size() < (headLen + 4))
      {
        continue;
      }

      const QString head = digitsOnly.left(headLen);
      const QString tail = digitsOnly.right(4);
      const QString plate = head + hangul + tail;
      if (!kFinalPlatePattern.match(plate).hasMatch())
      {
        continue;
      }

      const int score = platePlausibilityScore(plate);
      if (score > bestScore)
      {
        bestScore = score;
        best = plate;
      }
    }

    return best;
  }

} // namespace ocr::postprocess
