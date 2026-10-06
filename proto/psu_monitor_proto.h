#pragma once
// Bản tin giám sát PSU, CMD 2 byte 81 81, 266 byte (theo "Định nghĩa bản tin"), CRC phủ từ byte CMD:
// [AB CD][81 81][Addr][CỤM 1..4: 4x48B][SUPPLY 24B][RTC 7B][Trip 10B][Dự phòng 24B][CRC 2B][E1 E2]
// Mỗi cụm: 6 giá trị ADC + 2 giá trị AMC (3 byte) rồi 12 giá trị XDP (2 byte).
// Mọi giá trị đang là số thô không dấu (chưa có công thức quy đổi và chưa rõ dấu).
#include "../core/frame.h"
#include "field_table.h"
#include <QString>

namespace proto::psumon {

constexpr int kLength = 266, kCrcStart = 2, kOffAddr = 4;
constexpr int kNumCluster = 4, kClusterFields = 20, kNumSupply = 8, kNumRtc = 7, kNumTrip = 10;

// Thứ tự trường trong một cụm (cũng là thứ tự trong bảng trường).
enum ClusterField {
    IOutAdc1, TempAdc1, IOutAdc2, TempAdc2, IOutAdc3, TempAdc3, IInAmc, VInAmc,
    VInXdp, VInXdpPeak, VInXdpValley, VOutXdp, VOutXdpPeak, VOutXdpValley,
    IOutXdp, IOutXdpPeak, IOutXdpValley, IOutXdpRms, TempFet, TempXdp
};

constexpr int clusterField(int cluster, ClusterField f) { return cluster * kClusterFields + f; }
constexpr int kIdxSupply0 = kNumCluster * kClusterFields;
constexpr int kIdxRtc0 = kIdxSupply0 + kNumSupply;
constexpr int kIdxTrip0 = kIdxRtc0 + kNumRtc;
constexpr int kNumFields = kIdxTrip0 + kNumTrip;

QByteArray cmd();   // 81 81
core::FrameSpec spec(bool checkCrc);
const FieldTable &table();

// Nhãn tiếng Việt cho giao diện (tên trong bảng trường là mã ASCII dùng cho CSV và file ngưỡng).
QString clusterFieldLabel(int f);  // f = ClusterField
QString supplyLabel(int i);        // i = 0..kNumSupply-1
QString rtcLabel(int i);

} // namespace proto::psumon
