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

#include "flavours/MpOrbFlavour.h"

namespace inet {
namespace tcp {

Define_Module(MpOrbSubflowConnection);

namespace {
constexpr const char *MPORB_SUBFLOW_ALGORITHM = "MpOrbFlavour";
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
        openCmd->setTcpAlgorithmClass(MPORB_SUBFLOW_ALGORITHM);
    return processInternalCommand(TCP_C_OPEN_ACTIVE, openCmd);
}

bool MpOrbSubflowConnection::openPassive(L3Address localAddr, int localPort)
{
    TcpOpenCommand *openCmd = new TcpOpenCommand();
    openCmd->setLocalAddr(localAddr);
    openCmd->setLocalPort(localPort);
    openCmd->setFork(false);
    if (opp_isempty(openCmd->getTcpAlgorithmClass()))
        openCmd->setTcpAlgorithmClass(MPORB_SUBFLOW_ALGORITHM);
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
        openCmd->setTcpAlgorithmClass(MPORB_SUBFLOW_ALGORITHM);

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

void MpOrbSubflowConnection::sendToIP(Packet *tcpSegment, const Ptr<TcpHeader>& tcpHeader)
{
    if (tcpSegment != nullptr && tcpHeader != nullptr && tcpSegment->getByteLength() > 0 && !tcpHeader->findTag<IntTag>()) {
        auto *orbAlg = dynamic_cast<OrbtcpFamily *>(tcpAlgorithm);
        if (orbAlg != nullptr) {
            auto intTag = tcpHeader->addTagIfAbsent<IntTag>();
            intTag->setConnId(static_cast<unsigned long>(orbAlg->getConnId()));
            intTag->setRtt(orbAlg->getEstimatedRtt());
            intTag->setCwnd(orbAlg->getCwnd());
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

    auto intTag = tcpHeader->addTagIfAbsent<IntTag>();
    for (const auto& item : intData)
        intTag->getIntDataForUpdate().push_back(item);

    Packet *packet = new Packet("TcpAck");
    state->sndAck = true;
    sendToIP(packet, tcpHeader);
    state->sndAck = false;

    tcpAlgorithm->ackSent();
}

} // namespace tcp
} // namespace inet
