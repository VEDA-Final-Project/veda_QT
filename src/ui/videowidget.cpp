#include "videowidget.h"
#include <QDebug>
#include <QPainter>
#include <utility>

// Construct the widget and initialize async OCR resources.
VideoWidget::VideoWidget(QWidget *parent) : QLabel(parent)
{
  // Keep the video centered and use a black background when no frame is drawn.
  setAlignment(Qt::AlignCenter);
  setStyleSheet("background-color: black;");

  // Initialize OCR engine once; OCR requests reuse this manager asynchronously.
  m_ocrManager = new OcrManager();
  const auto &cfg = Config::instance();
  if (!m_ocrManager->init(cfg.tessdataPath(), cfg.ocrLanguage()))
  {
    // Fail-soft: OCR is optional, so keep video/object rendering alive.
    qDebug() << "Could not initialize OCR Manager.";
  }

  // Receive OCR completion on the UI thread and publish the result via signal.
  connect(&m_ocrWatcher, &QFutureWatcher<QString>::finished, this,
          &VideoWidget::onOcrFinished);
}

// Shutdown path: wait for in-flight OCR work, then release OCR manager.
VideoWidget::~VideoWidget()
{
  // Ensure no async OCR job outlives this widget.
  if (m_ocrWatcher.isRunning())
  {
    m_ocrWatcher.waitForFinished();
  }
  delete m_ocrManager;
  m_ocrManager = nullptr;
}

void VideoWidget::setSyncDelay(int delayMs)
{
  // Prevent negative delay values from breaking timestamp comparison.
  m_syncDelayMs = qMax(0, delayMs);
}

int VideoWidget::syncDelay() const { return m_syncDelayMs; }

// Replace existing ROI with a single rectangle ROI.
void VideoWidget::setUserRoi(const QRect &roi)
{
  // Clear previous polygons so this call behaves as "set", not "append".
  m_userRoiPolygons.clear();
  addUserRoi(roi);
}

// Convenience API: convert rectangle ROI into a 4-point polygon.
void VideoWidget::addUserRoi(const QRect &roi)
{
  // Normalize to handle reverse drag directions from user input.
  const QRect normalized = roi.normalized();
  if (normalized.isEmpty())
  {
    // Ignore invalid ROI input instead of storing a zero-area shape.
    return;
  }

  QPolygon polygon;
  // Build rectangle corners in clockwise order.
  polygon << normalized.topLeft() << normalized.topRight()
          << normalized.bottomRight() << normalized.bottomLeft();
  addUserRoiPolygon(polygon);
}

// Validate and store a polygon ROI used for filtering and overlay rendering.
void VideoWidget::addUserRoiPolygon(const QPolygon &polygon)
{
  if (polygon.size() < 3)
  {
    // A polygon needs at least 3 vertices.
    return;
  }

  const QRect bounds = polygon.boundingRect().normalized();
  if (bounds.isEmpty())
  {
    // Guard against degenerate geometry (collinear or collapsed points).
    return;
  }

  m_userRoiPolygons.append(polygon);
  // ROI mode is active when at least one valid polygon exists.
  m_roiEnabled = !m_userRoiPolygons.isEmpty();
  // Verbose debug output helps verify ROI import/drawing behavior.
  qDebug() << "[ROI][VideoWidget] polygon add: points=" << polygon.size()
           << "bounds=" << bounds << "count=" << m_userRoiPolygons.size();
  // Trigger repaint so new ROI overlay appears immediately.
  update();
}

// Enter interactive polygon drawing mode on top of the current frame.
void VideoWidget::startRoiDrawing()
{
  m_drawingMode = true;
  // Start with a clean temporary polygon buffer.
  m_drawingPolygonPoints.clear();
  m_hasHoverPoint = false;
  // Cross cursor gives visual feedback that clicks add polygon vertices.
  setCursor(Qt::CrossCursor);
  qDebug() << "[ROI][VideoWidget] polygon drawing mode enabled";
  update();
}

// Public completion entry point used by external UI actions.
bool VideoWidget::completeRoiDrawing()
{
  return finishPolygonDrawing();
}

void VideoWidget::updateMetadata(const QList<ObjectInfo> &objects)
{
  // Store metadata with arrival timestamp for delayed frame synchronization.
  m_metadataQueue.append(
      qMakePair(QDateTime::currentMSecsSinceEpoch(), objects));
}

void VideoWidget::updateFrame(const QImage &frame)
{
  // Current wall-clock time used to apply delayed metadata.
  qint64 now = QDateTime::currentMSecsSinceEpoch();

  // Apply the latest metadata snapshot whose delay window has elapsed.
  while (!m_metadataQueue.isEmpty())
  {
    if (m_metadataQueue.first().first + m_syncDelayMs <= now)
    {
      m_currentObjects = m_metadataQueue.takeFirst().second;
    }
    else
    {
      break;
    }
  }

  // Draw frame + overlays using the latest synchronized metadata.
  renderFrame(frame);
}

// Render current frame with object boxes, labels, ROI overlays, and OCR trigger.
void VideoWidget::renderFrame(const QImage &frame)
{
  // Remember source frame size for widget<->frame coordinate mapping.
  m_lastFrameSize = frame.size();
  QImage keyFrame = frame;
  QPainter painter(&keyFrame);

  // Default object box style.
  QPen pen(Qt::green, 3);
  painter.setPen(pen);

  // Label text style for object captions.
  QFont font = painter.font();
  font.setPointSize(14);
  font.setBold(true);
  painter.setFont(font);
  const auto &cfg = Config::instance();
  const double sourceHeight = static_cast<double>(cfg.sourceHeight());
  const double effectiveWidth = static_cast<double>(cfg.effectiveWidth());
  const double cropOffsetX = static_cast<double>(cfg.cropOffsetX());
  const QRegion roiRegion = roiRegionOnFrame(keyFrame.rect());

  // Draw user-defined ROI overlays for visual feedback.
  if (m_roiEnabled && !roiRegion.isEmpty())
  {
    // Red dashed outlines distinguish user ROI from detection boxes.
    painter.setPen(QPen(Qt::red, 2, Qt::DashLine));
    for (const QPolygon &polygon : std::as_const(m_userRoiPolygons))
    {
      if (polygon.size() >= 3)
      {
        painter.drawPolygon(polygon);
      }
    }
    // Restore default pen before drawing detections.
    painter.setPen(pen);
  }

  // Draw each synchronized object onto this frame.
  for (const ObjectInfo &obj : std::as_const(m_currentObjects))
  {
    // Convert detector coordinates (source space) to current frame space.
    const QRectF &srcRect = obj.rect;
    const double x =
        ((srcRect.x() - cropOffsetX) / effectiveWidth) * keyFrame.width();
    const double y = (srcRect.y() / sourceHeight) * keyFrame.height();
    const double w = (srcRect.width() / effectiveWidth) * keyFrame.width();
    const double h = (srcRect.height() / sourceHeight) * keyFrame.height();

    QRect rect(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w),
               static_cast<int>(h));
    if (m_roiEnabled && !roiRegion.intersects(rect))
    {
      // Skip objects outside ROI when ROI filtering is enabled.
      continue;
    }
    // Draw detection bounding box.
    painter.drawRect(rect);

    // Base object label always includes class name and tracking ID.
    QString text = QString("%1 (ID:%2)").arg(obj.type).arg(obj.id);

    // OCR branch is intentionally limited to detected license plates.
    if (obj.type == "LicensePlate")
    {
      // Clamp OCR crop to frame bounds to avoid invalid copy regions.
      QRect safeRect = rect.intersected(keyFrame.rect());
      if (!safeRect.isEmpty() && !m_ocrWatcher.isRunning())
      {
        // Run OCR asynchronously and throttle to one in-flight request.
        QImage roi = keyFrame.copy(safeRect);
        m_processingOcrId = obj.id;

        QFuture<QString> future = QtConcurrent::run(
            [this, roi]()
            { return m_ocrManager->performOcr(roi); });
        m_ocrWatcher.setFuture(future);
      }
    }

    // Append any extra metadata payload when available.
    if (!obj.extraInfo.isEmpty())
    {
      text += QString(" [%1]").arg(obj.extraInfo);
    }

    // Place label above the box and draw black background for readability.
    QRect textRect = painter.fontMetrics().boundingRect(text);
    textRect.moveTopLeft(rect.topLeft() - QPoint(0, textRect.height() + 5));

    painter.fillRect(textRect, Qt::black);
    painter.setPen(Qt::white);
    painter.drawText(textRect, Qt::AlignCenter, text);
    painter.setPen(pen);
  }

  if (m_currentObjects.isEmpty())
  {
    // Fallback hint while metadata stream has not produced objects yet.
    painter.setPen(Qt::yellow);
    painter.drawText(10, 30, "Waiting for AI Data...");
  }

  // End painting into the in-memory frame image.
  painter.end();

  // Fit the rendered frame into the label while preserving aspect ratio.
  setPixmap(QPixmap::fromImage(keyFrame).scaled(size(), Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation));
}

// Slot called when async OCR task finishes.
void VideoWidget::onOcrFinished()
{
  // Obtain OCR text from the watcher.
  QString result = m_ocrWatcher.result();
  if (!result.isEmpty())
  {
    // Emit only non-empty OCR output to avoid noisy downstream updates.
    qDebug() << "OID:" << m_processingOcrId << "Type: LicensePlate (Async)"
             << "OCR Result:" << result;
    emit ocrResult(m_processingOcrId, result);
  }
  // Reset tracking ID so stale ID is never reused accidentally.
  m_processingOcrId = -1;
}

// Paint preview overlays while user is interactively drawing polygon ROI.
void VideoWidget::paintEvent(QPaintEvent *event)
{
  // First let QLabel paint the current pixmap.
  QLabel::paintEvent(event);

  if (!m_drawingMode || m_lastFrameSize.isEmpty())
  {
    // Nothing extra to paint when ROI drawing mode is off or frame not ready.
    return;
  }

  const QRect pixRect = displayedPixmapRect();
  if (pixRect.isEmpty())
  {
    // Defensive exit when aspect-fit rectangle cannot be computed.
    return;
  }

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  // Preview line style for in-progress polygon.
  painter.setPen(QPen(Qt::red, 2, Qt::DashLine));

  const double sx = static_cast<double>(pixRect.width()) / m_lastFrameSize.width();
  const double sy = static_cast<double>(pixRect.height()) / m_lastFrameSize.height();
  // Map frame coordinates to widget coordinates for interactive ROI preview.
  auto toWidget = [&](const QPoint &framePoint) -> QPoint
  {
    return QPoint(pixRect.x() + static_cast<int>(framePoint.x() * sx),
                  pixRect.y() + static_cast<int>(framePoint.y() * sy));
  };

  QVector<QPoint> widgetPoints;
  // Reserve avoids repeated allocations while appending points.
  widgetPoints.reserve(m_drawingPolygonPoints.size());
  for (const QPoint &pt : std::as_const(m_drawingPolygonPoints))
  {
    widgetPoints.append(toWidget(pt));
  }

  // Draw polyline segments between already-placed vertices.
  for (int i = 1; i < widgetPoints.size(); ++i)
  {
    painter.drawLine(widgetPoints[i - 1], widgetPoints[i]);
  }

  // Draw dynamic segment from last vertex to current mouse hover point.
  if (!widgetPoints.isEmpty() && m_hasHoverPoint)
  {
    painter.drawLine(widgetPoints.last(), toWidget(m_hoverFramePoint));
  }

  // Preview closing edge to help user visualize final polygon shape.
  if (widgetPoints.size() >= 3)
  {
    const QPoint lastPoint =
        (m_hasHoverPoint ? toWidget(m_hoverFramePoint) : widgetPoints.last());
    painter.drawLine(lastPoint, widgetPoints.first());
  }

  // Draw visible handles for placed vertices.
  painter.setPen(QPen(Qt::yellow, 2));
  painter.setBrush(Qt::yellow);
  for (const QPoint &pt : std::as_const(widgetPoints))
  {
    painter.drawEllipse(pt, 3, 3);
  }
}

// Capture clicks as polygon points while drawing mode is active.
void VideoWidget::mousePressEvent(QMouseEvent *event)
{
  if (m_drawingMode && !m_lastFrameSize.isEmpty())
  {
    const QRect pixRect = displayedPixmapRect();
    const QPoint widgetPoint = event->position().toPoint();

    if (event->button() == Qt::LeftButton && pixRect.contains(widgetPoint))
    {
      // Capture polygon vertices in frame coordinates for resolution independence.
      const QPoint framePoint = mapWidgetToFrame(widgetPoint);
      m_drawingPolygonPoints.append(framePoint);
      m_hoverFramePoint = framePoint;
      m_hasHoverPoint = true;
      // Request immediate redraw so new vertex appears.
      update();
      return;
    }
  }
  // Default behavior when not consuming the event.
  QLabel::mousePressEvent(event);
}

// Update hover point so preview edge follows the mouse.
void VideoWidget::mouseMoveEvent(QMouseEvent *event)
{
  if (m_drawingMode && !m_lastFrameSize.isEmpty())
  {
    const QRect pixRect = displayedPixmapRect();
    const QPoint widgetPoint = event->position().toPoint();
    if (pixRect.contains(widgetPoint))
    {
      m_hoverFramePoint = mapWidgetToFrame(widgetPoint);
      m_hasHoverPoint = true;
    }
    else
    {
      // Stop preview edge when cursor leaves the displayed image area.
      m_hasHoverPoint = false;
    }
    update();
    return;
  }
  // Default behavior when not in drawing mode.
  QLabel::mouseMoveEvent(event);
}

void VideoWidget::mouseReleaseEvent(QMouseEvent *event)
{
  // Keep default QLabel release handling.
  QLabel::mouseReleaseEvent(event);
}

void VideoWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
  // Keep default QLabel double-click handling.
  QLabel::mouseDoubleClickEvent(event);
}

// Compute where the aspect-fitted pixmap is actually displayed inside the widget.
QRect VideoWidget::displayedPixmapRect() const
{
  if (m_lastFrameSize.isEmpty())
  {
    // No frame -> no displayed pixmap rectangle.
    return QRect();
  }
  const QSize scaledSize = m_lastFrameSize.scaled(size(), Qt::KeepAspectRatio);
  const int x = (width() - scaledSize.width()) / 2;
  const int y = (height() - scaledSize.height()) / 2;
  return QRect(x, y, scaledSize.width(), scaledSize.height());
}

// Convert widget-space mouse coordinates into source frame coordinates.
QPoint VideoWidget::mapWidgetToFrame(const QPoint &widgetPoint) const
{
  if (m_lastFrameSize.isEmpty())
  {
    // No valid mapping before first frame is rendered.
    return QPoint();
  }
  const QRect pixRect = displayedPixmapRect();
  if (pixRect.isEmpty())
  {
    // Keep caller safe when pixmap geometry is temporarily unavailable.
    return QPoint();
  }

  // Clamp cursor input to the displayed image area before normalization.
  const int clampedX = qBound(pixRect.left(), widgetPoint.x(), pixRect.right());
  const int clampedY = qBound(pixRect.top(), widgetPoint.y(), pixRect.bottom());
  const double nx = static_cast<double>(clampedX - pixRect.left()) / pixRect.width();
  const double ny = static_cast<double>(clampedY - pixRect.top()) / pixRect.height();

  // Convert normalized [0,1] coordinates back to frame pixel indices.
  const int frameX = qBound(0, static_cast<int>(nx * m_lastFrameSize.width()),
                            m_lastFrameSize.width() - 1);
  const int frameY = qBound(0, static_cast<int>(ny * m_lastFrameSize.height()),
                            m_lastFrameSize.height() - 1);
  return QPoint(frameX, frameY);
}

QRect VideoWidget::currentPreviewRoi() const
{
  // Legacy rectangular preview ROI built from drag start/current points.
  return QRect(m_dragStartFramePoint, m_dragCurrentFramePoint).normalized();
}

// Build a union region from all valid ROI polygons, clipped to frame bounds.
QRegion VideoWidget::roiRegionOnFrame(const QRect &frameRect) const
{
  QRegion region;
  const QRegion frameRegion(frameRect);
  for (const QPolygon &polygon : m_userRoiPolygons)
  {
    if (polygon.size() < 3)
    {
      // Skip invalid polygons defensively.
      continue;
    }
    // Winding fill handles concave polygons; intersection clips to frame.
    region = region.united(QRegion(polygon, Qt::WindingFill).intersected(frameRegion));
  }
  return region;
}

// Finalize interactive polygon drawing and publish ROI updates.
bool VideoWidget::finishPolygonDrawing()
{
  if (!m_drawingMode)
  {
    // Ignore accidental completion calls outside drawing mode.
    return false;
  }

  bool completed = false;
  if (m_drawingPolygonPoints.size() >= 3)
  {
    // Commit polygon ROI and notify both legacy(rect) and polygon listeners.
    QPolygon polygon(m_drawingPolygonPoints);
    addUserRoiPolygon(polygon);
    emit roiChanged(polygon.boundingRect());
    emit roiPolygonChanged(polygon);
    qDebug() << "[ROI][VideoWidget] polygon drawn: points=" << polygon.size()
             << "bounds=" << polygon.boundingRect();
    completed = true;
  }
  else
  {
    // Not enough points to build a valid polygon.
    qDebug() << "[ROI][VideoWidget] polygon canceled: need at least 3 points";
  }

  // Exit drawing mode and clear transient UI state regardless of completion.
  m_drawingMode = false;
  m_drawingPolygonPoints.clear();
  m_hasHoverPoint = false;
  unsetCursor();
  update();
  return completed;
}
