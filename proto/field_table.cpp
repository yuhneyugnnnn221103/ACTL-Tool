#include "field_table.h"
#include <cmath>

namespace proto {

int FieldTable::add(const QString &name, int offset, int size, double scale, const QString &unit,
                    const QString &group)
{
    m_fields.append({name, offset, size, scale, unit, group});
    return m_fields.size() - 1;
}

int FieldTable::indexOf(const QString &group, const QString &name) const
{
    for (int i = 0; i < m_fields.size(); ++i)
        if (m_fields.at(i).name == name && m_fields.at(i).group == group) return i;
    return -1;
}

QList<double> FieldTable::decode(const QByteArray &frame) const
{
    QList<double> out;
    out.reserve(m_fields.size());
    for (const Field &f : m_fields) {
        quint32 raw = 0;
        for (int i = 0; i < f.size; ++i)
            raw = (raw << 8) | quint8(frame.at(f.offset + i));
        if (f.isSigned) {                                   // mở rộng dấu từ bit cao nhất của trường
            const int bits = 8 * f.size;
            const qint64 v = (bits < 32 && (raw >> (bits - 1) & 1)) ? qint64(raw) - (qint64(1) << bits) : (bits == 32 ? qint64(qint32(raw)) : qint64(raw));
            out.append(double(v) * f.scale);
        } else {
            out.append(raw * f.scale);
        }
    }
    return out;
}

void FieldTable::encode(const QList<double> &values, QByteArray &frame) const
{
    for (int k = 0; k < m_fields.size() && k < values.size(); ++k) {
        const Field &f = m_fields.at(k);
        quint32 raw = quint32(std::llround(values.at(k) / f.scale));
        for (int i = f.size - 1; i >= 0; --i, raw >>= 8)
            frame[f.offset + i] = char(raw & 0xFF);
    }
}

QJsonObject FieldTable::toJson(const QList<double> &values) const
{
    QJsonObject root;
    for (int i = 0; i < m_fields.size(); ++i) {
        const Field &f = m_fields.at(i);
        QJsonObject g = root.value(f.group).toObject();
        g.insert(f.name, values.at(i));
        root.insert(f.group, g);
    }
    return root;
}

void FieldTable::fromJson(const QJsonObject &json, QList<double> &values) const
{
    for (int i = 0; i < m_fields.size(); ++i) {
        const Field &f = m_fields.at(i);
        const QJsonValue v = json.value(f.group).toObject().value(f.name);
        const double span = std::ldexp(1.0, 8 * f.size);                  // 2^(8*size)
        const double lo = f.isSigned ? -span / 2 : 0, hi = f.isSigned ? span / 2 - 1 : span - 1;
        if (v.isDouble() && v.toDouble() >= lo && v.toDouble() <= hi) values[i] = v.toDouble();
    }
}

} // namespace proto
