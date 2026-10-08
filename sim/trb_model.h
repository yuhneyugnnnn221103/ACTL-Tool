#pragma once
// Một TRB giả lập: giá trị giám sát, cấu hình, trạng thái điều khiển. Không biết gì về đường truyền.
#include "../core/frame.h"
#include "../proto/trb_control_proto.h"
#include <QHash>
#include <QList>
#include <QRandomGenerator>

namespace sim {

class TrbModel {
public:
    TrbModel(int mb, int trb, quint32 seed);

    int mb() const { return m_mb; }
    int trb() const { return m_trb; }

    // Mất kết nối: không trả lời gì (kể cả khi đang debug).
    bool online() const { return m_online; }
    void setOnline(bool on) { m_online = on; }

    bool debug() const { return m_debug; }
    const proto::trbctl::ControlCmd &lastControl() const { return m_control; }
    const proto::trbctl::BeamCmd &lastBeam() const { return m_beam; }
    const QList<double> &config() const { return m_config; }
    void setConfig(const QList<double> &values) { m_config = values; }

    // Ép giá trị một trường giám sát (chỉ số trong trbmon::table()) hoặc một byte trip code (0..15).
    void force(int field, double value) { m_forced.insert(field, value); }
    void unforce(int field) { m_forced.remove(field); }
    void clearForced() { m_forced.clear(); }
    void setTrip(int index, quint8 bits) { m_trip[index] = bits; }
    void clearTrip() { m_trip.fill(0); }
    quint8 trip(int index) const { return quint8(m_trip.at(index)); }
    bool hasTrip() const { for (char c : m_trip) if (c) return true; return false; }
    bool hasForced() const { return !m_forced.isEmpty(); }

    QList<double> sample();                    // giá trị giám sát hiện tại (có nhiễu nhỏ)
    QByteArray monitorFrame();                 // khung giám sát 280 byte
    QByteArray configReply() const;            // A4 A4

    // Điều khiển / beam / ghi cấu hình / hỏi cấu hình. Trả về khung trả lời, rỗng nếu không có.
    void applyControl(const proto::trbctl::ControlCmd &c);
    void applyBeam(const proto::trbctl::BeamCmd &b) { m_beam = b; m_beamSet = true; }
    void writeConfig(const core::Frame &frame);

private:
    int m_mb, m_trb;
    bool m_online = true, m_debug = false, m_beamSet = false;
    proto::trbctl::ControlCmd m_control;
    proto::trbctl::BeamCmd m_beam;
    QList<double> m_config;
    QHash<int, double> m_forced;
    QByteArray m_trip;
    QRandomGenerator m_rng;
};

} // namespace sim
