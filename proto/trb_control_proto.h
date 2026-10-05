#pragma once
// Bản tin PC -> TRB (theo cfg_protocol.h), CRC phủ từ byte địa chỉ:
//   Điều khiển 14 byte: [AB CD][A2 A2][MB][TRB][PA][CTRL][Dự phòng 2B][CRC][E1 E2]
//   Beam       17 byte: [AB CD][14 14][MB][TRB][PhaseTX][PhaseRX][AmpTX][AmpRX][CH_EN][ADAR][Dự phòng][CRC][E1 E2]
#include <QByteArray>

namespace proto::trbctl {

constexpr int kBroadcast = 0xFF; // dùng cho cả địa chỉ MB và TRB

struct ControlCmd {
    quint8 paMask = 0;      // bit0..3 = PA TRM1..4, 1 = bật
    bool clearTrip = false; // bit0 byte CTRL
    bool start = false;     // bit1 (0 = stop)
    bool beamSync = false;  // bit2
    bool debugMode = false; // bit4 (0 = normal)
};

struct BeamCmd {
    quint8 phaseTx = 0, phaseRx = 0, ampTx = 0, ampRx = 0;
    quint8 chMask = 0;      // bit0..3 = CH0..3
    quint8 adarMask = 0;    // bit0..7 = ADAR1..8
};

QByteArray buildControl(int mb, int trb, const ControlCmd &c);
QByteArray buildBeam(int mb, int trb, const BeamCmd &b);

} // namespace proto::trbctl
