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
    dynamic_cast<TcpPacedConnection *>(conn)->changeIntersendingTime(0.000001);
    state->ssthresh = 73000;
    connId = std::hash<std::string>{}(conn->localAddr.str() + "/" + std::to_string(conn->localPort) + "/" + conn->remoteAddr.str() + "/" + std::to_string(conn->remotePort));
    initPackets = true;
    EV_DETAIL << "MpOrb initial CWND is set to " << state->snd_cwnd << "\n";
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

void MpOrbUncoupled::receiveSeqChanged(IntDataVec intData)
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

void MpOrbUncoupled::receivedOutOfOrderSegment(IntDataVec intData)
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

void MpOrbUncoupled::receivedDataAck(uint32_t firstSeqAcked, IntDataVec intData)
{
    TcpTahoeRenoFamily::receivedDataAck(firstSeqAcked);
    EV_INFO << "\nMPORBInfo ___________________________________________" << endl;
    EV_INFO << "\nMPORBInfo - Received Data Ack" << endl;
    bool wasInLossRecovery = state->lossRecovery && state->sack_enabled;
    if (wasInLossRecovery) {
        if (seqGE(state->snd_una, state->recoveryPoint)) {
            EV_INFO << "Loss Recovery terminated.\n";
            state->lossRecovery = false;
        }
        else {
            dynamic_cast<TcpPacedConnection *>(conn)->doRetransmit();
        }
        conn->emit(recoveryPointSignal, state->recoveryPoint);

        conn->emit(cwndSignal, state->snd_cwnd);
        if (!reactTimer->isScheduled())
            conn->scheduleAt(simTime() + state->srtt.dbl(), reactTimer);
        conn->emit(sndUnaSignal, state->snd_una);
        conn->emit(sndMaxSignal, state->snd_max);
        return;
    }

    double uVal = measureInflight(intData);

    if (uVal > 0) {
        if (!pathChanged)
            state->snd_cwnd = computeWnd(uVal, updateWindow);
        state->L = intData;
    }

    state->lastUpdateSeq = state->snd_nxt;
    conn->emit(cwndSignal, state->snd_cwnd);

    if (state->snd_cwnd > 0) {
        uint32_t maxWindow = std::max(state->snd_cwnd, dynamic_cast<TcpPacedConnection *>(conn)->getBytesInFlight());
        uint32_t nominalBandwidth = (maxWindow / state->srtt.dbl());
        double pace = 1 / ((1.2 * (double)nominalBandwidth) / (double)state->snd_mss);
        dynamic_cast<TcpPacedConnection *>(conn)->changeIntersendingTime(pace);
    }

    sendData(false);

    if (!reactTimer->isScheduled())
        conn->scheduleAt(simTime() + state->srtt.dbl(), reactTimer);

    conn->emit(sndUnaSignal, state->snd_una);
    conn->emit(sndMaxSignal, state->snd_max);
}

void MpOrbUncoupled::receivedDuplicateAck()
{
    receivedDuplicateAck(state->snd_una, getCurrentIntData());
}

void MpOrbUncoupled::receivedDuplicateAck(uint32_t firstSeqAcked, IntDataVec intData)
{
    state->initialPhase = false;
    auto pacedConn = dynamic_cast<TcpPacedConnection *>(conn);
    if (shouldEnterLossRecoveryOnDuplicateAck()) {
        EV_INFO << "Reno on dupAcks == DUPTHRESH(=" << state->dupthresh << ": perform Fast Retransmit, and enter Fast Recovery:";

        if (state->sack_enabled) {
            if ((state->recoveryPoint == 0 || seqGE(state->snd_una, state->recoveryPoint)) && !state->lossRecovery) {
                state->recoveryPoint = state->snd_max;
                state->lossRecovery = true;
                conn->emit(recoveryPointSignal, state->recoveryPoint);
                pacedConn->setSackedHeadLostIfRackDisabled();
                pacedConn->updateInFlight();
                setRecoveryCongestionWindow();
                EV_DETAIL << " recoveryPoint=" << state->recoveryPoint;
                pacedConn->doRetransmit();
            }
        }

        if (state->sack_enabled) {
            if (state->lossRecovery) {
                EV_INFO << "Retransmission sent during recovery, restarting REXMIT timer.\n";
                restartRexmitTimer();
            }
        }
    }
    else if (state->lossRecovery && state->dupacks > state->dupthresh)
        EV_DETAIL << "Additional duplicate ACK during RACK recovery; cwnd remains "
                  << state->snd_cwnd << "\n";

    if (state->lossRecovery) {
        conn->emit(cwndSignal, state->snd_cwnd);
        if (!reactTimer->isScheduled())
            conn->scheduleAt(simTime() + state->srtt.dbl(), reactTimer);
        return;
    }

    double uVal = measureInflight(intData);
    if (uVal > 0) {
        if (!pathChanged)
            state->snd_cwnd = computeWnd(uVal, updateWindow);
        state->L = intData;
    }
    conn->emit(cwndSignal, state->snd_cwnd);
    if (state->snd_cwnd > 0) {
        uint32_t maxWindow = std::max(state->snd_cwnd, dynamic_cast<TcpPacedConnection *>(conn)->getBytesInFlight());
        uint32_t nominalBandwidth = (maxWindow / state->srtt.dbl());
        double pace = 1 / ((1.2 * (double)nominalBandwidth) / (double)state->snd_mss);
        dynamic_cast<TcpPacedConnection *>(conn)->changeIntersendingTime(pace);
    }

    sendData(false);

    if (!reactTimer->isScheduled())
        conn->scheduleAt(simTime() + state->srtt.dbl(), reactTimer);
}

void MpOrbUncoupled::processRexmitTimer(TcpEventCode& event)
{
    TcpPacedFamily::processRexmitTimer(event);
    if (event == TCP_E_ABORT)
        return;

    EV_INFO << "Begin Slow Start: resetting cwnd to " << state->snd_cwnd
            << ", ssthresh=" << state->ssthresh << "\n";

    state->afterRto = true;
    auto *pacedConnection = check_and_cast<TcpPacedConnection *>(conn);
    pacedConnection->cancelPaceTimer();
    pacedConnection->retransmitOneSegment(true);
}

} // namespace tcp
} // namespace inet
