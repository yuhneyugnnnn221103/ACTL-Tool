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

QByteArray cmd();
core::FrameSpec spec(bool checkCrc);
const FieldTable &table();

} // namespace proto::trbmon
