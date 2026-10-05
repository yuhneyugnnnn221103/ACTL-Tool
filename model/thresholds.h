#pragma once
// Ngưỡng min/max theo từng TRB, khóa theo chỉ số trường giám sát.
// Nguồn chính là cấu hình đọc về từ thiết bị (bước 5 sẽ điền vào đây); "mặc định" dùng cho
// trường mà thiết bị chưa có ngưỡng riêng. Lưu ra file JSON để mở app là có ngưỡng ngay.
#include "../proto/field_table.h"
#include <QHash>

namespace model {

struct Limit {
    double min = 0, max = 0;
    bool isSet() const { return min != 0 || max != 0; } // 0/0 = chưa đặt ngưỡng
    bool violatedBy(double v) const { return isSet() && (v < min || v > max); }
};

class Thresholds {
public:
    explicit Thresholds(const proto::FieldTable *table) : m_table(table) {}

    void setDefault(int field, Limit l) { m_limits[kDefault][field] = l; }
    void set(int mb, int trb, int field, Limit l) { m_limits[key(mb, trb)][field] = l; }
    Limit get(int mb, int trb, int field) const;

    // JSON: { "default": { "TRB.V": [min, max], ... }, "3/5": { ... } }
    bool load(const QString &path, QString *error = nullptr);
    bool save(const QString &path) const;

private:
    static constexpr int kDefault = -1;
    static int key(int mb, int trb) { return mb << 8 | trb; }

    const proto::FieldTable *m_table;
    QHash<int, QHash<int, Limit>> m_limits;
};

} // namespace model
