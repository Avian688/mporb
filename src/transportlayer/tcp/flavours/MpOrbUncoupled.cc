//
// Copyright (C) 2020 Marcel Marek
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//

#include "MpOrbUncoupled.h"

#include "../../../../../tcpPaced/src/transportlayer/tcp/TcpPacedConnection.h"
#include "../MpOrbSubflowConnection.h"

namespace inet {
namespace tcp {

Register_Class(MpOrbUncoupled);

IntDataVec MpOrbUncoupled::getCurrentIntData() const
{
    auto *subflow = dynamic_cast<MpOrbSubflowConnection *>(conn);
    if (subflow == nullptr)
        return IntDataVec();

    return subflow->getCurrentIntData();
}

void MpOrbUncoupled::established(bool active)
{
    state->snd_cwnd = 7300;
    state->prevWnd = state->snd_cwnd;
    dynamic_cast<TcpPacedConnection *>(conn)->changeIntersendingTime(0.000001);
    state->ssthresh = 73000;
    connId = std::hash<std::string>{}(conn->localAddr.str() + "/" + std::to_string(conn->localPort) + "/" + conn->remoteAddr.str() + "/" + std::to_string(conn->remotePort));
    initPackets = true;
    EV_DETAIL << "MpOrb initial CWND is set to " << state->snd_cwnd << "\n";
    conn->emit(cwndLimitedSignal, false);
    if (active) {
        EV_INFO << "Completing connection setup by sending ACK (possibly piggybacked on data)\n";
        sendData(false);
        conn->sendAck();
    }
}

void MpOrbUncoupled::receiveSeqChanged()
{
    receiveSeqChanged(getCurrentIntData());
}

void MpOrbUncoupled::receiveSeqChanged(const IntDataVec& intData)
{
    if (state->full_sized_segment_counter == 0 && !state->ack_now && state->last_ack_sent == state->rcv_nxt && !delayedAckTimer->isScheduled()) {
    }
    else {
        if (state->lossRecovery)
            state->ack_now = true;

        auto *subflow = dynamic_cast<MpOrbSubflowConnection *>(conn);
        if (subflow == nullptr)
            throw cRuntimeError("MpOrbUncoupled requires MpOrbSubflowConnection");

        if (!state->delayed_acks_enabled) {
            EV_INFO << "rcv_nxt changed to " << state->rcv_nxt << ", (delayed ACK disabled) sending ACK now\n";
            subflow->sendIntAck(intData);
        }
        else {
            if (state->ack_now) {
                EV_INFO << "rcv_nxt changed to " << state->rcv_nxt << ", (delayed ACK enabled, but ack_now is set) sending ACK now\n";
                subflow->sendIntAck(intData);
            }
            else if (state->full_sized_segment_counter >= 2) {
                EV_INFO << "rcv_nxt changed to " << state->rcv_nxt << ", (delayed ACK enabled, but full_sized_segment_counter=" << state->full_sized_segment_counter << ") sending ACK now\n";
                subflow->sendIntAck(intData);
            }
            else {
                EV_INFO << "rcv_nxt changed to " << state->rcv_nxt << ", (delayed ACK enabled and full_sized_segment_counter=" << state->full_sized_segment_counter << ") scheduling ACK\n";
                if (!delayedAckTimer->isScheduled())
                    conn->scheduleAfter(0.2, delayedAckTimer);
            }
        }
    }
}

void MpOrbUncoupled::receivedOutOfOrderSegment()
{
    receivedOutOfOrderSegment(getCurrentIntData());
}

void MpOrbUncoupled::receivedOutOfOrderSegment(const IntDataVec& intData)
{
    state->ack_now = true;
    EV_INFO << "Out-of-order segment, sending immediate ACK\n";
    auto *subflow = dynamic_cast<MpOrbSubflowConnection *>(conn);
    if (subflow == nullptr)
        throw cRuntimeError("MpOrbUncoupled requires MpOrbSubflowConnection");
    subflow->sendIntAck(intData);
}

void MpOrbUncoupled::receivedDataAck(uint32_t firstSeqAcked)
{
    receivedDataAck(firstSeqAcked, getCurrentIntData());
}

void MpOrbUncoupled::receivedDataAck(uint32_t firstSeqAcked, const IntDataVec& intData)
{
    OrbtcpPintFlavour::receivedDataAck(firstSeqAcked, intData);
}

void MpOrbUncoupled::receivedDuplicateAck()
{
    receivedDuplicateAck(state->snd_una, getCurrentIntData());
}

void MpOrbUncoupled::receivedDuplicateAck(uint32_t firstSeqAcked, const IntDataVec& intData)
{
    OrbtcpPintFlavour::receivedDuplicateAck(firstSeqAcked, intData);
}

void MpOrbUncoupled::processRexmitTimer(TcpEventCode& event)
{
    state->initialPhase = false;
    state->endInitialPhase = false;
    TcpPacedFamily::processRexmitTimer(event);
    if (event == TCP_E_ABORT)
        return;

    EV_INFO << "Begin Slow Start: resetting cwnd to " << state->snd_cwnd
            << ", ssthresh=" << state->ssthresh << "\n";

    state->afterRto = true;
    auto *pacedConnection = check_and_cast<TcpPacedConnection *>(conn);
    pacedConnection->cancelPaceTimer();
    pacedConnection->retransmitOneSegment(true);
    state->prevWnd = state->snd_cwnd;
}

} // namespace tcp
} // namespace inet
