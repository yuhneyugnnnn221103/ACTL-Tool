#include "trb_config.h"
#include "../proto/gateway_header_proto.h"
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

void TrbConfig::writeFullMany(const QList<Device> &devices, const QList<double> &values)
{
    QList<Task> t;
    for (const Device &d : devices) t.append({d, true, values});
    start(t);
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
    r.data = gwhdr::add(gwhdr::Trb, trbcfg::buildReadRequest(m_task.dev.first, m_task.dev.second));   // thêm DEV + LEN cho Gateway
    r.replyCmd = trbcfg::readReplyCmd();
    r.timeoutMs = m_readTimeoutMs;
    r.retries = 1; // hết thời gian chờ (mặc định 2 s) thì hỏi lại 1 lần nữa (tổng 2 lần)
    m_waitId = m_link->send(r);
}

void TrbConfig::sendWrite()
{
    m_stage = Stage::Write;
    core::Request r;
    r.data = gwhdr::add(gwhdr::Trb, trbcfg::buildWrite(m_task.dev.first, m_task.dev.second, m_task.values));
    // TRB ghi flash xong thì phản hồi A4 (bản cấu hình đang lưu). m_writeGapMs (mặc định 3 s) là THỜI GIAN CHỜ phản hồi đó: nhận được sớm thì
    // thoát ngay; không nhận được (hết giờ) thì mới hỏi lại bằng A3.
    r.replyCmd = trbcfg::readReplyCmd();
    r.timeoutMs = m_writeGapMs;
    r.retries = 0;
    m_waitId = m_link->send(r);
}

void TrbConfig::onRequestFinished(quint64 id, bool ok, const core::Frame &reply)
{
    if (m_stage == Stage::Idle || id != m_waitId) return;
    const int mb = m_task.dev.first, trb = m_task.dev.second;

    if (m_stage == Stage::Write) {
        // Có phản hồi A4 đúng địa chỉ và khớp giá trị vừa ghi: xong, không cần A3. Phản hồi lệch / sai địa chỉ / hết giờ: hỏi lại bằng A3 để chốt.
        if (ok && reply.at(trbcfg::kOffMb) == mb && reply.at(trbcfg::kOffTrb) == trb) {
            const QList<double> got = trbcfg::table().decode(reply.raw);
            absorb(got);
            if (diffNames(got).isEmpty()) { done(true, QStringLiteral("đã ghi, TRB phản hồi A4 khớp")); return; }
        }
        sendRead(Stage::Verify);
        return;
    }

    if (!ok) { done(false, QStringLiteral("không trả lời")); return; }
    if (reply.at(trbcfg::kOffMb) != mb || reply.at(trbcfg::kOffTrb) != trb) {
        done(false, QStringLiteral("trả lời sai địa chỉ (MB%1 / TRB%2)")
                        .arg(reply.at(trbcfg::kOffMb) + 1).arg(reply.at(trbcfg::kOffTrb) + 1));
        return;
    }

    const QList<double> got = trbcfg::table().decode(reply.raw);
    absorb(got);

    if (m_stage == Stage::Read) {
        if (!m_task.write) { done(true, QStringLiteral("đã đọc cấu hình")); return; }
        m_task.values = got;
        for (auto it = m_changes.cbegin(); it != m_changes.cend(); ++it) m_task.values[it.key()] = it.value();
        sendWrite();
        return;
    }

    // Verify
    const QStringList diff = diffNames(got);
    if (diff.isEmpty()) done(true, QStringLiteral("đã ghi và đọc lại khớp"));
    else done(false, QStringLiteral("đọc lại KHÔNG khớp %1 trường: %2%3").arg(diff.size())
                         .arg(diff.mid(0, 5).join(", "), diff.size() > 5 ? QStringLiteral(", …") : QString()));
}

// Lưu bản cấu hình đọc được: cập nhật bộ nhớ đệm, ngưỡng cảnh báo và báo cho giao diện.
void TrbConfig::absorb(const QList<double> &got)
{
    const int mb = m_task.dev.first, trb = m_task.dev.second;
    m_cache.insert(key(mb, trb), got);
    if (m_thresholds)
        for (const trbcfg::ThresholdMap &m : trbcfg::thresholdMap())
            m_thresholds->set(mb, trb, m.monitorField, {got.at(m.cfgMin), got.at(m.cfgMax)});
    emit configRead(mb, trb);
}

// Tên các trường khác với giá trị vừa ghi (rỗng = khớp hoàn toàn).
QStringList TrbConfig::diffNames(const QList<double> &got) const
{
    QStringList diff;
    for (int i = 0; i < got.size() && i < m_task.values.size(); ++i)
        if (got.at(i) != m_task.values.at(i)) diff << trbcfg::table().fields().at(i).group + '.' + trbcfg::table().fields().at(i).name;
    return diff;
}

void TrbConfig::done(bool ok, const QString &message)
{
    ++(ok ? m_ok : m_fail);
    emit deviceFinished(m_task.dev.first, m_task.dev.second, ok, message);
    next();
}

} // namespace services
