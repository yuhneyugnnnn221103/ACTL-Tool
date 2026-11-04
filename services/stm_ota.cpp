#include "stm_ota.h"
#include "../proto/gateway_header_proto.h"

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

void StmOta::start(const QList<int> &addrs, const QByteArray &image, quint32 version, bool autoCommit)
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
    send(stmota::buildCtrl(quint8(m_addr), stmota::kInfo), m_state == State::WaitRecovery ? State::WaitRecovery : State::Info, kReplyMs);
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
    if (!busy() || frame.cmd != stmota::cmdBytes(stmota::kAck)) return;
    const stmota::Ack a = stmota::parseAck(frame.raw);
    if (a.addr != m_addr) return;
    const QString why = stmota::statusText(a.status);

    switch (m_state) {
    case State::Info:
    case State::WaitRecovery: {
        if (a.ackCmd != stmota::kInfo) return;
        emit deviceInfo(m_addr, a.slot, a.info, a.trial);
        const bool recovery = a.slot == 'R';
        const QString trial = a.trial & 0x80 ? QStringLiteral("đã xác nhận") : QStringLiteral("đang thử, lần boot %1").arg(a.trial & 0x7F);
        if (m_infoOnly) {
            done(true, QStringLiteral("slot %1, version %2 (%3)").arg(a.slot).arg(a.info).arg(recovery ? QStringLiteral("bootloader recovery, chưa có app hợp lệ hoặc đang chờ nạp") : trial));
            return;
        }
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
        send(stmota::buildBegin(quint8(m_addr), quint32(m_image.size()), stmota::crc32(m_image), m_version), State::Begin, kReplyMs);
        break;

    case State::Begin:
        if (a.ackCmd != stmota::kBegin) return;
        if (a.status != stmota::Ok) { done(false, QStringLiteral("BEGIN bị từ chối: ") + why); return; }
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
        if (a.status == stmota::Ok) done(true, QStringLiteral("đã kích hoạt, bootloader đang chạy app mới"));
        else done(false, QStringLiteral("COMMIT bị từ chối: ") + why);
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
    case State::Data:
        if (++m_retries > kMaxRetries) done(false, QStringLiteral("chunk %1 không được ACK sau %2 lần gửi lại").arg(m_seq).arg(kMaxRetries));
        else sendChunk();
        break;
    case State::End:         done(false, QStringLiteral("quá thời gian chờ xác nhận END")); break;
    case State::Commit:
        // Thiết bị thường reset trước khi kịp gửi ACK: coi là thành công có điều kiện.
        done(true, QStringLiteral("không nhận được ACK COMMIT (thường do thiết bị đã reset); hãy đọc lại thông tin để xác nhận"));
        break;
    default: break;
    }
}

} // namespace services
