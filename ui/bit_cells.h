#pragma once
#include <QWidget>

namespace ui {

// Một byte hiển thị thành 8 ô (bit7 ở trái ... bit0 ở phải), tự vẽ. Bit 1 tô màu Trip, bit 0 tô nhạt.
class BitCells : public QWidget {
    Q_OBJECT
public:
    explicit BitCells(const QString &caption, QWidget *parent = nullptr);

    void setValue(int byte, bool known = true);   // known = false: chưa có dữ liệu, mọi ô nhạt

    // Lưới nhiều byte: captionFmt dạng "T%1" (đánh số từ 1), xếp columns cột.
    static QWidget *makeGrid(int count, int columns, const QString &captionFmt, QList<BitCells *> &out,
                             QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *) override;
    bool event(QEvent *e) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

private:
    int bitAt(const QPoint &p) const;   // 7..0, hoặc -1 nếu ngoài các ô
    int cellsLeft() const;

    QString m_caption;
    int m_value = 0;
    bool m_known = false;
};

} // namespace ui
