#pragma once
#include <QObject>

namespace core {

// Đường truyền byte thô. Thêm loại cổng mới = thêm một lớp con.
class Transport : public QObject {
    Q_OBJECT
public:
    enum class State { Closed, Waiting, Connected }; // Waiting: TCP server đang chờ Gateway kết nối
    Q_ENUM(State)

    using QObject::QObject;
    virtual void open() = 0;
    virtual void close() = 0;
    virtual void write(const QByteArray &data) = 0;
    virtual QString describe() const = 0;

    State state() const { return m_state; }

signals:
    void stateChanged(core::Transport::State state);
    void bytesReceived(const QByteArray &data);
    void errorOccurred(const QString &message);

protected:
    void setState(State s) { if (s != m_state) { m_state = s; emit stateChanged(s); } }

private:
    State m_state = State::Closed;
};

} // namespace core
