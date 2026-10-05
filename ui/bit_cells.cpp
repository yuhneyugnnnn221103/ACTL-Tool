#include "bit_cells.h"
#include "theme.h"
#include <QGridLayout>
#include <QHelpEvent>
#include <QPainter>
#include <QToolTip>

namespace ui {

namespace {
constexpr int kCell = 14, kGap = 2, kBits = 8, kCaptionGap = 6;
}

BitCells::BitCells(const QString &caption, QWidget *parent) : QWidget(parent), m_caption(caption)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void BitCells::setValue(int byte, bool known)
{
    byte &= 0xFF;
    if (byte == m_value && known == m_known) return;
    m_value = byte;
    m_known = known;
    update();
}

int BitCells::cellsLeft() const { return fontMetrics().horizontalAdvance(m_caption) + kCaptionGap; }

QSize BitCells::sizeHint() const
{
    return {cellsLeft() + kBits * kCell + (kBits - 1) * kGap, kCell + 4};
}

int BitCells::bitAt(const QPoint &p) const
{
    const int x = p.x() - cellsLeft();
    if (x < 0 || p.y() < 2 || p.y() > 2 + kCell) return -1;
    const int i = x / (kCell + kGap);
    if (i >= kBits || x % (kCell + kGap) >= kCell) return -1;
    return kBits - 1 - i;
}

void BitCells::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(theme::palette().textMuted);
    p.drawText(QRect(0, 0, cellsLeft() - kCaptionGap, height()), Qt::AlignLeft | Qt::AlignVCenter, m_caption);

    const QColor on = theme::statusColor(model::Status::Trip);
    const QColor off = theme::palette().border;
    p.setPen(Qt::NoPen);
    for (int i = 0; i < kBits; ++i) {
        const int bit = kBits - 1 - i;
        p.setBrush(m_known && (m_value >> bit & 1) ? on : off);
        p.drawRoundedRect(QRectF(cellsLeft() + i * (kCell + kGap), 2, kCell, kCell), 4, 4);
    }
}

bool BitCells::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        if (!m_known) { QToolTip::hideText(); return true; }
        const int bit = bitAt(he->pos());
        const QString text = bit >= 0
            ? QStringLiteral("%1 · bit %2 = %3").arg(m_caption).arg(bit).arg(m_value >> bit & 1)
            : QStringLiteral("%1 = 0x%2").arg(m_caption).arg(m_value, 2, 16, QLatin1Char('0')).toUpper();
        QToolTip::showText(he->globalPos(), text, this);
        return true;
    }
    return QWidget::event(e);
}

QWidget *BitCells::makeGrid(int count, int columns, const QString &captionFmt, QList<BitCells *> &out, QWidget *parent)
{
    auto *w = new QWidget(parent);
    auto *grid = new QGridLayout(w);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(theme::kMargin + 4);
    grid->setVerticalSpacing(theme::kSpace);
    // Số thứ tự chạy xuống từng cột trước (1..rows ở cột đầu) để đọc theo cột.
    const int rows = (count + columns - 1) / columns;
    for (int i = 0; i < count; ++i) {
        auto *c = new BitCells(captionFmt.arg(i + 1), w);
        out << c;
        grid->addWidget(c, i % rows, i / rows);
    }
    grid->setColumnStretch(columns, 1);
    return w;
}

} // namespace ui
