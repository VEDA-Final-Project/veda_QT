#include "ocr/postprocess/platepostprocessor.h"
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>

namespace ocr::postprocess
{
namespace
{
constexpr int kScoreTieThreshold = 35;
const QRegularExpression kFinalPlatePattern(
    QStringLiteral("^(?:\\d{2}|\\d{3})[가-힣]\\d{4}$"));

bool isHangulSyllable(const QChar ch)
{
  const ushort u = ch.unicode();
  return (u >= 0xAC00 && u <= 0xD7A3);
}

QChar confusableToDigit(const QChar ch)
{
  switch (ch.unicode())
  {
  case 'I':
  case 'i':
  case 'l':
  case '|':
  case '!':
  case '/':
  case '\\':
    return QChar('1');
  case 'O':
  case 'o':
  case 'Q':
  case 'q':
  case 'D':
  case 'd':
  case 'U':
  case 'u':
    return QChar('0');
  case 'S':
  case 's':
    return QChar('5');
  case 'B':
  case 'b':
    return QChar('8');
  case 'Z':
  case 'z':
    return QChar('2');
  case 'G':
  case 'g':
    return QChar('6');
  default:
    return QChar();
  }
}

void considerBestPatternCandidate(const OcrCandidate &candidate,
                                  const OcrCandidate **bestPattern)
{
  if (!kFinalPlatePattern.match(candidate.normalizedText).hasMatch())
  {
    return;
  }

  if (!(*bestPattern) || candidate.score > (*bestPattern)->score ||
      (candidate.score == (*bestPattern)->score &&
       candidate.confidence > (*bestPattern)->confidence))
  {
    *bestPattern = &candidate;
  }
}

} // namespace

QString normalizePlateText(const QString &raw)
{
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

QString normalizePlateTextWithConfusableFix(const QString &raw)
{
  QString normalized;
  normalized.reserve(raw.size());
  for (const QChar ch : raw)
  {
    if (ch.isDigit() || isHangulSyllable(ch))
    {
      normalized.append(ch);
      continue;
    }

    const QChar mapped = confusableToDigit(ch);
    if (!mapped.isNull())
    {
      normalized.append(mapped);
    }
  }
  return normalized;
}

QString canonicalizePlateCandidate(const QString &normalized)
{
  if (normalized.isEmpty())
  {
    return QString();
  }

  if (kFinalPlatePattern.match(normalized).hasMatch())
  {
    return normalized;
  }

  QString best = normalized;
  int bestScore = platePlausibilityScore(normalized);

  const int lengths[] = {8, 7};
  for (const int len : lengths)
  {
    if (normalized.size() < len)
    {
      continue;
    }

    for (int start = 0; start <= (normalized.size() - len); ++start)
    {
      const QString window = normalized.mid(start, len);
      const int score = platePlausibilityScore(window);
      if (score > bestScore)
      {
        best = window;
        bestScore = score;
      }
    }
  }

  return best;
}

int platePlausibilityScore(const QString &candidate)
{
  if (candidate.isEmpty())
  {
    return std::numeric_limits<int>::min() / 4;
  }

  int score = 0;
  const int len = candidate.size();
  if (len == 7 || len == 8)
  {
    score += 180;
  }
  else
  {
    score -= 25 * std::abs(len - 8);
  }

  int digitCount = 0;
  int hangulCount = 0;
  int hangulIndex = -1;
  for (int i = 0; i < len; ++i)
  {
    const QChar ch = candidate.at(i);
    if (ch.isDigit())
    {
      ++digitCount;
      continue;
    }
    if (isHangulSyllable(ch))
    {
      if (hangulIndex < 0)
      {
        hangulIndex = i;
      }
      ++hangulCount;
      continue;
    }
    score -= 300;
  }

  score += digitCount * 8;
  if (hangulCount == 1)
  {
    score += 260;
  }
  else
  {
    score -= 180 * std::abs(hangulCount - 1);
  }

  if (hangulIndex >= 0)
  {
    const int digitsBefore = hangulIndex;
    const int digitsAfter = len - hangulIndex - 1;

    if (digitsBefore == 2 || digitsBefore == 3)
    {
      score += 180;
    }
    else
    {
      score -= 80 * std::abs(digitsBefore - 3);
    }

    if (digitsAfter == 4)
    {
      score += 320;
    }
    else
    {
      score -= 100 * std::abs(digitsAfter - 4);
    }

    if ((len == 7 && hangulIndex == 2) || (len == 8 && hangulIndex == 3))
    {
      score += 140;
    }
    else if (len == 7 || len == 8)
    {
      score -= 70;
    }
  }
  else
  {
    score -= 280;
  }

  const int tailStart = std::max(0, len - 4);
  for (int i = tailStart; i < len; ++i)
  {
    score += candidate.at(i).isDigit() ? 20 : -60;
  }

  if (kFinalPlatePattern.match(candidate).hasMatch())
  {
    score += 1500;
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

  std::sort(ranked.begin(), ranked.end(), [](const OcrCandidate *lhs,
                                             const OcrCandidate *rhs) {
    if (lhs->score != rhs->score)
    {
      return lhs->score > rhs->score;
    }
    return lhs->confidence > rhs->confidence;
  });

  const OcrCandidate *bestPattern = nullptr;
  for (const OcrCandidate *candidate : ranked)
  {
    considerBestPatternCandidate(*candidate, &bestPattern);
  }

  const OcrCandidate *best = bestPattern ? bestPattern : ranked.front();
  out.selectedRawText = best->rawText;
  out.selectedCandidate = best->normalizedText;
  out.selectedScore = best->score;
  out.selectedConfidence = best->confidence;

  if (!bestPattern)
  {
    out.dropReason = QStringLiteral("no plate-like OCR candidate");
    return out;
  }

  out.text = bestPattern->normalizedText;
  if (!ranked.empty() && ranked.front() != bestPattern &&
      std::abs(ranked.front()->score - bestPattern->score) <= kScoreTieThreshold &&
      ranked.front()->confidence > bestPattern->confidence)
  {
    out.confidenceTiebreakUsed = true;
  }
  return out;
}

} // namespace ocr::postprocess
