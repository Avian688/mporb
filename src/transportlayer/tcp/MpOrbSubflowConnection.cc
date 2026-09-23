//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

#include "MpOrbSubflowConnection.h"

#include <cstring>
#include <cmath>

#include "../../../../mptcp/src/transportlayer/tcp/MpTcpConnection.h"
#include "../../../../orbtcp/src/common/PintSenderTelemetry.h"
#include "../../../../orbtcp/src/common/PintQueueingDelay.h"
#include "flavours/MpOrbUncoupled.h"

namespace inet {
namespace tcp {

Define_Module(MpOrbSubflowConnection);

namespace {
constexpr const char *MPORB_SUBFLOW_ALGORITHM = "MpOrbUncoupled";
}

const char *MpOrbSubflowConnection::getSubflowAlgorithmClass() const
{
    const char *configured = nullptr;
    if (tcpMain != nullptr)
        configured = tcpMain->par("tcpAlgorithmClass");

    if (opp_isempty(configured) || strncmp(configured, "MpTcp", 5) == 0)
        return MPORB_SUBFLOW_ALGORITHM;

    return configured;
}

void MpOrbSubflowConnection::pushIntContext(const Ptr<const TcpHeader>& tcpHeader)
{
    if (tcpHeader != nullptr && tcpHeader->findTag<IntTag>())
        intDataContextStack.push_back(tcpHeader->getTag<IntTag>()->getIntData());
    else
        intDataContextStack.emplace_back();
}

void MpOrbSubflowConnection::popIntContext()
{
    if (!intDataContextStack.empty())
        intDataContextStack.pop_back();
}

IntDataVec MpOrbSubflowConnection::getCurrentIntData() const
{
    if (intDataContextStack.empty())
        return IntDataVec();

    return intDataContextStack.back();
}

bool MpOrbSubflowConnection::openActive(L3Address localAddr, L3Address remoteAddr, int localPort, int remotePort)
{
    TcpOpenCommand *openCmd = new TcpOpenCommand();
    openCmd->setLocalAddr(localAddr);
    openCmd->setRemoteAddr(remoteAddr);
    openCmd->setLocalPort(localPort);
    openCmd->setRemotePort(remotePort);
    if (opp_isempty(openCmd->getTcpAlgorithmClass()))
        openCmd->setTcpAlgorithmClass(getSubflowAlgorithmClass());
    return processInternalCommand(TCP_C_OPEN_ACTIVE, openCmd);
}

bool MpOrbSubflowConnection::openPassive(L3Address localAddr, int localPort)
{
    TcpOpenCommand *openCmd = new TcpOpenCommand();
    openCmd->setLocalAddr(localAddr);
    openCmd->setLocalPort(localPort);
    openCmd->setFork(false);
    if (opp_isempty(openCmd->getTcpAlgorithmClass()))
        openCmd->setTcpAlgorithmClass(getSubflowAlgorithmClass());
    return processInternalCommand(TCP_C_OPEN_PASSIVE, openCmd);
}

void MpOrbSubflowConnection::setUpConnection(L3Address src, L3Address dest, int srcPort, int destPort)
{
    TcpOpenCommand *openCmd = new TcpOpenCommand();
    openCmd->setLocalAddr(dest);
    openCmd->setRemoteAddr(src);
    openCmd->setLocalPort(destPort);
    openCmd->setRemotePort(srcPort);
    if (opp_isempty(openCmd->getTcpAlgorithmClass()))
        openCmd->setTcpAlgorithmClass(getSubflowAlgorithmClass());

    initConnection(openCmd);
    state->active = false;
    state->fork = true;
    localAddr = openCmd->getRemoteAddr();
    remoteAddr = openCmd->getLocalAddr();
    localPort = openCmd->getRemotePort();
    remotePort = openCmd->getLocalPort();

    FSM_Goto(fsm, TCP_S_LISTEN);
}

TcpEventCode MpOrbSubflowConnection::processSegment1stThru8th(Packet *tcpSegment, const Ptr<const TcpHeader>& tcpHeader)
{
    pushIntContext(tcpHeader);
    auto event = SubflowConnection::processSegment1stThru8th(tcpSegment, tcpHeader);
    popIntContext();
    return event;
}

bool MpOrbSubflowConnection::processAckInEstabEtc(Packet *tcpSegment, const Ptr<const TcpHeader>& tcpHeader)
{
    pushIntContext(tcpHeader);
    bool ok = SubflowConnection::processAckInEstabEtc(tcpSegment, tcpHeader);
    popIntContext();
    return ok;
}

void MpOrbSubflowConnection::updateAckTelemetry(const Ptr<const TcpHeader>& tcpHeader)
{
    auto *orbAlgorithm = dynamic_cast<OrbtcpFamily *>(tcpAlgorithm);
    if (orbAlgorithm == nullptr)
        return;

    const auto tag = tcpHeader != nullptr ? tcpHeader->findTag<IntTag>() : nullptr;
    const IntDataVec feedback = tag != nullptr ? tag->getIntData() : IntDataVec{};
    orbAlgorithm->updateRttTelemetry(feedback);
    if (feedback.empty())
        return;

    const auto& sample = feedback.front();
    if (!sample.getSeparateQueueingDelay()) {
        hasDelayFeedback = false;
        return;
    }
    const simtime_t sampleTime = sample.getTs();
    if (sample.getB() <= 0 || !std::isfinite(sample.getPintUtilization()) ||
            sample.getPintUtilization() <= 0 || sampleTime < SIMTIME_ZERO || sampleTime > simTime() ||
            (hasDelayPath && sampleTime < delaySampleTime))
        return;

    const bool pathChanged = hasDelayPath && delayPathDigest != sample.getPathDigest();
    hasDelayPath = true;
    delayPathDigest = sample.getPathDigest();
    delaySampleTime = sampleTime;
    delayReceivedTime = simTime();
    // Do not combine the first ACK from a changed route with the old RTT
    // baseline. A subsequent fresh ACK can qualify the new route again.
    hasDelayFeedback = !pathChanged &&
            sample.getQueueingDelayCode() < pint::QUEUEING_DELAY_MAX_CODE &&
            sample.getReverseQueueingDelayCode() < pint::QUEUEING_DELAY_MAX_CODE;
    forwardQueueingDelay = pint::decodeQueueingDelay(sample.getQueueingDelayCode());
    reverseQueueingDelay = pint::decodeQueueingDelay(sample.getReverseQueueingDelayCode());
    static const simsignal_t forwardSignal = registerSignal("mpOrbForwardQueueingDelay");
    static const simsignal_t reverseSignal = registerSignal("mpOrbReverseQueueingDelay");
    emit(forwardSignal, forwardQueueingDelay);
    emit(reverseSignal, reverseQueueingDelay);
}

bool MpOrbSubflowConnection::getSchedulerForwardDelay(simtime_t& delay) const
{
    const simtime_t rtt = getSchedulingRtt();
    if (!hasDelayFeedback || state == nullptr || state->lossRecovery || state->afterRto ||
            rtt <= SIMTIME_ZERO || rtt == SIMTIME_MAX ||
            simTime() - delayReceivedTime > 2 * rtt || simTime() - delaySampleTime > 2 * rtt)
        return false;
    auto *orbAlgorithm = dynamic_cast<OrbtcpFamily *>(tcpAlgorithm);
    const simtime_t baseRtt = orbAlgorithm != nullptr ? orbAlgorithm->getEstimatedRtt() : SIMTIME_ZERO;
    if (baseRtt <= SIMTIME_ZERO)
        return false;
    // Only queueing is measured directionally. Half of the queue-corrected
    // RTT is an explicit symmetric-propagation approximation, not an oracle.
    delay = baseRtt / 2 + SimTime(forwardQueueingDelay);
    return true;
}

void MpOrbSubflowConnection::sendToIP(Packet *tcpSegment, const Ptr<TcpHeader>& tcpHeader)
{
    if (tcpSegment != nullptr && tcpHeader != nullptr && tcpSegment->getByteLength() > 0 && !tcpHeader->findTag<IntTag>()) {
        auto *orbAlg = dynamic_cast<OrbtcpFamily *>(tcpAlgorithm);
        if (orbAlg != nullptr) {
            auto intTag = tcpHeader->addTagIfAbsent<IntTag>();
            const simtime_t estimatedRtt = orbAlg->getEstimatedRtt();
            const uint32_t cwnd = orbAlg->getCwnd();
            intTag->setConnId(static_cast<unsigned long>(orbAlg->getConnId()));
            intTag->setRtt(estimatedRtt);
            intTag->setCwnd(cwnd);
            if (orbAlg->usesPintTelemetry()) {
                intTag->setRtt(orbAlg->getRtt());
                if (tcpMain->par("pintUseAverageRtt").boolValue()) {
                    intTag->setPintBaseRttCode(pint::encodeBaseRtt(estimatedRtt.dbl()));
                    intTag->setPintCwndCode(pint::encodeCwnd(cwnd));
                }
            }
            intTag->setInitialPhase(orbAlg->getInitialPhase());

            uint32_t endSeqNo = tcpHeader->getSequenceNo() + tcpSegment->getByteLength();
            if (tcpHeader->getFinBit())
                endSeqNo++;
            intTag->setRetrans(rexmitQueue != nullptr && rexmitQueue->isRetransmitted(endSeqNo));
        }
    }

    SubflowConnection::sendToIP(tcpSegment, tcpHeader);
}

void MpOrbSubflowConnection::sendIntAck(const IntDataVec& intData)
{
    const auto& tcpHeader = makeShared<TcpHeader>();

    tcpHeader->setAckBit(true);
    tcpHeader->setSequenceNo(state->snd_nxt);
    tcpHeader->setAckNo(state->rcv_nxt);
    tcpHeader->setWindow(updateRcvWnd());

    auto *tcpState = getState();
    if (tcpState != nullptr && tcpState->ect && tcpAlgorithm->shouldMarkAck()) {
        tcpHeader->setEceBit(true);
        EV_INFO << "In ecnEcho state... send ACK with ECE bit set\n";
    }

    writeHeaderOptions(tcpHeader);

    tcpHeader->addTagIfAbsent<DataAckTag>()->setDataAck(getDataAckToSend());

    auto intTag = tcpHeader->addTagIfAbsent<IntTag>();
    for (const auto& item : intData)
        intTag->getIntDataForUpdate().push_back(item);
    if (auto *orbAlgorithm = dynamic_cast<OrbtcpFamily *>(tcpAlgorithm)) {
        if (orbAlgorithm->usesPintTelemetry() && tcpMain->par("pintSeparateQueueingDelay").boolValue()) {
            for (auto& sample : intTag->getIntDataForUpdate()) {
                sample.setSeparateQueueingDelay(true);
                sample.setReverseQueueingDelayCode(0);
            }
        }
    }

    Packet *packet = new Packet("TcpAck");
    state->sndAck = true;
    sendToIP(packet, tcpHeader);
    state->sndAck = false;

    tcpAlgorithm->ackSent();
}

} // namespace tcp
} // namespace inet
