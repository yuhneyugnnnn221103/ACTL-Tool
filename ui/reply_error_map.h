#pragma once
#include "../model/psu_store.h"
#include <QWidget>

namespace ui {

// Bản đồ tỉ lệ lỗi bản tin giám sát: mỗi ô là một thiết bị, màu theo tỉ lệ khung rỗng (toàn 00, thiết bị không trả lời)
// trên tổng số lần trả lời trong cửa sổ trượt (model::ReplyStats::kWindow). Số % ở giữa ô. Lưới TRB (cột MB, hàng TRB) ở trên, dải PSU ở dưới.
class ReplyErrorMap : public QWidget {
    Q_OBJECT
public:
    ReplyErrorMap(const model::DeviceStore *trbs, const model::PsuStore *psus, QWidget *parent = nullptr);

    // Màu theo tỉ lệ lỗi 0..1: xanh (0%) -> vàng -> đỏ (từ kRedAt trở lên).
    static QColor rateColor(double rate);
    static constexpr double kRedAt = 0.5;
    // Tỉ lệ lỗi trung bình theo cửa sổ của mọi thiết bị đã có trả lời; -1 nếu chưa có thiết bị nào.
    double averageRate() const;

protected:
    void paintEvent(QPaintEvent *) override;
    bool event(QEvent *e) override;
    QSize minimumSizeHint() const override { return {560, 260}; }

private:
    struct Cell { QRectF rect; const model::ReplyStats *stats; QString name; };
    QList<Cell> layoutCells() const;
    static QString detail(const Cell &c);

    const model::DeviceStore *m_trbs;
    const model::PsuStore *m_psus;
};

} // namespace ui
