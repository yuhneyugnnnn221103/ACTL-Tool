#pragma once
// Khung bản tin dùng chung: [AB CD][CMD 1|2B][...][CRC16 2B][E1 E2], độ dài cố định theo CMD.
#include <QByteArray>
#include <QHash>
#include <QMetaType>
#include <QString>

namespace core {

constexpr quint8 kHeader1 = 0xAB, kHeader2 = 0xCD;
constexpr quint8 kTailer1 = 0xE1, kTailer2 = 0xE2;
constexpr int kMinFrameLen = 8; // header2 + cmd1 + addr1 + crc2 + tailer2

struct FrameSpec {
    QByteArray cmd;        // 1 hoặc 2 byte ngay sau header
    int length = 0;        // tổng độ dài khung
    int crcStart = 4;      // CRC phủ [crcStart .. length-5]; TRB/FPGA OTA = 4, STM/PSU = 2
    bool checkCrc = true;
    QString name;
};

struct Frame {
    QByteArray cmd;
    QByteArray raw;        // nguyên khung, kể cả header/tailer
    qint64 timestampMs = 0;

    quint8 at(int i) const { return static_cast<quint8>(raw.at(i)); }
};

// CRC16-CCITT-FALSE: poly 0x1021, init 0xFFFF — giống firmware và các tool cũ.
quint16 crc16(const quint8 *data, int len);

// Điền header, CRC (big-endian tại length-4) và tailer cho khung đã có sẵn CMD + payload.
void seal(QByteArray &frame, int crcStart);

class FrameRegistry {
public:
    // false nếu trùng CMD, hoặc CMD 1 byte trùng byte đầu của một CMD 2 byte (không phân biệt được).
    bool add(const FrameSpec &spec);
    // p trỏ tới byte ngay sau header, cần ít nhất 2 byte hợp lệ.
    const FrameSpec *match(const quint8 *p) const;

private:
    QHash<QByteArray, FrameSpec> m_specs;
};

} // namespace core

Q_DECLARE_METATYPE(core::Frame)
