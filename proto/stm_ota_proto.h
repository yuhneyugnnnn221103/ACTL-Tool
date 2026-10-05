#pragma once
// Nạp code STM32 trên PSU (theo stm_ota_protocol.h). CMD 1 byte, địa chỉ 1 byte, CRC phủ từ byte CMD.
//   0x90 BEGIN  20 byte: [AB CD][90][ADDR][size 4B][crc32 4B][version 4B][CRC][E1 E2]
//   0x91 DATA  268 byte: [AB CD][91][ADDR][seq 2B][len 2B][data 256B][CRC][E1 E2]
//   0x92 END / 0x93 COMMIT / 0x95 INFO, 8 byte: [AB CD][CMD][ADDR][CRC][E1 E2]
//   0x94 ACK    16 byte: [AB CD][94][ADDR][ack_cmd][status][info 4B][slot][dự phòng][CRC][E1 E2]
#include "../core/frame.h"

namespace proto::stmota {

constexpr int kChunk = 256;
constexpr quint8 kBegin = 0x90, kData = 0x91, kEnd = 0x92, kCommit = 0x93, kAck = 0x94, kInfo = 0x95;
enum AckStatus : quint8 { Ok, Refused, BadSize, BadSeq, CrcFail, Busy, NoCommitPending, EraseFail, WrongSlot };

struct Ack {
    quint8 addr = 0, ackCmd = 0, status = 0;
    quint32 info = 0;
    char slot = 0; // chỉ có nghĩa với trả lời INFO
};

core::FrameSpec ackSpec();
quint32 crc32(const QByteArray &data); // IEEE 802.3, khớp firmware

QByteArray buildBegin(quint8 addr, quint32 size, quint32 crc, quint32 version);
QByteArray buildData(quint8 addr, quint16 seq, const QByteArray &chunk);
QByteArray buildCtrl(quint8 addr, quint8 cmd); // END, COMMIT, INFO
Ack parseAck(const QByteArray &raw);
QString statusText(quint8 status);

} // namespace proto::stmota
