#include "thresholds.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace model {

Limit Thresholds::get(int mb, int trb, int field) const
{
    for (int k : {key(mb, trb), kDefault}) {
        const auto dev = m_limits.constFind(k);
        if (dev == m_limits.cend()) continue;
        const auto it = dev->constFind(field);
        if (it != dev->cend() && it->isSet()) return *it;
    }
    return {};
}

bool Thresholds::load(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = f.errorString(); return false; }
    QJsonParseError pe;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll(), &pe).object();
    if (pe.error != QJsonParseError::NoError) { if (error) *error = pe.errorString(); return false; }

    QHash<QString, int> index;
    for (int i = 0; i < m_table->size(); ++i) index.insert(m_table->fields().at(i).name, i);

    m_limits.clear();
    for (auto dev = root.begin(); dev != root.end(); ++dev) {
        int k = kDefault;
        if (dev.key() != QLatin1String("default")) {
            const QStringList p = dev.key().split('/');
            if (p.size() != 2) continue;
            k = key(p[0].toInt(), p[1].toInt());
        }
        const QJsonObject fields = dev.value().toObject();
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            const QJsonArray a = it.value().toArray();
            if (index.contains(it.key()) && a.size() == 2)
                m_limits[k][index.value(it.key())] = {a[0].toDouble(), a[1].toDouble()};
        }
    }
    return true;
}

bool Thresholds::save(const QString &path) const
{
    QJsonObject root;
    for (auto dev = m_limits.cbegin(); dev != m_limits.cend(); ++dev) {
        QJsonObject fields;
        for (auto it = dev->cbegin(); it != dev->cend(); ++it)
            if (it->isSet()) fields.insert(m_table->fields().at(it.key()).name, QJsonArray{it->min, it->max});
        root.insert(dev.key() == kDefault ? QStringLiteral("default")
                                          : QStringLiteral("%1/%2").arg(dev.key() >> 8).arg(dev.key() & 0xFF),
                    fields);
    }
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(QJsonDocument(root).toJson()) > 0;
}

} // namespace model
