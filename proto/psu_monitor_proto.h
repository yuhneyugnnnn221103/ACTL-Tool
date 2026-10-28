#pragma once
// Bản tin giám sát PSU, CMD 2 byte 81 81, 310 byte (cùng độ dài với khung giám sát TRB; các byte thêm là dự phòng) (theo "Định nghĩa bản tin"), CRC phủ từ byte địa chỉ (ngay sau CMD, như TRB):
// [AB CD][81 81][Addr][CỤM 1..4: 4x48B][SUPPLY 24B][RTC 7B][Trip 10B][Dự phòng 24B][CRC 2B][E1 E2]
// Mỗi cụm: 6 giá trị ADC + 2 giá trị AMC (3 byte) rồi 12 giá trị XDP (2 byte).
// Giá trị là số thô (chưa có công thức quy đổi). Supply là thanh ghi CURRENT / VBUS của INA228 (3 byte, dữ liệu ở bit 23..4):
// dòng có dấu (bù 2, 24 bit), áp không dấu. Giá trị lưu trong app là thô đủ 24 bit (so ngưỡng cùng đơn vị với firmware);
// khi hiển thị thì dịch phải 4 bit (supplyShown).
// Vùng dự phòng 24 byte (byte 239..262) là mở rộng của firmware APSU; byte 263..306 là dự phòng: chỉ dùng 4 trạng thái an toàn, 4 cờ EN, 4 bộ đếm trip (theo cụm), slot và version; còn lại dự phòng.
#include "../core/frame.h"
#include "field_table.h"
#include <QSet>
#include <QString>

namespace proto::psumon {

constexpr int kLength = 310, kCrcStart = 4, kOffAddr = 4;
constexpr int kNumCluster = 4, kClusterFields = 20, kNumSupply = 8, kNumRtc = 7, kNumTrip = 10;
constexpr int kPollLength = 13;   // khung hỏi 02 02

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
// Mở rộng firmware trong vùng dự phòng (theo thứ tự byte từ 239).
constexpr int kIdxExt0 = kIdxTrip0 + kNumTrip;
// Theo từng cụm DCM (mỗi cụm một chân EN riêng), mỗi cụm 3 byte: safe_state, PWR_EN (1/0), trip count.
constexpr int kFwPerCluster = 3;
constexpr int kIdxSafeState(int c) { return kIdxExt0 + c * kFwPerCluster; }
constexpr int kIdxEn(int c) { return kIdxExt0 + c * kFwPerCluster + 1; }
constexpr int kIdxTripCount(int c) { return kIdxExt0 + c * kFwPerCluster + 2; }
constexpr int kIdxSlot = kIdxExt0 + kNumCluster * kFwPerCluster;   // byte +12: slot đang chạy: 'A' / 'B' (/ 'R' recovery)
constexpr int kIdxVersion = kIdxSlot + 1;                   // +13..+16 version ảnh đang chạy (u32 BE)
constexpr int kNumFields = kIdxVersion + 1;

enum SafeState { SafeBoot = 0, SafeOff = 1, SafeArming = 2, SafeOn = 3, SafeTripped = 4, SafeLockout = 5 };
QString safeStateText(int state);
// Trạng thái theo từng cụm DCM (c = 0..3) từ bản tin đã giải mã (values theo thứ tự table()):
bool clusterTripped(const QList<double> &values, int c);   // safe_state của cụm là TRIPPED hoặc LOCKOUT
bool clusterOff(const QList<double> &values, int c);       // chân EN tắt và cụm không TRIPPED/LOCKOUT: không so ngưỡng, hiện "OFF"
QSet<int> offClusterFields(const QList<double> &values);   // chỉ số các trường đo của mọi cụm đang OFF (bỏ qua khi so ngưỡng)
           // "BOOT", "OFF", ...
QString tripName(int i);                    // i = 0..9 (Trip 1..10); bảng bit TẠM THỜI của firmware
QString tripBitName(int i, int bit);        // rỗng nếu bit chưa dùng
double supplyShown(double raw);             // dịch phải 4 bit (bậc thang xuống), giữ dấu

QByteArray cmd();   // 81 81
QByteArray pollCmd();                       // 02 02
core::FrameSpec pollSpec(bool checkCrc);    // khung hỏi giám sát 13 byte (cho giả lập/test)
QByteArray buildPoll(int addr);
QByteArray buildFrame(int addr, const QList<double> &values);   // 81 81 đủ 310 byte (giả lập/test)
core::FrameSpec spec(bool checkCrc);
const FieldTable &table();

// Nhãn tiếng Việt cho giao diện (tên trong bảng trường là mã ASCII dùng cho CSV và file ngưỡng).
QString clusterFieldLabel(int f);  // f = ClusterField
QString supplyLabel(int i);        // i = 0..kNumSupply-1
QString rtcLabel(int i);

} // namespace proto::psumon
