#include "plot_widget.h"
#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace ui {

namespace {
constexpr qreal kLeft = 64, kRight = 12, kTop = 22, kBottom = 22;

// Bước "đẹp" 1-2-5 cho trục giá trị.
double niceStep(double span, int maxTicks)
{
    const double raw = span / qMax(1, maxTicks);
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (raw <= m * mag) return m * mag;
    return 10 * mag;
}

QString valueText(double v, double step)
{
    if (std::fabs(v) >= 1e7 || (step > 0 && step < 1e-3)) return QString::number(v, 'g', 6);
    const int decimals = step >= 1 ? 0 : qMin(6, int(std::ceil(-std::log10(step))));
    return QString::number(v, 'f', decimals);
}
}

PlotWidget::PlotWidget(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(170);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void PlotWidget::setLines(const QVector<PlotLine> &lines)
{
    m_lines = lines;
    for (PlotLine &l : m_lines) {                         // ngưỡng cắt đường: gấp 10 lần khoảng cách mẫu điển hình, tối thiểu 5 s
        QVector<qint64> d;
        for (int i = 1; i < l.t.size() && i < 257; ++i) d << l.t.at(i) - l.t.at(i - 1);
        std::sort(d.begin(), d.end());
        const qint64 median = d.isEmpty() ? 0 : d.at(d.size() / 2);
        l.gapMs = qMax<qint64>(5000, 10 * median);
    }
    update();
}

void PlotWidget::setRange(qint64 t0, qint64 t1)
{
    if (t1 <= t0) t1 = t0 + 1;
    m_t0 = t0;
    m_t1 = t1;
    update();
}

QRectF PlotWidget::plotRect() const { return QRectF(kLeft, kTop, qMax<qreal>(10, width() - kLeft - kRight), qMax<qreal>(10, height() - kTop - kBottom)); }
qint64 PlotWidget::timeAt(qreal x) const { const QRectF r = plotRect(); return m_t0 + qint64((x - r.left()) / r.width() * double(m_t1 - m_t0)); }
qreal PlotWidget::xOf(qint64 t) const { const QRectF r = plotRect(); return r.left() + double(t - m_t0) / double(m_t1 - m_t0) * r.width(); }

qint64 PlotWidget::niceTimeStep(qint64 span, int maxTicks)
{
    static const qint64 steps[] = {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 15000, 30000, 60000, 120000, 300000, 600000,
                                   900000, 1800000, 3600000, 7200000, 10800000, 21600000, 43200000, 86400000, 172800000, 604800000};
    for (qint64 s : steps)
        if (span / s <= maxTicks) return s;
    return steps[sizeof(steps) / sizeof(steps[0]) - 1];
}

QString PlotWidget::timeLabel(qint64 t, qint64 step)
{
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(t);
    if (step >= 86400000) return dt.toString(QStringLiteral("dd/MM"));
    if (step >= 60000) return dt.toString(QStringLiteral("HH:mm"));
    if (step >= 1000) return dt.toString(QStringLiteral("HH:mm:ss"));
    return dt.toString(QStringLiteral("HH:mm:ss.zzz"));
}

// Khoảng giá trị của các mẫu đang nhìn thấy, thêm 6% lề.
bool PlotWidget::yRange(double &lo, double &hi) const
{
    bool any = false;
    for (const PlotLine &l : m_lines) {
        const int a = int(std::lower_bound(l.t.begin(), l.t.end(), m_t0) - l.t.begin());
        const int b = int(std::upper_bound(l.t.begin(), l.t.end(), m_t1) - l.t.begin());
        for (int i = a; i < b; ++i) {
            if (!any) { lo = hi = l.v.at(i); any = true; }
            else { lo = qMin(lo, l.v.at(i)); hi = qMax(hi, l.v.at(i)); }
        }
    }
    if (!any) return false;
    if (hi - lo < 1e-9) { lo -= 1; hi += 1; }
    const double pad = (hi - lo) * 0.06;
    lo -= pad;
    hi += pad;
    return true;
}

void PlotWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    const QColor text = palette().color(QPalette::WindowText);
    const QColor muted = palette().color(QPalette::Disabled, QPalette::WindowText);
    const QColor grid = QColor(text.red(), text.green(), text.blue(), 40);
    const QRectF r = plotRect();
    p.fillRect(rect(), palette().color(QPalette::Base));
    p.setPen(muted);
    p.drawRect(r);

    // Tiêu đề và chú giải.
    QFont bold = font();
    bold.setBold(true);
    p.setFont(bold);
    p.setPen(text);
    p.drawText(QRectF(kLeft, 2, r.width(), kTop - 4), Qt::AlignLeft | Qt::AlignVCenter, m_title);
    p.setFont(font());
    qreal lx = kLeft + QFontMetricsF(bold).horizontalAdvance(m_title) + 20;
    for (const PlotLine &l : m_lines) {
        const qreal w = QFontMetricsF(font()).horizontalAdvance(l.name);
        if (lx + w + 20 > width() - kRight) break;
        p.fillRect(QRectF(lx, 8, 10, 8), l.color);
        p.setPen(text);
        p.drawText(QRectF(lx + 14, 2, w + 6, kTop - 4), Qt::AlignLeft | Qt::AlignVCenter, l.name);
        lx += w + 28;
    }

    // Trục thời gian.
    const qint64 span = m_t1 - m_t0;
    const qint64 step = niceTimeStep(span, qMax(2, int(r.width() / 90)));
    const qint64 offset = QDateTime::fromMSecsSinceEpoch(m_t0).offsetFromUtc() * 1000;   // căn mốc theo giờ địa phương
    p.setFont(font());
    for (qint64 t = ((m_t0 + offset) / step) * step - offset; t <= m_t1; t += step) {
        if (t < m_t0) continue;
        const qreal x = xOf(t);
        p.setPen(grid);
        p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        p.setPen(muted);
        p.drawText(QRectF(x - 45, r.bottom() + 3, 90, kBottom - 3), Qt::AlignHCenter | Qt::AlignTop, timeLabel(t, step));
    }

    double lo = 0, hi = 1;
    if (!yRange(lo, hi)) {
        p.setPen(muted);
        p.drawText(r, Qt::AlignCenter, QStringLiteral("Không có dữ liệu trong khoảng này"));
    } else {
        const double ystep = niceStep(hi - lo, qMax(2, int(r.height() / 34)));
        auto yOf = [&](double v) { return r.bottom() - (v - lo) / (hi - lo) * r.height(); };
        for (double v = std::ceil(lo / ystep) * ystep; v <= hi; v += ystep) {
            const qreal y = yOf(v);
            p.setPen(grid);
            p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
            p.setPen(muted);
            p.drawText(QRectF(0, y - 8, kLeft - 4, 16), Qt::AlignRight | Qt::AlignVCenter, valueText(v, ystep));
        }

        p.setClipRect(r);
        const int cols = qMax(1, int(r.width()));
        for (const PlotLine &l : m_lines) {
            if (l.t.isEmpty()) continue;
            int a = int(std::lower_bound(l.t.begin(), l.t.end(), m_t0) - l.t.begin());
            int b = int(std::upper_bound(l.t.begin(), l.t.end(), m_t1) - l.t.begin());
            a = qMax(0, a - 1);                                  // thêm một mẫu mỗi phía để đường vào/ra khung
            b = qMin(l.t.size(), b + 1);
            QPen pen(l.color);
            pen.setWidthF(1.4);
            p.setPen(pen);
            if (b - a > cols * 2) {                              // dày: mỗi cột điểm ảnh vẽ đoạn min-max
                QVector<double> mn(cols, 1e300), mx(cols, -1e300);
                QVector<bool> has(cols, false);
                for (int i = a; i < b; ++i) {
                    const int c = qBound(0, int((xOf(l.t.at(i)) - r.left())), cols - 1);
                    mn[c] = qMin(mn.at(c), l.v.at(i));
                    mx[c] = qMax(mx.at(c), l.v.at(i));
                    has[c] = true;
                }
                for (int c = 0; c < cols; ++c)
                    if (has.at(c)) p.drawLine(QPointF(r.left() + c + 0.5, yOf(mn.at(c))), QPointF(r.left() + c + 0.5, yOf(mx.at(c))));
            } else {
                QPainterPath path;
                bool open = false;
                for (int i = a; i < b; ++i) {
                    const QPointF pt(xOf(l.t.at(i)), yOf(l.v.at(i)));
                    if (!open || l.t.at(i) - l.t.at(i - 1) > l.gapMs) { path.moveTo(pt); open = true; }
                    else path.lineTo(pt);
                }
                p.drawPath(path);
                if (b - a < cols / 3) {                          // thưa: đánh dấu từng mẫu
                    p.setBrush(l.color);
                    for (int i = a; i < b; ++i) p.drawEllipse(QPointF(xOf(l.t.at(i)), yOf(l.v.at(i))), 2.0, 2.0);
                    p.setBrush(Qt::NoBrush);
                }
            }
        }

        // Vạch sự kiện (trip...).
        for (const PlotMarker &m : m_markers) {
            if (m.t < m_t0 || m.t > m_t1) continue;
            const qreal x = xOf(m.t);
            QPen mp(m.color);
            mp.setStyle(Qt::DashLine);
            mp.setWidthF(1.2);
            p.setPen(mp);
            p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
            if (!m.label.isEmpty()) {
                p.setPen(m.color);
                p.drawText(QPointF(x + 3, r.top() + 12), m.label);
            }
        }
        p.setClipping(false);

        // Con trỏ và giá trị tại con trỏ.
        if (m_cursor >= m_t0 && m_cursor <= m_t1) {
            const qreal x = xOf(m_cursor);
            p.setPen(QPen(text, 1, Qt::DotLine));
            p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
            QStringList rows{QDateTime::fromMSecsSinceEpoch(m_cursor).toString(QStringLiteral("dd/MM HH:mm:ss.zzz"))};
            QVector<QColor> colors{text};
            for (const PlotLine &l : m_lines) {
                if (l.t.isEmpty()) continue;
                const int i = int(std::upper_bound(l.t.begin(), l.t.end(), m_cursor) - l.t.begin()) - 1;
                if (i < 0 || m_cursor - l.t.at(i) > l.gapMs) continue;
                rows << QStringLiteral("%1: %2").arg(l.name, QString::number(l.v.at(i), 'g', 10));
                colors << l.color;
            }
            const int shown = qMin(rows.size(), 9);
            qreal w = 0;
            for (int i = 0; i < shown; ++i) w = qMax(w, QFontMetricsF(font()).horizontalAdvance(rows.at(i)));
            const qreal boxX = x + 10 + w + 10 > r.right() ? x - 10 - w - 10 : x + 10;
            QRectF box(boxX, r.top() + 4, w + 10, shown * 15 + 6);
            p.fillRect(box, QColor(palette().color(QPalette::Base).red(), palette().color(QPalette::Base).green(), palette().color(QPalette::Base).blue(), 225));
            p.setPen(muted);
            p.drawRect(box);
            for (int i = 0; i < shown; ++i) {
                p.setPen(colors.at(i));
                p.drawText(QPointF(box.left() + 5, box.top() + 15 + i * 15 - 3), rows.at(i));
            }
        }
    }
}

void PlotWidget::wheelEvent(QWheelEvent *e)
{
    const qint64 anchor = timeAt(e->position().x());
    const double factor = e->angleDelta().y() > 0 ? 0.8 : 1.25;
    qint64 span = qint64(double(m_t1 - m_t0) * factor);
    span = qMax<qint64>(10, span);
    const double frac = double(anchor - m_t0) / double(m_t1 - m_t0);
    m_t0 = anchor - qint64(double(span) * frac);
    m_t1 = m_t0 + span;
    update();
    emit rangeChanged(m_t0, m_t1);
    e->accept();
}

void PlotWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    m_dragging = true;
    m_dragX = e->position().x();
    m_dragT0 = m_t0;
    m_dragT1 = m_t1;
    setCursor(Qt::ClosedHandCursor);
}

void PlotWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging) {
        const qint64 dt = qint64(double(m_dragX - e->position().x()) / plotRect().width() * double(m_dragT1 - m_dragT0));
        m_t0 = m_dragT0 + dt;
        m_t1 = m_dragT1 + dt;
        update();
        emit rangeChanged(m_t0, m_t1);
        return;
    }
    emit cursorChanged(timeAt(e->position().x()));
}

void PlotWidget::mouseReleaseEvent(QMouseEvent *)
{
    m_dragging = false;
    unsetCursor();
}

void PlotWidget::mouseDoubleClickEvent(QMouseEvent *) { emit resetRequested(); }

void PlotWidget::leaveEvent(QEvent *) { emit cursorChanged(-1); }

} // namespace ui
