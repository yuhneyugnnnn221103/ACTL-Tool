#pragma once
// Nạp code STM32 trên PSU. CMD 2 byte (mã lặp hai lần như TRB), địa chỉ 1 byte, CRC phủ từ byte CMD.
//   90 90 BEGIN  21 byte: [AB CD][90 90][ADDR][size 4B][crc32 4B][version 4B][CRC][E1 E2]
//   91 91 DATA  269 byte: [AB CD][91 91][ADDR][seq 2B][len 2B][data 256B][CRC][E1 E2]
//   92 92 END / 93 93 COMMIT / 95 95 INFO, 9 byte: [AB CD][CMD 2B][ADDR][CRC][E1 E2]
//   94 94 ACK    17 byte: [AB CD][94 94][ADDR][ack_cmd][status][info 4B][slot][dự phòng][CRC][E1 E2]
// Các hằng kBegin... là mã 1 byte (ack_cmd trong ACK vẫn là mã 1 byte); cmdBytes() ra CMD 2 byte trên đường truyền.
#include "../core/frame.h"

namespace proto::stmota {

constexpr int kChunk = 256, kOffAddr = 4;
constexpr quint8 kBegin = 0x90, kData = 0x91, kEnd = 0x92, kCommit = 0x93, kAck = 0x94, kInfo = 0x95;
enum AckStatus : quint8 { Ok, Refused, BadSize, BadSeq, CrcFail, Busy, NoCommitPending, EraseFail, WrongSlot };

struct Ack {
    quint8 addr = 0, ackCmd = 0, status = 0;
    quint32 info = 0;
    char slot = 0; // chỉ có nghĩa với trả lời INFO
};

QByteArray cmdBytes(quint8 code);   // {code, code}
core::FrameSpec ackSpec();
quint32 crc32(const QByteArray &data); // IEEE 802.3, khớp firmware

QByteArray buildBegin(quint8 addr, quint32 size, quint32 crc, quint32 version);
QByteArray buildData(quint8 addr, quint16 seq, const QByteArray &chunk);
QByteArray buildCtrl(quint8 addr, quint8 cmd); // END, COMMIT, INFO
Ack parseAck(const QByteArray &raw);
QString statusText(quint8 status);

} // namespace proto::stmota
