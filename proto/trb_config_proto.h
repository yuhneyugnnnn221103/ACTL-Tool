#pragma once
// Cấu hình TRB qua RS485 (theo gsdk_protocol.h), CRC phủ từ byte địa chỉ:
//   Ghi      A1 A1, 520 byte: [AB CD][A1 A1][MB][TRB][CAL 9][ADAR 8x25][LUT 28][TRM 4x48][GENERAL 38][DEBUG 6][Dự phòng 37][CRC][E1 E2]
//   Hỏi      A3 A3, 10 byte:  [AB CD][A3 A3][MB][TRB][CRC][E1 E2]
//   Trả lời  A4 A4, 520 byte: giống khung ghi, chỉ khác CMD
#include "../core/frame.h"
#include "field_table.h"
#include <QJsonObject>

namespace proto::trbcfg {

constexpr int kLength = 520, kOffMb = 4, kOffTrb = 5;

QByteArray readReplyCmd();
core::FrameSpec readReplySpec(bool checkCrc);
const FieldTable &table();                    // mọi trường sửa được (không gồm địa chỉ)

QByteArray buildWrite(int mb, int trb, const QList<double> &values);
QByteArray buildReadRequest(int mb, int trb);
QByteArray buildReadReply(int mb, int trb, const QList<double> &values); // A4 A4, dùng cho giả lập và test

// File JSON cùng định dạng tool cấu hình cũ: { "GENERAL": { "TEMP_MCU_MAX": 123, ... }, ... }
QJsonObject toJson(const QList<double> &values);
void fromJson(const QJsonObject &json, QList<double> &values); // trường thiếu trong file thì giữ nguyên

// Ngưỡng trong cấu hình -> trường giám sát tương ứng (chỉ số trong trbmon::table()).
struct ThresholdMap { int monitorField, cfgMax, cfgMin; };
const QList<ThresholdMap> &thresholdMap();

} // namespace proto::trbcfg
