#pragma once
// Bảng mô tả trường của một bản tin: dùng chung cho giải mã, đóng gói (mô phỏng),
// màn chi tiết, tiêu đề CSV và so sánh ngưỡng. Thêm trường = thêm một dòng add().
#include <QByteArray>
#include <QList>
#include <QString>

namespace proto {

struct Field {
    QString name;
    int offset = 0;
    int size = 1;          // 1..4 byte, không dấu, big-endian
    double scale = 1.0;    // giá trị = raw * scale
    QString unit;
    QString group;         // nhóm hiển thị (dùng cho bản tin cấu hình)
};

class FieldTable {
public:
    int add(const QString &name, int offset, int size, double scale = 1.0, const QString &unit = {},
            const QString &group = {});
    int indexOf(const QString &group, const QString &name) const;
    const QList<Field> &fields() const { return m_fields; }
    int size() const { return m_fields.size(); }

    QList<double> decode(const QByteArray &frame) const;
    void encode(const QList<double> &values, QByteArray &frame) const;

private:
    QList<Field> m_fields;
};

} // namespace proto
