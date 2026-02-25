#include "ocrmanager.h"
#include <algorithm>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfoList>
#include <QRegularExpression>
#include <atomic>
#include <limits>

namespace
{

  constexpr int kDebugSaveInterval = 10; // 매 N회 OCR마다 저장
  constexpr int kDebugKeepFilesPerStage = 10;
  constexpr char kDebugDirName[] = "ocr_debug";
  constexpr double kOcrUpscaleFactor = 2.0;
  constexpr tesseract::PageSegMode kPlatePageSegMode = tesseract::PSM_SINGLE_LINE;
  constexpr int kScoreTieThreshold = 30;
  const QRegularExpression kFinalPlatePattern(
      QStringLiteral("^(?:\\d{2}|\\d{3})[가-힣]\\d{4}$"));

  struct OcrCandidate
  {
    QString sourceTag;      // "binary", "binary_inv"
    QString rawText;        // Raw Tesseract output
    QString normalizedText; // Digits + Hangul only
    int score = std::numeric_limits<int>::min() / 4;
    int confidence = -1;    // Tesseract MeanTextConf
  };

  QDir ensureDebugDir()
  {
    const QString envDir = qEnvironmentVariable("OCR_DEBUG_DIR").trimmed();
    const QString basePath = envDir.isEmpty() ? QDir::currentPath() : envDir;
    const QString debugDirName = QString::fromLatin1(kDebugDirName);

    QDir rootDir(basePath);
    if (!rootDir.exists())
    {
      rootDir.mkpath(".");
    }

    const QString debugDirPath = (rootDir.dirName() == debugDirName)
                                     ? rootDir.absolutePath()
                                     : rootDir.filePath(debugDirName);
    QDir(debugDirPath).mkpath(".");
    return QDir(debugDirPath);
  }

  void rotateStageFiles(const QDir &dir, const QString &prefix, int keepCount)
  {
    if (keepCount < 0)
    {
      return;
    }

    const QStringList filters = {QString("%1_*.png").arg(prefix)};
    const QFileInfoList files =
        dir.entryInfoList(filters, QDir::Files, QDir::Time | QDir::Reversed);

    const int removeCount = files.size() - keepCount;
    for (int i = 0; i < removeCount; ++i)
    {
      if (!QFile::remove(files[i].absoluteFilePath()))
      {
        qWarning() << "[OCR][Debug] Failed to remove old file:"
                   << files[i].absoluteFilePath();
      }
    }
  }

  bool savePng(const QString &filePath, const cv::Mat &mat)
  {
    return cv::imwrite(QFile::encodeName(filePath).constData(), mat);
  }

  bool isHangulSyllable(const QChar ch)
  {
    const ushort u = ch.unicode();
    return (u >= 0xAC00 && u <= 0xD7A3);
  }

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

  int platePlausibilityScore(const QString &candidate)
  {
    if (candidate.isEmpty())
    {
      return std::numeric_limits<int>::min() / 4;
    }

    const int len = candidate.size();
    int score = 0;

    if (kFinalPlatePattern.match(candidate).hasMatch())
    {
      score += 1000;
    }

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

  OcrCandidate runOcrOnBinary(tesseract::TessBaseAPI *api, const cv::Mat &binary,
                              const QString &sourceTag)
  {
    OcrCandidate out;
    out.sourceTag = sourceTag;

    if (!api || binary.empty())
    {
      return out;
    }

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
    out.normalizedText = normalizePlateText(out.rawText);
    out.score = platePlausibilityScore(out.normalizedText);
    return out;
  }

  OcrResult chooseBetterPlateResult(const OcrCandidate &binaryCand,
                                    const OcrCandidate &binaryInvCand)
  {
    OcrResult out;

    const OcrCandidate *primary = &binaryCand;
    const OcrCandidate *secondary = &binaryInvCand;

    if (binaryInvCand.score > binaryCand.score)
    {
      primary = &binaryInvCand;
      secondary = &binaryCand;
    }

    const int scoreGap = std::abs(binaryCand.score - binaryInvCand.score);
    if (scoreGap <= kScoreTieThreshold &&
        binaryCand.confidence != binaryInvCand.confidence)
    {
      // 규칙 점수 차이가 작을 때만 confidence를 타이브레이크로 사용합니다.
      out.confidenceTiebreakUsed = true;
      if (binaryInvCand.confidence > binaryCand.confidence)
      {
        primary = &binaryInvCand;
        secondary = &binaryCand;
      }
      else
      {
        primary = &binaryCand;
        secondary = &binaryInvCand;
      }
    }
    else if (binaryCand.score == binaryInvCand.score &&
             binaryCand.confidence == binaryInvCand.confidence)
    {
      // 완전 동점이면 더 긴 정규화 문자열을 우선합니다.
      if (binaryInvCand.normalizedText.size() > binaryCand.normalizedText.size())
      {
        primary = &binaryInvCand;
        secondary = &binaryCand;
      }
    }

    out.selectedCandidate = primary->normalizedText;
    out.selectedScore = primary->score;
    out.selectedConfidence = primary->confidence;

    if (kFinalPlatePattern.match(primary->normalizedText).hasMatch())
    {
      out.text = primary->normalizedText;
      return out;
    }
    if (kFinalPlatePattern.match(secondary->normalizedText).hasMatch())
    {
      out.text = secondary->normalizedText;
      out.selectedCandidate = secondary->normalizedText;
      out.selectedScore = secondary->score;
      out.selectedConfidence = secondary->confidence;
      return out;
    }

    out.dropReason = QString(
                         "pattern mismatch: %1='%2'(score=%3,conf=%4) %5='%6'(score=%7,conf=%8)")
                         .arg(binaryCand.sourceTag)
                         .arg(binaryCand.normalizedText)
                         .arg(binaryCand.score)
                         .arg(binaryCand.confidence)
                         .arg(binaryInvCand.sourceTag)
                         .arg(binaryInvCand.normalizedText)
                         .arg(binaryInvCand.score)
                         .arg(binaryInvCand.confidence);
    return out;
  }

  void dumpOcrStages(const cv::Mat &roiRgb, const cv::Mat &gray,
                     const cv::Mat &binary, const cv::Mat &binaryInv)
  {
    static std::atomic<int> s_callCounter{0};
    static std::atomic<bool> s_loggedPath{false};

    const int callCount = ++s_callCounter;
    if ((callCount % kDebugSaveInterval) != 0)
    {
      return;
    }

    const QDir debugDir = ensureDebugDir();
    if (!s_loggedPath.exchange(true))
    {
      qDebug() << "[OCR][Debug] stage images saved under:"
               << debugDir.absolutePath();
    }

    const QString stamp =
        QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");
    const QString seq = QString("%1").arg(callCount, 6, 10, QChar('0'));

    // imwrite는 컬러 이미지를 BGR 기준으로 저장합니다.
    cv::Mat roiBgr;
    cv::cvtColor(roiRgb, roiBgr, cv::COLOR_RGB2BGR);

    const QString roiPath =
        debugDir.filePath(QString("roi_%1_%2.png").arg(stamp, seq));
    const QString grayPath =
        debugDir.filePath(QString("gray_%1_%2.png").arg(stamp, seq));
    const QString binaryPath =
        debugDir.filePath(QString("binary_%1_%2.png").arg(stamp, seq));
    const QString binaryInvPath =
        debugDir.filePath(QString("binary_inv_%1_%2.png").arg(stamp, seq));

    if (!savePng(roiPath, roiBgr))
    {
      qWarning() << "[OCR][Debug] Failed to save:" << roiPath;
    }
    if (!savePng(grayPath, gray))
    {
      qWarning() << "[OCR][Debug] Failed to save:" << grayPath;
    }
    if (!savePng(binaryPath, binary))
    {
      qWarning() << "[OCR][Debug] Failed to save:" << binaryPath;
    }
    if (!savePng(binaryInvPath, binaryInv))
    {
      qWarning() << "[OCR][Debug] Failed to save:" << binaryInvPath;
    }

    rotateStageFiles(debugDir, "roi", kDebugKeepFilesPerStage);
    rotateStageFiles(debugDir, "gray", kDebugKeepFilesPerStage);
    rotateStageFiles(debugDir, "binary", kDebugKeepFilesPerStage);
    rotateStageFiles(debugDir, "binary_inv", kDebugKeepFilesPerStage);
  }

} // namespace

// 생성자: OCR API 인스턴스 초기화
OcrManager::OcrManager() : m_tessApi(nullptr)
{
  m_tessApi = new tesseract::TessBaseAPI();
}

// 소멸자: OCR API 인스턴스 종료 및 메모리 해제
OcrManager::~OcrManager()
{
  if (m_tessApi)
  {
    m_tessApi->End();
    delete m_tessApi;
  }
}

// OCR 초기화: 데이터 경로와 언어 설정
bool OcrManager::init(const QString &datapath, const QString &language)
{

  QString tessdataDir = datapath;

  if (tessdataDir.isEmpty())
  {
    tessdataDir = qEnvironmentVariable("TESSDATA_PREFIX");
    if (tessdataDir.isEmpty())
    {
      tessdataDir = "C:/vcpkg/installed/x64-windows/share/tessdata/";
    }
  }

  // Tesseract API에 전달하기 위해 QByteArray 유지
  QByteArray pathBytes = tessdataDir.toUtf8();
  QByteArray langBytes = language.toUtf8();

  // 데이터 경로 유효성 검사 (디버깅용)
  qDebug() << "Initializing Tesseract with DataPath:" << tessdataDir << "Lang:" << language;

  // 언어 데이터 파일 존재 여부 확인 로직은 tessApi->Init 내부에서 처리됨
  // 하지만 "eng.traineddata"가 없으면 실패하므로 사용자에게 안내 필요

  if (m_tessApi->Init(pathBytes.constData(), langBytes.constData()))
  {
    qDebug() << "Could not initialize tesseract. Path:" << tessdataDir
             << "Lang:" << language;
    return false;
  }

  // 화이트리스트 적용: 숫자 + 한국 자동차 번호판에 사용되는 표준 한글
  // 0-9,
  // 가나다라마거너더러머버서어저고노도로모보소오조구누두루무부수우주아바사자하허호
  static const char kWhitelist[] =
      "0123456789"
      "가나다라마거너더러머버서어저고노도로모보소"
      "오조구누두루무부수우주아바사자하허호";
  m_tessApi->SetVariable("tessedit_char_whitelist", kWhitelist);

  // 번호판 한 줄 기준으로 분할 모드를 고정합니다.
  // 필요 시 PSM_SINGLE_WORD로 교체하여 비교할 수 있습니다.
  m_tessApi->SetPageSegMode(kPlatePageSegMode);

  return true;
}

OcrResult OcrManager::performOcrDetailed(const QImage &image)
{
  OcrResult out;
  if (image.isNull())
  {
    out.dropReason = QStringLiteral("empty input image");
    return out;
  }

  // 1. QImage -> cv::Mat 변환 (Format_RGB888 강제 변환)
  // 입력 이미지가 RGB888이 아닐 경우(예: RGB32) CV_8UC3와 호환되지 않아 메모리
  // 참조 오류 발생 가능
  QImage formattedImage = image.convertToFormat(QImage::Format_RGB888);

  cv::Mat matRoi(formattedImage.height(), formattedImage.width(), CV_8UC3,
                 (uchar *)formattedImage.bits(), formattedImage.bytesPerLine());

  // 2. 전처리 (2x 업스케일 -> Gray -> Binary)
  // 주의: 입력 이미지는 RGB라고 가정 (QImage::Format_RGB888)
  cv::Mat upscaledRoi, gray, binary, binaryInv;
  cv::resize(matRoi, upscaledRoi, cv::Size(), kOcrUpscaleFactor,
             kOcrUpscaleFactor, cv::INTER_CUBIC);
  cv::cvtColor(upscaledRoi, gray, cv::COLOR_RGB2GRAY);
  cv::threshold(gray, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
  cv::bitwise_not(binary, binaryInv);

  // 단계별 전처리 결과를 조건부 저장 (매 N회 저장 + stage별 10개 유지)
  dumpOcrStages(upscaledRoi, gray, binary, binaryInv);

  // 3. OCR 수행 (binary / binary_inv 모두 시도 후 더 그럴듯한 결과 선택)
  const OcrCandidate binaryCand =
      runOcrOnBinary(m_tessApi, binary, QStringLiteral("binary"));
  const OcrCandidate binaryInvCand =
      runOcrOnBinary(m_tessApi, binaryInv, QStringLiteral("binary_inv"));
  return chooseBetterPlateResult(binaryCand, binaryInvCand);
}

QString OcrManager::performOcr(const QImage &image)
{
  return performOcrDetailed(image).text;
}
