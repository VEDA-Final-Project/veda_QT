#include "ocr/postprocess/platepostprocessor.h"
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>

namespace ocr::postprocess
{
namespace
{
const QString kAllowedPlateHangul = QStringLiteral(
    "가나다라마바사아자하"
    "거너더러머버서어저허"
    "고노도로모보소오조호"
    "구누두루무부수우주");

const QRegularExpression kFinalPlatePattern(
    QStringLiteral("^(?:\\d{2}|\\d{3})[가-힣]\\d{4}$"));

bool isHangulSyllable(const QChar ch)
{
  const ushort u = ch.unicode();
  return (u >= 0xAC00 && u <= 0xD7A3);
}

bool isAllowedPlateHangul(const QChar ch)
{
  return kAllowedPlateHangul.contains(ch);
}

QChar normalizePlateHangul(const QChar ch)
{
  if (!isHangulSyllable(ch))
  {
    return QChar();
  }

  const ushort base = 0xAC00;
  const int syllableIndex = static_cast<int>(ch.unicode()) - base;
  const int jongseong = syllableIndex % 28;
  const QChar normalized =
      (jongseong == 0) ? ch : QChar(static_cast<char16_t>(ch.unicode() - jongseong));
  if (isAllowedPlateHangul(normalized))
  {
    return normalized;
  }

  return QChar();
}

QChar confusableToPlateHangul(const QChar ch)
{
  switch (ch.unicode())
  {
  case 'u':
  case 'U':
  case 'n':
  case 'N':
    return QChar(u'나');
  case 'a':
  case 'A':
    return QChar(u'아');
  case 'g':
  case 'G':
    return QChar(u'가');
  case 'd':
    return QChar(u'다');
  case 'm':
  case 'M':
    return QChar(u'마');
  case 'b':
    return QChar(u'바');
  case 's':
    return QChar(u'사');
  case 'j':
  case 'J':
    return QChar(u'자');
  case 'h':
  case 'H':
    return QChar(u'하');
  default:
    return QChar();
  }
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

} // namespace

QString normalizePlateText(const QString &raw)
{
  QString normalized;
  normalized.reserve(raw.size());
  for (const QChar ch : raw)
  {
    if (ch.isDigit() || isHangulSyllable(ch))
    {
      if (ch.isDigit())
      {
        normalized.append(ch);
      }
      else
      {
        const QChar mappedHangul = normalizePlateHangul(ch);
        if (!mappedHangul.isNull())
        {
          normalized.append(mappedHangul);
        }
      }
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
      if (ch.isDigit())
      {
        normalized.append(ch);
      }
      else
      {
        const QChar mappedHangul = normalizePlateHangul(ch);
        if (!mappedHangul.isNull())
        {
          normalized.append(mappedHangul);
        }
      }
      continue;
    }

    const QChar mappedHangul = confusableToPlateHangul(ch);
    if (!mappedHangul.isNull())
    {
      normalized.append(mappedHangul);
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

  const OcrCandidate &candidate = candidates.front();
  out.selectedRawText = candidate.rawText;
  out.selectedCandidate = candidate.normalizedText;
  out.selectedScore = candidate.score;
  out.selectedConfidence = candidate.confidence;

  if (candidate.normalizedText.isEmpty())
  {
    out.dropReason = QStringLiteral("empty OCR candidate");
    return out;
  }

  out.text = candidate.normalizedText;
  return out;
}

} // namespace ocr::postprocess
