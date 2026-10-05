#include "auth.h"
#include "../app/settings.h"
#include <QCryptographicHash>
#include <QInputDialog>
#include <QMessageBox>

namespace ui {

Auth::Auth(const QString &passwordHash, int lockMinutes, QObject *parent)
    : QObject(parent), m_hash(passwordHash.isEmpty() ? hash(kDefaultPassword) : passwordHash)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(lockMinutes * 60 * 1000);
    connect(&m_timer, &QTimer::timeout, this, &Auth::lock);
}

QString Auth::hash(const QString &password)
{
    return QString::fromLatin1(QCryptographicHash::hash("actl:" + password.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool Auth::unlock(QWidget *parent)
{
    if (m_unlocked) return true;
    bool ok = false;
    const QString pw = QInputDialog::getText(parent, QStringLiteral("Chế độ kỹ sư"), QStringLiteral("Mật khẩu:"),
                                             QLineEdit::Password, {}, &ok);
    if (!ok) return false;
    if (hash(pw) != m_hash) {
        QMessageBox::warning(parent, QStringLiteral("Chế độ kỹ sư"), QStringLiteral("Sai mật khẩu."));
        return false;
    }
    m_unlocked = true;
    m_timer.start();
    emit lockedChanged(false);
    return true;
}

void Auth::lock()
{
    if (!m_unlocked) return;
    m_unlocked = false;
    m_timer.stop();
    emit lockedChanged(true);
}

void Auth::touch()
{
    if (m_unlocked) m_timer.start();
}

bool Auth::changePassword(QWidget *parent)
{
    if (!m_unlocked) return false;
    bool ok = false;
    const QString a = QInputDialog::getText(parent, QStringLiteral("Đổi mật khẩu"), QStringLiteral("Mật khẩu mới:"),
                                            QLineEdit::Password, {}, &ok);
    if (!ok || a.isEmpty()) return false;
    const QString b = QInputDialog::getText(parent, QStringLiteral("Đổi mật khẩu"), QStringLiteral("Nhập lại:"),
                                            QLineEdit::Password, {}, &ok);
    if (!ok || a != b) {
        if (ok) QMessageBox::warning(parent, QStringLiteral("Đổi mật khẩu"), QStringLiteral("Hai lần nhập không giống nhau."));
        return false;
    }
    m_hash = hash(a);
    Settings::save("auth/passwordHash", m_hash);
    return true;
}

} // namespace ui
