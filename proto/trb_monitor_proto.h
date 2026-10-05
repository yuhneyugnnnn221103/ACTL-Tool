#pragma once
// Bản tin giám sát TRB, 280 byte (theo cfg_protocol.h):
// [AB CD][11 11][MB][TRB][TRM1..4: 4x50B][TRB 6B][Trip 16B][State 4B]
// [INIT_ADAR][PA][PG][Nhiệt MCU 2B][Độ ẩm power 2B][Dự phòng 37B][CRC 2B][E1 E2]
#include "../core/frame.h"
#include "field_table.h"

namespace proto::trbmon {

constexpr int kLength = 280, kCrcStart = 4, kOffMb = 4, kOffTrb = 5;
constexpr int kNumTrm = 4, kTrmFields = 25, kNumTrip = 16;

// Chỉ số trong bảng trường (thứ tự add() trong table()).
constexpr int kIdxTrm0 = 0;                                   // TRM t bắt đầu tại t * kTrmFields
constexpr int kIdxTrbV = kNumTrm * kTrmFields, kIdxTrbI = kIdxTrbV + 1, kIdxTrbTemp = kIdxTrbV + 2;
constexpr int kIdxTrip0 = kIdxTrbV + 3;
constexpr int kIdxState0 = kIdxTrip0 + kNumTrip;
constexpr int kIdxInitAdar = kIdxState0 + kNumTrm, kIdxPa = kIdxInitAdar + 1, kIdxPg = kIdxInitAdar + 2;
constexpr int kIdxMcuTemp = kIdxInitAdar + 3, kIdxHumidity = kIdxInitAdar + 4;
constexpr int kNumFields = kIdxHumidity + 1;

// Ý nghĩa 16 byte trip code (i = 0..15 ứng với Trip 1..16):
//   Trip 1-4   vượt max I_SEN1..8 của TRM1..4 (bit 0 = I_SEN1 ... bit 7 = I_SEN8)
//   Trip 5, 6  vượt max dòng PA: Trip 5 = TRM1 và TRM2, Trip 6 = TRM3 và TRM4 (bit 0-3 = PA1-4 của TRM lẻ, bit 4-7 = PA1-4 của TRM chẵn)
//   Trip 7     system: bit 0 TRM temp max, 1 TRM temp min, 2 V TRB max, 3 V TRB min, 4 I TRB max, 5 I TRB min (6, 7 chưa dùng)
//   Trip 10-15 như Trip 1-6 nhưng dưới min
//   Trip 8, 9, 16 dự phòng
QString tripName(int i);                 // tên ngắn, ví dụ "Max I_SEN TRM1"
QString tripBitName(int i, int bit);     // ý nghĩa một bit, ví dụ "TRM2 PA3"; rỗng nếu dự phòng

QByteArray cmd();
core::FrameSpec spec(bool checkCrc);
const FieldTable &table();

} // namespace proto::trbmon
