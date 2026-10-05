#pragma once
#include <QStringList>
#include <QWidget>

namespace ui {

// Một byte hiển thị thành 8 ô (bit7 ở trái ... bit0 ở phải), tự vẽ. Bit 1 tô màu Trip, bit 0 tô nhạt.
class BitCells : public QWidget {
    Q_OBJECT
public:
    explicit BitCells(const QString &caption, QWidget *parent = nullptr);

    void setValue(int byte, bool known = true);   // known = false: chưa có dữ liệu, mọi ô nhạt
    void setBitNames(const QStringList &names);   // names[bit] (8 phần tử): ý nghĩa từng bit, hiện ở tooltip
    void setCaptionWidth(int px);                 // để các hàng trong cùng cột thẳng hàng
    int captionTextWidth() const;

    // Lưới nhiều byte xếp columns cột, đánh số chạy xuống từng cột; chữ nhãn của mỗi cột được căn thẳng hàng.
    static QWidget *makeGrid(const QStringList &captions, int columns, QList<BitCells *> &out, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *) override;
    bool event(QEvent *e) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

private:
    int bitAt(const QPoint &p) const;   // 7..0, hoặc -1 nếu ngoài các ô
    int cellsLeft() const;

    QString m_caption;
    QStringList m_bitNames;
    int m_captionWidth = -1;
    int m_value = 0;
    bool m_known = false;
};

} // namespace ui
