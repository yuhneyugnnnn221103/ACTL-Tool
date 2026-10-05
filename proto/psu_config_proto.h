#pragma once
// Cấu hình PSU qua RS485 (theo "Định nghĩa bản tin"), CRC phủ từ byte CMD:
//   Hỏi      CMD 0x03, 12 byte:  [AB CD][03][Addr][Dự phòng 4B][CRC][E1 E2]
//   Ghi      CMD 0x04, 914 byte: [AB CD][04][Addr][Mask 2B][CỤM 1..4: 4x192B][INA 72B][Ngưỡng Supply 48B][Dự phòng 16B][CRC][E1 E2]
//   Trả lời  CMD 0x82, 914 byte: giống khung ghi (Mask không dùng)
// Mỗi cụm 192 byte: ngưỡng ADC/AMC/XDP (60) + cấu hình XDP (38) + cấu hình ADS (94).
// Trường Config Mask không có trong bảng: khi ghi luôn gửi 0xFFFF (ghi mọi nhóm).
#include "../core/frame.h"
#include "field_table.h"
#include <QJsonObject>

namespace proto::psucfg {

constexpr quint8 kWriteCmd = 0x04, kReadCmd = 0x03, kReplyCmd = 0x82;
constexpr int kLength = 914, kCrcStart = 2, kOffAddr = 3, kRequestLength = 12;
constexpr quint16 kConfigMaskAll = 0xFFFF;
constexpr int kNumCluster = 4;

core::FrameSpec readReplySpec(bool checkCrc);
const FieldTable &table();          // mọi trường sửa được (không gồm địa chỉ, mask, dự phòng)

QByteArray buildWrite(int addr, const QList<double> &values);
QByteArray buildReadRequest(int addr);

// Giá trị mặc định do hãng gợi ý (OPERATION, ENABLE_FAULTS, MASK_FAULTS, RETRY); trường khác = 0.
// Nếu có file JSON (cùng định dạng toJson) thì giá trị trong file thay cho mặc định cài sẵn.
QList<double> defaultValues(const QString &jsonPath = {});

// Ngưỡng trong cấu hình -> trường giám sát tương ứng (chỉ số trong psumon::table()).
struct ThresholdMap { int monitorField, cfgMax, cfgMin; };
const QList<ThresholdMap> &thresholdMap();

} // namespace proto::psucfg
