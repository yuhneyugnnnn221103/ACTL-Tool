#include "main_window.h"
#include "overview_grid.h"
#include "auth.h"
#include "firmware_page.h"
#include "psu_config_page.h"
#include "psu_grid.h"
#include "psu_page.h"
#include "trb_config_page.h"
#include "trb_detail_page.h"
#include "../proto/psu_monitor_proto.h"
#include "../proto/trb_monitor_proto.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QUrl>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>

namespace ui {

using model::Status;

namespace {
QString pill(const QString &text, const QColor &bg, const QColor &fg = Qt::white)
{
    return QStringLiteral("<span style='background:%1;color:%2;'>&nbsp;%3&nbsp;</span>")
        .arg(bg.name(), fg.name(), text);
}
QString trbName(int mb, int trb) { return QStringLiteral("MB%1 / TRB%2").arg(mb).arg(trb); }
}

MainWindow::MainWindow(const AppContext &ctx, QWidget *parent)
    : QMainWindow(parent), m_ctx(ctx), m_store(ctx.store)
{
    setWindowTitle(QStringLiteral("ACTL Tool"));

    // Điều hướng trái + các trang.
    m_nav = new QListWidget;
    m_nav->setFixedWidth(120);
    m_pages = new QStackedWidget;
    m_nav->addItem(QStringLiteral("Tổng quan"));
    m_pages->addWidget(buildOverviewPage());
    m_nav->addItem(QStringLiteral("Chi tiết TRB"));
    m_pages->addWidget(m_detail = new TrbDetailPage(m_store, ctx.trbControl, ctx.thresholds));
    m_nav->addItem(QStringLiteral("Cấu hình TRB"));
    m_pages->addWidget(new TrbConfigPage(ctx));
    m_nav->addItem(QStringLiteral("PSU"));
    m_pages->addWidget(m_psu = new PsuPage(ctx));
    m_nav->addItem(QStringLiteral("Cấu hình PSU"));
    m_pages->addWidget(new PsuConfigPage(ctx));
    m_nav->addItem(QStringLiteral("Nạp code"));
    m_pages->addWidget(new FirmwarePage(ctx));
    connect(m_nav, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    m_nav->setCurrentRow(0);
    connect(m_detail, &TrbDetailPage::deviceChanged, m_grid, &OverviewGrid::select);
    connect(ctx.trbControl, &services::TrbControl::commandFinished, this, [this](const QString &d, bool sent) {
        logEvent((sent ? QStringLiteral("Đã gửi (không chờ ACK): ") : QStringLiteral("KHÔNG gửi được, chưa kết nối Gateway: ")) + d);
    });
    connect(ctx.trbConfig, &services::TrbConfig::deviceFinished, this, [this](int mb, int trb, bool ok, const QString &m) {
        logEvent(QStringLiteral("Cấu hình %1: %2%3").arg(trbName(mb, trb), ok ? QString() : QStringLiteral("LỖI, "), m));
    });
    connect(ctx.psuControl, &services::PsuControl::commandFinished, this, [this](const QString &d, bool sent) {
        logEvent((sent ? QStringLiteral("Đã gửi (không chờ ACK): ") : QStringLiteral("KHÔNG gửi được, chưa kết nối Gateway: ")) + d);
    });
    connect(ctx.psuConfig, &services::PsuConfig::deviceFinished, this, [this](int addr, bool ok, const QString &m) {
        logEvent(QStringLiteral("Cấu hình PSU %1: %2%3").arg(addr).arg(ok ? QString() : QStringLiteral("LỖI, "), m));
    });
    connect(ctx.psuStore, &model::PsuStore::psuStatusChanged, this, [this](int addr, Status from, Status to) {
        m_listDirty = true;
        if (from == Status::NoData && to == Status::Ok) return; // lần đầu thấy PSU: không cần báo
        logEvent(QStringLiteral("PSU %1: %2 → %3").arg(addr).arg(statusText(from), statusText(to)));
    });
    connect(ctx.fpgaOta, &services::FpgaOta::finished, this, [this](const QString &s) { logEvent(QStringLiteral("Nạp FPGA: ") + s); });
    connect(ctx.stmOta, &services::StmOta::deviceFinished, this, [this](int addr, bool ok, const QString &m) {
        logEvent(QStringLiteral("Nạp STM32 PSU %1: %2%3").arg(addr).arg(ok ? QString() : QStringLiteral("LỖI, "), m));
    });
    connect(ctx.auth, &Auth::lockedChanged, this, [this](bool locked) {
        logEvent(locked ? QStringLiteral("Đã khóa chế độ kỹ sư") : QStringLiteral("Đã mở khóa chế độ kỹ sư"));
    });

    auto *body = new QWidget;
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->addWidget(m_nav);
    bodyLayout->addWidget(m_pages, 1);

    auto *split = new QSplitter(Qt::Vertical);
    split->addWidget(body);
    split->addWidget(buildLogPanel());
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 1);

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->addWidget(buildTopBar());
    layout->addWidget(split, 1);
    setCentralWidget(central);

    connect(m_store, &model::DeviceStore::trbStatusChanged, this,
            [this](int mb, int trb, Status from, Status to) {
        m_listDirty = true;
        if (from == Status::NoData && to == Status::Ok) return; // lần đầu thấy thiết bị: không cần báo
        logEvent(QStringLiteral("%1: %2 → %3").arg(trbName(mb, trb), statusText(from), statusText(to)));
    });

    watchLink(ctx.monitorLink, QStringLiteral("Giám sát (TCP)"), m_tcpPill, m_tcpButton);
    watchLink(ctx.serviceLink, QStringLiteral("Cấu hình (RS485)"), m_comPill, m_comButton);
    connect(ctx.monitorLink, &core::Link::rxErrorsChanged, this, [this](quint64 crc, quint64 dropped) {
        m_rxErrLabel->setText(QStringLiteral("Lỗi CRC: %1 · Byte bỏ qua: %2").arg(crc).arg(dropped));
    });

    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainWindow::refresh);
    timer->start(200);
    refresh();
}

QWidget *MainWindow::buildTopBar()
{
    const Settings &cfg = m_ctx.settings;
    auto *bar = new QWidget;
    auto *l = new QHBoxLayout(bar);
    l->setContentsMargins(0, 0, 0, 0);

    // Đường giám sát: PC là TCP server, Gateway kết nối tới.
    m_tcpAddress = new QLineEdit(cfg.monitorAddress);
    m_tcpAddress->setFixedWidth(110);
    m_tcpAddress->setToolTip(QStringLiteral("Địa chỉ IP của PC để lắng nghe (0.0.0.0 = mọi card mạng)"));
    m_tcpPort = new QSpinBox;
    m_tcpPort->setRange(1, 65535);
    m_tcpPort->setValue(cfg.monitorPort);
    m_tcpButton = new QPushButton;
    m_tcpPill = new QLabel;
    connect(m_tcpButton, &QPushButton::clicked, this, &MainWindow::toggleTcp);
    l->addWidget(m_tcpPill);
    if (m_ctx.tcp) {
        l->addWidget(m_tcpAddress);
        l->addWidget(m_tcpPort);
        l->addWidget(m_tcpButton);
    }
    l->addSpacing(16);

    // Đường cấu hình/nạp code: cổng COM (RS485).
    m_comPort = new QComboBox;
    m_comPort->setMinimumWidth(90);
    m_comBaud = new QComboBox;
    m_comBaud->setEditable(true);
    m_comBaud->addItems({"115200", "460800", "921600", "1000000"});
    m_comBaud->setCurrentText(QString::number(cfg.serviceBaud));
    auto *rescan = new QPushButton(QStringLiteral("↻"));
    rescan->setFixedWidth(28);
    rescan->setToolTip(QStringLiteral("Quét lại cổng COM"));
    m_comButton = new QPushButton;
    m_comPill = new QLabel;
    connect(rescan, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    connect(m_comButton, &QPushButton::clicked, this, &MainWindow::toggleSerial);
    refreshPorts();
    l->addWidget(m_comPill);
    l->addWidget(m_comPort);
    l->addWidget(rescan);
    l->addWidget(m_comBaud);
    l->addWidget(m_comButton);
    l->addSpacing(16);

    m_rxErrLabel = new QLabel(QStringLiteral("Lỗi CRC: 0 · Byte bỏ qua: 0"));
    m_counters = new QLabel;
    l->addWidget(m_rxErrLabel);
    l->addStretch(1);
    l->addWidget(m_counters);
    return bar;
}

// Theo dõi một link: nhãn trạng thái, chữ trên nút, sự kiện, hex thô.
void MainWindow::watchLink(core::Link *link, const QString &title, QLabel *pillLabel, QPushButton *button)
{
    using S = core::Transport::State;
    const bool isTcp = link == m_ctx.monitorLink;
    auto show = [=](S s) {
        (isTcp ? m_tcpState : m_comState) = s;
        const QString state = s == S::Connected ? QStringLiteral("đã kết nối")
                            : s == S::Waiting   ? QStringLiteral("chờ Gateway") : QStringLiteral("đóng");
        pillLabel->setText(pill(title + QStringLiteral(": ") + state,
                                s == S::Connected ? QColor(0x3B6D11) : s == S::Waiting ? QColor(0x854F0B) : QColor(0x5F5E5A)));
        button->setText(s == S::Closed ? (isTcp ? QStringLiteral("Lắng nghe") : QStringLiteral("Mở")) : QStringLiteral("Đóng"));
        m_tcpAddress->setEnabled(m_tcpState == S::Closed);
        m_tcpPort->setEnabled(m_tcpState == S::Closed);
        m_comPort->setEnabled(m_comState == S::Closed);
        m_comBaud->setEnabled(m_comState == S::Closed);
        return title + QStringLiteral(": ") + state;
    };
    show(S::Closed);
    connect(link, &core::Link::stateChanged, this, [=](S s) { logEvent(show(s)); });
    connect(link, &core::Link::errorOccurred, this,
            [=](const QString &m) { logEvent(QStringLiteral("Lỗi %1: %2").arg(title, m)); });

    auto hex = [=](const char *dir, const QByteArray &d) {
        if (m_hexEnable->isChecked())
            m_hexLog->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss.zzz ")
                                      + QStringLiteral("[%1] %2 ").arg(link->name(), QLatin1String(dir))
                                      + QString::fromLatin1(d.toHex(' ').toUpper()));
    };
    connect(link, &core::Link::frameReceived, this, [=](const core::Frame &f) { hex("RX", f.raw); });
    connect(link, &core::Link::rawSent, this, [=](const QByteArray &d) { hex("TX", d); });
}

void MainWindow::toggleTcp()
{
    core::Link *link = m_ctx.monitorLink;
    if (m_tcpState != core::Transport::State::Closed) { QMetaObject::invokeMethod(link, &core::Link::stop); return; }
    const QHostAddress addr(m_tcpAddress->text().trimmed());
    if (addr.isNull()) { logEvent(QStringLiteral("Địa chỉ IP không hợp lệ: ") + m_tcpAddress->text()); return; }
    const quint16 port = quint16(m_tcpPort->value());
    Settings::save("monitor/address", addr.toString());
    Settings::save("monitor/port", port);
    core::TcpServerTransport *t = m_ctx.tcp;
    QMetaObject::invokeMethod(link, [=] { t->configure(addr, port); link->start(); });
}

void MainWindow::toggleSerial()
{
    core::Link *link = m_ctx.serviceLink;
    if (m_comState != core::Transport::State::Closed) { QMetaObject::invokeMethod(link, &core::Link::stop); return; }
    const QString port = m_comPort->currentText();
    const int baud = m_comBaud->currentText().toInt();
    if (port.isEmpty() || baud <= 0) { logEvent(QStringLiteral("Chưa chọn cổng COM hoặc baudrate không hợp lệ")); return; }
    Settings::save("service/port", port);
    Settings::save("service/baud", baud);
    core::SerialTransport *t = m_ctx.serial;
    QMetaObject::invokeMethod(link, [=] { t->configure(port, baud); link->start(); });
}

void MainWindow::refreshPorts()
{
    const QString keep = m_comPort->count() ? m_comPort->currentText() : m_ctx.settings.servicePort;
    m_comPort->clear();
    for (const QSerialPortInfo &p : QSerialPortInfo::availablePorts()) m_comPort->addItem(p.portName());
    if (m_comPort->findText(keep) >= 0) m_comPort->setCurrentText(keep);
}

QWidget *MainWindow::buildOverviewPage()
{
    m_grid = new OverviewGrid(m_store);
    m_grid->setTooltipProvider([this](int mb, int trb) { return trbTooltip(mb, trb); });

    m_abnormal = new QListWidget;
    connect(m_abnormal, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        if (it->data(Qt::UserRole + 2).isValid()) showPsu(it->data(Qt::UserRole + 2).toInt()); // PSU
        else showDetail(it->data(Qt::UserRole).toInt(), it->data(Qt::UserRole + 1).toInt());
    });
    connect(m_grid, &OverviewGrid::trbClicked, this, &MainWindow::showDetail);
    auto *side = new QWidget;
    side->setFixedWidth(240);
    auto *sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->addWidget(new QLabel(QStringLiteral("Thiết bị bất thường")));
    sideLayout->addWidget(m_abnormal, 1);

    m_psuGrid = new PsuGrid(m_ctx.psuStore);
    m_psuGrid->setTooltipProvider([this](int addr, int cluster) { return psuTooltip(addr, cluster); });
    connect(m_psuGrid, &PsuGrid::psuClicked, this, &MainWindow::showPsu);
    auto *grids = new QVBoxLayout;
    grids->addWidget(m_grid, 1);
    grids->addWidget(new QLabel(QStringLiteral("PSU · 4 cụm DCM")));
    grids->addWidget(m_psuGrid);

    auto *page = new QWidget;
    auto *l = new QHBoxLayout(page);
    l->addLayout(grids, 1);
    l->addWidget(side);
    return page;
}

QWidget *MainWindow::buildLogPanel()
{
    m_eventLog = new QPlainTextEdit;
    m_hexLog = new QPlainTextEdit;
    for (QPlainTextEdit *e : {m_eventLog, m_hexLog}) {
        e->setReadOnly(true);
        e->setMaximumBlockCount(5000);
        e->setLineWrapMode(QPlainTextEdit::NoWrap);
        e->setFont(QFont(QStringLiteral("monospace")));
    }
    m_hexEnable = new QCheckBox(QStringLiteral("Hiện bản tin hex"));
    auto *clear = new QPushButton(QStringLiteral("Xóa"));

    auto *tabs = new QTabWidget;
    tabs->addTab(m_eventLog, QStringLiteral("Sự kiện"));
    tabs->addTab(m_hexLog, QStringLiteral("Hex thô"));
    auto *corner = new QWidget;
    auto *cl = new QHBoxLayout(corner);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->addWidget(m_hexEnable);
    if (m_ctx.logger) {
        auto *open = new QPushButton(QStringLiteral("Mở thư mục log"));
        connect(open, &QPushButton::clicked, this, [this] {
            QDir().mkpath(m_ctx.logger->dir());
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_ctx.logger->dir()));
        });
        cl->addWidget(open);
    }
    cl->addWidget(clear);
    tabs->setCornerWidget(corner);
    connect(clear, &QPushButton::clicked, this, [this, tabs] {
        static_cast<QPlainTextEdit *>(tabs->currentWidget())->clear();
    });
    return tabs;
}

void MainWindow::logEvent(const QString &text)
{
    m_eventLog->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss.zzz ") + text);
    if (m_ctx.logger) m_ctx.logger->logEvent(text);
    m_listDirty = true; // sự kiện cảnh báo có thể đổi lý do hiển thị trong danh sách bất thường
}

void MainWindow::showDetail(int mb, int trb)
{
    m_detail->setDevice(mb, trb);
    m_nav->setCurrentRow(1);
}

void MainWindow::showPsu(int addr)
{
    m_psu->setDevice(addr);
    m_nav->setCurrentRow(m_pages->indexOf(m_psu));
}

void MainWindow::refresh()
{
    m_store->markStale(QDateTime::currentMSecsSinceEpoch(), m_ctx.settings.staleMs);
    m_grid->update();
    m_psuGrid->update();
    m_ctx.psuStore->markStale(QDateTime::currentMSecsSinceEpoch(), m_ctx.settings.staleMs);
    if (m_pages->currentWidget() == m_detail) m_detail->refresh();
    if (m_pages->currentWidget() == m_psu) m_psu->refresh();

    auto item = [this](Status s, const QString &label) {
        return pill(QStringLiteral("%1 %2").arg(m_store->count(s)).arg(label), OverviewGrid::statusColor(s),
                    s == Status::NoData ? QColor(0x444441) : QColor(Qt::white));
    };
    m_counters->setText(item(Status::Ok, QStringLiteral("tốt")) + ' '
                        + item(Status::Warning, QStringLiteral("quá ngưỡng")) + ' '
                        + item(Status::Trip, QStringLiteral("trip")) + ' '
                        + item(Status::Lost, QStringLiteral("mất kết nối")) + ' '
                        + item(Status::NoData, QStringLiteral("chưa có dữ liệu")));
    if (m_listDirty) rebuildAbnormalList();
}

void MainWindow::rebuildAbnormalList()
{
    m_listDirty = false;
    m_abnormal->clear();
    for (Status want : {Status::Trip, Status::Warning, Status::Lost}) {
        for (int mb = 0; mb < m_store->mbCount(); ++mb) {
            for (int trb = 0; trb < m_store->trbPerMb(); ++trb) {
                if (m_store->trb(mb, trb).status != want) continue;
                QString why = statusText(want);
                const QList<int> &bad = m_store->trb(mb, trb).alarms;
                if (want == Status::Warning && !bad.isEmpty()) {
                    why = proto::trbmon::table().fields().at(bad.first()).name;
                    if (bad.size() > 1) why += QStringLiteral(" (+%1)").arg(bad.size() - 1);
                }
                auto *it = new QListWidgetItem(QStringLiteral("%1 · %2").arg(trbName(mb, trb), why));
                it->setData(Qt::UserRole, mb);
                it->setData(Qt::UserRole + 1, trb);
                it->setForeground(OverviewGrid::statusColor(want).darker(130));
                m_abnormal->addItem(it);
            }
        }
        for (int i = 0; i < m_ctx.psuStore->count(); ++i) {
            const int addr = m_ctx.psuStore->firstAddr() + i;
            const model::PsuState &s = m_ctx.psuStore->psu(addr);
            if (s.status != want) continue;
            QString why = statusText(want);
            if (want == Status::Warning && !s.alarms.isEmpty()) {
                why = proto::psumon::table().fields().at(s.alarms.first()).name;
                if (s.alarms.size() > 1) why += QStringLiteral(" (+%1)").arg(s.alarms.size() - 1);
            }
            auto *it = new QListWidgetItem(QStringLiteral("PSU %1 · %2").arg(addr).arg(why));
            it->setData(Qt::UserRole + 2, addr);
            it->setForeground(OverviewGrid::statusColor(want).darker(130));
            m_abnormal->addItem(it);
        }
    }
}

QString MainWindow::psuTooltip(int addr, int cluster) const
{
    using namespace proto::psumon;
    const model::PsuState &s = m_ctx.psuStore->psu(addr);
    QString t = QStringLiteral("PSU %1").arg(addr);
    if (cluster >= 0) t += QStringLiteral(" · Cụm %1").arg(cluster + 1);
    t += QStringLiteral("\nTrạng thái: %1").arg(statusText(cluster >= 0 ? m_ctx.psuStore->clusterStatus(addr, cluster) : s.status));
    if (cluster >= 0 && s.status == Status::Trip)
        t += QStringLiteral("\nPSU đang Trip (chưa xác định được cụm nào, chờ bảng trip code)");
    if (s.frames == 0) return t;

    t += QStringLiteral("\nCập nhật: %1 s trước · %2 bản tin")
             .arg((QDateTime::currentMSecsSinceEpoch() - s.lastSeenMs) / 1000.0, 0, 'f', 1).arg(s.frames);
    if (cluster < 0) {
        QStringList trips;
        for (int i = 0; i < kNumTrip; ++i)
            trips << QStringLiteral("%1").arg(int(s.values.at(kIdxTrip0 + i)), 2, 16, QLatin1Char('0')).toUpper();
        t += QStringLiteral("\nTrip code: ") + trips.join(' ');
    }
    const QList<int> bad = cluster >= 0 ? m_ctx.psuStore->clusterAlarms(addr, cluster) : s.alarms;
    for (int f : bad) t += QStringLiteral("\nQuá ngưỡng: ") + table().fields().at(f).name;
    return t;
}

QString MainWindow::trbTooltip(int mb, int trb) const
{
    using namespace proto::trbmon;
    const model::TrbState &s = m_store->trb(mb, trb);
    QString t = QStringLiteral("%1\nTrạng thái: %2").arg(trbName(mb, trb), statusText(s.status));
    if (s.frames == 0) return t;

    QStringList trips;
    for (int i = 0; i < kNumTrip; ++i)
        trips << QStringLiteral("%1").arg(int(s.values.at(kIdxTrip0 + i)), 2, 16, QLatin1Char('0')).toUpper();
    t += QStringLiteral("\nCập nhật: %1 s trước · %2 bản tin")
             .arg((QDateTime::currentMSecsSinceEpoch() - s.lastSeenMs) / 1000.0, 0, 'f', 1).arg(s.frames);
    t += QStringLiteral("\nĐiện áp / dòng / nhiệt TRB (thô): %1 / %2 / %3")
             .arg(s.values.at(kIdxTrbV)).arg(s.values.at(kIdxTrbI)).arg(s.values.at(kIdxTrbTemp));
    t += QStringLiteral("\nTrip code: ") + trips.join(' ');
    for (int f : s.alarms) t += QStringLiteral("\nQuá ngưỡng: ") + table().fields().at(f).name;
    return t;
}

} // namespace ui
