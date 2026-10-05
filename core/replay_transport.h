#pragma once
#include "transport.h"
#include <QList>
#include <QTimer>

namespace core {

// Phát lại file hex thô (mỗi dòng một đoạn byte dạng "AB CD 11 ...") để test khi chưa có phần cứng.
class ReplayTransport : public Transport {
    Q_OBJECT
public:
    ReplayTransport(const QString &hexFile, int intervalMs, bool loop, QObject *parent = nullptr);
    void open() override;
    void close() override;
    void write(const QByteArray &) override {}
    QString describe() const override { return QStringLiteral("Replay %1").arg(m_file); }

private:
    QString m_file;
    bool m_loop;
    QList<QByteArray> m_chunks;
    int m_next = 0;
    QTimer m_timer;
};

} // namespace core
