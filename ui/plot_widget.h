#pragma once
// Đồ thị đường theo thời gian (không dùng QtCharts): nhiều đường, vạch đánh dấu sự kiện, con trỏ dùng chung giữa các panel,
// lăn chuột để phóng to/thu nhỏ trục thời gian, kéo để dịch, nhấp đúp để xem toàn bộ. Dữ liệu dày được rút gọn theo cột điểm ảnh (min/max).
#include <QColor>
#include <QVector>
#include <QWidget>

namespace ui {

struct PlotLine {
    QString name;
    QColor color;
    QVector<qint64> t;       // ms từ epoch, tăng dần
    QVector<double> v;
    qint64 gapMs = 5000;     // cách quãng lớn hơn mức này thì ngắt đường (mất dữ liệu / giữa hai file)
};

struct PlotMarker {
    qint64 t = 0;
    QColor color;
    QString label;
};

class PlotWidget : public QWidget {
    Q_OBJECT
public:
    explicit PlotWidget(QWidget *parent = nullptr);

    void setTitle(const QString &title) { m_title = title; update(); }
    void setLines(const QVector<PlotLine> &lines);
    void setMarkers(const QVector<PlotMarker> &markers) { m_markers = markers; update(); }
    void setRange(qint64 t0, qint64 t1);
    qint64 rangeStart() const { return m_t0; }
    qint64 rangeEnd() const { return m_t1; }
    void setCursorTime(qint64 t) { m_cursor = t; update(); }

    // Chọn bước chia nhãn thời gian đẹp (ms) cho khoảng nhìn span và định dạng nhãn; dùng chung và để test.
    static qint64 niceTimeStep(qint64 spanMs, int maxTicks);
    static QString timeLabel(qint64 t, qint64 stepMs);

signals:
    void rangeChanged(qint64 t0, qint64 t1);     // người dùng phóng to / dịch
    void cursorChanged(qint64 t);                // -1 khi chuột rời đồ thị
    void resetRequested();                       // nhấp đúp

protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;
    QSize sizeHint() const override { return {600, 200}; }

private:
    QRectF plotRect() const;
    qint64 timeAt(qreal x) const;
    qreal xOf(qint64 t) const;
    bool yRange(double &lo, double &hi) const;

    QString m_title;
    QVector<PlotLine> m_lines;
    QVector<PlotMarker> m_markers;
    qint64 m_t0 = 0, m_t1 = 1000, m_cursor = -1;
    bool m_dragging = false;
    qreal m_dragX = 0;
    qint64 m_dragT0 = 0, m_dragT1 = 0;
};

} // namespace ui
