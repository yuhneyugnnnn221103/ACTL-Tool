#pragma once
#include <QString>
#include <QWidget>

namespace ui {

// Đèn LED tự vẽ (QPainter, gradient hướng tâm) kèm nhãn bên dưới. Trạng thái luôn có ký hiệu trong đèn
// (✓ ✕ – ?) nên không chỉ dựa vào màu.
class LedIndicator : public QWidget {
    Q_OBJECT
public:
    enum class State { Unknown, On, Fault, Idle };  // Fault: tắt và đó là lỗi; Idle: tắt bình thường

    explicit LedIndicator(const QString &caption, QWidget *parent = nullptr);

    void setState(State s, const QString &toolTip = {});
    State state() const { return m_state; }

protected:
    void paintEvent(QPaintEvent *) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

private:
    QString m_caption;
    State m_state = State::Unknown;
};

} // namespace ui
