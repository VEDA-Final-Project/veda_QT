/**
 * rpi_db_viewer — RPi 터치스크린용 주차 DB 뷰어
 *
 * QT 앱(DbBroadcastServer, 포트 12346)에 TCP 클라이언트로 연결하여
 * 1초 주기로 수신되는 주차 DB JSON을 파싱, QTableWidget에 표시합니다.
 *
 * 빌드:
 *   cmake -B build -S .
 *   cmake --build build
 *
 * 실행 (RPi에서):
 *   ./rpi_db_viewer <QT앱_IP> [port=12346]
 *   예: ./rpi_db_viewer 192.168.0.10
 */

#include <QApplication>
#include <QDateTime>
#include <QFont>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QStatusBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTcpSocket>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

// ─────────────────────────────────────────────
// DbViewerWindow : 수신 데이터를 테이블로 표시
// ─────────────────────────────────────────────
class DbViewerWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit DbViewerWindow(const QString &host, quint16 port,
                            QWidget *parent = nullptr)
        : QMainWindow(parent) {
        setWindowTitle("주차 DB 뷰어");
        showFullScreen(); // 터치스크린 전체화면

        // ── 중앙 위젯 ──
        auto *central = new QWidget(this);
        setCentralWidget(central);
        auto *layout = new QVBoxLayout(central);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(6);

        // ── 상단 레이블 ──
        m_titleLabel = new QLabel("주차 현황", this);
        QFont titleFont = m_titleLabel->font();
        titleFont.setPointSize(18);
        titleFont.setBold(true);
        m_titleLabel->setFont(titleFont);
        m_titleLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_titleLabel);

        // ── 테이블 ──
        m_table = new QTableWidget(this);
        m_table->setColumnCount(7);
        m_table->setHorizontalHeaderLabels(
            {"ID", "번호판", "구역", "입차 시각", "출차 시각", "결제", "요금(원)"});
        m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_table->verticalHeader()->setVisible(false);
        m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_table->setAlternatingRowColors(true);

        // 터치스크린 최적화 – 큰 폰트 & 행 높이
        QFont tableFont = m_table->font();
        tableFont.setPointSize(14);
        m_table->setFont(tableFont);
        m_table->verticalHeader()->setDefaultSectionSize(46);
        m_table->horizontalHeader()->setFont(tableFont);

        layout->addWidget(m_table);

        // ── 상태바 ──
        m_statusLabel = new QLabel("연결 중...", this);
        statusBar()->addWidget(m_statusLabel, 1);

        // ── 소켓 ──
        m_socket = new QTcpSocket(this);
        connect(m_socket, &QTcpSocket::connected,
                this, &DbViewerWindow::onConnected);
        connect(m_socket, &QTcpSocket::disconnected,
                this, &DbViewerWindow::onDisconnected);
        connect(m_socket, &QTcpSocket::readyRead,
                this, &DbViewerWindow::onReadyRead);
        connect(m_socket, &QTcpSocket::errorOccurred,
                this, &DbViewerWindow::onSocketError);

        // ── 재연결 타이머 ──
        m_reconnectTimer = new QTimer(this);
        m_reconnectTimer->setSingleShot(true);
        connect(m_reconnectTimer, &QTimer::timeout,
                this, [this, host, port]() { connectToHost(host, port); });

        connectToHost(host, port);
    }

private slots:
    void onConnected() {
        m_statusLabel->setText(
            QString("연결됨 — %1").arg(
                QDateTime::currentDateTime().toString("yyyy/MM/dd HH:mm:ss")));
        setStatus("연결됨");
    }

    void onDisconnected() {
        setStatus("연결 끊김 — 재연결 중...");
        m_reconnectTimer->start(3000);
    }

    void onSocketError() {
        setStatus(QString("오류: %1 — 재연결 중...").arg(m_socket->errorString()));
        if (!m_reconnectTimer->isActive()) {
            m_reconnectTimer->start(3000);
        }
    }

    void onReadyRead() {
        m_readBuffer.append(m_socket->readAll());
        while (true) {
            const int nl = m_readBuffer.indexOf('\n');
            if (nl < 0) break;
            const QByteArray line = m_readBuffer.left(nl).trimmed();
            m_readBuffer.remove(0, nl + 1);
            if (!line.isEmpty()) {
                parseJson(line);
            }
        }
    }

private:
    void connectToHost(const QString &host, quint16 port) {
        if (m_socket->state() != QAbstractSocket::UnconnectedState) {
            m_socket->abort();
        }
        setStatus(QString("연결 중... (%1:%2)").arg(host).arg(port));
        m_socket->connectToHost(host, port);
    }

    void setStatus(const QString &msg) {
        const QString ts =
            QDateTime::currentDateTime().toString("HH:mm:ss");
        m_statusLabel->setText(QString("[%1] %2").arg(ts, msg));
    }

    void parseJson(const QByteArray &data) {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            return;
        }
        const QJsonObject obj = doc.object();
        if (obj.value("type").toString() != "db_data") {
            return;
        }
        populateTable(obj.value("rows").toArray());
        setStatus(QString("마지막 수신: %1 (%2건)")
                      .arg(QDateTime::currentDateTime().toString("HH:mm:ss"))
                      .arg(obj.value("rows").toArray().size()));
    }

    void populateTable(const QJsonArray &rows) {
        m_table->setRowCount(0);
        for (int i = 0; i < rows.size(); ++i) {
            const QJsonObject row = rows[i].toObject();
            m_table->insertRow(i);

            auto *item0 = new QTableWidgetItem(QString::number(row["id"].toInt()));
            item0->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(i, 0, item0);
            m_table->setItem(i, 1, new QTableWidgetItem(row["plate"].toString()));
            m_table->setItem(i, 2, new QTableWidgetItem(row["zone"].toString()));
            m_table->setItem(i, 3, new QTableWidgetItem(formatDt(row["entry_time"].toString())));
            m_table->setItem(i, 4, new QTableWidgetItem(formatDt(row["exit_time"].toString())));

            const QString status = row["pay_status"].toString();
            auto *statusItem = new QTableWidgetItem(status);
            statusItem->setTextAlignment(Qt::AlignCenter);
            if (status == "paid") {
                statusItem->setForeground(QColor(0, 160, 80));
            } else if (!status.isEmpty()) {
                statusItem->setForeground(QColor(220, 60, 60));
            }
            m_table->setItem(i, 5, statusItem);

            auto *feeItem = new QTableWidgetItem(
                QString::number(row["fee"].toInt()));
            feeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(i, 6, feeItem);
        }
    }

    static QString formatDt(const QString &raw) {
        if (raw.isEmpty()) return QString();
        QDateTime dt = QDateTime::fromString(raw, Qt::ISODateWithMs);
        if (!dt.isValid()) dt = QDateTime::fromString(raw, Qt::ISODate);
        if (!dt.isValid()) return raw;
        return dt.toLocalTime().toString("yyyy/MM/dd HH:mm");
    }

    QTcpSocket   *m_socket         = nullptr;
    QTimer       *m_reconnectTimer = nullptr;
    QTableWidget *m_table          = nullptr;
    QLabel       *m_titleLabel     = nullptr;
    QLabel       *m_statusLabel    = nullptr;
    QByteArray    m_readBuffer;
};

// ─────────────────────────────────────────────
// main
// ─────────────────────────────────────────────
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    const QString host = (argc >= 2) ? QString::fromLocal8Bit(argv[1])
                                     : QStringLiteral("192.168.0.1");
    const quint16 port = (argc >= 3) ? static_cast<quint16>(atoi(argv[2]))
                                     : 12346;

    DbViewerWindow window(host, port);
    window.show();
    return app.exec();
}

#include "main.moc"
