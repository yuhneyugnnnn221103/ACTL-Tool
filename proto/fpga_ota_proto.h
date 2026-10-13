#pragma once
// Nạp code FPGA trên TRB qua RS485 (theo fpga_ota_frame.h). CRC phủ từ byte địa chỉ.
//   PC -> FPGA, 269 byte: [AB CD][CMD CMD][MB][TRB][ID 2B][Length][Data 256B][CRC][E1 E2]
//       CMD: 55 nạp gói, 66 xóa flash, 88 hỏi trạng thái, AA yêu cầu boot. Địa chỉ FF FF = broadcast.
//   FPGA -> PC, 40 byte:  [AB CD][99 99][Ack][CODE][STAT 4B][BOOTSTS 4B][WBSTAR 4B][VERSION 4B][Dự phòng 14B][CRC][E1 E2]
//       Khung trả lời KHÔNG mang địa chỉ: chỉ dùng được với câu hỏi gửi tới đúng một thiết bị.
//   PC -> FPGA, 269 byte: ghi version code mới: [AB CD][44 44][MB][TRB][VERSION 4B big-endian = số nguyên từ tên file][đệm 0xFF][CRC][E1 E2] (không có trả lời)
#include "../core/frame.h"

namespace proto::fpgaota {

constexpr int kChunk = 256, kBroadcast = 0xFF, kVersionSize = 4;

enum ErrorBit : quint8 {
    ErrUartFrame = 1 << 0,   // lỗi khung của bản tin gần nhất
    ErrFlashWrite = 1 << 1,
    ErrFlashErase = 1 << 2,
    ErrHwicapRead = 1 << 3,
    ErrIprogReboot = 1 << 4,
};

struct Status {
    quint8 ack = 0, code = 0;
    quint32 stat = 0, bootsts = 0, wbstar = 0;
    QByteArray version; // 4 byte thô
};

QByteArray replyCmd();
core::FrameSpec replySpec();

QByteArray buildErase(int mb, int trb);
QByteArray buildLoad(int mb, int trb, quint16 packetId, const QByteArray &chunk); // chunk 1..256 byte
QByteArray buildQuery(int mb, int trb);
QByteArray buildBoot(int mb, int trb);
QByteArray buildWriteVersion(int mb, int trb, const QByteArray &version); // version 4 byte
// Version = tên file .bin (bỏ đuôi) toàn chữ số, đổi thành số nguyên (vd 20261013; cùng ngày thêm số thứ tự cuối: 2026101302),
// ghi 4 byte big-endian. versionFromFileName trả 0 nếu tên không hợp lệ (có chữ, rỗng, quá 32 bit).
quint32 versionFromFileName(const QString &fileName);
QByteArray versionBytes(quint32 v);
quint32 versionValue(const QByteArray &version);
QString versionText(const QByteArray &version); // số thập phân; rỗng nếu 0 hoặc 0xFFFFFFFF
Status parseStatus(const QByteArray &raw);
QString errorText(quint8 code);

} // namespace proto::fpgaota
