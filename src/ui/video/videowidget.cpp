#include "videowidget.h"
#include <QDebug>
#include <QtGlobal>

VideoWidget::VideoWidget(QWidget *parent) : QLabel(parent)
{
    setAlignment(Qt::AlignCenter);
    setStyleSheet("background-color: black;");
}

void VideoWidget::setUserRoi(const QRect &roi)
{
    m_roiState.setUserRoi(roi);
    update();
}

void VideoWidget::addUserRoi(const QRect &roi)
{
    m_roiState.addUserRoi(roi);
    update();
}

void VideoWidget::addUserRoiPolygon(const QPolygon &polygon)
{
    m_roiState.addUserRoiPolygon(polygon);
    update();
}

void VideoWidget::queueNormalizedRoiPolygons(const QList<QPolygonF> &normalizedPolygons)
{
    m_pendingNormalizedRoiPolygons = normalizedPolygons;
}

bool VideoWidget::removeRoiAt(int index)
{
    if (!m_pendingNormalizedRoiPolygons.isEmpty())
    {
        if (index < 0 || index >= m_pendingNormalizedRoiPolygons.size())
        {
            return false;
        }
        m_pendingNormalizedRoiPolygons.removeAt(index);
        return true;
    }

    const bool removed = m_roiState.removeRoiAt(index);
    if (removed)
    {
        update();
    }
    return removed;
}

int VideoWidget::roiCount() const
{
    return m_roiState.roiCount();
}

void VideoWidget::startRoiDrawing()
{
    m_roiState.startDrawing();
    setCursor(Qt::CrossCursor);
    qDebug() << "[ROI][VideoWidget] polygon drawing mode enabled";
    update();
}

bool VideoWidget::completeRoiDrawing()
{
    const RoiFinishResult result = m_roiState.finishDrawing();

    if (result.completed)
    {
        emit roiChanged(result.boundingRect);
        emit roiPolygonChanged(result.polygon, m_lastFrameSize);
        qDebug() << "[ROI][VideoWidget] polygon drawn: points=" << result.polygon.size()
                 << "bounds=" << result.boundingRect;
    }
    else
    {
        qDebug() << "[ROI][VideoWidget] polygon canceled: need at least 3 points";
    }

    unsetCursor();
    update();
    return result.completed;
}

void VideoWidget::updateMetadata(const QList<ObjectInfo> &objects)
{
    m_currentObjects = objects;
}

void VideoWidget::updateFrame(const QImage &frame)
{
    renderFrame(frame);
}

void VideoWidget::renderFrame(const QImage &frame)
{
    m_lastFrameSize = frame.size();
    if (!m_pendingNormalizedRoiPolygons.isEmpty() && !m_lastFrameSize.isEmpty())
    {
        for (const QPolygonF &normalizedPolygon : m_pendingNormalizedRoiPolygons)
        {
            QPolygon framePolygon;
            framePolygon.reserve(normalizedPolygon.size());
            for (const QPointF &normPoint : normalizedPolygon)
            {
                const int frameX = qBound(0,
                                          static_cast<int>(normPoint.x() * m_lastFrameSize.width()),
                                          m_lastFrameSize.width() - 1);
                const int frameY = qBound(0,
                                          static_cast<int>(normPoint.y() * m_lastFrameSize.height()),
                                          m_lastFrameSize.height() - 1);
                framePolygon << QPoint(frameX, frameY);
            }
            m_roiState.addUserRoiPolygon(framePolygon);
        }
        m_pendingNormalizedRoiPolygons.clear();
    }

    QList<OcrRequest> ocrRequests;
    const QImage composed =
        m_frameRenderer.compose(frame, m_currentObjects, m_roiState.roiPolygons(),
                               m_roiState.roiEnabled(), &ocrRequests);

    for (const OcrRequest &req : ocrRequests)
    {
        emit ocrRequested(req.objectId, req.crop);
    }

    setPixmap(QPixmap::fromImage(composed).scaled(
        size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void VideoWidget::paintEvent(QPaintEvent *event)
{
    QLabel::paintEvent(event);
    m_roiState.paintDrawingOverlay(this, m_lastFrameSize);
}

void VideoWidget::mousePressEvent(QMouseEvent *event)
{
    if (m_roiState.handleMousePress(event, size(), m_lastFrameSize))
    {
        update();
        return;
    }
    QLabel::mousePressEvent(event);
}

void VideoWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_roiState.handleMouseMove(event, size(), m_lastFrameSize))
    {
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
