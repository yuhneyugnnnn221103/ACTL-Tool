#pragma once
// Nạp code STM32 trên PSU. CMD 2 byte (mã lặp hai lần như TRB), địa chỉ 1 byte, CRC phủ từ byte địa chỉ.
// KIẾN TRÚC: firmware đang chạy (app) KHÔNG nhận firmware; chỉ bootloader ở chế độ recovery ('R') mới nạp. Không còn slot B, không còn ROLLBACK
// (muốn về bản cũ thì nạp lại file .bin cũ).
//   90 90 BEGIN     21 byte: [AB CD][90 90][ADDR][size 4B][crc32 4B][version 4B][CRC][E1 E2]   (size <= 639 KB; chỉ sau ERASE)
//   91 91 DATA     269 byte: [AB CD][91 91][ADDR][seq 2B][len 2B][data 256B][CRC][E1 E2]         (gói cuối đệm FF, khung luôn 269 byte)
//   92 92 END / 93 93 COMMIT / 95 95 INFO / 96 96 ERASE / 98 98 ENTER_REC, 9 byte: [AB CD][CMD 2B][ADDR][CRC][E1 E2]
//   94 94 ACK       17 byte: [AB CD][94 94][ADDR][ack_cmd][status][info 4B][slot][trial][CRC][E1 E2]
//       slot: 'A' = app đang chạy, 'R' = bootloader recovery; trial chỉ có ở trả lời INFO của app (bit7 đã confirm, bit6..0 boot_count)
// Trình tự: INFO (app trả slot 'A' + version) -> ENTER_REC (ACK rồi reset sau ~50 ms) -> hỏi INFO lặp lại tới khi slot = 'R'
//   -> ERASE (xóa vùng app 5 sector; một ACK khi xóa xong, info = 1; PC chờ >= 30 s, chưa đo) -> BEGIN (ACK ngay; chưa ERASE thì NOT_ERASED)
//   -> DATA... -> END -> COMMIT (ACK rồi reset, bootloader chạy app mới). Bootloader chưa có app hợp lệ (board mới / app hỏng) đã ở recovery sẵn: bỏ qua ENTER_REC.
//   App treo: cấp nguồn lại và gửi INFO trong 300 ms đầu, bootloader ở lại recovery. Stop-and-wait: chờ ACK rồi mới gửi khung kế.
// Các hằng kBegin... là mã 1 byte (ack_cmd trong ACK vẫn là mã 1 byte); cmdBytes() ra CMD 2 byte trên đường truyền.
#include "../core/frame.h"

namespace proto::stmota {

constexpr int kChunk = 256, kOffAddr = 4;
constexpr int kMaxImageBytes = 639 * 1024;   // BEGIN: size <= 639 KB (vùng app)
constexpr quint8 kBegin = 0x90, kData = 0x91, kEnd = 0x92, kCommit = 0x93, kAck = 0x94, kInfo = 0x95, kErase = 0x96, kEnterRec = 0x98;
enum AckStatus : quint8 { Ok, Refused, BadSize, BadSeq, CrcFail, Busy, NoCommitPending, EraseFail, WrongSlot, ProgFail, Reserved10, NotErased, NotRecovery };

struct Ack {
    quint8 addr = 0, ackCmd = 0, status = 0;
    quint32 info = 0;
    char slot = 0;       // 'A' (app) / 'R' (recovery); chỉ có nghĩa với trả lời INFO
    quint8 trial = 0;    // chỉ có ở trả lời INFO: bit7 đã confirm, bit6..0 boot_count
};

QByteArray cmdBytes(quint8 code);   // {code, code}
core::FrameSpec ackSpec();
quint32 crc32(const QByteArray &data); // IEEE 802.3, khớp firmware

QByteArray buildBegin(quint8 addr, quint32 size, quint32 crc, quint32 version);
QByteArray buildData(quint8 addr, quint16 seq, const QByteArray &chunk);
QByteArray buildCtrl(quint8 addr, quint8 cmd); // END, COMMIT, INFO, ERASE, ENTER_REC
Ack parseAck(const QByteArray &raw);
QString statusText(quint8 status);

} // namespace proto::stmota
