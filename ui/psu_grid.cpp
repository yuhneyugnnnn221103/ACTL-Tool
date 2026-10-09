#include "psu_grid.h"
#include "overview_grid.h"
#include "theme.h"
#include "../proto/psu_monitor_proto.h"
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

namespace ui {

using model::Status;

namespace {
constexpr int kLeft = 64, kTop = 26, kGap = 3, kRowHeight = 28;
constexpr int kClusters = proto::psumon::kNumCluster;
}

PsuGrid::PsuGrid(const model::PsuStore *store, QWidget *parent) : QWidget(parent), m_store(store)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize PsuGrid::sizeHint() const { return {560, kTop + kRowHeight * m_store->count()}; }

QRectF PsuGrid::cellRect(int row, int cluster) const
{
    const qreal w = qreal(width() - kLeft) / kClusters;
    const qreal h = qreal(height() - kTop) / m_store->count();
    return QRectF(kLeft + cluster * w, kTop + row * h, w - kGap, h - kGap);
}

QRectF PsuGrid::nameRect(int row) const
{
    const qreal h = qreal(height() - kTop) / m_store->count();
    return QRectF(0, kTop + row * h, kLeft - kGap - 4, h - kGap);
}

bool PsuGrid::hitTest(const QPointF &p, int &addr, int &cluster) const
{
    if (p.y() < kTop) return false;
    const int row = int((p.y() - kTop) * m_store->count() / (height() - kTop));
    if (row < 0 || row >= m_store->count()) return false;
    addr = m_store->firstAddr() + row;
    if (nameRect(row).contains(p)) { cluster = -1; return true; }
    if (p.x() < kLeft) return false;
    cluster = int((p.x() - kLeft) * kClusters / (width() - kLeft));
    return cluster >= 0 && cluster < kClusters && cellRect(row, cluster).contains(p);
}

void PsuGrid::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor labelColor = palette().color(QPalette::Disabled, QPalette::WindowText);

    p.setPen(labelColor);
    for (int c = 0; c < kClusters; ++c) {
        const QRectF r = cellRect(0, c);
        p.drawText(QRectF(r.left(), 0, r.width(), kTop - 4), Qt::AlignCenter, QStringLiteral("Cụm %1").arg(c + 1));
    }

    QFont markFont = font();
    markFont.setBold(true);
    p.setFont(markFont);
    auto fill = [&](const QRectF &r, Status s, const QString &text, bool lost) {
        p.setPen(Qt::NoPen);
        p.setBrush(OverviewGrid::statusColor(s));
        p.drawRoundedRect(r, 6, 6);
        p.setPen(theme::statusTextColor(s));
        p.drawText(r, Qt::AlignCenter, lost && text.isEmpty() ? QStringLiteral("?") : text);   // mất kết nối: nền theo số liệu cuối, thêm dấu ?
    };
    for (int row = 0; row < m_store->count(); ++row) {
        const int addr = m_store->firstAddr() + row;
        const bool lost = m_store->isLost(addr);
        fill(nameRect(row), m_store->displayStatus(addr), lost ? QStringLiteral("PSU %1 ?").arg(addr) : QStringLiteral("PSU %1").arg(addr), false);
        for (int c = 0; c < kClusters; ++c) fill(cellRect(row, c), m_store->clusterDisplayStatus(addr, c), {}, lost);
    }
}

void PsuGrid::mousePressEvent(QMouseEvent *e)
{
    int addr, cluster;
    if (e->button() == Qt::LeftButton && hitTest(e->position(), addr, cluster)) emit psuClicked(addr);
}

bool PsuGrid::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        int addr, cluster;
        if (m_tooltip && hitTest(he->pos(), addr, cluster))
            QToolTip::showText(he->globalPos(), m_tooltip(addr, cluster), this);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

} // namespace ui
