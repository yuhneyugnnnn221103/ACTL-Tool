#include "psu_config.h"
#include "../proto/psu_config_proto.h"

namespace services {

using namespace proto;

bool PsuConfig::registerFrames(core::FrameRegistry &registry, bool checkCrc)
{
    return registry.add(psucfg::readReplySpec(checkCrc));
}

PsuConfig::PsuConfig(core::Link *link, model::Thresholds *thresholds, const QString &thresholdsFile,
                     int readTimeoutMs, int writeGapMs, QObject *parent)
    : QObject(parent), m_link(link), m_thresholds(thresholds), m_thresholdsFile(thresholdsFile),
      m_readTimeoutMs(readTimeoutMs), m_writeGapMs(writeGapMs)
{
    connect(link, &core::Link::requestFinished, this, &PsuConfig::onRequestFinished);
}

const QList<double> *PsuConfig::cached(int addr) const
{
    const auto it = m_cache.constFind(addr);
    return it == m_cache.cend() ? nullptr : &it.value();
}

void PsuConfig::readDevices(const QList<int> &addrs)
{
    QList<Task> t;
    for (int a : addrs) t.append({a, false, {}});
    start(t);
}

void PsuConfig::writeFull(int addr, const QList<double> &values)
{
    start({Task{addr, true, values}});
}

void PsuConfig::applyChanges(const QList<int> &addrs, const QHash<int, double> &changes)
{
    if (busy()) return;
    m_changes = changes;
    QList<Task> t;
    for (int a : addrs) t.append({a, true, {}});
    start(t);
}

void PsuConfig::cancel()
{
    m_tasks.clear(); // PSU đang xử lý vẫn chạy cho xong để không bỏ dở giữa ghi và kiểm tra
}

void PsuConfig::start(const QList<Task> &tasks)
{
    if (busy() || tasks.isEmpty()) return;
    m_tasks = tasks;
    m_total = tasks.size();
    m_ok = m_fail = 0;
    next();
}

void PsuConfig::next()
{
    emit progress(m_ok + m_fail, m_total);
    if (m_tasks.isEmpty()) {
        m_stage = Stage::Idle;
        if (m_thresholds && m_ok > 0) m_thresholds->save(m_thresholdsFile);
        emit finished(m_ok, m_fail);
        return;
    }
    m_task = m_tasks.takeFirst();
    if (m_task.write && !m_task.values.isEmpty()) sendWrite();
    else sendRead(Stage::Read);
}

void PsuConfig::sendRead(Stage stage)
{
    m_stage = stage;
    core::Request r;
    r.data = psucfg::buildReadRequest(m_task.addr);
    r.replyCmd = QByteArray(1, char(psucfg::kReplyCmd));
    r.timeoutMs = m_readTimeoutMs;
    r.retries = 1;
    m_waitId = m_link->send(r);
}

void PsuConfig::sendWrite()
{
    m_stage = Stage::Write;
    core::Request r;
    r.data = psucfg::buildWrite(m_task.addr, m_task.values);
    r.gapMs = m_writeGapMs; // chờ PSU lưu xong trước khi đọc lại
    m_waitId = m_link->send(r);
}

void PsuConfig::onRequestFinished(quint64 id, bool ok, const core::Frame &reply)
{
    if (m_stage == Stage::Idle || id != m_waitId) return;
    const int addr = m_task.addr;

    if (m_stage == Stage::Write) {
        if (ok) sendRead(Stage::Verify);
        else done(false, QStringLiteral("không gửi được (chưa mở cổng RS485)"));
        return;
    }

    if (!ok) { done(false, QStringLiteral("không trả lời")); return; }
    if (reply.at(psucfg::kOffAddr) != addr) {
        done(false, QStringLiteral("trả lời sai địa chỉ (PSU %1)").arg(reply.at(psucfg::kOffAddr)));
        return;
    }

    const QList<double> got = psucfg::table().decode(reply.raw);
    m_cache.insert(addr, got);
    if (m_thresholds)
        for (const psucfg::ThresholdMap &m : psucfg::thresholdMap())
            m_thresholds->set(addr, 0, m.monitorField, {got.at(m.cfgMin), got.at(m.cfgMax)});
    emit configRead(addr);

    if (m_stage == Stage::Read) {
        if (!m_task.write) { done(true, QStringLiteral("đã đọc cấu hình")); return; }
        m_task.values = got;
        for (auto it = m_changes.cbegin(); it != m_changes.cend(); ++it) m_task.values[it.key()] = it.value();
        sendWrite();
        return;
    }

    // Verify
    QStringList diff;
    for (int i = 0; i < got.size(); ++i)
        if (got.at(i) != m_task.values.at(i)) {
            const proto::Field &f = psucfg::table().fields().at(i);
            diff << f.group + '.' + f.name;
        }
    if (diff.isEmpty()) done(true, QStringLiteral("đã ghi và đọc lại khớp"));
    else done(false, QStringLiteral("đọc lại KHÔNG khớp %1 trường: %2%3").arg(diff.size())
                         .arg(diff.mid(0, 5).join(", "), diff.size() > 5 ? QStringLiteral(", …") : QString()));
}

void PsuConfig::done(bool ok, const QString &message)
{
    ++(ok ? m_ok : m_fail);
    emit deviceFinished(m_task.addr, ok, message);
    next();
}

} // namespace services
