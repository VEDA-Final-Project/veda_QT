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

OcrResult chooseBestPlateResult(const OcrCandidate &candidate)
{
  OcrResult out;
  if (candidate.normalizedText.isEmpty() && candidate.rawText.isEmpty())
  {
    out.dropReason = QStringLiteral("no OCR candidate");
    return out;
  }

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
