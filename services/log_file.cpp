#include "log_file.h"
#include "../proto/psu_monitor_proto.h"
#include "../proto/trb_monitor_proto.h"
#include <QDateTime>
#include <QRegularExpression>
#include <algorithm>
#include <numeric>

namespace logfile {

static_assert(kTrbBodySize == proto::trbmon::kLength - 8, "thân khung TRB = độ dài khung - 8");
static_assert(kPsuBodySize == proto::psumon::kLength - 8, "thân khung PSU = độ dài khung - 8");

namespace {
const char kMagic[] = "ACTLLOG";
constexpr int kMagicLen = 7;
constexpr int kHeaderLen = kMagicLen + 1 + 8;
constexpr int kBlockLimit = 1 << 20;     // gom tối đa ~1 MB chưa nén rồi ghi khối

void putU32(QByteArray &b, quint32 v) { for (int i = 0; i < 4; ++i) b.append(char(v >> (8 * i) & 0xFF)); }
void putI64(QByteArray &b, qint64 v) { for (int i = 0; i < 8; ++i) b.append(char(quint64(v) >> (8 * i) & 0xFF)); }
void putVarint(QByteArray &b, quint64 v)
{
    while (v >= 0x80) { b.append(char(v & 0x7F | 0x80)); v >>= 7; }
    b.append(char(v));
}
quint32 getU32(const char *p) { quint32 v = 0; for (int i = 3; i >= 0; --i) v = v << 8 | quint8(p[i]); return v; }
qint64 getI64(const char *p) { quint64 v = 0; for (int i = 7; i >= 0; --i) v = v << 8 | quint8(p[i]); return qint64(v); }

// Đọc varint từ [p, end); false nếu hết dữ liệu hoặc quá dài.
bool getVarint(const char *&p, const char *end, quint64 &v)
{
    v = 0;
    for (int shift = 0; shift < 64 && p < end; shift += 7) {
        const quint8 byte = quint8(*p++);
        v |= quint64(byte & 0x7F) << shift;
        if (!(byte & 0x80)) return true;
    }
    return false;
}
}

QString deviceName(int key)
{
    return keyKind(key) == KindTrb ? QStringLiteral("MB%1 / TRB%2").arg(keyA(key) + 1).arg(keyB(key) + 1) : QStringLiteral("PSU %1").arg(keyA(key));
}

// ---------------------------------------------------------------- ghi

bool Writer::open(const QString &path, qint64 startMs, QString *error)
{
    close();
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = m_file.errorString();
        return false;
    }
    QByteArray head(kMagic, kMagicLen);
    head.append(char(kVersion));
    putI64(head, startMs);
    m_file.write(head);
    m_buf.clear();
    m_prev.clear();
    m_blockOpen = false;
    m_records = 0;
    return true;
}

void Writer::addRecord(qint64 t, quint8 type, const QByteArray &head, const QByteArray &body, int key)
{
    if (!m_file.isOpen()) return;
    if (!m_blockOpen) { m_base = m_last = t; m_blockOpen = true; }
    putVarint(m_buf, quint64(qMax<qint64>(0, t - m_last)));
    m_last = qMax(m_last, t);
    m_buf.append(char(type));
    m_buf.append(head);
    if (!body.isEmpty()) {
        QByteArray &prev = m_prev[key];
        if (prev.size() != body.size()) prev = QByteArray(body.size(), '\0');
        QByteArray x(body.size(), '\0');
        for (int i = 0; i < body.size(); ++i) x[i] = char(quint8(body.at(i)) ^ quint8(prev.at(i)));
        m_buf.append(x);
        prev = body;
    }
    ++m_records;
    if (m_buf.size() >= kBlockLimit) flush();
}

void Writer::addTrb(qint64 t, int mb, int trb, int status, const QByteArray &frame)
{
    if (frame.size() != proto::trbmon::kLength) return;
    QByteArray head;
    head.append(char(mb)); head.append(char(trb)); head.append(char(status));
    addRecord(t, 1, head, frame.mid(kBodyFrameOffset, kTrbBodySize), deviceKey(KindTrb, mb, trb));
}

void Writer::addPsu(qint64 t, int addr, int status, const QByteArray &frame)
{
    if (frame.size() != proto::psumon::kLength) return;
    QByteArray head;
    head.append(char(addr)); head.append(char(status));
    addRecord(t, 2, head, frame.mid(kBodyFrameOffset, kPsuBodySize), deviceKey(KindPsu, addr));
}

void Writer::addEvent(qint64 t, const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    QByteArray head;
    putVarint(head, quint64(utf8.size()));
    addRecord(t, 3, head + utf8, {}, 0);
}

bool Writer::writeBlock()
{
    if (!m_blockOpen) return true;
    QByteArray payload;
    putI64(payload, m_base);
    payload += m_buf;
    const QByteArray packed = qCompress(payload, 9);
    QByteArray out;
    putU32(out, quint32(packed.size()));
    out += packed;
    const bool ok = m_file.write(out) == out.size();
    m_buf.clear();
    m_prev.clear();                    // khối sau không phụ thuộc khối trước
    m_blockOpen = false;
    return ok;
}

bool Writer::flush()
{
    if (!m_file.isOpen()) return false;
    const bool ok = writeBlock();
    m_file.flush();
    return ok;
}

void Writer::close()
{
    if (!m_file.isOpen()) return;
    writeBlock();
    m_file.close();
}

// ---------------------------------------------------------------- đọc

namespace {
void extend(Data &d, qint64 t)
{
    if (d.startMs == 0 || t < d.startMs) d.startMs = t;
    if (t > d.endMs) d.endMs = t;
}
}

bool load(const QString &path, Data &out, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = f.errorString(); return false; }
    const QByteArray all = f.readAll();
    if (all.size() < kHeaderLen || all.left(kMagicLen) != QByteArray(kMagic, kMagicLen)) {
        if (error) *error = QStringLiteral("không phải file log ACTL (.alog)");
        return false;
    }
    if (quint8(all.at(kMagicLen)) != kVersion) {
        if (error) *error = QStringLiteral("phiên bản file log %1 không được hỗ trợ").arg(int(quint8(all.at(kMagicLen))));
        return false;
    }
    Data d;
    int pos = kHeaderLen;
    int blockNo = 0;
    while (pos < all.size()) {
        ++blockNo;
        if (all.size() - pos < 4) { d.warnings << QStringLiteral("file bị cắt ở khối %1").arg(blockNo); break; }
        const quint32 len = getU32(all.constData() + pos);
        pos += 4;
        if (len > quint32(all.size() - pos)) { d.warnings << QStringLiteral("khối %1 bị cắt dở, bỏ qua").arg(blockNo); break; }
        const QByteArray payload = qUncompress(reinterpret_cast<const uchar *>(all.constData() + pos), int(len));
        pos += int(len);
        if (payload.size() < 8) { d.warnings << QStringLiteral("khối %1 bị hỏng, bỏ qua phần còn lại").arg(blockNo); break; }

        const char *p = payload.constData() + 8;
        const char *end = payload.constData() + payload.size();
        qint64 t = getI64(payload.constData());
        QHash<int, QByteArray> prev;
        bool bad = false;
        while (p < end && !bad) {
            quint64 dt;
            if (!getVarint(p, end, dt) || p >= end) { bad = true; break; }
            t += qint64(dt);
            const quint8 type = quint8(*p++);
            if (type == 1 || type == 2) {
                const int headLen = type == 1 ? 3 : 2, bodyLen = type == 1 ? kTrbBodySize : kPsuBodySize;
                if (end - p < headLen + bodyLen) { bad = true; break; }
                const int key = type == 1 ? deviceKey(KindTrb, quint8(p[0]), quint8(p[1])) : deviceKey(KindPsu, quint8(p[0]));
                const int status = quint8(p[headLen - 1]);
                QByteArray &pv = prev[key];
                if (pv.size() != bodyLen) pv = QByteArray(bodyLen, '\0');
                QByteArray body(bodyLen, '\0');
                for (int i = 0; i < bodyLen; ++i) body[i] = char(quint8(p[headLen + i]) ^ quint8(pv.at(i)));
                pv = body;
                p += headLen + bodyLen;
                Series &s = d.series[key];
                s.bodySize = bodyLen;
                s.t.append(t);
                s.status.append(char(status));
                s.bodies.append(body);
                extend(d, t);
            } else if (type == 3) {
                quint64 n;
                if (!getVarint(p, end, n) || quint64(end - p) < n) { bad = true; break; }
                d.events.append({t, QString::fromUtf8(p, int(n))});
                p += int(n);
                extend(d, t);
            } else {
                bad = true;
            }
        }
        if (bad) d.warnings << QStringLiteral("khối %1 có bản ghi hỏng, phần còn lại của khối bị bỏ qua").arg(blockNo);
    }
    out = d;
    return true;
}

void merge(Data &into, const Data &other)
{
    if (other.startMs != 0 && (into.startMs == 0 || other.startMs < into.startMs)) into.startMs = other.startMs;
    into.endMs = qMax(into.endMs, other.endMs);
    for (auto it = other.series.cbegin(); it != other.series.cend(); ++it) {
        Series &s = into.series[it.key()];
        const Series &o = it.value();
        if (s.bodySize == 0) s.bodySize = o.bodySize;
        const bool overlap = !s.t.isEmpty() && !o.t.isEmpty() && o.t.first() < s.t.last();
        s.t += o.t;
        s.status += o.status;
        s.bodies += o.bodies;
        if (overlap) {                                       // hai file chồng thời gian: sắp lại theo thời gian (ổn định)
            QVector<int> idx(s.t.size());
            std::iota(idx.begin(), idx.end(), 0);
            std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return s.t.at(a) < s.t.at(b); });
            Series sorted;
            sorted.bodySize = s.bodySize;
            for (int i : idx) { sorted.t.append(s.t.at(i)); sorted.status.append(s.status.at(i)); sorted.bodies.append(s.bodies.mid(i * s.bodySize, s.bodySize)); }
            s = sorted;
        }
    }
    into.events += other.events;
    std::stable_sort(into.events.begin(), into.events.end(), [](const Event &a, const Event &b) { return a.t < b.t; });
    into.warnings += other.warnings;
}

double fieldValue(const Series &s, int i, const proto::Field &f)
{
    const int off = i * s.bodySize + f.offset - kBodyFrameOffset;
    if (f.offset < kBodyFrameOffset || off + f.size > s.bodies.size()) return 0;
    quint32 raw = 0;
    for (int k = 0; k < f.size; ++k) raw = raw << 8 | quint8(s.bodies.at(off + k));
    if (f.isSigned) {
        const int bits = 8 * f.size;
        const qint64 v = (bits < 32 && (raw >> (bits - 1) & 1)) ? qint64(raw) - (qint64(1) << bits) : (bits == 32 ? qint64(qint32(raw)) : qint64(raw));
        return double(v) * f.scale;
    }
    return raw * f.scale;
}

// ---------------------------------------------------------------- tên file

static const char *kStamp = "yyyyMMdd-HHmmss";

QString fileName(const QDateTime &start, const QDateTime &end)
{
    return QStringLiteral("ACTL_%1_%2.alog").arg(start.toString(QLatin1String(kStamp)),
                                                  end.isValid() ? end.toString(QLatin1String(kStamp)) : QStringLiteral("dang-ghi"));
}

bool parseFileName(const QString &name, QDateTime &start, QDateTime &end)
{
    static const QRegularExpression re(QStringLiteral("^ACTL_(\\d{8}-\\d{6})_(\\d{8}-\\d{6}|dang-ghi)(?:-\\d+)?\\.alog$"));
    const QRegularExpressionMatch m = re.match(name);
    if (!m.hasMatch()) return false;
    start = QDateTime::fromString(m.captured(1), QLatin1String(kStamp));
    end = m.captured(2) == QLatin1String("dang-ghi") ? QDateTime() : QDateTime::fromString(m.captured(2), QLatin1String(kStamp));
    return start.isValid();
}

} // namespace logfile
