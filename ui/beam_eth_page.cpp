#include "beam_eth_page.h"
#include "log_filter.h"
#include "status_pill.h"
#include "theme.h"
#include <QButtonGroup>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTime>

namespace ui {

using namespace proto::beameth;
using State = core::Transport::State;

namespace {
QDoubleSpinBox *angleBox()
{
    auto *b = new QDoubleSpinBox;
    b->setDecimals(1);                        // một chữ số sau dấu phẩy; bản tin gửi giá trị x 10
    b->setRange(kAngleMin, kAngleMax);
    b->setSingleStep(0.1);
    b->setAlignment(Qt::AlignRight);
    return b;
}
}

BeamEthPage::BeamEthPage(const AppContext &ctx, QWidget *parent) : QWidget(parent), m_ctx(ctx)
{
    // Kết nối: PC là TCP client, nối tới thiết bị beam.
    m_pill = new StatusPill;
    m_address = new QLineEdit(ctx.settings.beamAddress);
    m_address->setFixedWidth(140);
    m_address->setPlaceholderText(QStringLiteral("IP thiết bị beam"));
    m_port = new QSpinBox;
    m_port->setRange(1, 65535);
    m_port->setValue(ctx.settings.beamPort);
    m_connect = new QPushButton;
    connect(m_connect, &QPushButton::clicked, this, &BeamEthPage::toggleConnection);
    auto *conn = new QGroupBox(QStringLiteral("Kết nối Ethernet"));
    auto *cl = new QHBoxLayout(conn);
    cl->addWidget(m_pill);
    cl->addWidget(m_address);
    cl->addWidget(m_port);
    cl->addWidget(m_connect);
    cl->addStretch(1);

    // Tham số bản tin.
    m_esp = angleBox();
    m_beta = angleBox();
    m_freq = new QSpinBox;
    m_freq->setRange(0, kFreqMax);
    m_freq->setSuffix(QStringLiteral(" MHz"));
    m_freq->setAlignment(Qt::AlignRight);
    m_modeValue = new QSpinBox;
    m_modeValue->setRange(0, kModeValueMax);
    m_modeValue->setAlignment(Qt::AlignRight);
    m_mode = new QButtonGroup(this);          // chọn đúng một trong A0, A1, A2
    auto *modeRow = new QHBoxLayout;
    for (ModeSel m : {ModeSel::A0, ModeSel::A1, ModeSel::A2}) {
        auto *r = new QRadioButton(modeName(m));
        r->setToolTip(QStringLiteral("Gửi byte 0x%1").arg(modeByte(m), 2, 16, QLatin1Char('0')).toUpper());
        m_mode->addButton(r, int(m));
        modeRow->addWidget(r);
    }
    modeRow->addStretch(1);
    m_mode->button(int(ModeSel::A0))->setChecked(true);

    auto *form = new QGroupBox(QStringLiteral("Bản tin beam"));
    auto *f = new QFormLayout(form);
    f->addRow(QStringLiteral("Esp"), m_esp);
    f->addRow(QStringLiteral("Beta"), m_beta);
    f->addRow(QStringLiteral("Freq"), m_freq);
    f->addRow(QStringLiteral("Mode (byte 9)"), m_modeValue);
    f->addRow(QStringLiteral("Mode (byte 10)"), modeRow);
    m_esp->setToolTip(QStringLiteral("Số thập phân có dấu, một chữ số sau dấu phẩy; bản tin gửi giá trị x 10 (16 bit có dấu)"));
    m_beta->setToolTip(m_esp->toolTip());
    m_freq->setToolTip(QStringLiteral("Tần số, đơn vị MHz, số nguyên"));
    for (QAbstractSpinBox *b : {static_cast<QAbstractSpinBox *>(m_esp), static_cast<QAbstractSpinBox *>(m_beta),
                                static_cast<QAbstractSpinBox *>(m_freq), static_cast<QAbstractSpinBox *>(m_modeValue)})
        b->setFixedWidth(160);

    m_preview = new QLabel;
    m_preview->setFont(QFont(QStringLiteral("monospace")));
    m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_preview->setWordWrap(true);
    m_send = new QPushButton(QStringLiteral("Gửi bản tin beam"));
    m_result = new QLabel;
    theme::setRole(m_result, "muted");
    auto *sendRow = new QHBoxLayout;
    sendRow->addWidget(m_send);
    sendRow->addWidget(m_result, 1);
    connect(m_send, &QPushButton::clicked, this, &BeamEthPage::send);

    auto *l = new QVBoxLayout(this);
    l->addWidget(conn);
    l->addWidget(form);
    l->addWidget(new QLabel(QStringLiteral("Bản tin sẽ gửi (hex):")));
    l->addWidget(m_preview);
    l->addLayout(sendRow);
    l->addStretch(1);

    for (QDoubleSpinBox *b : {m_esp, m_beta}) connect(b, &QDoubleSpinBox::valueChanged, this, &BeamEthPage::updatePreview);
    for (QSpinBox *b : {m_freq, m_modeValue}) connect(b, &QSpinBox::valueChanged, this, &BeamEthPage::updatePreview);
    connect(m_mode, &QButtonGroup::idClicked, this, &BeamEthPage::updatePreview);
    connect(ctx.beamLink, &core::Link::stateChanged, this, &BeamEthPage::showState);
    connect(ctx.beamLink, &core::Link::errorOccurred, this, [this](const QString &m) {
        emit logEvent(tagged(EventKind::Err, QStringLiteral("Lỗi Beam Ethernet: %1").arg(m)));
    });
    showState(State::Closed);
    updatePreview();
}

Params BeamEthPage::params() const
{
    Params p;
    p.esp = m_esp->value();
    p.beta = m_beta->value();
    p.freqMhz = m_freq->value();
    p.modeValue = m_modeValue->value();
    p.mode = ModeSel(m_mode->checkedId());
    return p;
}

void BeamEthPage::updatePreview()
{
    const QByteArray frame = buildFrame(params());
    m_preview->setText(frame.isEmpty() ? validate(params()) : QString::fromLatin1(frame.toHex(' ').toUpper()));
}

void BeamEthPage::showState(State s)
{
    m_state = s;
    const model::Status look = s == State::Connected ? model::Status::Ok : s == State::Waiting ? model::Status::Warning : model::Status::Lost;
    const QString text = s == State::Connected ? QStringLiteral("đã kết nối") : s == State::Waiting ? QStringLiteral("đang nối thiết bị") : QStringLiteral("đóng");
    m_pill->setPill(theme::statusMark(look) + QLatin1Char(' ') + QStringLiteral("Beam (Ethernet): ") + text,
                    theme::statusColor(look), theme::statusTextColor(look));
    m_connect->setText(s == State::Closed ? QStringLiteral("Kết nối") : QStringLiteral("Ngắt"));
    m_address->setEnabled(s == State::Closed);
    m_port->setEnabled(s == State::Closed);
    m_send->setEnabled(s == State::Connected);
    emit logEvent(tagged(EventKind::Net, QStringLiteral("Beam (Ethernet): ") + text));
}

void BeamEthPage::toggleConnection()
{
    core::Link *link = m_ctx.beamLink;
    if (m_state != State::Closed) { QMetaObject::invokeMethod(link, &core::Link::stop); return; }
    const QString host = m_address->text().trimmed();
    if (host.isEmpty()) { emit logEvent(tagged(EventKind::Err, QStringLiteral("Chưa nhập địa chỉ IP của thiết bị beam"))); return; }
    const quint16 port = quint16(m_port->value());
    Settings::save("beam/address", host);
    Settings::save("beam/port", port);
    core::TcpClientTransport *t = m_ctx.beamTcp;
    QMetaObject::invokeMethod(link, [=] { t->configure(host, port); link->start(); });
}

void BeamEthPage::send()
{
    const Params p = params();
    const QString error = validate(p);
    if (!error.isEmpty()) { m_result->setText(error); return; }
    core::Request r;
    r.data = buildFrame(p);                       // không có trả lời: gửi xong là xong
    m_ctx.beamLink->send(r);
    const QString text = QStringLiteral("Esp %1, Beta %2, Freq %3 MHz, Mode %4 / %5")
                             .arg(p.esp, 0, 'f', 1).arg(p.beta, 0, 'f', 1).arg(p.freqMhz).arg(p.modeValue).arg(modeName(p.mode));
    m_result->setText(QStringLiteral("Đã gửi lúc %1: %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), text));
    emit logEvent(tagged(EventKind::Ctrl, QStringLiteral("Đã gửi (không chờ ACK): Beam Ethernet, %1 [%2]")
                                              .arg(text, QString::fromLatin1(r.data.toHex(' ').toUpper()))));
}

} // namespace ui
