#include "fpga_ota.h"

namespace services {

using namespace proto;
using fpgaota::kBroadcast;

bool FpgaOta::registerFrames(core::FrameRegistry &registry) { return registry.add(fpgaota::replySpec()); }

FpgaOta::FpgaOta(core::Link *link, QObject *parent) : QObject(parent), m_link(link)
{
    connect(link, &core::Link::requestFinished, this, &FpgaOta::onRequestFinished);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (m_phase == Phase::EraseWait) {
            if (--m_countdown > 0) {
                emit phaseChanged(QStringLiteral("Chờ xóa flash: còn %1 s").arg(m_countdown));
                return;
            }
            m_erased = true;
            finish(QStringLiteral("Đã xóa flash, có thể nạp code"));
        } else if (m_phase == Phase::BootWait) {
            m_timer.stop();
            m_phase = Phase::Verify;
            emit phaseChanged(QStringLiteral("Hỏi trạng thái sau boot"));
            m_node = -1;
            if (nextNode(false)) query(); else finish(QStringLiteral("Không có TRB nào để xác nhận"));
        }
    });
}

void FpgaOta::setNodes(const QList<Device> &devices)
{
    if (busy()) return;
    m_nodes.clear();
    for (const Device &d : devices) m_nodes.append({d, false, false, {}, {}});
}

int FpgaOta::countUsable() const
{
    int n = 0;
    for (const Node &x : m_nodes) n += usable(x);
    return n;
}

// Chuyển m_node tới thiết bị kế tiếp cần xử lý; false nếu hết.
bool FpgaOta::nextNode(bool onlyUsable)
{
    while (++m_node < m_nodes.size())
        if (onlyUsable ? usable(m_nodes.at(m_node)) : m_nodes.at(m_node).online) return true;
    return false;
}

void FpgaOta::query()
{
    core::Request r;
    r.data = fpgaota::buildQuery(m_nodes.at(m_node).dev.first, m_nodes.at(m_node).dev.second);
    r.replyCmd = fpgaota::replyCmd();
    r.timeoutMs = 300;
    r.retries = kMaxTries;
    m_waitId = m_link->send(r);
}

void FpgaOta::send(const QByteArray &frame, int gapMs, bool wait)
{
    core::Request r;
    r.data = frame;
    r.gapMs = gapMs;
    const quint64 id = m_link->send(r);
    if (wait) m_waitId = id;
}

void FpgaOta::failNode(const QString &note)
{
    m_nodes[m_node].failed = true;
    m_nodes[m_node].note = note;
    emit nodeChanged(m_node);
}

void FpgaOta::finish(const QString &summary)
{
    m_timer.stop();
    m_phase = Phase::Idle;
    m_cancel = false;
    emit phaseChanged(summary);
    emit finished(summary);
}

void FpgaOta::cancel()
{
    if (!busy()) return;
    m_cancel = true; // các pha chờ trả lời sẽ dừng ở câu trả lời kế tiếp
    if (m_phase == Phase::EraseWait || m_phase == Phase::BootWait) finish(QStringLiteral("Đã hủy"));
}

// ---- Bước 1 ----
void FpgaOta::checkOnline()
{
    if (busy() || m_nodes.isEmpty()) return;
    m_phase = Phase::Online;
    m_erased = false;
    emit phaseChanged(QStringLiteral("Kiểm tra kết nối"));
    for (Node &n : m_nodes) n = {n.dev, true, false, {}, {}}; // tạm coi là online để nextNode() duyệt hết
    m_node = -1;
    nextNode(false);
    query();
}

// ---- Bước 2: xóa flash ----
void FpgaOta::erase(const Options &options)
{
    if (busy()) return;
    if (countUsable() == 0) { emit finished(QStringLiteral("Không có TRB nào trực tuyến, hãy kiểm tra kết nối trước")); return; }
    m_opt = options;
    m_erased = false;

    if (m_opt.broadcast) {
        send(fpgaota::buildErase(kBroadcast, kBroadcast), 20, false);
    } else {
        for (const Node &n : m_nodes)
            if (usable(n)) send(fpgaota::buildErase(n.dev.first, n.dev.second), 20, false);
    }
    m_phase = Phase::EraseWait;
    m_countdown = m_opt.eraseWaitSec;
    emit phaseChanged(QStringLiteral("Chờ xóa flash: còn %1 s").arg(m_countdown));
    emit progress(0, 1);
    m_timer.start(1000);
}

void FpgaOta::skipEraseWait()
{
    if (m_phase == Phase::EraseWait) m_countdown = 1;
}

// ---- Bước 3: nạp các gói ----
void FpgaOta::load(const QByteArray &firmware, const Options &options)
{
    if (busy() || firmware.isEmpty()) return;
    if (countUsable() == 0) { emit finished(QStringLiteral("Không có TRB nào trực tuyến, hãy kiểm tra kết nối trước")); return; }
    m_firmware = firmware;
    m_opt = options;
    m_erased = false; // mỗi lần nạp cần một lần xóa mới
    m_totalPackets = (firmware.size() + fpgaota::kChunk - 1) / fpgaota::kChunk;
    m_packet = 0;
    loadPacket();
}

// Gửi liên tục từng gói, chỉ nghỉ packetGapMs giữa các gói; không hỏi trạng thái giữa chừng.
// Lỗi ghi flash chỉ được phát hiện ở lượt hỏi 0x88 sau gói cuối.
void FpgaOta::loadPacket()
{
    emit progress(m_packet, m_totalPackets);
    if (m_packet >= m_totalPackets) {
        m_phase = Phase::PostCheck;
        emit phaseChanged(QStringLiteral("Hỏi trạng thái sau khi nạp"));
        m_node = -1;
        if (nextNode(false)) query(); else finish(QStringLiteral("Không còn TRB nào"));
        return;
    }
    if (m_packet == 0) emit phaseChanged(QStringLiteral("Đang nạp %1 gói").arg(m_totalPackets));
    m_phase = Phase::Load;
    const QByteArray chunk = m_firmware.mid(m_packet * fpgaota::kChunk, fpgaota::kChunk);
    if (m_opt.broadcast) {
        send(fpgaota::buildLoad(kBroadcast, kBroadcast, quint16(m_packet), chunk), m_opt.packetGapMs, true);
        return;
    }
    int last = -1;                                   // gửi riêng: cùng một gói lần lượt tới từng TRB còn dùng được
    for (int i = 0; i < m_nodes.size(); ++i) if (usable(m_nodes.at(i))) last = i;
    for (int i = 0; i < m_nodes.size(); ++i) {
        const Node &n = m_nodes.at(i);
        if (!usable(n)) continue;
        send(fpgaota::buildLoad(n.dev.first, n.dev.second, quint16(m_packet), chunk), m_opt.packetGapMs, i == last);
    }
}

// ---- Bước 4: boot ----
void FpgaOta::bootAndVerify(const Options &options)
{
    if (busy() || m_nodes.isEmpty()) return;
    m_opt = options;
    if (m_opt.broadcast) {
        send(fpgaota::buildBoot(kBroadcast, kBroadcast), 0, false);
    } else {
        for (const Node &n : m_nodes)
            if (usable(n)) send(fpgaota::buildBoot(n.dev.first, n.dev.second), 20, false);
    }
    m_phase = Phase::BootWait;
    emit phaseChanged(QStringLiteral("Đã gửi lệnh boot, chờ FPGA nạp lại cấu hình"));
    m_timer.start(m_opt.bootWaitMs);
}

// Ghi version mới cho các TRB không lỗi rồi đọc lại từng TRB. false nếu không có gì để ghi.
bool FpgaOta::writeVersions()
{
    if (fpgaota::versionValue(m_opt.version) == 0 || countUsable() == 0) return false;
    m_phase = Phase::WriteVersion;
    emit phaseChanged(QStringLiteral("Ghi version %1").arg(fpgaota::versionText(m_opt.version)));
    for (const Node &n : m_nodes)
        if (usable(n)) send(fpgaota::buildWriteVersion(n.dev.first, n.dev.second, m_opt.version), 20, false);
    m_node = -1;
    nextNode(true);
    query();
    return true;
}

void FpgaOta::onRequestFinished(quint64 id, bool ok, const core::Frame &reply)
{
    if (!busy() || id != m_waitId) return;
    if (m_cancel) { finish(QStringLiteral("Đã hủy")); return; }

    fpgaota::Status st;
    if (ok && m_phase != Phase::Load) {
        st = fpgaota::parseStatus(reply.raw);
        m_nodes[m_node].status = st;
    }

    switch (m_phase) {
    case Phase::Online:
        m_nodes[m_node].online = ok;
        m_nodes[m_node].note = ok ? QStringLiteral("Trực tuyến") : QStringLiteral("Không trả lời");
        emit nodeChanged(m_node);
        emit progress(m_node + 1, m_nodes.size());
        if (++m_node < m_nodes.size()) { query(); break; }
        finish(QStringLiteral("%1 / %2 TRB trực tuyến").arg(countUsable()).arg(m_nodes.size()));
        break;

    case Phase::Load:
        ++m_packet;
        loadPacket();
        break;

    case Phase::PostCheck:
        if (!ok) failNode(QStringLiteral("Không trả lời sau khi nạp"));
        else if (st.code & (fpgaota::ErrFlashWrite | fpgaota::ErrFlashErase)) failNode(QStringLiteral("Sau nạp: ") + fpgaota::errorText(st.code));
        else if (!m_nodes.at(m_node).failed) { m_nodes[m_node].note = QStringLiteral("Đã nạp xong"); }
        emit nodeChanged(m_node);
        if (nextNode(false)) { query(); break; }
        if (writeVersions()) break;
        finish(QStringLiteral("Nạp xong: %1 TRB tốt, %2 TRB lỗi").arg(countUsable()).arg(m_nodes.size() - countUsable()));
        break;

    case Phase::WriteVersion: { // đọc lại version vừa ghi
        Node &n = m_nodes[m_node];
        if (!ok) n.note += QStringLiteral("; không đọc lại được version");
        else if (st.version != m_opt.version) n.note += QStringLiteral("; version đọc lại là \"%1\", khác \"%2\"")
                                                   .arg(fpgaota::versionText(st.version), fpgaota::versionText(m_opt.version));
        else n.note += QStringLiteral("; đã ghi version %1").arg(fpgaota::versionText(st.version));
        emit nodeChanged(m_node);
        if (nextNode(true)) { query(); break; }
        finish(QStringLiteral("Nạp xong: %1 TRB tốt, %2 TRB lỗi; đã ghi version %3")
                   .arg(countUsable()).arg(m_nodes.size() - countUsable()).arg(fpgaota::versionText(m_opt.version)));
        break;
    }

    case Phase::Verify:
        if (!ok) failNode(QStringLiteral("Không trả lời sau boot"));
        else if (st.code != 0) failNode(QStringLiteral("Sau boot: ") + fpgaota::errorText(st.code));
        else if (!m_nodes.at(m_node).failed) { m_nodes[m_node].note = QStringLiteral("Boot xong, không lỗi"); emit nodeChanged(m_node); }
        if (nextNode(false)) { query(); break; }
        finish(QStringLiteral("Boot xong: %1 TRB tốt, %2 TRB lỗi").arg(countUsable()).arg(m_nodes.size() - countUsable()));
        break;

    default:
        break;
    }
}

} // namespace services
