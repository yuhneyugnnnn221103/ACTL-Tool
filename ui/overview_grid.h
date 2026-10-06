#pragma once
#include "../model/device_store.h"
#include <QWidget>
#include <functional>

namespace ui {

// Lưới tổng quan: cột = mother board, hàng = TRB. Tự co giãn theo kích thước widget.
class OverviewGrid : public QWidget {
    Q_OBJECT
public:
    explicit OverviewGrid(const model::DeviceStore *store, QWidget *parent = nullptr);

    static QColor statusColor(model::Status s);
    void setTooltipProvider(std::function<QString(int mb, int trb)> f) { m_tooltip = std::move(f); }
    void select(int mb, int trb);
    void setDebugTrb(int mb, int trb);   // TRB đang ở chế độ Debug (vẽ viền xanh); -1 = không có

signals:
    void trbClicked(int mb, int trb);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    bool event(QEvent *e) override;
    QSize minimumSizeHint() const override { return {560, 220}; }

private:
    QRectF cellRect(int mb, int trb) const;
    bool hitTest(const QPointF &p, int &mb, int &trb) const;

    const model::DeviceStore *m_store;
    std::function<QString(int, int)> m_tooltip;
    int m_selMb = -1, m_selTrb = -1, m_dbgMb = -1, m_dbgTrb = -1;
};

} // namespace ui
