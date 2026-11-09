#include "stm_ota.h"
#include "../proto/gateway_header_proto.h"

namespace services {

using namespace proto;

bool StmOta::registerFrames(core::FrameRegistry &registry) { return registry.add(stmota::ackSpec()) && registry.add(stmota::versionReplySpec()); }

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

void StmOta::start(const QList<int> &addrs, const QByteArray &image, const QString &version, bool autoCommit)
{
    if (busy() || addrs.isEmpty()) return;
    m_queue = addrs;
    m_image = image;
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
    done(false, QStringLiteral("đã hủy; nếu đang nạp dở thì PSU nằm ở recovery với vùng app chưa hoàn chỉnh, nạp lại để khởi động app"));
}

void StmOta::nextDevice()
{
    if (m_queue.isEmpty()) {
        m_state = State::Idle;
        emit finished(m_ok, m_fail);
        return;
    }
    m_addr = m_queue.takeFirst();
    m_polls = 0;
    m_state = State::Info;
    pollInfo();
}

void StmOta::send(const QByteArray &frame, State state, int timeoutMs)
{
    core::Request r;
    r.data = gwhdr::add(gwhdr::Psu, frame);   // mọi khung nạp STM32 đều gửi cho PSU
    m_link->send(r);
    m_state = state;
    m_timer.start(timeoutMs);
}

// INFO của lúc đầu (State::Info) hoặc của lúc chờ vào recovery (State::WaitRecovery); chỉ tăng bộ đếm hỏi ở đây.
void StmOta::pollInfo()
{
    ++m_polls;
    const bool keep = m_state == State::WaitRecovery || m_state == State::PostInfo;
    send(stmota::buildCtrl(quint8(m_addr), stmota::kInfo), keep ? m_state : State::Info, kReplyMs);
}

void StmOta::sendGetVersion()
{
    send(stmota::buildCtrl(quint8(m_addr), stmota::kGetVersion), State::GetVersion, kReplyMs);
}

void StmOta::sendSetVersion()
{
    send(stmota::buildSetVersion(quint8(m_addr), m_version), State::SetVersion, kReplyMs);
}

// Kết thúc sau khi đã đọc version về: so với chuỗi đã gửi (khi nạp) hoặc chỉ báo (khi chỉ đọc).
void StmOta::verifyDone(const QString &got)
{
    emit deviceVersion(m_addr, got);
    const QString shown = got.isEmpty() ? QStringLiteral("(chưa đặt)") : got;
    if (m_infoOnly) { done(true, QStringLiteral("%1, version %2").arg(m_slot == 'R' ? QStringLiteral("recovery") : QStringLiteral("app"), shown)); return; }
    if (m_version.isEmpty()) { done(true, QStringLiteral("đã kích hoạt, app mới đang chạy (nạp không có version, đọc lại: %1)").arg(shown)); return; }
    if (got == m_version) done(true, QStringLiteral("đã kích hoạt, version %1 (đọc lại khớp)").arg(got));
    else done(false, QStringLiteral("đã kích hoạt nhưng version đọc lại không khớp: gửi \"%1\", đọc được \"%2\"").arg(m_version, got));
}

void StmOta::beginErase()
{
    m_chunks = (m_image.size() + stmota::kChunk - 1) / stmota::kChunk;
    m_seq = m_retries = 0;
    emit deviceProgress(m_addr, 0, QStringLiteral("Xóa vùng app (5 sector), có thể mất tới vài chục giây"));
    send(stmota::buildCtrl(quint8(m_addr), stmota::kErase), State::Erase, kEraseMs);
}

void StmOta::sendChunk()
{
    send(stmota::buildData(quint8(m_addr), quint16(m_seq), m_image.mid(m_seq * stmota::kChunk, stmota::kChunk)),
         State::Data, kReplyMs);
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
    if (commit) send(stmota::buildCtrl(quint8(m_addr), stmota::kCommit), State::Commit, kReplyMs);
    else done(true, QStringLiteral("đã nạp và kiểm tra CRC32, CHƯA kích hoạt (PSU vẫn ở recovery)"));
}

void StmOta::onFrame(const core::Frame &frame)
{
    if (busy() && frame.cmd == stmota::cmdBytes(stmota::kVersionReply)) {
        if (m_state != State::GetVersion || quint8(frame.raw.at(stmota::kOffAddr)) != m_addr) return;
        verifyDone(stmota::parseVersionReply(frame.raw));
        return;
    }
    if (!busy() || frame.cmd != stmota::cmdBytes(stmota::kAck)) return;
    const stmota::Ack a = stmota::parseAck(frame.raw);
    if (a.addr != m_addr) return;
    const QString why = stmota::statusText(a.status);

    switch (m_state) {
    case State::Info:
    case State::WaitRecovery: {
        if (a.ackCmd != stmota::kInfo) return;
        emit deviceInfo(m_addr, a.slot, a.trial);
        m_slot = a.slot;
        const bool recovery = a.slot == 'R';
        const QString trial = a.trial & 0x80 ? QStringLiteral("đã xác nhận") : QStringLiteral("đang thử, lần boot %1").arg(a.trial & 0x7F);
        if (m_infoOnly) { m_retries = 0; sendGetVersion(); return; }   // chỉ đọc: hỏi tiếp GET_VERSION rồi báo
        if (m_state == State::WaitRecovery && !recovery) {          // chưa reset xong: hỏi lại sau một lúc
            m_timer.start(kRecoveryRetryMs);
            return;
        }
        if (m_image.isEmpty() || m_image.size() > stmota::kMaxImageBytes) {   // kiểm tra TRƯỚC khi ENTER_REC / ERASE, để không xóa app khi ảnh không hợp lệ
            done(false, m_image.isEmpty() ? QStringLiteral("chưa chọn file .bin")
                                          : QStringLiteral("file .bin %1 byte vượt giới hạn %2 byte (639 KB)").arg(m_image.size()).arg(stmota::kMaxImageBytes));
            return;
        }
        if (recovery) {
            emit deviceProgress(m_addr, 0, m_state == State::WaitRecovery ? QStringLiteral("Đã vào recovery") : QStringLiteral("Đã ở recovery sẵn, bỏ qua ENTER_REC"));
            beginErase();
        } else if (a.slot == 'A') {
            emit deviceProgress(m_addr, 0, QStringLiteral("Đang chạy app (version %1): chuyển sang recovery").arg(a.info));
            send(stmota::buildCtrl(quint8(m_addr), stmota::kEnterRec), State::EnterRec, kReplyMs);
        } else {
            done(false, QStringLiteral("slot lạ '%1' trong trả lời INFO").arg(QChar(a.slot)));
        }
        break;
    }

    case State::EnterRec:
        if (a.ackCmd != stmota::kEnterRec) return;
        if (a.status != stmota::Ok) { done(false, QStringLiteral("ENTER_REC bị từ chối: ") + why); return; }
        emit deviceProgress(m_addr, 0, QStringLiteral("Thiết bị đang reset vào recovery"));
        m_state = State::WaitRecovery;
        m_polls = 0;
        m_timer.start(kRecoveryFirstMs);          // hết giờ thì hỏi INFO (onTimeout)
        break;

    case State::Erase:
        if (a.ackCmd != stmota::kErase) return;
        if (a.status == stmota::EraseFail) { done(false, QStringLiteral("xóa sector thất bại (mã HAL 0x%1)").arg(a.info, 8, 16, QLatin1Char('0'))); return; }
        if (a.status != stmota::Ok) { done(false, QStringLiteral("ERASE bị từ chối: ") + why); return; }
        emit deviceProgress(m_addr, 0, QStringLiteral("Gửi BEGIN"));
        send(stmota::buildBegin(quint8(m_addr), quint32(m_image.size()), stmota::crc32(m_image)), State::Begin, kReplyMs);
        break;

    case State::Begin:
        if (a.ackCmd != stmota::kBegin) return;
        if (a.status != stmota::Ok) { done(false, QStringLiteral("BEGIN bị từ chối: ") + why); return; }
        if (m_version.isEmpty()) { sendChunk(); break; }
        m_retries = 0;
        emit deviceProgress(m_addr, 0, QStringLiteral("Gửi SET_VERSION \"%1\"").arg(m_version));
        sendSetVersion();
        break;

    case State::SetVersion:
        if (a.ackCmd != stmota::kSetVersion) return;
        if (a.status != stmota::Ok) { done(false, QStringLiteral("SET_VERSION bị từ chối: ") + why); return; }
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
        } else if (a.status == stmota::ProgFail) {
            done(false, QStringLiteral("lỗi ghi flash ở chunk %1 (info 0x%2)").arg(m_seq).arg(a.info, 8, 16, QLatin1Char('0')));
            return;
        } else {
            done(false, QStringLiteral("DATA chunk %1: %2").arg(m_seq).arg(why));
            return;
        }
        emit deviceProgress(m_addr, m_seq * 100 / m_chunks, QStringLiteral("Chunk %1 / %2").arg(m_seq).arg(m_chunks));
        if (m_seq >= m_chunks) send(stmota::buildCtrl(quint8(m_addr), stmota::kEnd), State::End, kEndMs);
        else sendChunk();
        break;

    case State::End:
        if (a.ackCmd != stmota::kEnd) return;
        if (a.status != stmota::Ok) { done(false, QStringLiteral("END: %1 (CRC thiết bị tính = 0x%2)").arg(why).arg(a.info, 8, 16, QLatin1Char('0'))); return; }
        emit deviceProgress(m_addr, 100, QStringLiteral("CRC32 đúng"));
        if (m_autoCommit) { send(stmota::buildCtrl(quint8(m_addr), stmota::kCommit), State::Commit, kReplyMs); return; }
        m_timer.stop();
        m_state = State::AwaitUser;
        emit commitRequested(m_addr);
        break;

    case State::Commit:
        if (a.ackCmd != stmota::kCommit) return;
        if (a.status != stmota::Ok) { done(false, QStringLiteral("COMMIT bị từ chối: ") + why); return; }
        emit deviceProgress(m_addr, 100, QStringLiteral("Đã COMMIT, chờ app mới chạy rồi đọc lại version"));
        m_state = State::PostCommit;
        m_timer.start(kPostCommitMs);
        break;

    case State::PostInfo:
        if (a.ackCmd != stmota::kInfo) return;
        emit deviceInfo(m_addr, a.slot, a.trial);
        m_slot = a.slot;
        if (a.slot == 'A') { m_retries = 0; sendGetVersion(); }
        else m_timer.start(kRecoveryRetryMs * 2);   // chưa chạy app: hỏi lại sau một lúc
        break;

    default:
        break;
    }
}

void StmOta::onTimeout()
{
    switch (m_state) {
    case State::Info:
        if (m_infoOnly) { done(false, QStringLiteral("không trả lời")); break; }
        if (m_polls >= kInfoPolls) { done(false, QStringLiteral("không trả lời INFO; nếu app treo, cấp nguồn lại PSU rồi nạp lại (bootloader ở lại recovery khi nhận INFO trong 300 ms đầu)")); break; }
        emit deviceProgress(m_addr, 0, QStringLiteral("Chưa thấy trả lời INFO, đang hỏi lại (app treo thì cấp nguồn lại PSU)"));
        pollInfo();
        break;
    case State::EnterRec:
        // ACK có thể bị mất vì thiết bị reset ngay: không kết luận thất bại, hỏi INFO xem đã vào recovery chưa.
        emit deviceProgress(m_addr, 0, QStringLiteral("Không nhận được ACK ENTER_REC, kiểm tra xem đã vào recovery chưa"));
        m_state = State::WaitRecovery;
        m_polls = 0;
        pollInfo();
        break;
    case State::WaitRecovery:                      // hết thời gian chờ ACK INFO, hoặc hết khoảng nghỉ giữa hai lần hỏi
        if (m_polls >= kRecoveryPolls) { done(false, QStringLiteral("thiết bị không vào được recovery sau ENTER_REC")); break; }
        pollInfo();
        break;
    case State::Erase:       done(false, QStringLiteral("quá thời gian chờ xóa vùng app (%1 s)").arg(kEraseMs / 1000)); break;
    case State::Begin:       done(false, QStringLiteral("không trả lời BEGIN")); break;
    case State::SetVersion:
        if (++m_retries >= kVersionTries) done(false, QStringLiteral("SET_VERSION không được ACK sau %1 lần (firmware PSU cũ chưa hỗ trợ version chuỗi?)").arg(kVersionTries));
        else sendSetVersion();
        break;
    case State::PostCommit:                       // hết giờ chờ app mới chạy: hỏi INFO
        m_polls = 0;
        m_state = State::PostInfo;
        pollInfo();
        break;
    case State::PostInfo:
        if (m_polls >= kRecoveryPolls) { done(true, QStringLiteral("đã kích hoạt nhưng chưa xác nhận được app mới (PSU chưa trả lời INFO ở slot A); hãy bấm \"Đọc slot và version\"")); break; }
        pollInfo();
        break;
    case State::GetVersion:
        if (++m_retries >= kVersionTries) {
            if (m_infoOnly) done(true, QStringLiteral("slot %1, không đọc được version (không có trả lời GET_VERSION)").arg(m_slot));
            else done(false, QStringLiteral("đã kích hoạt nhưng không đọc lại được version (không có trả lời GET_VERSION)"));
        } else sendGetVersion();
        break;
    case State::Data:
        if (++m_retries > kMaxRetries) done(false, QStringLiteral("chunk %1 không được ACK sau %2 lần gửi lại").arg(m_seq).arg(kMaxRetries));
        else sendChunk();
        break;
    case State::End:         done(false, QStringLiteral("quá thời gian chờ xác nhận END")); break;
    case State::Commit:
        // Thiết bị thường reset trước khi kịp gửi ACK: không kết luận, chờ rồi hỏi INFO + GET_VERSION để xác nhận.
        emit deviceProgress(m_addr, 100, QStringLiteral("Không nhận ACK COMMIT (thiết bị thường reset ngay), kiểm tra lại bằng INFO"));
        m_state = State::PostCommit;
        m_timer.start(kPostCommitMs);
        break;
    default: break;
    }
}

} // namespace services
