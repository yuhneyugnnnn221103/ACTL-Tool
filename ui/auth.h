#pragma once
#include <QObject>
#include <QTimer>

namespace ui {

// Khóa chế độ kỹ sư (cấu hình, nạp code). Đây là khóa chống thao tác nhầm của người vận hành,
// không phải bảo mật: hash mật khẩu nằm trong file ini.
class Auth : public QObject {
    Q_OBJECT
public:
    static constexpr const char *kDefaultPassword = "admin";

    Auth(const QString &passwordHash, int lockMinutes, QObject *parent = nullptr);

    bool isUnlocked() const { return m_unlocked; }
    bool unlock(QWidget *parent);          // hỏi mật khẩu
    void lock();
    void touch();                          // có thao tác: lùi thời điểm tự khóa
    bool changePassword(QWidget *parent);

signals:
    void lockedChanged(bool locked);

private:
    static QString hash(const QString &password);

    QString m_hash;
    bool m_unlocked = false;
    QTimer m_timer;
};

} // namespace ui
