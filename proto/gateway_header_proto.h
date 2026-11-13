#pragma once
// Phần đầu thêm cho MỌI bản tin PC -> Gateway trên đường Ethernet (điều khiển TRB / PSU, beam, cấu hình, nạp code, Calib), để Gateway biết chuyển cho
// loại thiết bị nào và biết độ dài trọn khung (không còn khung điều khiển tổng 107 byte có mask: mỗi lệnh là một bản tin độc lập):
//   [AB CD][DEV][LEN 2B, big-endian][CMD ...][...][CRC][E1 E2]
//   DEV: 0x01 = TRB, 0x02 = PSU, 0x03 = TRSU (sau này).
//   LEN: tổng độ dài của cả khung SAU KHI chèn (tính cả AB CD, DEV, LEN, CMD ... E1 E2).
//   CRC vẫn tính từ byte địa chỉ như khung gốc (DEV và LEN không nằm trong CRC), nên không đổi khi Gateway bỏ 3 byte này đi để ghép lại khung gốc gửi xuống thiết bị.
// Riêng bản tin beam RDPU (42 byte = 3 khung 14 byte nối nhau, mỗi khung có AB CD riêng) KHÔNG chèn vào giữa mà đặt thêm 5 byte đứng TRƯỚC toàn bộ 42 byte:
//   [AB CD][TYPE = 0x05][LEN 2B, big-endian][42 byte beam nguyên vẹn]   LEN = 47 (tổng độ dài cả khung, cùng quy ước với các khung trên).
//   Gateway nhận thì bỏ 5 byte đầu, phần còn lại là khối 42 byte gốc.
// Không áp dụng cho: chế độ Debug COM (nối thẳng xuống thiết bị, không có Gateway: gửi khung gốc, xem setDirectMode) và chiều trả lời (Gateway -> PC, vẫn là khung gốc).
#include <QByteArray>

namespace proto::gwhdr {

enum Device : quint8 { Trb = 0x01, Psu = 0x02, Trsu = 0x03 };
constexpr int kOffDevice = 2;
constexpr int kExtra = 3;                  // DEV + LEN(2)
constexpr quint8 kTypeBeam = 0x05;         // TYPE của bản tin beam RDPU
constexpr int kBeamHeader = 5;             // AB CD + TYPE + LEN(2), đứng trước khối 42 byte

// DEV cho từng loại bản tin điều khiển: lệnh TRB (A2, 14 14) đi cho TRB (beam RDPU dùng TYPE 05 riêng, xem addBeam), lệnh PSU đi cho PSU.
constexpr Device kDevTrbControl = Trb, kDevPsuControl = Psu;

// Bản tin beam RDPU: đặt AB CD 05 LEN trước khối beam (khối beam giữ nguyên). Debug COM thì trả nguyên khối. insertBeam luôn đặt.
QByteArray addBeam(const QByteArray &block);
QByteArray insertBeam(const QByteArray &block);

// Chế độ Debug COM: bật thì add() trả nguyên khung gốc (không DEV + LEN). Cờ dùng chung toàn chương trình, đặt từ thread giao diện.
void setDirectMode(bool on);
bool directMode();

// Chèn DEV và LEN sau AB CD (trả nguyên khung gốc khi đang ở chế độ Debug COM).
QByteArray add(Device dev, const QByteArray &frame);
// Luôn chèn, không xét chế độ (dùng cho test và mô phỏng phía Gateway).
QByteArray insert(Device dev, const QByteArray &frame);
// Bỏ DEV và LEN (hoặc 5 byte đầu của bản tin beam), ghép lại khung gốc; rỗng nếu thiếu AB CD, DEV không hợp lệ hoặc LEN không bằng độ dài khung.
QByteArray strip(const QByteArray &frame);

// Phía nhận (Gateway giả lập): đọc luồng byte, khung có phần đầu DEV/LEN thì bỏ phần đó đi rồi trả luồng khung gốc;
// khung không có (khung gốc của Debug COM) cho đi qua nguyên vẹn.
class Unwrapper {
public:
    QByteArray feed(const QByteArray &data);
    void reset() { m_buf.clear(); }
private:
    QByteArray m_buf;
};

} // namespace proto::gwhdr
