#include "direct_page.h"
#include "log_filter.h"
#include "theme.h"
#include "../proto/gateway_header_proto.h"
#include "../proto/psu_address.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QStandardItemModel>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QVBoxLayout>

namespace ui {

using State = core::Transport::State;
using services::DirectPoller;

DirectPage::DirectPage(const AppContext &ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    const Settings &cfg = ctx.settings;
    auto *l = new QVBoxLayout(this);

    m_direct = new QCheckBox(QStringLiteral("Bật chế độ Debug COM: ngắt Ethernet, nối cổng COM thẳng xuống thiết bị (không qua Gateway)"));
    l->addWidget(m_direct);
    auto *note = new QLabel(QStringLiteral(
        "Mỗi lúc chỉ một thiết bị (TRB hoặc PSU) trên cổng COM; mọi trang khác (Chi tiết, Cấu hình, Nạp code, Calib, Giả lập beam…) vẫn dùng được nhưng đi qua cổng COM này. "
        "Mọi bản tin gửi khung gốc, không có byte thiết bị và độ dài như khi đi qua Gateway."));
    note->setWordWrap(true);
    theme::setRole(note, "muted");
    l->addWidget(note);

    // --- Cổng COM
    auto *conn = new QGroupBox(QStringLiteral("Cổng COM (RS422 qua bộ chuyển đổi)"));
    auto *cl = new QHBoxLayout(conn);
    m_port = new QComboBox;
    m_port->setEditable(true);
    m_port->setMinimumWidth(140);
    m_refresh = new QPushButton(QStringLiteral("Quét cổng"));
    theme::setRole(m_refresh, "secondary");
    m_baud = new QComboBox;
    m_baud->setEditable(true);
    m_baud->setValidator(new QIntValidator(1200, 4000000, m_baud));
    for (int b : {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600}) m_baud->addItem(QString::number(b));
    m_baud->setCurrentText(QString::number(cfg.directBaud));
    m_open = new QPushButton;
    m_status = new QLabel;
    cl->addWidget(new QLabel(QStringLiteral("Cổng")));
    cl->addWidget(m_port);
    cl->addWidget(m_refresh);
    cl->addWidget(new QLabel(QStringLiteral("Baud")));
    cl->addWidget(m_baud);
    cl->addWidget(m_open);
    cl->addWidget(m_status, 1);
    l->addWidget(conn);

    // --- Đích và hỏi giám sát
    auto *target = new QGroupBox(QStringLiteral("Thiết bị đang nối và hỏi giám sát"));
    auto *tl = new QVBoxLayout(target);
    auto *row1 = new QHBoxLayout;
    m_kind = new QComboBox;
    m_kind->addItem(QStringLiteral("TRB"));
    m_kind->addItem(QStringLiteral("PSU"));
    m_kind->addItem(QStringLiteral("TRSU (chưa có bản tin)"));
    qobject_cast<QStandardItemModel *>(m_kind->model())->item(KindTrsu)->setEnabled(false);
    m_a = new QSpinBox;
    m_b = new QSpinBox;
    m_aLabel = new QLabel;
    m_bLabel = new QLabel;
    m_a->setToolTip(QStringLiteral("Giá trị đúng như byte địa chỉ trong khung (tính từ 0 với TRB)"));
    m_b->setToolTip(m_a->toolTip());
    row1->addWidget(new QLabel(QStringLiteral("Loại")));
    row1->addWidget(m_kind);
    row1->addWidget(m_aLabel);
    row1->addWidget(m_a);
    row1->addWidget(m_bLabel);
    row1->addWidget(m_b);
    row1->addStretch(1);
    tl->addLayout(row1);

    m_query = new QLabel;
    m_query->setFont(QFont(QStringLiteral("monospace")));
    m_query->setTextInteractionFlags(Qt::TextSelectableByMouse);
    tl->addWidget(new QLabel(QStringLiteral("Lệnh hỏi giám sát sẽ gửi (hex):")));
    tl->addWidget(m_query);

    auto *row2 = new QHBoxLayout;
    m_period = new QSpinBox;
    m_period->setRange(10, 60000);
    m_period->setValue(cfg.directPeriodMs);
    m_period->setSuffix(QStringLiteral(" ms"));
    m_once = new QPushButton(QStringLiteral("Hỏi một lần"));
    m_loop = new QPushButton;
    m_view = new QPushButton(QStringLiteral("Xem chi tiết"));
    theme::setRole(m_once, "secondary");
    theme::setRole(m_view, "secondary");
    m_stats = new QLabel;
    theme::setRole(m_stats, "muted");
    row2->addWidget(new QLabel(QStringLiteral("Chu kỳ hỏi")));
    row2->addWidget(m_period);
    row2->addWidget(m_once);
    row2->addWidget(m_loop);
    row2->addWidget(m_view);
    row2->addWidget(m_stats, 1);
    tl->addLayout(row2);
    l->addWidget(target);

    // --- Gửi hex tự do
    auto *raw = new QGroupBox(QStringLiteral("Gửi hex tự do (gửi nguyên các byte, không tự thêm CRC)"));
    auto *rl = new QHBoxLayout(raw);
    m_hex = new QLineEdit;
    m_hex->setFont(QFont(QStringLiteral("monospace")));
    m_hex->setPlaceholderText(QStringLiteral("AB CD ..."));
    m_sendHex = new QPushButton(QStringLiteral("Gửi"));
    m_hexResult = new QLabel;
    theme::setRole(m_hexResult, "muted");
    rl->addWidget(m_hex, 1);
    rl->addWidget(m_sendHex);
    rl->addWidget(m_hexResult);
    l->addWidget(raw);
    l->addStretch(1);

    refreshPorts();
    if (!cfg.directPort.isEmpty()) m_port->setCurrentText(cfg.directPort);

    if (!ctx.sw) {   // đang phát lại file hex: không có cổng thật
        m_direct->setEnabled(false);
        m_direct->setToolTip(QStringLiteral("Không dùng được khi đang phát lại file hex"));
    }
    connect(m_direct, &QCheckBox::toggled, this, &DirectPage::setDirect);
    connect(m_refresh, &QPushButton::clicked, this, &DirectPage::refreshPorts);
    connect(m_open, &QPushButton::clicked, this, &DirectPage::toggleOpen);
    connect(m_kind, &QComboBox::currentIndexChanged, this, [this](int k) {
        const bool trb = k == KindTrb;
        m_aLabel->setText(trb ? QStringLiteral("MB (0..%1)").arg(m_ctx.store->mbCount() - 1) : QStringLiteral("PSU (1..%1)").arg(m_ctx.psuStore->count()));
        m_bLabel->setText(QStringLiteral("TRB (0..%1)").arg(m_ctx.store->trbPerMb() - 1));
        m_a->setRange(trb ? 0 : proto::psuaddr::displayNo(m_ctx.psuStore->firstAddr()), trb ? m_ctx.store->mbCount() - 1 : proto::psuaddr::displayNo(m_ctx.psuStore->firstAddr() + m_ctx.psuStore->count() - 1));   // PSU hiển thị 1..5, byte địa chỉ 0..4
        m_bLabel->setVisible(trb);
        m_b->setVisible(trb);
        m_b->setRange(0, m_ctx.store->trbPerMb() - 1);
        applyTarget();
    });
    connect(m_a, &QSpinBox::valueChanged, this, &DirectPage::applyTarget);
    connect(m_b, &QSpinBox::valueChanged, this, &DirectPage::applyTarget);
    connect(m_period, &QSpinBox::valueChanged, this, [this](int ms) {
        m_ctx.directPoller->setPeriodMs(ms);
        Settings::save("direct/periodMs", ms);
    });
    connect(m_once, &QPushButton::clicked, m_ctx.directPoller, &DirectPoller::pollOnce);
    connect(m_loop, &QPushButton::clicked, this, [this] {
        if (m_ctx.directPoller->running()) m_ctx.directPoller->stop(); else m_ctx.directPoller->start();
    });
    connect(m_ctx.directPoller, &DirectPoller::runningChanged, this, [this](bool on) {
        m_loop->setText(on ? QStringLiteral("Dừng hỏi") : QStringLiteral("Hỏi liên tục"));
        theme::setRole(m_loop, on ? "danger" : nullptr);
        m_loop->style()->unpolish(m_loop);
        m_loop->style()->polish(m_loop);
        emit logEvent(tagged(EventKind::Net, on ? QStringLiteral("Debug COM: bắt đầu hỏi giám sát liên tục (%1 ms)").arg(m_period->value())
                                                : QStringLiteral("Debug COM: dừng hỏi giám sát")));
    });
    connect(m_ctx.directPoller, &DirectPoller::statsChanged, this, &DirectPage::showStats);
    connect(m_view, &QPushButton::clicked, this, &DirectPage::viewDevice);
    connect(m_sendHex, &QPushButton::clicked, this, &DirectPage::sendHex);
    connect(m_hex, &QLineEdit::returnPressed, this, &DirectPage::sendHex);
    if (m_ctx.monitorLink) connect(m_ctx.monitorLink, &core::Link::stateChanged, this, &DirectPage::showState);

    m_loop->setText(QStringLiteral("Hỏi liên tục"));
    emit m_kind->currentIndexChanged(KindTrb);
    showStats();
    showState(State::Closed);
}

void DirectPage::refreshPorts()
{
    const QString keep = m_port->currentText();
    m_port->clear();
    for (const QSerialPortInfo &i : QSerialPortInfo::availablePorts())
        m_port->addItem(i.portName());
    m_port->setCurrentText(keep);
}

void DirectPage::setDirect(bool on)
{
    m_ctx.directPoller->stop();
    proto::gwhdr::setDirectMode(on);
    core::SwitchTransport *sw = m_ctx.sw;
    core::Link *link = m_ctx.monitorLink;
    if (on) {
        QMetaObject::invokeMethod(link, [=] { sw->setMode(core::SwitchTransport::Mode::Direct); link->setIdleResetMs(200); });   // bỏ khung dở nếu đường im 200 ms
    } else {
        QMetaObject::invokeMethod(link, [=] { sw->setMode(core::SwitchTransport::Mode::Ethernet); link->setIdleResetMs(0); link->start(); });   // TCP tự nối lại Gateway
    }
    emit directModeChanged(on);
    emit logEvent(tagged(EventKind::Net, on ? QStringLiteral("Chế độ Debug COM: đã ngắt Ethernet, dùng cổng COM trực tiếp; gửi khung gốc không DEV + LEN")
                                            : QStringLiteral("Chế độ Debug COM: tắt, quay lại Ethernet tới Gateway")));
    showState(State::Closed);
}

void DirectPage::showState(State s)
{
    m_state = s;
    const bool direct = m_direct->isChecked();
    for (QWidget *w : {static_cast<QWidget *>(m_port), static_cast<QWidget *>(m_baud), static_cast<QWidget *>(m_refresh)})
        w->setEnabled(direct && s == State::Closed);
    m_open->setEnabled(direct);
    m_open->setText(s == State::Closed ? QStringLiteral("Mở cổng") : QStringLiteral("Đóng cổng"));
    const bool open = direct && s == State::Connected;
    for (QWidget *w : {static_cast<QWidget *>(m_once), static_cast<QWidget *>(m_loop), static_cast<QWidget *>(m_sendHex)})
        w->setEnabled(open);
    m_status->setText(!direct ? QStringLiteral("Chưa bật chế độ Debug COM") : open ? QStringLiteral("Đã mở cổng, sẵn sàng") : QStringLiteral("Cổng đóng"));
}

void DirectPage::toggleOpen()
{
    core::Link *link = m_ctx.monitorLink;
    if (m_state != State::Closed) { QMetaObject::invokeMethod(link, &core::Link::stop); return; }
    const QString port = m_port->currentText().trimmed();
    const int baud = m_baud->currentText().toInt();
    if (port.isEmpty() || baud <= 0) { m_status->setText(QStringLiteral("Chưa chọn cổng COM hoặc baud sai")); return; }
    Settings::save("direct/port", port);
    Settings::save("direct/baud", baud);
    core::SerialTransport *serial = m_ctx.serial;
    QMetaObject::invokeMethod(link, [=] { serial->configure(port, baud); link->start(); });
}

void DirectPage::applyTarget()
{
    if (m_kind->currentIndex() == KindTrb) m_ctx.directPoller->setTarget(DirectPoller::Kind::Trb, m_a->value(), m_b->value());
    else m_ctx.directPoller->setTarget(DirectPoller::Kind::Psu, proto::psuaddr::addrOf(m_a->value()));
    m_query->setText(QString::fromLatin1(m_ctx.directPoller->query().toHex(' ').toUpper()));
}

void DirectPage::showStats()
{
    const services::DirectPoller *p = m_ctx.directPoller;
    m_stats->setText(QStringLiteral("Đã hỏi %1 · có trả lời %2 · không trả lời %3").arg(p->sent()).arg(p->replied()).arg(p->timeouts()));
}

void DirectPage::viewDevice()
{
    if (m_kind->currentIndex() == KindTrb) emit showTrb(m_a->value(), m_b->value());
    else emit showPsu(proto::psuaddr::addrOf(m_a->value()));
}

void DirectPage::sendHex()
{
    const QString text = m_hex->text().remove(QRegularExpression(QStringLiteral("[^0-9A-Fa-f]")));
    if (text.isEmpty() || text.size() % 2) { m_hexResult->setText(QStringLiteral("Chuỗi hex phải có số ký tự chẵn")); return; }
    core::Request r;
    r.data = QByteArray::fromHex(text.toLatin1());
    m_ctx.monitorLink->send(r);
    m_hexResult->setText(QStringLiteral("Đã gửi %1 byte").arg(r.data.size()));
    emit logEvent(tagged(EventKind::Ctrl, QStringLiteral("Debug COM: gửi hex tự do %1 byte [%2]").arg(r.data.size()).arg(QString::fromLatin1(r.data.left(32).toHex(' ').toUpper()))));
}

} // namespace ui
