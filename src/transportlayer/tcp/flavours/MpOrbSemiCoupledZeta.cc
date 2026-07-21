//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#include "MpOrbSemiCoupledZeta.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "../../../../../mptcp/src/transportlayer/tcp/MpTcpConnection.h"

namespace inet {
namespace tcp {

Register_Class(MpOrbSemiCoupledZeta);

simsignal_t MpOrbSemiCoupledZeta::pathCostSignal = cComponent::registerSignal("semiCoupledZetaPathCost");
simsignal_t MpOrbSemiCoupledZeta::pathWeightSignal = cComponent::registerSignal("semiCoupledZetaPathWeight");
simsignal_t MpOrbSemiCoupledZeta::connectionAiRateSignal = cComponent::registerSignal("semiCoupledZetaConnectionAiRate");

namespace {

struct SubflowMetrics {
    MpOrbSemiCoupledZeta *algorithm;
    double deliveryRate;
    double rtt;
    double uncoupledAiRate;
    double pathCost;
    double pathOpportunity;
};

} // namespace

void MpOrbSemiCoupledZeta::adjustAdditiveIncrease()
{
    if (state == nullptr)
        return;

    refreshDeliveryRate();
    if (firstRTT || state->initialPhase)
        return;

    MpTcpConnection *metaConnection = getMetaConnection();
    if (metaConnection == nullptr)
        return;

    std::vector<SubflowMetrics> subflows;
    double connectionRate = 0.0;
    double opportunitySum = 0.0;
    for (auto *subflow : metaConnection->getSubflows()) {
        if (subflow == nullptr)
            continue;

        const int tcpState = subflow->getFsmState();
        if (tcpState != TCP_S_ESTABLISHED && tcpState != TCP_S_CLOSE_WAIT)
            continue;

        auto *algorithm = dynamic_cast<MpOrbSemiCoupledZeta *>(subflow->getTcpAlgorithm());
        if (algorithm == nullptr)
            continue;

        const auto *subflowState = static_cast<const OrbtcpStateVariables *>(subflow->getState());
        if (subflowState == nullptr || !std::isfinite(subflowState->eta) ||
                subflowState->eta <= 0.0 || !std::isfinite(subflowState->bottBW) ||
                subflowState->bottBW <= 0.0 ||
                !std::isfinite(subflowState->additiveIncreasePercent) ||
                subflowState->additiveIncreasePercent <= 0.0 ||
                algorithm->rtt <= SIMTIME_ZERO || algorithm->pathHopMetrics.empty())
            continue;

        const double deliveryRate = getDeliveryRate(algorithm, subflowState);
        const double connectionCount = std::max(1.0,
                static_cast<double>(subflowState->sharingFlows));
        const double uncoupledAiRate = subflowState->bottBW / connectionCount *
                subflowState->additiveIncreasePercent;

        double pathCost = 0.0;
        bool validPath = true;
        for (const auto& hop : algorithm->pathHopMetrics) {
            if (hop.fairRate <= 0.0 || !std::isfinite(hop.fairRate) ||
                    !std::isfinite(hop.utilization)) {
                validPath = false;
                break;
            }
            // Every traversed hop has a base scarcity cost; load above eta raises it.
            const double loadFactor = std::max(1.0,
                    hop.utilization / subflowState->eta);
            pathCost += loadFactor / hop.fairRate;
        }

        if (!validPath || !std::isfinite(deliveryRate) || deliveryRate < 0.0 ||
                !std::isfinite(uncoupledAiRate) || uncoupledAiRate <= 0.0 ||
                !std::isfinite(pathCost) || pathCost <= 0.0)
            continue;

        const double pathOpportunity = 1.0 / pathCost;
        if (!std::isfinite(pathOpportunity) || pathOpportunity <= 0.0)
            continue;
        subflows.push_back({algorithm, deliveryRate, algorithm->rtt.dbl(),
                uncoupledAiRate, pathCost, pathOpportunity});
        connectionRate += deliveryRate;
        opportunitySum += pathOpportunity;
    }

    if (subflows.empty() || !std::isfinite(connectionRate) || connectionRate <= 0.0 ||
            !std::isfinite(opportunitySum) || opportunitySum <= 0.0)
        return;

    // Preserve one delivery-weighted AI-rate budget for the whole connection.
    double connectionAiRate = 0.0;
    for (const auto& subflow : subflows)
        connectionAiRate += subflow.deliveryRate / connectionRate * subflow.uncoupledAiRate;

    auto current = std::find_if(subflows.begin(), subflows.end(),
            [this](const SubflowMetrics& subflow) { return subflow.algorithm == this; });
    if (current == subflows.end() || !std::isfinite(connectionAiRate) ||
            connectionAiRate <= 0.0)
        return;

    const double pathWeight = current->pathOpportunity / opportunitySum;
    const long double additiveIncrease = current->rtt * connectionAiRate * pathWeight;
    if (!std::isfinite(additiveIncrease) || additiveIncrease <= 0.0)
        return;

    state->additiveIncrease = static_cast<uint32_t>(std::clamp(additiveIncrease, 1.0L,
            static_cast<long double>(std::numeric_limits<uint32_t>::max())));

    conn->emit(pathCostSignal, current->pathCost);
    conn->emit(pathWeightSignal, pathWeight);
    conn->emit(connectionAiRateSignal, connectionAiRate);
}

} // namespace tcp
} // namespace inet
