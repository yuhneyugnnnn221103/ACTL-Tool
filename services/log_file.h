#pragma once
// File log nhị phân nén (đuôi .alog), mỗi lần ghi log chỉ ra một file; ghi lại từng bản tin giám sát nhận được và các sự kiện.
//
//   Đầu file: "ACTLLOG" (7 byte) + phiên bản (1 byte = 1) + thời điểm bắt đầu ghi (int64 LE, ms từ epoch)
//   Rồi liên tiếp các khối: [độ dài khối nén uint32 LE][khối nén bằng qCompress/zlib mức 9]
//   Khối giải nén: [mốc thời gian đầu khối int64 LE] rồi các bản ghi:
//     varint dt (ms kể từ bản ghi trước, bản ghi đầu tính từ mốc khối) + loại (1 byte)
//       loại 1 (TRB):  mb, trb, trạng thái (model::Status) + thân khung
//       loại 2 (PSU):  địa chỉ, trạng thái + thân khung
//       loại 3 (sự kiện): varint độ dài + chuỗi UTF-8
//     thân khung = byte 5 .. (độ dài - 4) của khung gốc (từ byte địa chỉ tới hết dữ liệu; bỏ AB CD, CMD, CRC, đuôi, ghép lại khi đọc),
//     lưu dạng XOR với thân bản tin trước của cùng thiết bị trong khối (bản đầu của khối: XOR với 0). Các byte không đổi thành 0 nên nén rất tốt.
//   Mỗi khối độc lập nên file luôn đọc được tới khối hoàn chỉnh cuối cùng, kể cả khi app bị tắt đột ngột.
//   Tên file: ACTL_<bắt đầu>_<kết thúc>.alog (yyyyMMdd-HHmmss); đang ghi thì phần kết thúc là "dang-ghi" và được đổi tên khi dừng.
#include "../model/device_store.h"
#include "../model/psu_store.h"
#include "../proto/field_table.h"
#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QMap>
#include <QString>
#include <QVector>

namespace logfile {

constexpr int kVersion = 1;
constexpr int kTrbBodySize = 302, kPsuBodySize = 302;     // = độ dài khung 310 - 8; static_assert ở .cpp
constexpr int kBodyFrameOffset = 4;                       // thân khung bắt đầu ở byte thứ 5 của khung (chỉ số 4)

enum Kind { KindTrb = 0, KindPsu = 1 };
inline int deviceKey(int kind, int a, int b = 0) { return kind << 16 | a << 8 | b; }   // TRB: (mb, trb); PSU: (địa chỉ, 0)
inline int keyKind(int key) { return key >> 16; }
inline int keyA(int key) { return key >> 8 & 0xFF; }
inline int keyB(int key) { return key & 0xFF; }
QString deviceName(int key);                               // "MB3 / TRB5" hoặc "PSU 2" (đếm từ 1 cho TRB như giao diện)

// Ghi tuần tự. Dữ liệu gom trong bộ nhớ và nén thành khối khi flush() hoặc khi đủ lớn.
class Writer {
public:
    bool open(const QString &path, qint64 startMs, QString *error = nullptr);
    bool isOpen() const { return m_file.isOpen(); }
    QString path() const { return m_file.fileName(); }
    qint64 fileSize() const { return m_file.size(); }
    quint64 records() const { return m_records; }

    void addTrb(qint64 t, int mb, int trb, int status, const QByteArray &frame);    // frame = khung 310 byte nguyên vẹn
    void addPsu(qint64 t, int addr, int status, const QByteArray &frame);
    void addEvent(qint64 t, const QString &text);
    bool flush();                                          // nén và ghi khối đang gom
    void close();

private:
    void addRecord(qint64 t, quint8 type, const QByteArray &head, const QByteArray &body, int key);
    bool writeBlock();

    QFile m_file;
    QByteArray m_buf;                                      // các bản ghi của khối đang gom (chưa kèm mốc thời gian đầu khối)
    qint64 m_base = 0, m_last = 0;
    bool m_blockOpen = false;
    QHash<int, QByteArray> m_prev;                         // thân khung trước đó của từng thiết bị trong khối
    quint64 m_records = 0;
};

// Dữ liệu của một thiết bị: các mẫu theo thứ tự thời gian, thân khung nối liền nhau để tiết kiệm bộ nhớ.
struct Series {
    int bodySize = 0;
    QVector<qint64> t;
    QByteArray status;                                     // mỗi mẫu một byte (model::Status)
    QByteArray bodies;                                     // mẫu i ở [i * bodySize, (i + 1) * bodySize)
    int count() const { return t.size(); }
    QByteArray body(int i) const { return bodies.mid(i * bodySize, bodySize); }
};

struct Event { qint64 t = 0; QString text; };

struct Data {
    qint64 startMs = 0, endMs = 0;                         // khoảng thời gian có dữ liệu (đầu và cuối mẫu/sự kiện)
    QMap<int, Series> series;                              // khóa deviceKey
    QVector<Event> events;
    QStringList warnings;                                  // ví dụ file bị cắt giữa chừng
};

// Đọc một file; false và error nếu không phải file log hợp lệ. Khối cuối bị hỏng chỉ thành cảnh báo.
bool load(const QString &path, Data &out, QString *error = nullptr);
// Ghép nhiều file lên cùng trục thời gian (mẫu của mỗi thiết bị được sắp theo thời gian).
void merge(Data &into, const Data &other);
// Giá trị của một trường tại mẫu i của series (field.offset tính theo khung gốc).
double fieldValue(const Series &s, int i, const proto::Field &f);

// Tên file theo thời điểm bắt đầu và kết thúc (kết thúc rỗng = đang ghi).
QString fileName(const QDateTime &start, const QDateTime &end);
// Đọc khoảng thời gian từ tên file; false nếu tên không đúng dạng.
bool parseFileName(const QString &name, QDateTime &start, QDateTime &end);

} // namespace logfile
