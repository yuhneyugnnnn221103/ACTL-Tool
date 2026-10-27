#include "beam_eth_page.h"
#include "log_filter.h"
#include "theme.h"
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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
    // Ba cột ứng với ba mode (byte 10 của MODE = 01, 10, 11); mỗi cột nhập Esp, Beta, Freq, Mode riêng.
    auto *form = new QGroupBox(QStringLiteral("Bản tin beam"));
    auto *g = new QGridLayout(form);
    const QStringList rows{QStringLiteral("Esp"), QStringLiteral("Beta"), QStringLiteral("Freq"), QStringLiteral("Mode")};
    for (int r = 0; r < rows.size(); ++r) g->addWidget(new QLabel(rows.at(r)), r + 1, 0);
    for (int k = 0; k < kFrameCount; ++k) {
        auto *title = new QLabel(QStringLiteral("Mode %1").arg(modeByte(k), 2, 16, QLatin1Char('0')).toUpper());
        title->setAlignment(Qt::AlignCenter);
        QFont tf = title->font();
        tf.setBold(true);
        title->setFont(tf);
        g->addWidget(title, 0, k + 1);

        m_esp[k] = angleBox();
        m_beta[k] = angleBox();
        m_freq[k] = new QSpinBox;
        m_freq[k]->setRange(0, kFreqMax);
        m_freq[k]->setSuffix(QStringLiteral(" MHz"));
        m_freq[k]->setAlignment(Qt::AlignRight);
        m_modeValue[k] = new QSpinBox;
        m_modeValue[k]->setRange(0, kModeValueMax);
        m_modeValue[k]->setAlignment(Qt::AlignRight);
        m_esp[k]->setToolTip(QStringLiteral("Số thập phân có dấu, một chữ số sau dấu phẩy; bản tin gửi giá trị x 10 (16 bit có dấu)"));
        m_beta[k]->setToolTip(m_esp[k]->toolTip());
        m_freq[k]->setToolTip(QStringLiteral("Tần số, đơn vị MHz, số nguyên"));
        m_modeValue[k]->setToolTip(QStringLiteral("Byte 9 của MODE trong khung này; byte 10 do chương trình đặt = %1")
                                       .arg(modeByte(k), 2, 16, QLatin1Char('0')).toUpper());
        g->addWidget(m_esp[k], 1, k + 1);
        g->addWidget(m_beta[k], 2, k + 1);
        g->addWidget(m_freq[k], 3, k + 1);
        g->addWidget(m_modeValue[k], 4, k + 1);
        g->setColumnStretch(k + 1, 1);
        for (QAbstractSpinBox *b : {static_cast<QAbstractSpinBox *>(m_esp[k]), static_cast<QAbstractSpinBox *>(m_beta[k]),
                                    static_cast<QAbstractSpinBox *>(m_freq[k]), static_cast<QAbstractSpinBox *>(m_modeValue[k])}) {
            b->setMinimumWidth(130);
            if (auto *d = qobject_cast<QDoubleSpinBox *>(b)) connect(d, &QDoubleSpinBox::valueChanged, this, &BeamEthPage::updatePreview);
            else connect(static_cast<QSpinBox *>(b), &QSpinBox::valueChanged, this, &BeamEthPage::updatePreview);
        }
    }

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

    connect(ctx.monitorLink, &core::Link::stateChanged, this, &BeamEthPage::showState);   // beam đi chung đường giám sát (Ethernet tới Gateway)
    showState(State::Closed);
    updatePreview();
}

Params BeamEthPage::params() const
{
    Params p;
    for (int k = 0; k < kFrameCount; ++k) {
        p.frame[k].esp = m_esp[k]->value();
        p.frame[k].beta = m_beta[k]->value();
        p.frame[k].freqMhz = m_freq[k]->value();
        p.frame[k].modeValue = m_modeValue[k]->value();
    }
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
    r.data = proto::ctlframe::wrap(proto::ctlframe::SecBeam, buildFrame(p));                       // không có trả lời: gửi xong là xong
    m_ctx.monitorLink->send(r);
    QStringList parts;
    for (int k = 0; k < kFrameCount; ++k) {
        const FrameParams &f = p.frame[k];
        parts << QStringLiteral("[%1] Esp %2, Beta %3, Freq %4 MHz, Mode %5").arg(modeByte(k), 2, 16, QLatin1Char('0'))
                     .arg(f.esp, 0, 'f', 1).arg(f.beta, 0, 'f', 1).arg(f.freqMhz).arg(f.modeValue);
    }
    const QString text = parts.join(QStringLiteral("; "));
    m_result->setText(QStringLiteral("Đã gửi lúc %1: %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), text));
    emit logEvent(tagged(EventKind::Ctrl, QStringLiteral("Đã gửi (không chờ ACK): Beam Ethernet, %1 [%2]")
                                              .arg(text, QString::fromLatin1(r.data.toHex(' ').toUpper()))));
}

} // namespace ui
