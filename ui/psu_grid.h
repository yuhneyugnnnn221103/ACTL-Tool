#pragma once
#include "../model/psu_store.h"
#include <QWidget>
#include <functional>

namespace ui {

// Lưới tổng quan PSU: hàng = PSU, cột = 4 cụm DCM. Ô tên PSU tô theo trạng thái cả PSU (kể cả Trip),
// ô cụm tô theo trạng thái cụm.
class PsuGrid : public QWidget {
    Q_OBJECT
public:
    explicit PsuGrid(const model::PsuStore *store, QWidget *parent = nullptr);

    void setTooltipProvider(std::function<QString(int addr, int cluster)> f) { m_tooltip = std::move(f); }

signals:
    void psuClicked(int addr);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    bool event(QEvent *e) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return {560, sizeHint().height()}; }

private:
    QRectF nameRect(int row) const;
    QRectF cellRect(int row, int cluster) const;
    bool hitTest(const QPointF &p, int &addr, int &cluster) const; // cluster = -1: ô tên PSU

    const model::PsuStore *m_store;
    std::function<QString(int, int)> m_tooltip;
};

} // namespace ui
