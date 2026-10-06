#pragma once
// Cấu hình PSU qua RS485 (theo "Định nghĩa bản tin"), CRC phủ từ byte CMD:
//   Hỏi      CMD 03 03, 13 byte:  [AB CD][03 03][Addr][Dự phòng 4B][CRC][E1 E2]
//   Ghi      CMD 04 04, 915 byte: [AB CD][04 04][Addr][Mask 2B][CỤM 1..4: 4x192B][INA 72B][Ngưỡng Supply 48B][Dự phòng 16B][CRC][E1 E2]
//   Trả lời  CMD 82 82, 915 byte: giống khung ghi (Mask không dùng)
// Mỗi cụm 192 byte: ngưỡng ADC/AMC/XDP (60) + cấu hình XDP (38) + cấu hình ADS (94).
// Trường Config Mask không có trong bảng: khi ghi luôn gửi 0xFFFF (ghi mọi nhóm).
#include "../core/frame.h"
#include "field_table.h"
#include <QJsonObject>

namespace proto::psucfg {

constexpr int kLength = 915, kCrcStart = 2, kOffAddr = 4, kOffMask = 5, kOffFirstField = 7, kRequestLength = 13;
constexpr quint16 kConfigMaskAll = 0xFFFF;
constexpr int kNumCluster = 4;

QByteArray writeCmd();      // 04 04
QByteArray readCmd();       // 03 03
QByteArray replyCmd();      // 82 82
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
