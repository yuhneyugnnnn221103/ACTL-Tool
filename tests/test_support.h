#pragma once
// Dùng chung cho các test: transport giả và kiểm tra CRC.
#include "../core/frame.h"
#include "../core/transport.h"
#include <QTimer>
#include <functional>

namespace testsupport {

class FakeTransport : public core::Transport {
public:
    std::function<void(const QByteArray &)> onWrite;
    QList<QByteArray> written;
    void open() override { setState(State::Connected); }
    void close() override { setState(State::Closed); }
    void write(const QByteArray &data) override { written << data; if (onWrite) onWrite(data); }
    QString describe() const override { return QStringLiteral("fake"); }
    void inject(const QByteArray &bytes) { emit bytesReceived(bytes); }
    // Trả lời không đồng bộ như thiết bị thật (trả lời ngay trong write() sẽ lẫn với việc Link đang gửi).
    void injectLater(const QByteArray &bytes) { QTimer::singleShot(0, this, [this, bytes] { inject(bytes); }); }
};

inline bool crcOk(const QByteArray &f, int crcStart)
{
    const quint16 crc = core::crc16(reinterpret_cast<const quint8 *>(f.constData()) + crcStart, f.size() - 4 - crcStart);
    return quint8(f.at(f.size() - 4)) == (crc >> 8) && quint8(f.at(f.size() - 3)) == (crc & 0xFF);
}

} // namespace testsupport
