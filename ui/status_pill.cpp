#include "status_pill.h"
#include <QMouseEvent>
#include <QPainter>

namespace ui {

StatusPill::StatusPill(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void StatusPill::setPill(const QString &text, const QColor &background, const QColor &foreground)
{
    if (text == m_text && background == m_bg && foreground == m_fg) return;
    m_text = text;
    m_bg = background;
    m_fg = foreground;
    updateGeometry();
    update();
}

void StatusPill::setClickable(bool on)
{
    m_clickable = on;
    setCursor(on ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void StatusPill::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_clickable && e->button() == Qt::LeftButton && rect().contains(e->position().toPoint())) emit clicked();
    QWidget::mouseReleaseEvent(e);
}

QSize StatusPill::sizeHint() const
{
    QFont f = font();
    f.setBold(true);
    return {QFontMetrics(f).horizontalAdvance(m_text) + 20, 24};
}

void StatusPill::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(m_bg);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), height() / 2.0, height() / 2.0);
    QFont f = font();
    f.setBold(true);
    p.setFont(f);
    p.setPen(m_fg);
    p.drawText(rect(), Qt::AlignCenter, m_text);
}

} // namespace ui
