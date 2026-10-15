#include "beam_eth_page.h"
#include "log_filter.h"
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
    m_hint = new QLabel(QStringLiteral("Chưa kết nối đường giám sát (Ethernet tới Gateway): bấm thẻ kết nối ở góc trên bên trái để nhập IP, cổng và kết nối. Bản tin beam đi chung đường này."));
    theme::setRole(m_hint, "muted");
    l->addWidget(m_hint);
    l->addWidget(form);
    l->addWidget(new QLabel(QStringLiteral("Bản tin sẽ gửi (hex):")));
    l->addWidget(m_preview);
    l->addLayout(sendRow);
    l->addStretch(1);

    for (QDoubleSpinBox *b : {m_esp, m_beta}) connect(b, &QDoubleSpinBox::valueChanged, this, &BeamEthPage::updatePreview);
    for (QSpinBox *b : {m_freq, m_modeValue}) connect(b, &QSpinBox::valueChanged, this, &BeamEthPage::updatePreview);
    connect(m_mode, &QButtonGroup::idClicked, this, &BeamEthPage::updatePreview);
    connect(ctx.monitorLink, &core::Link::stateChanged, this, &BeamEthPage::showState);   // beam đi chung đường giám sát (Ethernet tới Gateway)
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
    m_send->setEnabled(s == State::Connected);
    m_hint->setVisible(s != State::Connected);
}

void BeamEthPage::send()
{
    const Params p = params();
    const QString error = validate(p);
    if (!error.isEmpty()) { m_result->setText(error); return; }
    core::Request r;
    r.data = buildFrame(p);                       // không có trả lời: gửi xong là xong
    m_ctx.monitorLink->send(r);
    const QString text = QStringLiteral("Esp %1, Beta %2, Freq %3 MHz, Mode %4 / %5")
                             .arg(p.esp, 0, 'f', 1).arg(p.beta, 0, 'f', 1).arg(p.freqMhz).arg(p.modeValue).arg(modeName(p.mode));
    m_result->setText(QStringLiteral("Đã gửi lúc %1: %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), text));
    emit logEvent(tagged(EventKind::Ctrl, QStringLiteral("Đã gửi (không chờ ACK): Beam Ethernet, %1 [%2]")
                                              .arg(text, QString::fromLatin1(r.data.toHex(' ').toUpper()))));
}

} // namespace ui
