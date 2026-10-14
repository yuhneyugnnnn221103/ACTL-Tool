#include "led_indicator.h"
#include "theme.h"
#include <QPainter>
#include <QRadialGradient>

namespace ui {

namespace {
constexpr int kDiameter = 28, kGap = 2;
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
    return {qMax(44, fontMetrics().horizontalAdvance(m_caption) + 8), kDiameter + kGap + fontMetrics().height() + 4};
}

void LedIndicator::paintEvent(QPaintEvent *)
{
    QColor base;
    switch (m_state) {
    case State::On:      base = theme::statusColor(model::Status::Ok); break;
    case State::Fault:   base = theme::statusColor(model::Status::Trip); break;
    case State::Idle:    base = QColor(0xA8, 0xA6, 0x9C); break;     // tắt bình thường: xám đậm
    case State::Unknown: base = theme::statusColor(model::Status::NoData); break;
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

    p.setFont(font());
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(QRectF(0, led.bottom() + kGap, width(), height() - led.bottom() - kGap), Qt::AlignHCenter | Qt::AlignTop,
               m_caption);
}

} // namespace ui
