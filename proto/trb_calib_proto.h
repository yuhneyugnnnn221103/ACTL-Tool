#pragma once
// Bản tin Calib PC -> Gateway -> TRB (CMD 18 18), 150 byte, CRC phủ từ byte địa chỉ MB như các khung TRB khác:
//   [AB CD][18 18][MB][TRB][RX_CHN_EN 4B][TX_CHN_EN 4B][32 kênh x 4B][Dự phòng 4B][CRC 2B][E1 E2]
//   Byte đếm từ 1: MB 5, TRB 6, RX_CHN_EN 7-10, TX_CHN_EN 11-14, kênh 0 từ byte 15, kênh k từ byte 15 + 4k, kênh 31 tới byte 142,
//   dự phòng 143-146, CRC 147-148, E1 E2 149-150.
//   RX_CHN_EN / TX_CHN_EN: bit c = kênh c bật (1). Byte đầu (7 hoặc 11) = kênh 24-31, byte cuối (10 hoặc 14) = kênh 0-7;
//   trong mỗi byte bit 0 = kênh thấp nhất của byte đó.
//   Mỗi kênh 4 byte: phase TX, phase RX, amp TX, amp RX (số nguyên không dấu 0..255).
// Gửi qua đường Ethernet tới Gateway, không có trả lời.
#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace proto::calib {

constexpr int kLength = 150, kCrcStart = 4, kOffMb = 4, kOffTrb = 5;
constexpr int kOffRxEn = 6, kOffTxEn = 10, kOffChannel0 = 14, kChannels = 32, kChannelBytes = 4;
constexpr int kBroadcast = 0xFF;      // MB / TRB = 0xFF: gửi cho tất cả (như các khung điều khiển TRB)

struct Data {
    quint32 rxEn = 0, txEn = 0;       // bit c = kênh c
    quint8 phaseTx[kChannels] = {}, phaseRx[kChannels] = {}, ampTx[kChannels] = {}, ampRx[kChannels] = {};
};

QByteArray cmd();                     // 18 18
QByteArray buildFrame(int mb, int trb, const Data &d);
// Đọc lại dữ liệu từ khung 150 byte (cho test, giả lập); false nếu độ dài hoặc CMD sai.
bool parseFrame(const QByteArray &frame, Data &d);

// File lưu sẵn để tái sử dụng (JSON): bật/tắt kênh theo danh sách kênh, giá trị từng kênh.
QJsonObject toJson(const Data &d);
// Rỗng nếu hợp lệ, ngược lại là câu báo lỗi; d chỉ được ghi khi hợp lệ.
QString fromJson(const QJsonObject &o, Data &d);

} // namespace proto::calib
