#pragma once
#include <QColor>
#include <QString>
#include <QWidget>

namespace ui {

// Nhãn trạng thái dạng viên thuốc, tự vẽ. Màu do người gọi truyền vào (theme::statusColor) và nên kèm ký hiệu trong chữ.
class StatusPill : public QWidget {
    Q_OBJECT
public:
    explicit StatusPill(QWidget *parent = nullptr);

    void setPill(const QString &text, const QColor &background, const QColor &foreground = Qt::white);

protected:
    void paintEvent(QPaintEvent *) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

private:
    QString m_text;
    QColor m_bg{0xE4, 0xE2, 0xDA}, m_fg{Qt::white};
};

} // namespace ui
