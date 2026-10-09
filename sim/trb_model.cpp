#include "trb_model.h"
#include "../proto/trb_config_proto.h"
#include "../proto/trb_monitor_proto.h"

namespace sim {

using namespace proto;

TrbModel::TrbModel(int mb, int trb, quint32 seed)
    : m_mb(mb), m_trb(trb), m_config(trbcfg::table().size(), 0.0), m_trip(trbmon::kNumTrip, '\0'),
      m_rng(seed ^ quint32(mb * 131 + trb * 7919 + 1))
{
}

QList<double> TrbModel::sample()
{
    QList<double> v(trbmon::kNumFields, 0.0);
    auto r = [this](int lo, int hi) { return double(m_rng.bounded(lo, hi + 1)); };
    for (int trm = 0; trm < trbmon::kNumTrm; ++trm) {
        const int base = trbmon::kIdxTrm0 + trm * trbmon::kTrmFields;
        for (int ch = 0; ch < 8; ++ch) {
            v[base + ch] = r(90, 110);        // I SEN
            v[base + 8 + ch] = r(900, 1100);  // DET
        }
        for (int k = 0; k < 4; ++k) {
            v[base + 16 + k] = r(35, 45);     // TEMP
            v[base + 20 + k] = r(45, 55);     // I PA
        }
        v[base + 24] = r(15, 25);             // I LNA
    }
    v[trbmon::kIdxTrbV] = 150 + r(-5, 5);
    v[trbmon::kIdxTrbI] = 500 + r(-20, 20);
    v[trbmon::kIdxTrbTemp] = 40;
    v[trbmon::kIdxMcuTemp] = 50;
    v[trbmon::kIdxHumidity] = 40;
    v[trbmon::kIdxPeriodTxen] = 1000;           // giá trị mẫu cho các trường chu kỳ / độ rộng xung
    v[trbmon::kIdxPulseTxen] = 100;
    v[trbmon::kIdxPeriodBeamsync] = 2000;
    v[trbmon::kIdxPulseBeamsync] = 200;
    // Bit i của INIT_ADAR = ADAR(i+1) đang bật, của PA = PA TRM(i+1) đang bật (1 bật, 0 tắt), theo lệnh gần nhất.
    // Chưa có lệnh beam nào thì coi cả 8 ADAR đã khởi tạo xong; chưa có lệnh điều khiển thì PA tắt.
    v[trbmon::kIdxInitAdar] = m_beamSet ? m_beam.adarMask : 0xFF;
    v[trbmon::kIdxPa] = m_control.paMask & 0x0F;
    v[trbmon::kIdxPg] = 0x0F;
    for (int i = 0; i < trbmon::kNumTrip; ++i) v[trbmon::kIdxTrip0 + i] = quint8(m_trip.at(i));
    for (auto it = m_forced.cbegin(); it != m_forced.cend(); ++it) v[it.key()] = it.value();
    return v;
}

QByteArray TrbModel::monitorFrame()
{
    return trbmon::buildFrame(m_mb, m_trb, sample());
}

QByteArray TrbModel::configReply() const
{
    return trbcfg::buildReadReply(m_mb, m_trb, m_config);
}

void TrbModel::applyControl(const trbctl::ControlCmd &c)
{
    m_control = c;
    m_debug = c.debugMode;
    if (c.clearTrip) clearTrip();
}

void TrbModel::writeConfig(const core::Frame &frame)
{
    m_config = trbcfg::table().decode(frame.raw);
}

} // namespace sim
