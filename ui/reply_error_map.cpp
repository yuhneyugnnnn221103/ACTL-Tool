#include "reply_error_map.h"
#include "../proto/psu_address.h"
#include <QHelpEvent>
#include <QPainter>
#include <QToolTip>
#include <cmath>

namespace ui {

namespace {
constexpr int kLeft = 52, kTop = 26, kGap = 3, kLegend = 22, kPsuLabel = 20;
}

ReplyErrorMap::ReplyErrorMap(const model::DeviceStore *trbs, const model::PsuStore *psus, QWidget *parent)
    : QWidget(parent), m_trbs(trbs), m_psus(psus)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

QColor ReplyErrorMap::rateColor(double rate)
{
    const double t = std::sqrt(qBound(0.0, rate / kRedAt, 1.0));   // căn để vài % lỗi đã thấy được khác 0
    return QColor::fromHsv(int(120 * (1.0 - t)), 150, 225);
}

double ReplyErrorMap::averageRate() const
{
    double sum = 0;
    int n = 0;
    for (int mb = 0; mb < m_trbs->mbCount(); ++mb)
        for (int t = 0; t < m_trbs->trbPerMb(); ++t) {
            const model::ReplyStats &s = m_trbs->trb(mb, t).reply;
            if (s.count()) { sum += s.rate(); ++n; }
        }
    for (int a = m_psus->firstAddr(); a < m_psus->firstAddr() + m_psus->count(); ++a) {
        const model::ReplyStats &s = m_psus->psu(a).reply;
        if (s.count()) { sum += s.rate(); ++n; }
    }
    return n ? sum / n : -1.0;
}

QList<ReplyErrorMap::Cell> ReplyErrorMap::layoutCells() const
{
    QList<Cell> cells;
    const int mbs = m_trbs->mbCount(), per = m_trbs->trbPerMb();
    const int psuH = qMax(28, qMin(46, height() / 8));
    const qreal gridH = height() - kTop - kLegend - kPsuLabel - psuH;
    const qreal w = qreal(width() - kLeft) / mbs, h = gridH / per;
    for (int mb = 0; mb < mbs; ++mb)
        for (int t = 0; t < per; ++t)
            cells.append({QRectF(kLeft + mb * w, kTop + t * h, w - kGap, h - kGap), &m_trbs->trb(mb, t).reply,
                          QStringLiteral("MB%1 · TRB%2").arg(mb + 1).arg(t + 1)});
    const int n = m_psus->count();
    const qreal pw = qreal(width() - kLeft) / qMax(n, mbs);
    const qreal top = kTop + gridH + kPsuLabel;
    for (int i = 0; i < n; ++i)
        cells.append({QRectF(kLeft + i * pw, top, pw - kGap, psuH - kGap), &m_psus->psu(m_psus->firstAddr() + i).reply,
                      QStringLiteral("PSU %1").arg(proto::psuaddr::displayNo(m_psus->firstAddr() + i))});
    return cells;
}

QString ReplyErrorMap::detail(const Cell &c)
{
    const model::ReplyStats &s = *c.stats;
    if (!s.count()) return QStringLiteral("%1\nChưa có trả lời nào").arg(c.name);
    return QStringLiteral("%1\nTỉ lệ lỗi: %2% (%3/%4 trả lời gần nhất là khung rỗng)\nTổng cộng: %5 có dữ liệu, %6 khung rỗng")
        .arg(c.name).arg(s.rate() * 100, 0, 'f', 1).arg(s.windowEmpty).arg(s.count()).arg(s.okTotal).arg(s.emptyTotal);
}

void ReplyErrorMap::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor label = palette().color(QPalette::Disabled, QPalette::WindowText);
    const QList<Cell> cells = layoutCells();
    const int mbs = m_trbs->mbCount(), per = m_trbs->trbPerMb();

    p.setPen(label);
    for (int mb = 0; mb < mbs; ++mb) {
        const QRectF c = cells.at(mb * per).rect;
        p.drawText(QRectF(c.left(), 0, c.width(), kTop - 4), Qt::AlignCenter,
                   c.width() >= 44 ? QStringLiteral("MB%1").arg(mb + 1) : QString::number(mb + 1));
    }
    for (int t = 0; t < per; ++t) {
        const QRectF c = cells.at(t).rect;
        p.drawText(QRectF(0, c.top(), kLeft - 8, c.height()), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("TRB%1").arg(t + 1));
    }
    const int firstPsu = mbs * per;
    if (cells.size() > firstPsu)
        p.drawText(QRectF(kLeft, cells.at(firstPsu).rect.top() - kPsuLabel, 300, kPsuLabel), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("PSU"));

    for (const Cell &c : cells) {
        const model::ReplyStats &s = *c.stats;
        const QColor bg = s.count() ? rateColor(s.rate()) : palette().color(QPalette::Disabled, QPalette::Button);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(c.rect, 6, 6);
        QFont f = font();
        f.setBold(true);
        f.setPixelSize(qBound(9, int(qMin(c.rect.height(), c.rect.width()) * 0.45), 18));
        p.setFont(f);
        p.setPen(bg.lightness() < 140 ? Qt::white : QColor(0x1F, 0x29, 0x37));
        const double pct = s.rate() * 100;
        p.drawText(c.rect, Qt::AlignCenter,
                   !s.count() ? QStringLiteral("–") : pct > 0 && pct < 0.5 ? QStringLiteral("<1") : QString::number(int(std::round(pct))));
    }

    // Chú giải: thanh gam màu 0% -> kRedAt, kèm ghi chú đơn vị.
    p.setFont(font());
    const QRectF bar(kLeft, height() - kLegend + 4, qMin(240, width() - kLeft - 200), 10);
    for (int i = 0; i < int(bar.width()); ++i) {
        p.setPen(rateColor(kRedAt * i / bar.width()));
        p.drawLine(QPointF(bar.left() + i, bar.top()), QPointF(bar.left() + i, bar.bottom()));
    }
    p.setPen(label);
    p.drawText(QRectF(bar.left() - 36, bar.top() - 4, 32, 18), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("0%"));
    p.drawText(QRectF(bar.right() + 4, bar.top() - 4, width() - bar.right(), 18), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("≥%1%  ·  %2 lần trả lời gần nhất, số trong ô là %")
                   .arg(int(kRedAt * 100)).arg(model::ReplyStats::kWindow));
}

bool ReplyErrorMap::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        for (const Cell &c : layoutCells())
            if (c.rect.contains(he->pos())) {
                QToolTip::showText(he->globalPos(), detail(c), this, c.rect.toRect());
                return true;
            }
        QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

} // namespace ui
