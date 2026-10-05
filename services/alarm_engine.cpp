#include "alarm_engine.h"

namespace services {

AlarmEngine::AlarmEngine(const model::Thresholds *thresholds, const proto::FieldTable *table, QObject *parent)
    : QObject(parent), m_thresholds(thresholds), m_table(table) {}

QList<int> AlarmEngine::check(int mb, int trb, const QList<double> &values)
{
    QSet<int> &active = m_active[mb << 8 | trb];
    QList<int> now;
    for (int i = 0; i < values.size(); ++i) {
        const model::Limit l = m_thresholds->get(mb, trb, i);
        const bool bad = l.violatedBy(values.at(i));
        if (bad) now.append(i);
        if (bad == active.contains(i)) continue;

        const QString name = m_table->fields().at(i).name;
        auto num = [](double v) { return QString::number(v, 'g', 10); };   // số nguyên lớn không hiện dạng 1.2e+06
        if (bad) {
            active.insert(i);
            emit alarmEvent(mb, trb, QStringLiteral("%1 = %2 ngoài ngưỡng [%3 … %4]")
                                         .arg(name, num(values.at(i)), num(l.min), num(l.max)), true);
        } else {
            active.remove(i);
            emit alarmEvent(mb, trb, QStringLiteral("%1 = %2 trở lại trong ngưỡng").arg(name, num(values.at(i))), false);
        }
    }
    return now;
}

} // namespace services
