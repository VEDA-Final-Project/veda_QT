#include "videowidget.h"
#include <QDebug>
#include <QPainter>
#include <utility>

VideoWidget::VideoWidget(QWidget *parent) : QLabel(parent)
{
    setAlignment(Qt::AlignCenter);
    setStyleSheet("background-color: black;");

    m_ocrManager = new OcrManager();
    const auto &cfg = Config::instance();
    if (!m_ocrManager->init(cfg.tessdataPath(), cfg.ocrLanguage()))
    {
        qDebug() << "Could not initialize OCR Manager.";
    }

    connect(&m_ocrWatcher, &QFutureWatcher<QString>::finished, this,
            &VideoWidget::onOcrFinished);
}

VideoWidget::~VideoWidget()
{
    if (m_ocrWatcher.isRunning())
    {
        m_ocrWatcher.waitForFinished();
    }
    delete m_ocrManager;
    m_ocrManager = nullptr;
}

void VideoWidget::setSyncDelay(int delayMs)
{
    m_syncDelayMs = qMax(0, delayMs);
}

int VideoWidget::syncDelay() const { return m_syncDelayMs; }

void VideoWidget::setUserRoi(const QRect &roi)
{
    m_userRoiPolygons.clear();
    addUserRoi(roi);
}

void VideoWidget::addUserRoi(const QRect &roi)
{
    const QRect normalized = roi.normalized();
    if (normalized.isEmpty())
    {
        return;
    }

    QPolygon polygon;
    polygon << normalized.topLeft() << normalized.topRight()
            << normalized.bottomRight() << normalized.bottomLeft();
    addUserRoiPolygon(polygon);
}

void VideoWidget::addUserRoiPolygon(const QPolygon &polygon)
{
    if (polygon.size() < 3)
    {
        return;
    }

    const QRect bounds = polygon.boundingRect().normalized();
    if (bounds.isEmpty())
    {
        return;
    }

    m_userRoiPolygons.append(polygon);
    m_roiEnabled = !m_userRoiPolygons.isEmpty();
    qDebug() << "[ROI][VideoWidget] polygon add: points=" << polygon.size()
             << "bounds=" << bounds << "count=" << m_userRoiPolygons.size();
    update();
}

void VideoWidget::startRoiDrawing()
{
    m_drawingMode = true;
    m_drawingPolygonPoints.clear();
    m_hasHoverPoint = false;
    setCursor(Qt::CrossCursor);
    qDebug() << "[ROI][VideoWidget] polygon drawing mode enabled";
    update();
}

bool VideoWidget::completeRoiDrawing()
{
    return finishPolygonDrawing();
}

void VideoWidget::updateMetadata(const QList<ObjectInfo> &objects)
{
    m_metadataQueue.append(
        qMakePair(QDateTime::currentMSecsSinceEpoch(), objects));
}

void VideoWidget::updateFrame(const QImage &frame)
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();

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

    renderFrame(frame);
}

void VideoWidget::renderFrame(const QImage &frame)
{
    m_lastFrameSize = frame.size();
    QImage keyFrame = frame;
    QPainter painter(&keyFrame);

    QPen pen(Qt::green, 3);
    painter.setPen(pen);

    QFont font = painter.font();
    font.setPointSize(14);
    font.setBold(true);
    painter.setFont(font);

    const auto &cfg = Config::instance();
    const double sourceHeight = static_cast<double>(cfg.sourceHeight());
    const double effectiveWidth = static_cast<double>(cfg.effectiveWidth());
    const double cropOffsetX = static_cast<double>(cfg.cropOffsetX());
    const QRegion roiRegion = roiRegionOnFrame(keyFrame.rect());

    if (m_roiEnabled && !roiRegion.isEmpty())
    {
        painter.setPen(QPen(Qt::red, 2, Qt::DashLine));
        for (const QPolygon &polygon : std::as_const(m_userRoiPolygons))
        {
            if (polygon.size() >= 3)
            {
                painter.drawPolygon(polygon);
            }
        }
        painter.setPen(pen);
    }

    for (const ObjectInfo &obj : std::as_const(m_currentObjects))
    {
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
            continue;
        }

        painter.drawRect(rect);

        QString text = QString("%1 (ID:%2)").arg(obj.type).arg(obj.id);

        if (obj.type == "LicensePlate")
        {
            QRect safeRect = rect.intersected(keyFrame.rect());
            if (!safeRect.isEmpty() && !m_ocrWatcher.isRunning())
            {
                QImage roi = keyFrame.copy(safeRect);
                m_processingOcrId = obj.id;

                QFuture<QString> future = QtConcurrent::run(
                    [this, roi]()
                    { return m_ocrManager->performOcr(roi); });
                m_ocrWatcher.setFuture(future);
            }
        }

        if (!obj.extraInfo.isEmpty())
        {
            text += QString(" [%1]").arg(obj.extraInfo);
        }

        QRect textRect = painter.fontMetrics().boundingRect(text);
        textRect.moveTopLeft(rect.topLeft() - QPoint(0, textRect.height() + 5));

        painter.fillRect(textRect, Qt::black);
        painter.setPen(Qt::white);
        painter.drawText(textRect, Qt::AlignCenter, text);
        painter.setPen(pen);
    }

    if (m_currentObjects.isEmpty())
    {
        painter.setPen(Qt::yellow);
        painter.drawText(10, 30, "Waiting for AI Data...");
    }

    painter.end();

    setPixmap(QPixmap::fromImage(keyFrame).scaled(
        size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void VideoWidget::onOcrFinished()
{
    QString result = m_ocrWatcher.result();
    if (!result.isEmpty())
    {
        qDebug() << "OID:" << m_processingOcrId << "Type: LicensePlate (Async)"
                 << "OCR Result:" << result;
        emit ocrResult(m_processingOcrId, result);
    }
    m_processingOcrId = -1;
}

void VideoWidget::paintEvent(QPaintEvent *event)
{
    QLabel::paintEvent(event);

    if (!m_drawingMode || m_lastFrameSize.isEmpty())
    {
        return;
    }

    const QRect pixRect = displayedPixmapRect();
    if (pixRect.isEmpty())
    {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(Qt::red, 2, Qt::DashLine));

    const double sx = static_cast<double>(pixRect.width()) / m_lastFrameSize.width();
    const double sy = static_cast<double>(pixRect.height()) / m_lastFrameSize.height();

    auto toWidget = [&](const QPoint &framePoint) -> QPoint
    {
        return QPoint(pixRect.x() + static_cast<int>(framePoint.x() * sx),
                      pixRect.y() + static_cast<int>(framePoint.y() * sy));
    };

    QVector<QPoint> widgetPoints;
    widgetPoints.reserve(m_drawingPolygonPoints.size());
    for (const QPoint &pt : std::as_const(m_drawingPolygonPoints))
    {
        widgetPoints.append(toWidget(pt));
    }

    for (int i = 1; i < widgetPoints.size(); ++i)
    {
        painter.drawLine(widgetPoints[i - 1], widgetPoints[i]);
    }

    if (!widgetPoints.isEmpty() && m_hasHoverPoint)
    {
        painter.drawLine(widgetPoints.last(), toWidget(m_hoverFramePoint));
    }

    if (widgetPoints.size() >= 3)
    {
        const QPoint lastPoint =
            (m_hasHoverPoint ? toWidget(m_hoverFramePoint) : widgetPoints.last());
        painter.drawLine(lastPoint, widgetPoints.first());
    }

    painter.setPen(QPen(Qt::yellow, 2));
    painter.setBrush(Qt::yellow);
    for (const QPoint &pt : std::as_const(widgetPoints))
    {
        painter.drawEllipse(pt, 3, 3);
    }
}

void VideoWidget::mousePressEvent(QMouseEvent *event)
{
    if (m_drawingMode && !m_lastFrameSize.isEmpty())
    {
        const QRect pixRect = displayedPixmapRect();
        const QPoint widgetPoint = event->position().toPoint();

        if (event->button() == Qt::LeftButton && pixRect.contains(widgetPoint))
        {
            const QPoint framePoint = mapWidgetToFrame(widgetPoint);
            m_drawingPolygonPoints.append(framePoint);
            m_hoverFramePoint = framePoint;
            m_hasHoverPoint = true;
            update();
            return;
        }
    }
    QLabel::mousePressEvent(event);
}

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
            m_hasHoverPoint = false;
        }
        update();
        return;
    }
    QLabel::mouseMoveEvent(event);
}

void VideoWidget::mouseReleaseEvent(QMouseEvent *event)
{
    QLabel::mouseReleaseEvent(event);
}

void VideoWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    QLabel::mouseDoubleClickEvent(event);
}

QRect VideoWidget::displayedPixmapRect() const
{
    if (m_lastFrameSize.isEmpty())
    {
        return QRect();
    }
    const QSize scaledSize = m_lastFrameSize.scaled(size(), Qt::KeepAspectRatio);
    const int x = (width() - scaledSize.width()) / 2;
    const int y = (height() - scaledSize.height()) / 2;
    return QRect(x, y, scaledSize.width(), scaledSize.height());
}

QPoint VideoWidget::mapWidgetToFrame(const QPoint &widgetPoint) const
{
    if (m_lastFrameSize.isEmpty())
    {
        return QPoint();
    }
    const QRect pixRect = displayedPixmapRect();
    if (pixRect.isEmpty())
    {
        return QPoint();
    }

    const int clampedX = qBound(pixRect.left(), widgetPoint.x(), pixRect.right());
    const int clampedY = qBound(pixRect.top(), widgetPoint.y(), pixRect.bottom());
    const double nx = static_cast<double>(clampedX - pixRect.left()) / pixRect.width();
    const double ny = static_cast<double>(clampedY - pixRect.top()) / pixRect.height();

    const int frameX = qBound(0, static_cast<int>(nx * m_lastFrameSize.width()),
                              m_lastFrameSize.width() - 1);
    const int frameY = qBound(0, static_cast<int>(ny * m_lastFrameSize.height()),
                              m_lastFrameSize.height() - 1);
    return QPoint(frameX, frameY);
}

QRect VideoWidget::currentPreviewRoi() const
{
    return QRect(m_dragStartFramePoint, m_dragCurrentFramePoint).normalized();
}

QRegion VideoWidget::roiRegionOnFrame(const QRect &frameRect) const
{
    QRegion region;
    const QRegion frameRegion(frameRect);
    for (const QPolygon &polygon : m_userRoiPolygons)
    {
        if (polygon.size() < 3)
        {
            continue;
        }
        region = region.united(QRegion(polygon, Qt::WindingFill).intersected(frameRegion));
    }
    return region;
}

bool VideoWidget::finishPolygonDrawing()
{
    if (!m_drawingMode)
    {
        return false;
    }

    bool completed = false;
    if (m_drawingPolygonPoints.size() >= 3)
    {
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
        qDebug() << "[ROI][VideoWidget] polygon canceled: need at least 3 points";
    }

    m_drawingMode = false;
    m_drawingPolygonPoints.clear();
    m_hasHoverPoint = false;
    unsetCursor();
    update();
    return completed;
}
