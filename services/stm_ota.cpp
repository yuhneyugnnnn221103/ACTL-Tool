#include "stm_ota.h"

namespace services {

using namespace proto;

bool StmOta::registerFrames(core::FrameRegistry &registry) { return registry.add(stmota::ackSpec()); }

StmOta::StmOta(core::Link *link, QObject *parent) : QObject(parent), m_link(link)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &StmOta::onTimeout);
    connect(link, &core::Link::frameReceived, this, &StmOta::onFrame);
}

void StmOta::queryInfo(const QList<int> &addrs)
{
    if (busy() || addrs.isEmpty()) return;
    m_queue = addrs;
    m_infoOnly = true;
    m_ok = m_fail = 0;
    nextDevice();
}

void StmOta::start(const QList<int> &addrs, const QByteArray &imageA, const QByteArray &imageB,
                   quint32 version, bool autoCommit)
{
    if (busy() || addrs.isEmpty()) return;
    m_queue = addrs;
    m_imageA = imageA;
    m_imageB = imageB;
    m_version = version;
    m_autoCommit = autoCommit;
    m_infoOnly = false;
    m_ok = m_fail = 0;
    nextDevice();
}

void StmOta::cancel()
{
    if (!busy()) return;
    m_queue.clear();
    done(false, QStringLiteral("đã hủy; firmware đang chạy không bị ảnh hưởng, slot đích có thể còn dữ liệu dở"));
}

void StmOta::nextDevice()
{
    if (m_queue.isEmpty()) {
        m_state = State::Idle;
        emit finished(m_ok, m_fail);
        return;
    }
    m_addr = m_queue.takeFirst();
    send(stmota::buildCtrl(quint8(m_addr), stmota::kInfo), State::Info, 1000);
}

void StmOta::send(const QByteArray &frame, State state, int timeoutMs)
{
    core::Request r;
    r.data = frame;
    m_link->send(r);
    m_state = state;
    m_timer.start(timeoutMs);
}

void StmOta::sendChunk()
{
    send(stmota::buildData(quint8(m_addr), quint16(m_seq), m_image.mid(m_seq * stmota::kChunk, stmota::kChunk)),
         State::Data, 800);
}

void StmOta::done(bool ok, const QString &message)
{
    m_timer.stop();
    ++(ok ? m_ok : m_fail);
    emit deviceFinished(m_addr, ok, message);
    nextDevice();
}

void StmOta::confirmCommit(bool commit)
{
    if (m_state != State::AwaitUser) return;
    if (commit) send(stmota::buildCtrl(quint8(m_addr), stmota::kCommit), State::Commit, 2000);
    else done(true, QStringLiteral("đã nạp và kiểm tra CRC32, CHƯA kích hoạt"));
}

void StmOta::onFrame(const core::Frame &frame)
{
    if (!busy() || frame.cmd != stmota::cmdBytes(stmota::kAck)) return;
    const stmota::Ack a = stmota::parseAck(frame.raw);
    if (a.addr != m_addr) return;
    const QString why = stmota::statusText(a.status);

    switch (m_state) {
    case State::Info: {
        if (a.ackCmd != stmota::kInfo) return;
        emit deviceInfo(m_addr, a.slot, a.info);
        if (m_infoOnly) { done(true, QStringLiteral("slot %1, version %2").arg(a.slot).arg(a.info)); return; }
        const char target = a.slot == 'A' ? 'B' : 'A';
        m_image = target == 'A' ? m_imageA : m_imageB;
        if (m_image.isEmpty()) { done(false, QStringLiteral("đang chạy slot %1 nhưng chưa chọn file cho slot %2").arg(a.slot).arg(target)); return; }
        m_chunks = (m_image.size() + stmota::kChunk - 1) / stmota::kChunk;
        m_seq = m_retries = 0;
        emit deviceProgress(m_addr, 0, QStringLiteral("Gửi BEGIN (ảnh slot %1)").arg(target));
        send(stmota::buildBegin(quint8(m_addr), quint32(m_image.size()), stmota::crc32(m_image), m_version),
             State::BeginAccept, 2000);
        break;
    }
    case State::BeginAccept:
    case State::BeginReady:
        if (a.ackCmd != stmota::kBegin) return;
        if (a.status == stmota::EraseFail) { done(false, QStringLiteral("xóa sector thất bại (mã HAL 0x%1)").arg(a.info, 8, 16, QLatin1Char('0'))); return; }
        if (a.status != stmota::Ok) { done(false, QStringLiteral("BEGIN bị từ chối: ") + why); return; }
        // Hai ACK cùng ack_cmd và status; chỉ ACK có info = 1 mới nghĩa là đã xóa xong.
        if (a.info != 1u) {
            m_state = State::BeginReady;
            m_timer.start(15000);
            emit deviceProgress(m_addr, 0, QStringLiteral("Thiết bị đang xóa sector"));
            return;
        }
        sendChunk();
        break;

    case State::Data:
        if (a.ackCmd != stmota::kData) return;
        if (a.status == stmota::Ok) {
            m_retries = 0;
            ++m_seq;
        } else if (a.status == stmota::BadSeq) {
            if (++m_retries > kMaxRetries) { done(false, QStringLiteral("thiết bị liên tục báo sai thứ tự ở chunk %1").arg(m_seq)); return; }
            m_seq = int(a.info) + 1; // firmware báo seq cuối đã nhận
        } else {
            done(false, QStringLiteral("DATA chunk %1: %2").arg(m_seq).arg(why));
            return;
        }
        emit deviceProgress(m_addr, m_seq * 100 / m_chunks, QStringLiteral("Chunk %1 / %2").arg(m_seq).arg(m_chunks));
        if (m_seq >= m_chunks) send(stmota::buildCtrl(quint8(m_addr), stmota::kEnd), State::End, 3000);
        else sendChunk();
        break;

    case State::End:
        if (a.ackCmd != stmota::kEnd) return;
        if (a.status != stmota::Ok) { done(false, QStringLiteral("END: %1 (CRC thiết bị tính = 0x%2)").arg(why).arg(a.info, 8, 16, QLatin1Char('0'))); return; }
        emit deviceProgress(m_addr, 100, QStringLiteral("CRC32 đúng"));
        if (m_autoCommit) { send(stmota::buildCtrl(quint8(m_addr), stmota::kCommit), State::Commit, 2000); return; }
        m_timer.stop();
        m_state = State::AwaitUser;
        emit commitRequested(m_addr);
        break;

    case State::Commit:
        if (a.ackCmd != stmota::kCommit) return;
        if (a.status == stmota::Ok) done(true, QStringLiteral("đã kích hoạt, thiết bị đang khởi động lại"));
        else done(false, QStringLiteral("COMMIT bị từ chối: ") + why);
        break;

    default:
        break;
    }
}

void StmOta::onTimeout()
{
    switch (m_state) {
    case State::Info:        done(false, QStringLiteral("không trả lời")); break;
    case State::BeginAccept: done(false, QStringLiteral("không trả lời BEGIN")); break;
    case State::BeginReady:  done(false, QStringLiteral("quá thời gian chờ xóa sector")); break;
    case State::Data:
        if (++m_retries > kMaxRetries) done(false, QStringLiteral("chunk %1 không được ACK sau %2 lần gửi lại").arg(m_seq).arg(kMaxRetries));
        else sendChunk();
        break;
    case State::End:         done(false, QStringLiteral("quá thời gian chờ xác nhận END")); break;
    case State::Commit:
        // Thiết bị thường reset trước khi kịp gửi ACK: coi là thành công có điều kiện, như tool cũ.
        done(true, QStringLiteral("không nhận được ACK COMMIT (thường do thiết bị đã reset); hãy đọc lại thông tin để xác nhận"));
        break;
    default: break;
    }
}

} // namespace services
