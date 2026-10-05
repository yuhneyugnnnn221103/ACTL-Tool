#include "led_indicator.h"
#include "theme.h"
#include <QPainter>
#include <QRadialGradient>

namespace ui {

namespace {
constexpr int kDiameter = 36, kGap = 4;
}

LedIndicator::LedIndicator(const QString &caption, QWidget *parent) : QWidget(parent), m_caption(caption)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

void LedIndicator::setState(State s, const QString &toolTip)
{
    setToolTip(toolTip);
    if (s == m_state) return;
    m_state = s;
    update();
}

QSize LedIndicator::sizeHint() const
{
    return {qMax(48, fontMetrics().horizontalAdvance(m_caption) + 8), kDiameter + kGap + fontMetrics().height() + 4};
}

void LedIndicator::paintEvent(QPaintEvent *)
{
    QColor base;
    QString glyph;
    switch (m_state) {
    case State::On:      base = theme::statusColor(model::Status::Ok);   glyph = QStringLiteral("✓"); break;
    case State::Fault:   base = theme::statusColor(model::Status::Trip); glyph = QStringLiteral("✕"); break;
    case State::Idle:    base = QColor(0xB4, 0xB2, 0xA9);                glyph = QStringLiteral("–"); break;
    case State::Unknown: base = theme::statusColor(model::Status::NoData); glyph = QStringLiteral("?"); break;
    }

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF led((width() - kDiameter) / 2.0, 2, kDiameter, kDiameter);

    QRadialGradient g(led.center() - QPointF(kDiameter * 0.18, kDiameter * 0.22), kDiameter * 0.75);
    g.setColorAt(0.0, base.lighter(160));
    g.setColorAt(0.55, base);
    g.setColorAt(1.0, base.darker(130));
    p.setPen(QPen(base.darker(150), 1));
    p.setBrush(g);
    p.drawEllipse(led);

    QFont f = font();
    f.setBold(true);
    f.setPixelSize(20);
    p.setFont(f);
    p.setPen(m_state == State::Unknown ? QColor(0x6B, 0x72, 0x80) : QColor(Qt::white));
    p.drawText(led, Qt::AlignCenter, glyph);

    p.setFont(font());
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(QRectF(0, led.bottom() + kGap, width(), height() - led.bottom() - kGap), Qt::AlignHCenter | Qt::AlignTop,
               m_caption);
}

} // namespace ui
