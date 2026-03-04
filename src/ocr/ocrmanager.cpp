#include "ocrmanager.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QPainter>
#include <QRegularExpression>
#include <algorithm>
OcrManager::OcrManager() : m_tessApi(nullptr) {
  m_tessApi = new tesseract::TessBaseAPI();
}

OcrManager::~OcrManager() {
  if (m_tessApi) {
    m_tessApi->End();
    delete m_tessApi;
  }
}

bool OcrManager::init(const QString &datapath, const QString &language) {
  // 데이터 경로 결정 순서:
  // 1. 매개변수로 전달된 경로
  // 2. TESSDATA_PREFIX 환경 변수
  // 3. vcpkg 기본 경로
  QString tessdataDir = datapath;

  if (tessdataDir.isEmpty()) {
    tessdataDir = qEnvironmentVariable("TESSDATA_PREFIX");
    if (tessdataDir.isEmpty()) {
      tessdataDir = "C:/vcpkg/installed/x64-windows/share/tessdata/";
    }
  }

  // Tesseract API에 전달하기 위해 QByteArray 유지
  QByteArray pathBytes = tessdataDir.toUtf8();
  QByteArray langBytes = language.toUtf8();

  // 데이터 경로 유효성 검사 (디버깅용)
  qDebug() << "Initializing Tesseract with DataPath:" << tessdataDir
           << "Lang:" << language;

  // 언어 데이터 파일 존재 여부 확인 로직은 tessApi->Init 내부에서 처리됨
  // 하지만 "eng.traineddata"가 없으면 실패하므로 사용자에게 안내 필요

  if (m_tessApi->Init(pathBytes.constData(), langBytes.constData())) {
    qDebug() << "Could not initialize tesseract. Path:" << tessdataDir
             << "Lang:" << language;
    return false;
  }

  // 화이트리스트 적용: 숫자 + 한국 자동차 번호판에 사용되는 표준 한글
  // 0-9,
  // 가나다라마거너더러머버서어저고노도로모보소오조구누두루무부수우주아바사자하허호
  // NOTE: Keep the whitelist as a single literal to avoid accidental newlines.
  static const char kWhitelist[] = "0123456789"
                                   "가나다라마거너더러머버서어저고노도로모보소"
                                   "오조구누두루무부수우주아바사자하허호";
  m_tessApi->SetVariable("tessedit_char_whitelist", kWhitelist);

  return true;
}

OcrFullResult OcrManager::performOcr(const QImage &image) {
  QElapsedTimer timer;
  timer.start();

  OcrFullResult finalResult;

  if (image.isNull())
    return finalResult;

  // 1. QImage -> cv::Mat 변환 (Format_RGB888 강제 변환)
  QImage formattedImage = image.convertToFormat(QImage::Format_RGB888);
  cv::Mat matRoi(formattedImage.height(), formattedImage.width(), CV_8UC3,
                 (uchar *)formattedImage.bits(), formattedImage.bytesPerLine());

  // 2. 전처리 (RGB -> Gray)
  cv::Mat gray;
  cv::cvtColor(matRoi, gray, cv::COLOR_RGB2GRAY);

  // === 투영 변환 (Perspective Transform) 로직 ===
  cv::Mat warped = gray.clone();
  bool isWarped = false;

  cv::Mat blurred, canned;
  cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 0);
  cv::Canny(blurred, canned, 50, 150);
  cv::dilate(canned, canned, cv::Mat(), cv::Point(-1, -1), 1);

  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(canned, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

  std::vector<cv::Point> plateContour;
  double maxArea = 0;
  for (const auto &cnt : contours) {
    double area = cv::contourArea(cnt);
    if (area > maxArea) {
      maxArea = area;
      plateContour = cnt;
    }
  }

  if (!plateContour.empty() && maxArea > 500) {
    std::vector<cv::Point2f> srcPoints(4);
    std::vector<cv::Point> candidates;
    std::vector<cv::Point> approx;
    double peri = cv::arcLength(plateContour, true);
    cv::approxPolyDP(plateContour, approx, 0.02 * peri, true);

    if (approx.size() == 4) {
      for (const auto &p : approx)
        candidates.push_back(p);
    } else {
      cv::Point tl_p = plateContour[0], br_p = plateContour[0],
                tr_p = plateContour[0], bl_p = plateContour[0];
      float minSum = (float)plateContour[0].x + plateContour[0].y;
      float maxSum = (float)plateContour[0].x + plateContour[0].y;
      float minDiff = (float)plateContour[0].x - plateContour[0].y;
      float maxDiff = (float)plateContour[0].x - plateContour[0].y;

      for (const auto &p : plateContour) {
        float sum = (float)p.x + p.y;
        float diff = (float)p.x - p.y;
        if (sum < minSum) {
          minSum = sum;
          tl_p = p;
        }
        if (sum > maxSum) {
          maxSum = sum;
          br_p = p;
        }
        if (diff < minDiff) {
          minDiff = diff;
          bl_p = p;
        }
        if (diff > maxDiff) {
          maxDiff = diff;
          tr_p = p;
        }
      }
      candidates = {tl_p, tr_p, br_p, bl_p};
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const cv::Point &a, const cv::Point &b) { return a.y < b.y; });
    if (candidates[0].x > candidates[1].x)
      std::swap(candidates[0], candidates[1]);
    if (candidates[2].x < candidates[3].x)
      std::swap(candidates[2], candidates[3]);

    srcPoints = {candidates[0], candidates[1], candidates[2], candidates[3]};

    double dx = srcPoints[1].x - srcPoints[0].x;
    double dy = srcPoints[1].y - srcPoints[0].y;
    double angle = std::abs(std::atan2(dy, dx) * 180.0 / 3.14159265);

    std::vector<cv::Point> srcIntPoints;
    for (const auto &p : srcPoints)
      srcIntPoints.push_back(cv::Point(p.x, p.y));
    bool isConvex = cv::isContourConvex(srcIntPoints);
    double srcArea = cv::contourArea(srcIntPoints);

    if (!(angle > 45.0 || !isConvex || srcArea < 400)) {
      int targetW = 189, targetH = 40;
      std::vector<cv::Point2f> dstPoints = {{0, 0},
                                            {(float)targetW, 0},
                                            {(float)targetW, (float)targetH},
                                            {0, (float)targetH}};
      cv::Mat M = cv::getPerspectiveTransform(srcPoints, dstPoints);
      cv::warpPerspective(gray, warped, M, cv::Size(targetW, targetH));
      isWarped = true;
    }
  }

  cv::Mat binary;
  cv::threshold(warped, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

  // === 3.5. [Fast-Path] PSM_SINGLE_LINE Strategy ===
  m_tessApi->SetPageSegMode(tesseract::PSM_SINGLE_LINE);
  m_tessApi->SetImage(binary.data, binary.cols, binary.rows, 1, binary.step);
  m_tessApi->SetSourceResolution(300);
  char *lineOutText = m_tessApi->GetUTF8Text();
  QString lineResult = QString::fromUtf8(lineOutText).trimmed();
  delete[] lineOutText;

  QString cleanedLine = lineResult;
  cleanedLine =
      cleanedLine.replace(" ", "").replace("\t", "").replace("\n", "").replace(
          "\r", "");
  QRegularExpression plateRe(QStringLiteral("\\d{2,3}[가-힣]+\\d{4}"));
  QRegularExpressionMatch reMatchLine = plateRe.match(cleanedLine);

  finalResult.raw = cleanedLine;

  if (reMatchLine.hasMatch()) {
    QString finalPlate = reMatchLine.captured(0);
    finalResult.filtered = finalPlate;
    finalResult.latencyMs = static_cast<int>(timer.elapsed());

    if (isWarped && !m_isBenchmarkMode) {
      QString debugPath = QCoreApplication::applicationDirPath() + "/debug_ocr";
      QDir().mkpath(debugPath);
      QString timestamp =
          QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
      QImage vizImg(binary.data, binary.cols, binary.rows, binary.step,
                    QImage::Format_Grayscale8);
      vizImg = vizImg.convertToFormat(QImage::Format_RGB32);
      QPainter painter(&vizImg);
      painter.setPen(Qt::blue);
      painter.drawText(10, 20, finalPlate);
      vizImg.save(debugPath + "/_LINE_RESULT_" + finalPlate + "_" + timestamp +
                  ".png");
    }
    return finalResult;
  }

  // === 4. [Char-Fallback] Strategy ===
  cv::Mat inverted;
  cv::bitwise_not(binary, inverted);
  cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2));
  cv::Mat eroded;
  cv::erode(inverted, eroded, kernel);

  std::vector<std::vector<cv::Point>> charContours;
  cv::findContours(eroded, charContours, cv::RETR_EXTERNAL,
                   cv::CHAIN_APPROX_SIMPLE);

  int imgH = binary.rows, imgW = binary.cols;
  double minCharH = imgH * 0.3, maxCharH = imgH * 1.0;
  double minCharW = 5.0, maxCharW = imgW * 0.8;

  struct CharRect {
    cv::Rect rect;
  };
  std::vector<CharRect> charRects;

  for (const auto &cnt : charContours) {
    cv::Rect br = cv::boundingRect(cnt);
    double aspectRatio = (double)br.width / br.height;
    if (br.height >= minCharH && br.height <= maxCharH &&
        br.width >= minCharW && br.width <= maxCharW) {
      if (aspectRatio < 0.15)
        continue;
      if (aspectRatio > 0.85 && aspectRatio <= 1.5) {
        int w1 = br.width / 2;
        charRects.push_back({cv::Rect(br.x, br.y, w1, br.height)});
        charRects.push_back(
            {cv::Rect(br.x + w1, br.y, br.width - w1, br.height)});
      } else if (aspectRatio > 1.5 && aspectRatio <= 2.5) {
        int w1 = br.width / 3, w2 = br.width / 3;
        charRects.push_back({cv::Rect(br.x, br.y, w1, br.height)});
        charRects.push_back({cv::Rect(br.x + w1, br.y, w2, br.height)});
        charRects.push_back(
            {cv::Rect(br.x + w1 + w2, br.y, br.width - (w1 + w2), br.height)});
      } else if (aspectRatio <= 2.5) {
        charRects.push_back({br});
      }
    }
  }

  // Box Merging
  bool merged;
  do {
    merged = false;
    for (size_t i = 0; i < charRects.size(); ++i) {
      for (size_t j = i + 1; j < charRects.size(); ++j) {
        cv::Rect r1 = charRects[i].rect, r2 = charRects[j].rect;
        cv::Rect intersection = r1 & r2;
        int minArea = std::min(r1.area(), r2.area());
        int dx = std::max(0, std::max(r1.x, r2.x) -
                                 std::min(r1.x + r1.width, r2.x + r2.width));
        int dy_overlap =
            std::max(0, std::min(r1.y + r1.height, r2.y + r2.height) -
                            std::max(r1.y, r2.y));

        if ((intersection.area() > minArea * 0.3) ||
            (dx <= 6 && dy_overlap > std::min(r1.height, r2.height) * 0.5 &&
             ((double)(r1 | r2).width / (r1 | r2).height <= 1.2))) {
          charRects[i].rect = r1 | r2;
          charRects.erase(charRects.begin() + j);
          merged = true;
          break;
        }
      }
      if (merged)
        break;
    }
  } while (merged);

  std::sort(
      charRects.begin(), charRects.end(),
      [](const CharRect &a, const CharRect &b) { return a.rect.x < b.rect.x; });

  QString charResult;
  struct CharDebugInfo {
    cv::Rect rect;
    QString text;
  };
  std::vector<CharDebugInfo> debugChars;

  m_tessApi->SetPageSegMode(tesseract::PSM_SINGLE_CHAR);
  for (const auto &cr : charRects) {
    int pad = 2;
    int x = std::max(0, cr.rect.x - pad), y = std::max(0, cr.rect.y - pad);
    int w = std::min(cr.rect.width + pad * 2, imgW - x),
        h = std::min(cr.rect.height + pad * 2, imgH - y);
    cv::Mat charRoi = binary(cv::Rect(x, y, w, h));
    cv::Mat resized;
    cv::resize(charRoi, resized,
               cv::Size((int)(charRoi.cols * 48.0 / charRoi.rows), 48), 0, 0,
               cv::INTER_CUBIC);
    cv::Mat padded;
    cv::copyMakeBorder(resized, padded, 10, 10, 10, 10, cv::BORDER_CONSTANT,
                       cv::Scalar(255));
    m_tessApi->SetImage(padded.data, padded.cols, padded.rows, 1, padded.step);
    char *outChar = m_tessApi->GetUTF8Text();
    QString ch = QString::fromUtf8(outChar)
                     .trimmed()
                     .replace(" ", "")
                     .replace("\n", "")
                     .replace("\r", "");
    delete[] outChar;
    if (!ch.isEmpty()) {
      QChar first = ch.at(0);
      if (first.unicode() >= 0xAC00 && first.unicode() <= 0xD7A3) {
        double relX = (cr.rect.x + cr.rect.width / 2.0) / imgW;
        if (relX < 0.35 || relX > 0.65)
          continue;
      }
      charResult += ch.at(0);
      debugChars.push_back({cr.rect, ch.at(0)});
    }
  }

  QRegularExpression fallbackPlateRe(QStringLiteral("\\d{2,3}[가-힣]+\\d{4}"));
  QRegularExpressionMatch reMatch = fallbackPlateRe.match(charResult);
  QString result = reMatch.hasMatch() ? reMatch.captured(0) : "";

  if (isWarped && !m_isBenchmarkMode) {
    QString debugPath = QCoreApplication::applicationDirPath() + "/debug_ocr";
    QDir().mkpath(debugPath);
    QString timestamp =
        QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
    QImage vizImg(binary.data, binary.cols, binary.rows, binary.step,
                  QImage::Format_Grayscale8);
    vizImg = vizImg.convertToFormat(QImage::Format_RGB32);
    QPainter painter(&vizImg);
    painter.setPen(Qt::red);
    for (const auto &dc : debugChars) {
      painter.drawRect(dc.rect.x, dc.rect.y, dc.rect.width, dc.rect.height);
      painter.setPen(Qt::blue);
      painter.drawText(dc.rect.x, dc.rect.y - 2, dc.text);
      painter.setPen(Qt::red);
    }
    painter.end();
    vizImg.save(debugPath + "/" + timestamp + "_RESULT_" +
                (result.isEmpty() ? "FAIL" : result) + ".png");
  }

  finalResult.filtered = result;
  finalResult.latencyMs = static_cast<int>(timer.elapsed());
  return finalResult;
}

QString OcrManager::performRawOcr(const QImage &image) {
  if (image.isNull())
    return QString();

  QImage formattedImage = image.convertToFormat(QImage::Format_RGB888);
  cv::Mat mat(formattedImage.height(), formattedImage.width(), CV_8UC3,
              (uchar *)formattedImage.bits(), formattedImage.bytesPerLine());

  cv::Mat gray;
  cv::cvtColor(mat, gray, cv::COLOR_RGB2GRAY);

  cv::Mat binary;
  cv::threshold(gray, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

  m_tessApi->SetPageSegMode(tesseract::PSM_SINGLE_LINE);
  m_tessApi->SetImage(binary.data, binary.cols, binary.rows, 1, binary.step);
  m_tessApi->SetSourceResolution(300);

  char *outText = m_tessApi->GetUTF8Text();
  QString result = QString::fromUtf8(outText).trimmed();
  delete[] outText;

  result = result.replace(" ", "").replace("\t", "").replace("\n", "").replace(
      "\r", "");
  return result;
}
