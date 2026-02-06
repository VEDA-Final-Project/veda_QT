
#include "mainwindow.h"
#include <QPushButton>
#include <QVBoxLayout>
#include <QPainter>
#include <QDateTime>
#include <QDebug>
#include <QTextEdit> // Added for QTextEdit

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout *layout = new QVBoxLayout(centralWidget);

    // 버튼 레이아웃 (가로 배치)
    QHBoxLayout *btnLayout = new QHBoxLayout();

    QPushButton *btnPlay = new QPushButton("CCTV 보기", this);
    QPushButton *btnExit = new QPushButton("종료", this);

    // 싱크 조절 UI
    QPushButton *btnSyncDown = new QPushButton("<< Sync(-)", this);
    QPushButton *btnSyncUp = new QPushButton("Sync(+) >>", this);
    m_lblSync = new QLabel("Delay: 0ms", this);

    btnLayout->addWidget(btnPlay);
    btnLayout->addWidget(btnExit);
    btnLayout->addSpacing(20);
    btnLayout->addWidget(btnSyncDown);
    btnLayout->addWidget(m_lblSync);
    btnLayout->addWidget(btnSyncUp);

    layout->addLayout(btnLayout);

    m_videoLabel = new QLabel(this);
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setStyleSheet("background-color: black;");
    layout->addWidget(m_videoLabel);

    // 로그 출력용 TextEdit
    m_logView = new QTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setMaximumHeight(100);
    layout->addWidget(m_logView);

    // 비디오 스레드 초기화
    m_videoThread = new VideoThread(this);
    m_metadataThread = new MetadataThread(this);

    m_syncDelayMs = 0; // 초기 딜레이 0ms

    connect(btnPlay, &QPushButton::clicked, this, &MainWindow::playCctv);
    connect(btnExit, &QPushButton::clicked, this, &MainWindow::close);

    // 싱크 조절 연결
    connect(btnSyncDown, &QPushButton::clicked, this, &MainWindow::decreaseSync);
    connect(btnSyncUp, &QPushButton::clicked, this, &MainWindow::increaseSync);

    connect(m_videoThread, &VideoThread::frameCaptured, this, &MainWindow::updateFrame);

    connect(m_metadataThread, &MetadataThread::metadataReceived, this, &MainWindow::updateMetadata);
    connect(m_metadataThread, &MetadataThread::logMessage, this, &MainWindow::logMessage);

    resize(800, 600);
}

MainWindow::~MainWindow() {
    if (m_videoThread->isRunning()) {
        m_videoThread->stop();
        m_videoThread->wait();
    }
    if (m_metadataThread->isRunning()) {
        m_metadataThread->stop();
        m_metadataThread->wait();
    }
}

void MainWindow::playCctv() {
    if (m_videoThread->isRunning()) return;

    // RTSP 주소
    QString ip = "192.168.0.12";
    QString id = "admin";
    QString pw = "5hanwha!";

    // 비디오 시작
    QString url = QString("rtsp://%1:%2@%3/profile2/media.smp").arg(id, pw, ip);
    m_videoThread->setUrl(url);
    m_videoThread->start();

    // 메타데이터 시작
    m_metadataThread->setConnectionInfo(ip, id, pw);
    m_metadataThread->start();
}

void MainWindow::close() {
    if (m_videoThread->isRunning()) {
        m_videoThread->stop();
        m_videoThread->wait();
    }
    if (m_metadataThread->isRunning()) {
        m_metadataThread->stop();
        m_metadataThread->wait();
    }
    QMainWindow::close();
}

void MainWindow::decreaseSync() {
    m_syncDelayMs -= 100;
    // 음수 딜레이는 사실상 0과 같음 (미래 데이터를 보여줄 순 없으니, 즉시 보여주기)
    if (m_syncDelayMs < 0) m_syncDelayMs = 0;
    updateSyncLabel();
}

void MainWindow::increaseSync() {
    m_syncDelayMs += 100;
    updateSyncLabel();
}

void MainWindow::updateSyncLabel() {
    m_lblSync->setText(QString("Delay: %1ms").arg(m_syncDelayMs));
}

void MainWindow::updateMetadata(const QList<ObjectInfo> &objects) {
    // 바로 적용하지 않고 큐에 넣음 (현재 시간과 함께)
    m_metadataQueue.append(qMakePair(QDateTime::currentMSecsSinceEpoch(), objects));
}

void MainWindow::logMessage(const QString &msg) {
    qDebug() << "[Metadata]" << msg;
}

void MainWindow::updateFrame(const QImage &frame) {
    // [싱크 로직] 큐에 쌓인 메타데이터 중, 딜레이 시간이 지난 것을 꺼내옴
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    while (!m_metadataQueue.isEmpty()) {
        // (수신시간 + 딜레이)가 현재시간보다 같거나 작으면 -> 이제 보여줄 때가 됨
        if (m_metadataQueue.first().first + m_syncDelayMs <= now) {
            m_currentObjects = m_metadataQueue.takeFirst().second;
        } else {
            // 아직 보여줄 때가 안 된 데이터가 앞에 있으면 중단
            break;
        }
    }

    QImage keyFrame = frame;

    QPainter painter(&keyFrame);

    QPen pen(Qt::green, 3); // 실제 데이터는 초록색으로 표시
    painter.setPen(pen);

    QFont font = painter.font();
    font.setPointSize(14);
    font.setBold(true);
    painter.setFont(font);

    // 실제 메타데이터 그리기
    for (const ObjectInfo &obj : m_currentObjects) {
        // XML 데이터 분석 결과 좌표 범위가 0~1000 이 아니라 큰 값(예: 1986.0)으로 옴.
        // 카메라 해상도에 맞춘 픽셀 좌표로 추정됨.
        // 임시로 5MP (2592 x 1944) 또는 4K (3840 x 2160) 중 하나로 가정 후 비율 계산.
        // 현재 bottom이 1986인 것으로 보아 높이가 약 1944(5MP) 또는 2160(4K)일 가능성 높음.
        // 안전하게 비율계산을 위해 SOURCE_WIDTH/HEIGHT 상수를 정의하여 사용 권장.

        // 일단 사용자 테스트를 위해 1920x1080(FHD) 기준으로 보정 시도.
        // 만약 박스가 너무 작게 나오면 소스 해상도를 더 키워야 함.
        // XML에 포함된 Transformation Scale (0.000521)을 역산해보면
        // 2 / 0.000521 = 3838.7 => 약 3840 (4K UHD)
        // 2 / 0.000926 = 2159.8 => 약 2160 (4K UHD)
        // 즉, 5MP가 아니라 4K 해상도 기준으로 좌표가 오고 있습니다.

        // 화면 비율 불일치 보정 (16:9 AI -> 4:3 Video)
        // AI는 4K(3840x2160, 16:9) 전체를 보지만,
        // 영상은 800x600(4:3)으로 오므로 좌우가 잘렸을(Crop) 확률이 높음.

        const float SOURCE_TOTAL_WIDTH = 3840.0f;
        const float SOURCE_HEIGHT = 2160.0f;

        // 4:3 비율일 때 유효한 가로 폭 계산
        // Height 2160 기준 4:3 Width = 2160 * (4.0/3.0) = 2880
        const float EFFECTIVE_WIDTH = 2880.0f;
        const float CROP_OFFSET_X = (SOURCE_TOTAL_WIDTH - EFFECTIVE_WIDTH) / 2.0f; // (3840-2880)/2 = 480

        // 좌표 보정: (원본X - 오프셋) / 유효폭 * 화면폭
        int x = ((obj.rect.x() - CROP_OFFSET_X) / EFFECTIVE_WIDTH) * keyFrame.width();
        int y = (obj.rect.y() / SOURCE_HEIGHT) * keyFrame.height();

        // 폭과 높이도 동일한 비율로 스케일링
        int w = (obj.rect.width() / EFFECTIVE_WIDTH) * keyFrame.width();
        int h = (obj.rect.height() / SOURCE_HEIGHT) * keyFrame.height();

        // 잘려나간 영역의 객체는 안 그리거나 클리핑됨 (Qt Painter가 알아서 클리핑함)

        QRect rect(x, y, w, h);
        painter.drawRect(rect);

        // 텍스트 배경 그리기 (가독성 위해)
        QString text = QString("%1 (ID:%2)").arg(obj.type).arg(obj.id);
        if (!obj.extraInfo.isEmpty()) {
            text += QString(" [%1]").arg(obj.extraInfo);
        }

        QRect textRect = painter.fontMetrics().boundingRect(text);
        textRect.moveTopLeft(rect.topLeft() - QPoint(0, textRect.height() + 5));

        painter.fillRect(textRect, Qt::black);
        painter.setPen(Qt::white);
        painter.drawText(textRect, Qt::AlignCenter, text);
        painter.setPen(pen); // 다시 초록색 복구
    }

    // 아무 데이터가 없으면 좌측 상단에 상태 표시
    if (m_currentObjects.isEmpty()) {
        painter.setPen(Qt::yellow);
        painter.drawText(10, 30, "Waiting for AI Data...");
    }

    painter.end();

    m_videoLabel->setPixmap(QPixmap::fromImage(keyFrame).scaled(m_videoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
