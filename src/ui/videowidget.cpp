#include "videowidget.h"
#include <QDebug>

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
        emit roiPolygonChanged(result.polygon);
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
