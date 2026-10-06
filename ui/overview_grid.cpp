#include "overview_grid.h"
#include "theme.h"
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

namespace ui {

using model::Status;

namespace {
constexpr int kLeft = 52, kTop = 26, kGap = 3;

}

QColor OverviewGrid::statusColor(Status s) { return theme::statusColor(s); }

OverviewGrid::OverviewGrid(const model::DeviceStore *store, QWidget *parent)
    : QWidget(parent), m_store(store)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void OverviewGrid::select(int mb, int trb)
{
    m_selMb = mb;
    m_selTrb = trb;
    update();
}

void OverviewGrid::setDebugTrb(int mb, int trb)
{
    if (mb == m_dbgMb && trb == m_dbgTrb) return;
    m_dbgMb = mb;
    m_dbgTrb = trb;
    update();
}

QRectF OverviewGrid::cellRect(int mb, int trb) const
{
    const qreal w = qreal(width() - kLeft) / m_store->mbCount();
    const qreal h = qreal(height() - kTop) / m_store->trbPerMb();
    return QRectF(kLeft + mb * w, kTop + trb * h, w - kGap, h - kGap);
}

bool OverviewGrid::hitTest(const QPointF &p, int &mb, int &trb) const
{
    if (p.x() < kLeft || p.y() < kTop) return false;
    mb = int((p.x() - kLeft) * m_store->mbCount() / (width() - kLeft));
    trb = int((p.y() - kTop) * m_store->trbPerMb() / (height() - kTop));
    return m_store->contains(mb, trb) && cellRect(mb, trb).contains(p);
}

void OverviewGrid::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor labelColor = palette().color(QPalette::Disabled, QPalette::WindowText);

    p.setPen(labelColor);
    for (int mb = 0; mb < m_store->mbCount(); ++mb) {
        const QRectF c = cellRect(mb, 0);
        p.drawText(QRectF(c.left(), 0, c.width(), kTop - 4), Qt::AlignCenter,
                   c.width() >= 44 ? QStringLiteral("MB%1").arg(mb) : QString::number(mb));
    }
    for (int trb = 0; trb < m_store->trbPerMb(); ++trb) {
        const QRectF c = cellRect(0, trb);
        p.drawText(QRectF(0, c.top(), kLeft - 8, c.height()), Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("TRB%1").arg(trb));
    }

    for (int mb = 0; mb < m_store->mbCount(); ++mb) {
        for (int trb = 0; trb < m_store->trbPerMb(); ++trb) {
            const Status s = m_store->trb(mb, trb).status;
            const QRectF c = cellRect(mb, trb);
            p.setPen(Qt::NoPen);
            p.setBrush(statusColor(s));
            p.drawRoundedRect(c, 6, 6);
            if (mb == m_dbgMb && trb == m_dbgTrb) {   // viền xanh dương: TRB đang Debug
                p.setPen(QPen(theme::statusColor(Status::Updating).darker(130), 3));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(c.adjusted(1.5, 1.5, -1.5, -1.5), 6, 6);
            }
            if (mb == m_selMb && trb == m_selTrb) {
                p.setPen(QPen(palette().color(QPalette::WindowText), 2));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(c.adjusted(1, 1, -1, -1), 6, 6);
            }
        }
    }
}

void OverviewGrid::mousePressEvent(QMouseEvent *e)
{
    int mb, trb;
    if (e->button() == Qt::LeftButton && hitTest(e->position(), mb, trb)) {
        select(mb, trb);
        emit trbClicked(mb, trb);
    }
}

bool OverviewGrid::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        int mb, trb;
        if (m_tooltip && hitTest(he->pos(), mb, trb))
            QToolTip::showText(he->globalPos(), m_tooltip(mb, trb), this, cellRect(mb, trb).toRect());
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

} // namespace ui
