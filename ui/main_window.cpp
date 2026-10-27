#include "main_window.h"
#include "overview_grid.h"
#include "auth.h"
#include "beam_eth_page.h"
#include "firmware_page.h"
#include "psu_config_page.h"
#include "psu_grid.h"
#include "psu_page.h"
#include "sim_page.h"
#include "status_pill.h"
#include "theme.h"
#include "time_text.h"
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
#include <QMessageBox>
#include <QGridLayout>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QDialog>
#include <QDesktopServices>
#include <QDir>
#include <QUrl>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QScrollBar>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>

namespace ui {

namespace {
// Định dạng một dòng sự kiện theo mã loại ở đầu dòng ("12:00:00.000 [TRIP] ..."); TRIP/PG/WARN có thêm nền nhạt để thấy ngay.
QTextCharFormat eventFormat(const QString &line)
{
    struct Look { const char *tag, *fg, *bg; bool bold; };
    static const Look kLooks[] = {
        {"TRIP", "#B91C1C", "#FEE2E2", true},  {"PG", "#B91C1C", "#FEE2E2", true}, {"ERR", "#B91C1C", nullptr, true},
        {"WARN", "#B45309", "#FEF3C7", true},  {"OK", "#15803D", nullptr, false},  {"CTRL", "#1D4ED8", nullptr, false},
        {"CFG", "#0F766E", nullptr, false},    {"DBG", "#7C3AED", nullptr, false}, {"OTA", "#9333EA", nullptr, false},
        {"NET", "#6B7280", nullptr, false},    {"AUTH", "#92400E", nullptr, false}};
    QTextCharFormat f;
    const int open = line.indexOf(QLatin1Char('['));
    const int close = open >= 0 ? line.indexOf(QLatin1Char(']'), open) : -1;
    if (open > 0 && close > open) {
        const QString tag = line.mid(open + 1, close - open - 1);
        for (const Look &l : kLooks)
            if (tag == QLatin1String(l.tag)) {
                f.setForeground(QColor(QLatin1String(l.fg)));
                if (l.bg) f.setBackground(QColor(QLatin1String(l.bg)));
                if (l.bold) f.setFontWeight(QFont::DemiBold);
                break;
            }
    }
    return f;
}

// Thêm một dòng có màu vào cuối khung; cuộn theo nếu đang ở cuối. Mỗi dòng là một block riêng định dạng mặc định
// để màu nền của dòng trước không lan sang dòng sau.
void appendColored(QPlainTextEdit *edit, const QString &line)
{
    QScrollBar *bar = edit->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum();
    QTextCursor c(edit->document());
    c.movePosition(QTextCursor::End);
    if (!edit->document()->isEmpty()) c.insertBlock(QTextBlockFormat(), QTextCharFormat());
    c.insertText(line, eventFormat(line));
    if (atBottom) bar->setValue(bar->maximum());
}

// Mã sự kiện theo trạng thái TRB / PSU vừa chuyển tới.
EventKind kindOf(model::Status to)
{
    using model::Status;
    return to == Status::Trip ? EventKind::Trip : to == Status::Warning ? EventKind::Warn
         : to == Status::Updating ? EventKind::Ota : EventKind::Ok;
}
}

using model::Status;

namespace {
QString trbName(int mb, int trb) { return QStringLiteral("MB%1 / TRB%2").arg(mb + 1).arg(trb + 1); }
}

MainWindow::MainWindow(const AppContext &ctx, QWidget *parent)
    : QMainWindow(parent), m_ctx(ctx), m_store(ctx.store)
{
    setWindowTitle(QStringLiteral("ACTL Tool"));
    setMinimumSize(1100, 560);   // đặt sẵn để layout không tự nới cửa sổ quá màn hình; trang nào lớn hơn thì cuộn
    // Nhóm log hiển thị ở từng trang (theo thứ tự trang bên dưới); Tổng quan hiện tất cả.
    // Đường kết nối của từng trang: 0 giám sát (Ethernet; điều khiển PSU và beam cũng đi đường này), 1 RS485 (cấu hình, nạp code, điều khiển và beam TRB), -1 không có.
    m_pageConn = {0, 0, 1, 0, 1, 1, 0, -1};
    m_pageGroups = {LogAll, LogTrbMonitor, LogTrbConfig, LogPsuMonitor, LogPsuConfig, LogFirmware, LogBeam, LogSystem};

    // Điều hướng trái + các trang.
    m_nav = new QListWidget;
    m_nav->setObjectName(QStringLiteral("nav"));
    m_nav->setFixedWidth(168);
    m_pages = new QStackedWidget;
    // Mỗi trang nằm trong vùng cuộn: cửa sổ nhỏ hơn kích thước thiết kế thì cuộn, không làm cửa sổ tràn quá màn hình.
    auto scrollPage = [](QWidget *page) {
        auto *sa = new QScrollArea;
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);
        sa->setWidget(page);
        sa->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; } QScrollArea > QWidget > QWidget { background: transparent; }"));
        return sa;
    };
    m_nav->addItem(QStringLiteral("Tổng quan"));
    m_pages->addWidget(scrollPage(buildOverviewPage()));
    m_nav->addItem(QStringLiteral("Chi tiết TRB"));
    m_pages->addWidget(m_detailHolder = scrollPage(m_detail = new TrbDetailPage(m_store, ctx.trbControl, ctx.thresholds)));
    m_nav->addItem(QStringLiteral("Cấu hình TRB"));
    m_pages->addWidget(scrollPage(new TrbConfigPage(ctx)));
    m_nav->addItem(QStringLiteral("PSU"));
    m_pages->addWidget(m_psuHolder = scrollPage(m_psu = new PsuPage(ctx)));
    m_nav->addItem(QStringLiteral("Cấu hình PSU"));
    m_pages->addWidget(scrollPage(new PsuConfigPage(ctx)));
    m_nav->addItem(QStringLiteral("Nạp code"));
    m_pages->addWidget(scrollPage(new FirmwarePage(ctx)));
    m_nav->addItem(QStringLiteral("Beam Ethernet"));
    auto *beamPage = new BeamEthPage(ctx);
    m_pages->addWidget(scrollPage(beamPage));
    connect(beamPage, &BeamEthPage::logEvent, this, [this](const QString &text) { logEvent(text, LogBeam); });
    m_nav->addItem(QStringLiteral("Giả lập"));
    auto *simPage = new SimPage(ctx);
    m_pages->addWidget(scrollPage(simPage));
    const int simRow = m_nav->count() - 1;
    connect(simPage, &SimPage::runningChanged, this, [this, simRow](bool on) {   // nhắc đang giả lập để không nhầm với thiết bị thật
        m_nav->item(simRow)->setText(on ? QStringLiteral("Giả lập (đang chạy)") : QStringLiteral("Giả lập"));
    });
    connect(m_nav, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    connect(m_nav, &QListWidget::currentRowChanged, this, [this] { refilterLogs(); });
    m_nav->setCurrentRow(0);
    connect(ctx.serviceLink, &core::Link::stateChanged, this, [this](core::Transport::State s) { m_detail->setControlLinkConnected(s == core::Transport::State::Connected); });
    m_detail->setControlLinkConnected(false);
    connect(m_detail, &TrbDetailPage::deviceChanged, m_grid, &OverviewGrid::select);
    connect(m_detail, &TrbDetailPage::deviceChanged, this, [this] { refilterLogs(); });   // trang chi tiết chỉ hiện log của TRB đang xem
    connect(ctx.trbControl, &services::TrbControl::commandFinished, this, [this](const QString &d, bool sent) {
        logEvent(tagged(sent ? EventKind::Ctrl : EventKind::Err,
                         (sent ? QStringLiteral("Đã gửi (không chờ ACK): ") : QStringLiteral("KHÔNG gửi được, chưa mở cổng RS485: ")) + d),
                 LogTrbMonitor);
    });
    connect(ctx.trbConfig, &services::TrbConfig::deviceFinished, this, [this](int mb, int trb, bool ok, const QString &m) {
        logEvent(tagged(ok ? EventKind::Cfg : EventKind::Err,
                         QStringLiteral("Cấu hình %1: %2%3").arg(trbName(mb, trb), ok ? QString() : QStringLiteral("LỖI, "), m)), LogTrbConfig);
    });
    connect(ctx.trbControl, &services::TrbControl::debugTrbChanged, this, [this] {
        const auto d = m_ctx.trbControl->debugTrb();
        logEvent(tagged(EventKind::Debug, d ? QStringLiteral("Chế độ Debug: %1 (chỉ một TRB được Debug)").arg(trbName(d->first, d->second))
                                          : QStringLiteral("Không còn TRB nào ở chế độ Debug")), LogTrbMonitor);
    });
    connect(ctx.psuControl, &services::PsuControl::commandFinished, this, [this](const QString &d, bool sent) {
        logEvent(tagged(sent ? EventKind::Ctrl : EventKind::Err,
                         (sent ? QStringLiteral("Đã gửi (không chờ ACK): ") : QStringLiteral("KHÔNG gửi được, chưa kết nối Gateway: ")) + d),
                 LogPsuMonitor);
    });
    connect(ctx.psuConfig, &services::PsuConfig::deviceFinished, this, [this](int addr, bool ok, const QString &m) {
        logEvent(tagged(ok ? EventKind::Cfg : EventKind::Err,
                         QStringLiteral("Cấu hình PSU %1: %2%3").arg(addr).arg(ok ? QString() : QStringLiteral("LỖI, "), m)), LogPsuConfig);
    });
    connect(ctx.psuStore, &model::PsuStore::psuStatusChanged, this, [this](int addr, Status from, Status to) {
        m_listDirty = true;
        if (from == Status::NoData && to == Status::Ok) return; // lần đầu thấy PSU: không cần báo
        if (to == Status::Lost || (from == Status::Lost && to == Status::Ok)) return;   // bỏ qua sự kiện mất / có lại kết nối
        logEvent(tagged(kindOf(to), from == Status::Lost ? QStringLiteral("PSU %1: %2").arg(addr).arg(statusText(to))
                                                         : QStringLiteral("PSU %1: %2 → %3").arg(addr).arg(statusText(from), statusText(to))),
                 LogPsuMonitor);
    });
    connect(ctx.fpgaOta, &services::FpgaOta::finished, this, [this](const QString &s) { logEvent(tagged(EventKind::Ota, QStringLiteral("Nạp FPGA: ") + s), LogFirmware); });
    connect(ctx.stmOta, &services::StmOta::deviceFinished, this, [this](int addr, bool ok, const QString &m) {
        logEvent(tagged(ok ? EventKind::Ota : EventKind::Err,
                         QStringLiteral("Nạp STM32 PSU %1: %2%3").arg(addr).arg(ok ? QString() : QStringLiteral("LỖI, "), m)), LogFirmware);
    });
    connect(ctx.auth, &Auth::lockedChanged, this, [this](bool locked) {
        logEvent(tagged(EventKind::Auth, locked ? QStringLiteral("Đã khóa chế độ kỹ sư") : QStringLiteral("Đã mở khóa chế độ kỹ sư")));
    });

    auto *body = new QWidget;
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(theme::kSpace);
    bodyLayout->addWidget(m_nav);
    bodyLayout->addWidget(m_pages, 1);

    auto *split = new QSplitter(Qt::Vertical);
    split->addWidget(body);
    split->addWidget(buildLogPanel());
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 1);

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(theme::kMargin, theme::kMargin, theme::kMargin, theme::kMargin);
    layout->setSpacing(theme::kSpace);
    layout->addWidget(buildTopBar());
    layout->addWidget(split, 1);
    setCentralWidget(central);
    connect(m_nav, &QListWidget::currentRowChanged, this, [this] { showConnBox(); });
    showConnBox();   // thanh trên cùng đã dựng xong

    connect(m_store, &model::DeviceStore::trbStatusChanged, this,
            [this](int mb, int trb, Status from, Status to) {
        m_listDirty = true;
        if (from == Status::NoData && to == Status::Ok) return; // lần đầu thấy thiết bị: không cần báo
        if (to == Status::Lost || (from == Status::Lost && to == Status::Ok)) return;   // bỏ qua sự kiện mất / có lại kết nối
        logEvent(tagged(kindOf(to), from == Status::Lost ? QStringLiteral("%1: %2").arg(trbName(mb, trb), statusText(to))
                                                         : QStringLiteral("%1: %2 → %3").arg(trbName(mb, trb), statusText(from), statusText(to))),
                 LogTrbMonitor, mb, trb);
    });

    watchLink(ctx.monitorLink, QStringLiteral("Giám sát (TCP)"), m_tcpPill, m_tcpButton);
    watchLink(ctx.serviceLink, QStringLiteral("Cấu hình (RS485)"), m_comPill, m_comButton);
    connect(ctx.monitorLink, &core::Link::rxErrorsChanged, this, [this](quint64 crc, quint64 dropped) {
        m_rxErrText = QStringLiteral("Lỗi CRC: %1 · Byte bỏ qua: %2").arg(crc).arg(dropped);
        if (m_rxErrLabel) m_rxErrLabel->setText(m_rxErrText);
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
    auto *l = new QHBoxLayout(bar);          // một hàng: thẻ đường kết nối, ô kết nối của trang, bộ đếm trạng thái
    l->setContentsMargins(0, 0, 0, 0);

    // Thẻ cho đường của trang đang mở nằm đầu hàng, bên trái ô kết nối; bấm thì thả ra trạng thái đủ 4 đường (2 Ethernet + 2 RS485).
    m_connPill = new StatusPill;
    m_connPill->setClickable(true);
    l->addWidget(m_connPill);
    connect(m_connPill, &StatusPill::clicked, this, &MainWindow::showConnPopup);

    // Cửa sổ thả: mỗi đường một dòng gồm thẻ trạng thái (luôn hiện), ô cấu hình và nút kết nối.
    m_connPopup = new QFrame(this, Qt::Popup);
    m_connPopup->setFrameShape(QFrame::StyledPanel);
    auto *pl = new QGridLayout(m_connPopup);
    pl->setHorizontalSpacing(12);
    pl->setVerticalSpacing(8);
    m_tcpPill = new StatusPill;
    m_comPill = new StatusPill;
    m_sparePill = new StatusPill;
    m_sparePill->setPill(theme::statusMark(Status::NoData) + QStringLiteral(" RS485 #2"), theme::statusColor(Status::NoData), theme::statusTextColor(Status::NoData));
    m_sparePill->setToolTip(QStringLiteral("RS485 #2: dành cho chức năng chưa bổ sung"));
    StatusPill *rowPill[] = {m_tcpPill, m_comPill, m_sparePill};   // thứ tự dòng: giám sát (Ethernet, dùng chung cho điều khiển PSU và beam), RS485 (cấu hình, nạp code, điều khiển TRB), RS485 #2
    for (int r = 0; r < 3; ++r) pl->addWidget(rowPill[r], r, 0);
    const int rowOf[2] = {0, 1};         // chỉ số "ô" (0 giám sát, 1 RS485 cấu hình) -> dòng trong cửa sổ
    auto box = [&](int index) {
        auto *w = new QWidget;
        auto *bl = new QHBoxLayout(w);
        bl->setContentsMargins(0, 0, 0, 0);
        pl->addWidget(w, rowOf[index], 1);
        return bl;
    };

    // Giám sát (Tổng quan, Chi tiết TRB, PSU): PC là TCP client, nối tới Gateway (TCP server).
    m_tcpAddress = new QLineEdit(cfg.monitorAddress);
    m_tcpAddress->setFixedWidth(120);
    m_tcpAddress->setToolTip(QStringLiteral("Địa chỉ IP của Gateway (cùng dải mạng với PC, ví dụ 192.168.1.10)"));
    m_tcpAddress->setPlaceholderText(QStringLiteral("IP Gateway"));
    m_tcpPort = new QSpinBox;
    m_tcpPort->setRange(1, 65535);
    m_tcpPort->setValue(cfg.monitorPort);
    m_tcpButton = new QPushButton;
    connect(m_tcpButton, &QPushButton::clicked, this, &MainWindow::toggleTcp);
    {
        auto *bl = box(0);
        if (m_ctx.tcp) {
            bl->addWidget(m_tcpAddress);
            bl->addWidget(m_tcpPort);
            bl->addWidget(m_tcpButton);
        } else {
            bl->addWidget(new QLabel(QStringLiteral("đang phát lại file hex")));
        }
    }

    // Cấu hình và nạp code: cổng COM (RS485).
    m_comPort = new QComboBox;
    m_comPort->setMinimumWidth(90);
    m_comBaud = new QComboBox;
    m_comBaud->setEditable(true);
    m_comBaud->addItems({"115200", "460800", "921600", "1000000"});
    m_comBaud->setCurrentText(QString::number(cfg.serviceBaud));
    auto *rescan = new QPushButton(QStringLiteral("↻"));
    rescan->setFixedWidth(36);
    theme::setRole(rescan, "secondary");
    rescan->setToolTip(QStringLiteral("Quét lại cổng COM"));
    m_comButton = new QPushButton;
    connect(rescan, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    connect(m_comButton, &QPushButton::clicked, this, &MainWindow::toggleSerial);
    refreshPorts();
    {
        auto *bl = box(1);
        bl->addWidget(m_comPort);
        bl->addWidget(rescan);
        bl->addWidget(m_comBaud);
        bl->addWidget(m_comButton);
    }

    l->addStretch(1);

    for (StatusPill *&pill : m_counterPills) {
        pill = new StatusPill;
        l->addWidget(pill);
    }

    {   // dòng RS485 #2 (chưa có chức năng) và thống kê lỗi nhận
        auto *spare = new QLabel(QStringLiteral("dành cho chức năng chưa bổ sung"));
        theme::setRole(spare, "muted");
        pl->addWidget(spare, 2, 1);
        m_rxErrLabel = new QLabel(m_rxErrText);
        theme::setRole(m_rxErrLabel, "muted");
        pl->addWidget(m_rxErrLabel, 3, 0, 1, 2);
    }
    return bar;
}

// Thẻ trên thanh = bản sao thẻ của đường mà trang đang mở dùng (trang không có đường thì thẻ trung tính).
void MainWindow::syncConnPill()
{
    if (!m_connPill) return;
    StatusPill *pills[] = {m_tcpPill, m_comPill};
    const int page = m_nav ? m_nav->currentRow() : 0;
    const int which = page >= 0 && page < m_pageConn.size() ? m_pageConn.at(page) : -1;
    // m_pageConn: 0 giám sát (Ethernet), 1 RS485 -> chỉ số mảng pills theo cùng thứ tự
    if (which >= 0) {
        StatusPill *src = pills[which];
        m_connPill->setPill(src->text() + QStringLiteral(" ▾"), src->background(), src->foreground());
        m_connPill->setToolTip(src->toolTip() + QStringLiteral("\nBấm để xem đủ các đường kết nối"));
    } else {
        m_connPill->setPill(QStringLiteral("Các cổng ▾"), theme::statusColor(Status::NoData), theme::statusTextColor(Status::NoData));
        m_connPill->setToolTip(QStringLiteral("Bấm để xem đủ các đường kết nối"));
    }
}

void MainWindow::showConnPopup()
{
    syncConnPill();
    m_connPopup->adjustSize();
    const QPoint below = m_connPill->mapToGlobal(QPoint(0, m_connPill->height() + 4));
    m_connPopup->move(below);
    m_connPopup->show();
}

// Hiện đúng ô kết nối của trang đang mở.
void MainWindow::showConnBox()
{
    syncConnPill();
}

// Theo dõi một link: nhãn trạng thái, chữ trên nút, sự kiện, hex thô.
void MainWindow::watchLink(core::Link *link, const QString &title, StatusPill *pillLabel, QPushButton *button)
{
    using S = core::Transport::State;
    const bool isTcp = link == m_ctx.monitorLink;
    auto show = [=](S s) {
        (isTcp ? m_tcpState : m_comState) = s;
        const QString state = s == S::Connected ? QStringLiteral("đã kết nối")
                            : s == S::Waiting   ? (isTcp ? QStringLiteral("đang nối Gateway") : QStringLiteral("chờ")) : QStringLiteral("đóng");
        const Status look = s == S::Connected ? Status::Ok : s == S::Waiting ? Status::Warning : Status::Lost;
        pillLabel->setPill(theme::statusMark(look) + QLatin1Char(' ') + (isTcp ? QStringLiteral("Giám sát (ETH)") : QStringLiteral("Cấu hình (485)")),
                           theme::statusColor(look), theme::statusTextColor(look));
        pillLabel->setToolTip(title + QStringLiteral(": ") + state);
        button->setText(s == S::Closed ? (isTcp ? QStringLiteral("Kết nối") : QStringLiteral("Mở")) : (isTcp ? QStringLiteral("Ngắt") : QStringLiteral("Đóng")));
        m_tcpAddress->setEnabled(m_tcpState == S::Closed);
        m_tcpPort->setEnabled(m_tcpState == S::Closed);
        m_comPort->setEnabled(m_comState == S::Closed);
        m_comBaud->setEnabled(m_comState == S::Closed);
        return title + QStringLiteral(": ") + state;
    };
    show(S::Closed);
    connect(link, &core::Link::stateChanged, this, [=](S s) { logEvent(tagged(EventKind::Net, show(s))); });
    connect(link, &core::Link::errorOccurred, this,
            [=](const QString &m) { logEvent(tagged(EventKind::Err, QStringLiteral("Lỗi %1: %2").arg(title, m))); });

    auto hex = [=](const char *dir, const QByteArray &d) { logHex(link->name(), dir, d); };
    connect(link, &core::Link::frameReceived, this, [=](const core::Frame &f) { hex("RX", f.raw); });
    connect(link, &core::Link::rawSent, this, [=](const QByteArray &d) { hex("TX", d); });
}

void MainWindow::toggleTcp()
{
    core::Link *link = m_ctx.monitorLink;
    if (m_tcpState != core::Transport::State::Closed) { QMetaObject::invokeMethod(link, &core::Link::stop); return; }
    const QString host = m_tcpAddress->text().trimmed();
    if (host.isEmpty()) { logEvent(tagged(EventKind::Err, QStringLiteral("Chưa nhập địa chỉ IP của Gateway"))); return; }
    const quint16 port = quint16(m_tcpPort->value());
    Settings::save("monitor/gatewayAddress", host);
    Settings::save("monitor/gatewayPort", port);
    core::TcpClientTransport *t = m_ctx.tcp;
    QMetaObject::invokeMethod(link, [=] { t->configure(host, port); link->start(); });
}

void MainWindow::toggleSerial()
{
    core::Link *link = m_ctx.serviceLink;
    if (m_comState != core::Transport::State::Closed) { QMetaObject::invokeMethod(link, &core::Link::stop); return; }
    const QString port = m_comPort->currentText();
    const int baud = m_comBaud->currentText().toInt();
    if (port.isEmpty() || baud <= 0) { logEvent(tagged(EventKind::Err, QStringLiteral("Chưa chọn cổng COM hoặc baudrate không hợp lệ"))); return; }
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
    theme::setRole(clear, "secondary");

    auto *tabs = new QTabWidget;
    tabs->addTab(m_eventLog, QStringLiteral("Sự kiện"));
    tabs->addTab(m_hexLog, QStringLiteral("Hex thô"));
    auto *corner = new QWidget;
    auto *cl = new QHBoxLayout(corner);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->addWidget(m_hexEnable);
    if (m_ctx.logger) {
        services::CsvLogger *logger = m_ctx.logger;
        auto *toggle = new QPushButton(QStringLiteral("Ghi log"));
        auto *schedule = new QPushButton(QStringLiteral("Hẹn giờ…"));
        theme::setRole(schedule, "secondary");
        auto refresh = [=] { // không ghi: nút không màu; đang ghi: nền xanh. Giờ hẹn xem ở chú thích.
            const QString fmt = QStringLiteral("HH:mm:ss dd/MM");
            QStringList tip{logger->recording() ? QStringLiteral("Đang ghi log. Bấm để dừng.") : QStringLiteral("Không ghi log. Bấm để bắt đầu.")};
            if (logger->startAt().isValid()) tip << QStringLiteral("Hẹn bắt đầu: %1").arg(logger->startAt().toString(fmt));
            if (logger->stopAt().isValid()) tip << QStringLiteral("Hẹn dừng: %1").arg(logger->stopAt().toString(fmt));
            toggle->setToolTip(tip.join('\n'));
            toggle->setText(logger->recording() ? QStringLiteral("Đang ghi log") : QStringLiteral("Ghi log"));
            theme::setRole(toggle, logger->recording() ? "recording" : "secondary");
            toggle->style()->unpolish(toggle);   // đổi role lúc chạy cần áp lại stylesheet
            toggle->style()->polish(toggle);
        };
        connect(logger, &services::CsvLogger::recordingChanged, this, refresh);
        connect(logger, &services::CsvLogger::scheduleChanged, this, refresh);
        connect(toggle, &QPushButton::clicked, this, [=] {
            if (!logger->recording()) logger->setSchedule(QDateTime(), logger->stopAt()); // bắt đầu tay thì bỏ hẹn giờ bắt đầu
            logger->setRecording(!logger->recording());
        });
        connect(schedule, &QPushButton::clicked, this, [=] { editLogSchedule(); });
        refresh();
        cl->addWidget(toggle);
        cl->addWidget(schedule);

        auto *open = new QPushButton(QStringLiteral("Mở thư mục log"));
        theme::setRole(open, "secondary");
        connect(open, &QPushButton::clicked, this, [this] {
            QDir().mkpath(m_ctx.logger->dir());
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_ctx.logger->dir()));
        });
        cl->addWidget(open);
    }
    cl->addWidget(clear);
    tabs->setCornerWidget(corner);
    connect(clear, &QPushButton::clicked, this, [this, tabs] {
        auto *edit = static_cast<QPlainTextEdit *>(tabs->currentWidget());
        (edit == m_eventLog ? m_eventBuffer : m_hexBuffer).clear();
        edit->clear();
    });
    return tabs;
}

// Hộp thoại hẹn giờ ghi log: mỗi mốc bật/tắt riêng. Chỉ hiệu lực một lần.
void MainWindow::editLogSchedule()
{
    services::CsvLogger *logger = m_ctx.logger;
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Hẹn giờ ghi log"));
    auto *startOn = new QCheckBox(QStringLiteral("Bắt đầu ghi lúc"));
    auto *stopOn = new QCheckBox(QStringLiteral("Dừng ghi lúc"));
    auto makeEdit = [&](const QDateTime &v, QCheckBox *box) {
        auto *e = new QDateTimeEdit(v.isValid() ? v : QDateTime::currentDateTime().addSecs(60));
        e->setDisplayFormat(QStringLiteral("dd/MM/yyyy HH:mm:ss"));
        e->setCalendarPopup(true);
        e->setEnabled(v.isValid());
        box->setChecked(v.isValid());
        connect(box, &QCheckBox::toggled, e, &QWidget::setEnabled);
        return e;
    };
    auto *startAt = makeEdit(logger->startAt(), startOn);
    auto *stopAt = makeEdit(logger->stopAt(), stopOn);
    auto *hint = new QLabel(QStringLiteral("Mỗi mốc chỉ dùng một lần. Nút \"Bắt đầu ghi\" / \"Dừng ghi\" vẫn bấm tay được bất kỳ lúc nào."));
    hint->setWordWrap(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto *grid = new QGridLayout(&dlg);
    grid->addWidget(startOn, 0, 0);
    grid->addWidget(startAt, 0, 1);
    grid->addWidget(stopOn, 1, 0);
    grid->addWidget(stopAt, 1, 1);
    grid->addWidget(hint, 2, 0, 1, 2);
    grid->addWidget(buttons, 3, 0, 1, 2);
    if (dlg.exec() != QDialog::Accepted) return;

    const QDateTime start = startOn->isChecked() ? startAt->dateTime() : QDateTime();
    const QDateTime stop = stopOn->isChecked() ? stopAt->dateTime() : QDateTime();
    if (start.isValid() && stop.isValid() && stop <= start) {
        QMessageBox::warning(this, QStringLiteral("Hẹn giờ ghi log"), QStringLiteral("Thời điểm dừng phải sau thời điểm bắt đầu."));
        return;
    }
    logger->setSchedule(start, stop);
}

void MainWindow::logEvent(const QString &text, int group, int mb, int trb)
{
    appendLog(m_eventLog, m_eventBuffer,
              {QDateTime::currentDateTime().toString("HH:mm:ss.zzz ") + text, group, mb, trb});
    if (m_ctx.logger) m_ctx.logger->logEvent(text);   // file CSV luôn ghi đủ, không lọc theo trang
    m_listDirty = true; // sự kiện cảnh báo có thể đổi lý do hiển thị trong danh sách bất thường
}

void MainWindow::logHex(const QString &link, const char *dir, const QByteArray &raw)
{
    if (!m_hexEnable->isChecked()) return;
    const FrameClass fc = classifyFrame(raw);
    appendLog(m_hexLog, m_hexBuffer,
              {QDateTime::currentDateTime().toString("HH:mm:ss.zzz ") + QStringLiteral("[%1] %2 ").arg(link, QLatin1String(dir))
                   + QString::fromLatin1(raw.toHex(' ').toUpper()),
               fc.group, fc.mb, fc.trb});
}

// Dòng có nhóm LogSystem hiện ở mọi trang; còn lại chỉ hiện ở trang có nhóm đó. Trang chi tiết TRB
// còn lọc theo TRB đang xem (dòng không có địa chỉ vẫn hiện).
bool MainWindow::logVisible(const LogEntry &e) const
{
    const int page = m_pages->currentIndex();
    const int mask = page >= 0 && page < m_pageGroups.size() ? m_pageGroups.at(page) : LogAll;
    if (e.group == LogSystem) return true;
    if (!(mask & e.group)) return false;
    if (m_pages->currentWidget() == m_detailHolder && e.group == LogTrbMonitor && e.mb >= 0)
        return e.mb == m_detail->mb() && e.trb == m_detail->trb();
    return true;
}

void MainWindow::appendLog(QPlainTextEdit *edit, QList<LogEntry> &buffer, const LogEntry &e)
{
    buffer.append(e);
    if (buffer.size() > 5000) buffer.removeFirst();
    if (!logVisible(e)) return;
    if (edit == m_eventLog) appendColored(edit, e.text);
    else edit->appendPlainText(e.text);
}

void MainWindow::rebuildLog(QPlainTextEdit *edit, const QList<LogEntry> &buffer)
{
    if (edit == m_eventLog) {
        edit->clear();
        QTextCursor c(edit->document());
        c.beginEditBlock();
        bool first = true;
        for (const LogEntry &e : buffer) {
            if (!logVisible(e)) continue;
            if (!first) c.insertBlock(QTextBlockFormat(), QTextCharFormat());
            c.insertText(e.text, eventFormat(e.text));
            first = false;
        }
        c.endEditBlock();
    } else {
        QStringList lines;
        for (const LogEntry &e : buffer)
            if (logVisible(e)) lines << e.text;
        edit->setPlainText(lines.join(QLatin1Char('\n')));
    }
    edit->moveCursor(QTextCursor::End);
}

void MainWindow::refilterLogs()
{
    if (!m_eventLog || !m_hexLog) return;
    rebuildLog(m_eventLog, m_eventBuffer);
    rebuildLog(m_hexLog, m_hexBuffer);
}

void MainWindow::showDetail(int mb, int trb)
{
    m_detail->setDevice(mb, trb);
    m_nav->setCurrentRow(1);
}

void MainWindow::showPsu(int addr)
{
    m_psu->setDevice(addr);
    m_nav->setCurrentRow(m_pages->indexOf(m_psuHolder));
}

void MainWindow::refresh()
{
    syncConnPill();
    m_store->markStale(QDateTime::currentMSecsSinceEpoch(), m_ctx.settings.staleMs);
    const auto debug = m_ctx.trbControl->debugTrb();
    m_grid->setDebugTrb(debug ? debug->first : -1, debug ? debug->second : -1);
    m_grid->update();
    m_psuGrid->update();
    m_ctx.psuStore->markStale(QDateTime::currentMSecsSinceEpoch(), m_ctx.settings.staleMs);
    if (m_pages->currentWidget() == m_detailHolder) m_detail->refresh();
    if (m_pages->currentWidget() == m_psuHolder) m_psu->refresh();

    const std::pair<Status, QString> counters[] = {
        {Status::Ok, QStringLiteral("tốt")}, {Status::Warning, QStringLiteral("quá ngưỡng")},
        {Status::Trip, QStringLiteral("trip")}, {Status::Lost, QStringLiteral("mất kết nối")},
        {Status::NoData, QStringLiteral("chưa có dữ liệu")}};
    for (int i = 0; i < 5; ++i) {
        const Status st = counters[i].first;
        const QString mark = theme::statusMark(st);
        m_counterPills[i]->setPill(QStringLiteral("%1%2 %3").arg(mark.isEmpty() ? QString() : mark + QLatin1Char(' '))
                                       .arg(m_store->countCondition(st)).arg(counters[i].second),
                                   theme::statusColor(st), theme::statusTextColor(st));
    }
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
    if (cluster >= 0) t += cluster < kNumCluster ? QStringLiteral(" · Cụm %1").arg(cluster + 1) : QStringLiteral(" · Supply");
    const bool isCluster = cluster >= 0 && cluster < kNumCluster;
    if (isCluster && m_ctx.psuStore->clusterOff(addr, cluster))
        t += QStringLiteral("\nTrạng thái: OFF (chân EN tắt, không so ngưỡng)");
    else
        t += QStringLiteral("\nTrạng thái: %1").arg(statusText(cluster >= 0 ? m_ctx.psuStore->clusterStatus(addr, cluster) : s.status));
    if (isCluster && s.frames > 0) {
        t += QStringLiteral("\nAn toàn: %1 · EN: %2 · trip liên tiếp: %3").arg(safeStateText(int(s.values.at(kIdxSafeState(cluster)))))
                 .arg(int(s.values.at(kIdxEn(cluster))) & 1 ? QStringLiteral("bật") : QStringLiteral("tắt")).arg(s.values.at(kIdxTripCount(cluster)));
    }
    if (cluster >= 0 && s.status == Status::Trip && !m_ctx.psuStore->clusterTripped(addr, cluster))
        t += QStringLiteral("\nPSU đang Trip theo trip code (bảng trip code chưa gán theo cụm)");
    if (s.frames == 0) return t;

    t += QStringLiteral("\nCập nhật: %1 trước · %2 bản tin")
             .arg(elapsedText(QDateTime::currentMSecsSinceEpoch() - s.lastSeenMs)).arg(s.frames);
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
    if (s.status == Status::Lost && s.lastLive != Status::NoData)
        t += QStringLiteral(" (trước đó: %1)").arg(statusText(s.lastLive));
    if (s.noReply) t += QStringLiteral("\nX: Gateway vừa báo TRB không phản hồi (khung rỗng)");
    else if (s.status == Status::Lost) t += QStringLiteral("\n?: quá hạn không có bản tin mới, số liệu bên dưới là số liệu cũ");
    if (const auto d = m_ctx.trbControl->debugTrb(); d && d->first == mb && d->second == trb)
        t += QStringLiteral("\nĐang ở chế độ Debug");
    if (s.frames == 0) return t;

    QStringList trips;
    for (int i = 0; i < kNumTrip; ++i)
        trips << QStringLiteral("%1").arg(int(s.values.at(kIdxTrip0 + i)), 2, 16, QLatin1Char('0')).toUpper();
    t += QStringLiteral("\nCập nhật: %1 trước · %2 bản tin")
             .arg(elapsedText(QDateTime::currentMSecsSinceEpoch() - s.lastSeenMs)).arg(s.frames);
    t += QStringLiteral("\nĐiện áp / dòng / nhiệt TRB (thô): %1 / %2 / %3")
             .arg(s.values.at(kIdxTrbV)).arg(s.values.at(kIdxTrbI)).arg(s.values.at(kIdxTrbTemp));
    t += QStringLiteral("\nTrip code: ") + trips.join(' ');
    if (const int pg = pgFaultBits(s.values.at(kIdxPg))) {
        QStringList bad;
        for (int i = 0; i < 4; ++i) if (pg >> i & 1) bad << QStringLiteral("TRM%1").arg(i + 1);
        t += QStringLiteral("\nMất PG (Power Good): ") + bad.join(", ");
    }
    for (int f : s.alarms) t += QStringLiteral("\nQuá ngưỡng: ") + table().fields().at(f).name;
    return t;
}

} // namespace ui
