#include "ocrmanager.h"
#include "ocr/debug/ocrdebugdumper.h"
#include "ocr/postprocess/platepostprocessor.h"
#include "ocr/preprocess/platepreprocessor.h"
#include "ocr/recognition/tesseractrunner.h"
#include <QByteArray>
#include <QDebug>
#include <QtGlobal>

OcrManager::OcrManager() : m_tessApi(nullptr)
{
  m_tessApi = new tesseract::TessBaseAPI();
}

OcrManager::~OcrManager()
{
  if (m_tessApi)
  {
    m_tessApi->End();
    delete m_tessApi;
  }
}

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

  QByteArray pathBytes = tessdataDir.toUtf8();
  QByteArray langBytes = language.toUtf8();

  qDebug() << "Initializing Tesseract with DataPath:" << tessdataDir
           << "Lang:" << language;

  if (m_tessApi->Init(pathBytes.constData(), langBytes.constData()))
  {
    qDebug() << "Could not initialize tesseract. Path:" << tessdataDir
             << "Lang:" << language;
    return false;
  }

  m_tessApi->SetVariable("tessedit_char_whitelist",
                         ocr::postprocess::plateWhitelist());
  m_tessApi->SetPageSegMode(ocr::postprocess::primaryPageSegMode());

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

  QImage formattedImage = image.convertToFormat(QImage::Format_RGB888);
  cv::Mat matRoi(formattedImage.height(), formattedImage.width(), CV_8UC3,
                 (uchar *)formattedImage.bits(), formattedImage.bytesPerLine());

  ocr::preprocess::PreprocessOutput preprocess;
  if (!ocr::preprocess::preprocessPlateRoi(matRoi, &preprocess))
  {
    out.dropReason = QStringLiteral("preprocess failed");
    return out;
  }

  ocr::debug::dumpOcrStages(preprocess.roiDebugRgb, preprocess.ocrGray,
                            preprocess.ocrRgb, preprocess.quadDebug,
                            preprocess.binary, preprocess.binaryInv);

  const std::vector<ocr::postprocess::OcrCandidate> candidates =
      ocr::recognition::collectCandidates(
          m_tessApi, preprocess.binary, preprocess.binaryInv,
          preprocess.adaptiveBinary, preprocess.adaptiveUsed);

  OcrResult result = ocr::postprocess::chooseBestPlateResult(candidates);
  if (result.text.isEmpty())
  {
    QString recovered = ocr::postprocess::recoverPlateUsingCenterHangul(
        m_tessApi, preprocess.binary, preprocess.binaryInv,
        result.selectedCandidate);

    cv::Mat adaptiveInv;
    const bool hasAdaptive =
        preprocess.adaptiveUsed && !preprocess.adaptiveBinary.empty();
    if (hasAdaptive)
    {
      cv::bitwise_not(preprocess.adaptiveBinary, adaptiveInv);
    }

    if (recovered.isEmpty() && hasAdaptive)
    {
      recovered = ocr::postprocess::recoverPlateUsingCenterHangul(
          m_tessApi, preprocess.adaptiveBinary, adaptiveInv,
          result.selectedCandidate);
    }

    if (!recovered.isEmpty())
    {
      result.text = recovered;
      result.selectedCandidate = recovered;
      result.selectedScore = ocr::postprocess::platePlausibilityScore(recovered);
      result.dropReason.clear();
    }
  }

  return result;
}

QString OcrManager::performOcr(const QImage &image)
{
  return performOcrDetailed(image).text;
}
