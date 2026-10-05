#include "trb_config.h"
#include "../proto/trb_config_proto.h"

namespace services {

using namespace proto;

static int key(int mb, int trb) { return mb << 8 | trb; }

bool TrbConfig::registerFrames(core::FrameRegistry &registry, bool checkCrc)
{
    return registry.add(trbcfg::readReplySpec(checkCrc));
}

TrbConfig::TrbConfig(core::Link *link, model::Thresholds *thresholds, const QString &thresholdsFile,
                     int readTimeoutMs, int writeGapMs, QObject *parent)
    : QObject(parent), m_link(link), m_thresholds(thresholds), m_thresholdsFile(thresholdsFile),
      m_readTimeoutMs(readTimeoutMs), m_writeGapMs(writeGapMs)
{
    connect(link, &core::Link::requestFinished, this, &TrbConfig::onRequestFinished);
}

const QList<double> *TrbConfig::cached(int mb, int trb) const
{
    const auto it = m_cache.constFind(key(mb, trb));
    return it == m_cache.cend() ? nullptr : &it.value();
}

void TrbConfig::readDevices(const QList<Device> &devices)
{
    QList<Task> t;
    for (const Device &d : devices) t.append({d, false, {}});
    start(t);
}

void TrbConfig::writeFull(Device device, const QList<double> &values)
{
    start({Task{device, true, values}});
}

void TrbConfig::applyChanges(const QList<Device> &devices, const QHash<int, double> &changes)
{
    m_changes = changes;
    QList<Task> t;
    for (const Device &d : devices) t.append({d, true, {}});
    start(t);
}

void TrbConfig::cancel()
{
    m_tasks.clear(); // thiết bị đang xử lý vẫn chạy cho xong để không bỏ dở giữa ghi và kiểm tra
}

void TrbConfig::start(const QList<Task> &tasks)
{
    if (busy() || tasks.isEmpty()) return;
    m_tasks = tasks;
    m_total = tasks.size();
    m_ok = m_fail = 0;
    next();
}

void TrbConfig::next()
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

void TrbConfig::sendRead(Stage stage)
{
    m_stage = stage;
    core::Request r;
    r.data = trbcfg::buildReadRequest(m_task.dev.first, m_task.dev.second);
    r.replyCmd = trbcfg::readReplyCmd();
    r.timeoutMs = m_readTimeoutMs;
    r.retries = 1;
    m_waitId = m_link->send(r);
}

void TrbConfig::sendWrite()
{
    m_stage = Stage::Write;
    core::Request r;
    r.data = trbcfg::buildWrite(m_task.dev.first, m_task.dev.second, m_task.values);
    r.gapMs = m_writeGapMs; // chờ thiết bị lưu xong trước khi đọc lại
    m_waitId = m_link->send(r);
}

void TrbConfig::onRequestFinished(quint64 id, bool ok, const core::Frame &reply)
{
    if (m_stage == Stage::Idle || id != m_waitId) return;
    const int mb = m_task.dev.first, trb = m_task.dev.second;

    if (m_stage == Stage::Write) {
        if (ok) sendRead(Stage::Verify);
        else done(false, QStringLiteral("không gửi được (chưa mở cổng RS485)"));
        return;
    }

    if (!ok) { done(false, QStringLiteral("không trả lời")); return; }
    if (reply.at(trbcfg::kOffMb) != mb || reply.at(trbcfg::kOffTrb) != trb) {
        done(false, QStringLiteral("trả lời sai địa chỉ (MB%1 / TRB%2)")
                        .arg(reply.at(trbcfg::kOffMb)).arg(reply.at(trbcfg::kOffTrb)));
        return;
    }

    const QList<double> got = trbcfg::table().decode(reply.raw);
    m_cache.insert(key(mb, trb), got);
    if (m_thresholds)
        for (const trbcfg::ThresholdMap &m : trbcfg::thresholdMap())
            m_thresholds->set(mb, trb, m.monitorField, {got.at(m.cfgMin), got.at(m.cfgMax)});
    emit configRead(mb, trb);

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
        if (got.at(i) != m_task.values.at(i)) diff << trbcfg::table().fields().at(i).group + '.' + trbcfg::table().fields().at(i).name;
    if (diff.isEmpty()) done(true, QStringLiteral("đã ghi và đọc lại khớp"));
    else done(false, QStringLiteral("đọc lại KHÔNG khớp %1 trường: %2%3").arg(diff.size())
                         .arg(diff.mid(0, 5).join(", "), diff.size() > 5 ? QStringLiteral(", …") : QString()));
}

void TrbConfig::done(bool ok, const QString &message)
{
    ++(ok ? m_ok : m_fail);
    emit deviceFinished(m_task.dev.first, m_task.dev.second, ok, message);
    next();
}

} // namespace services
