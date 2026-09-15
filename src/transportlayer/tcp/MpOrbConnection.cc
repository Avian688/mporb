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

#include "MpOrbConnection.h"

#include "flavours/MpOrbPressure.h"
#include "flavours/MpOrbPressurePolicy.h"
#include "../../../../orbtcp/src/common/PintFlowCount.h"

namespace inet {
namespace tcp {

Define_Module(MpOrbConnection);

namespace {
constexpr const char *MPORB_META_ALGORITHM = "MpTcpMetaCubic";
}

void MpOrbConnection::process_OPEN_ACTIVE(TcpEventCode& event, TcpCommand *tcpCommand, cMessage *msg)
{
    auto *openCmd = check_and_cast<TcpOpenCommand *>(tcpCommand);
    // MpOrb runs the Orb flavour on the subflows; keep the meta connection on the
    // lightweight MPTCP meta algorithm regardless of the user-facing tcpAlgorithmClass.
    openCmd->setTcpAlgorithmClass(MPORB_META_ALGORITHM);

    MpTcpConnection::process_OPEN_ACTIVE(event, tcpCommand, msg);
}

void MpOrbConnection::process_OPEN_PASSIVE(TcpEventCode& event, TcpCommand *tcpCommand, cMessage *msg)
{
    auto *openCmd = check_and_cast<TcpOpenCommand *>(tcpCommand);
    // Passive meta sockets use the same meta-side algorithm as active ones.
    openCmd->setTcpAlgorithmClass(MPORB_META_ALGORITHM);

    MpTcpConnection::process_OPEN_PASSIVE(event, tcpCommand, msg);
}

void MpOrbConnection::recordPressureFeedback(SubflowConnection *subflow,
        const IntMetaData& feedback, int flowCountBits, int maxFlowCount)
{
    if (subflow == nullptr)
        return;
    const uint32_t flows = pint::decodeFlowCount(feedback.getPintTotalFlowCountCode(),
            flowCountBits, maxFlowCount);
    if (!mporbpressure::positiveFinite(mporbpressure::fairRate(feedback.getB(), flows)))
        return;
    pressureFeedback[subflow->getId()] = {
        static_cast<double>(feedback.getB()), flows, feedback.getTs(), simTime()
    };
}

MpOrbConnection::PressureAllocation MpOrbConnection::getPressureAllocation(
        const SubflowConnection *requester) const
{
    PressureAllocation allocation;
    mporbpressure::RateBudget budget;
    for (auto *subflow : getSubflows()) {
        if (subflow == nullptr || !subflow->isTransportActiveForScheduler() ||
                subflow->isSchedulerStale())
            continue;
        const auto *algorithm = dynamic_cast<MpOrbPressure *>(subflow->getTcpAlgorithm());
        if (algorithm == nullptr || !algorithm->isPressureCouplingReady())
            continue;
        const auto entry = pressureFeedback.find(subflow->getId());
        if (entry == pressureFeedback.end())
            continue;
        const auto& feedback = entry->second;
        const auto *subflowState = static_cast<const OrbtcpStateVariables *>(subflow->getState());
        if (!mporbpressure::fresh(simTime().dbl(), feedback.sampledAt.dbl(),
                feedback.receivedAt.dbl(), subflowState->srtt.dbl()))
            continue;
        const double rate = algorithm->getPressureRateEstimate();
        const double fairRate = mporbpressure::fairRate(feedback.bandwidth, feedback.flows);
        if (!budget.add(rate, fairRate))
            continue;
        if (subflow == requester) {
            allocation.fairRate = fairRate;
            allocation.subflowRate = rate;
        }
    }
    // A missing/stale requester falls back to its uncoupled update. An idle
    // sibling cannot indefinitely dilute the connection's rate estimate.
    const auto share = budget.allocate(allocation.subflowRate);
    allocation.weight = share.weight;
    allocation.connectionRate = budget.totalRate;
    allocation.weightedFairRate = share.weightedFairRate;
    allocation.freshSubflows = budget.paths;
    return allocation;
}

void MpOrbConnection::forgetPressureFeedback(const SubflowConnection *subflow)
{
    if (subflow != nullptr)
        pressureFeedback.erase(subflow->getId());
}

void MpOrbConnection::removeSubflow(SubflowConnection *subflow)
{
    forgetPressureFeedback(subflow);
    MpTcpConnection::removeSubflow(subflow);
}

} // namespace tcp
} // namespace inet
