#include "bit_cells.h"
#include "theme.h"
#include <QGridLayout>
#include <QHelpEvent>
#include <QPainter>
#include <QToolTip>

namespace ui {

namespace {
constexpr int kCell = 16, kGap = 2, kBits = 8, kCaptionGap = 8;
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

int BitCells::captionTextWidth() const { return fontMetrics().horizontalAdvance(m_caption); }

void BitCells::setCaptionWidth(int px)
{
    m_captionWidth = px;
    updateGeometry();
    update();
}

void BitCells::setTooltipTitle(const QString &title) { m_tipTitle = title; }

void BitCells::setBitNames(const QStringList &names) { m_bitNames = names; }

int BitCells::cellsLeft() const { return (m_captionWidth >= 0 ? m_captionWidth : captionTextWidth()) + kCaptionGap; }

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
        auto bitName = [this](int b) { return b < m_bitNames.size() && !m_bitNames.at(b).isEmpty() ? m_bitNames.at(b) : QString(); };
        const QString title = m_tipTitle.isEmpty() ? m_caption : m_tipTitle;
        QString text;
        if (bit >= 0) {
            text = QStringLiteral("%1 · bit %2 = %3").arg(title).arg(bit).arg(m_value >> bit & 1);
            if (!bitName(bit).isEmpty()) text += QStringLiteral("\n") + bitName(bit);
        } else {
            text = QStringLiteral("%1 = 0x%2").arg(title).arg(m_value, 2, 16, QLatin1Char('0')).toUpper();
            QStringList active;
            for (int b = 7; b >= 0; --b)
                if (m_value >> b & 1) active << (bitName(b).isEmpty() ? QStringLiteral("bit %1").arg(b) : bitName(b));
            if (!active.isEmpty()) text += QStringLiteral("\nĐang báo: ") + active.join(QStringLiteral(", "));
        }
        QToolTip::showText(he->globalPos(), text, this);
        return true;
    }
    return QWidget::event(e);
}

QWidget *BitCells::makeGrid(const QStringList &captions, int columns, QList<BitCells *> &out, QWidget *parent)
{
    auto *w = new QWidget(parent);
    auto *grid = new QGridLayout(w);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(theme::kSpace);
    grid->setVerticalSpacing(4);
    // Số thứ tự chạy xuống từng cột trước để đọc theo cột.
    const int count = captions.size(), rows = (count + columns - 1) / columns;
    QList<BitCells *> cells;
    for (int i = 0; i < count; ++i) {
        auto *c = new BitCells(captions.at(i), w);
        cells << c;
        out << c;
        grid->addWidget(c, i % rows, 1 + 2 * (i / rows));   // cột lẻ chứa nội dung, cột chẵn là khoảng giãn
    }
    for (int col = 0; col < columns; ++col) {          // nhãn mỗi cột có chung độ rộng: các cụm 8 ô thẳng hàng
        int widest = 0;
        for (int i = col * rows; i < qMin(count, (col + 1) * rows); ++i) widest = qMax(widest, cells[i]->captionTextWidth());
        for (int i = col * rows; i < qMin(count, (col + 1) * rows); ++i) cells[i]->setCaptionWidth(widest);
    }
    for (int col = 0; col <= columns; ++col) grid->setColumnStretch(2 * col, 1);   // giãn đều hai lề và giữa các cột
    return w;
}

} // namespace ui
