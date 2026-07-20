//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#include "MpOrbSemiCoupledBase.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

#include "../MpOrbSubflowConnection.h"
#include "../../../../../mptcp/src/transportlayer/tcp/MpTcpConnection.h"
#include "../../../../../tcpPaced/src/transportlayer/tcp/TcpPacedConnection.h"

namespace inet {
namespace tcp {

namespace {
constexpr double DELIVERY_RATE_EWMA_GAIN = 0.125;
}

MpTcpConnection *MpOrbSemiCoupledBase::getMetaConnection() const
{
    auto *subflow = dynamic_cast<MpOrbSubflowConnection *>(conn);
    return subflow != nullptr ? subflow->getMetaConnection() : nullptr;
}

void MpOrbSemiCoupledBase::refreshDeliveryRate()
{
    if (!pathId.empty()) {
        if (!observedPathId.empty() && observedPathId != pathId) {
            smoothedDeliveryRate = 0.0;
            deliveryRateUpdatedAt = SIMTIME_ZERO;
        }
        observedPathId = pathId;
    }

    auto *pacedConnection = dynamic_cast<TcpPacedConnection *>(conn);
    if (pacedConnection == nullptr)
        return;

    const auto sample = pacedConnection->getRateSample();
    const double deliveryRate = sample.m_deliveryRate;
    if (deliveryRate <= 0.0 || sample.m_interval <= SIMTIME_ZERO ||
            !std::isfinite(deliveryRate))
        return;

    smoothedDeliveryRate = smoothedDeliveryRate > 0.0 ?
            (1.0 - DELIVERY_RATE_EWMA_GAIN) * smoothedDeliveryRate +
            DELIVERY_RATE_EWMA_GAIN * deliveryRate : deliveryRate;
    deliveryRateUpdatedAt = simTime();
}

double MpOrbSemiCoupledBase::getDeliveryRate(const MpOrbSemiCoupledBase *algorithm,
        const OrbtcpStateVariables *subflowState) const
{
    if (algorithm == nullptr || subflowState == nullptr ||
            algorithm->deliveryRateUpdatedAt == SIMTIME_ZERO ||
            subflowState->srtt <= SIMTIME_ZERO)
        return -1.0;

    double deliveryRate = algorithm->smoothedDeliveryRate;
    if (!std::isfinite(deliveryRate) || deliveryRate < 0.0)
        return -1.0;

    if (simTime() - algorithm->deliveryRateUpdatedAt > subflowState->srtt * 2)
        deliveryRate = 0.0;

    // txRate is aggregate bottleneck service measured from INT txBytes. It is
    // not a per-subflow rate, but it is a useful upper bound on a stale sample.
    if (subflowState->txRate > 0.0 && std::isfinite(subflowState->txRate))
        deliveryRate = std::min(deliveryRate, subflowState->txRate);

    return deliveryRate;
}

size_t MpOrbSemiCoupledBase::getConnId()
{
    auto *meta = getMetaConnection();
    if (meta == nullptr)
        return MpOrbUncoupled::getConnId();

    const std::string key = meta->getLocalAddressForSubflows().str() + "/" +
            std::to_string(meta->getLocalPortNumber()) + "/" +
            meta->getRemoteAddressForSubflows().str() + "/" +
            std::to_string(meta->getRemotePortNumber());
    return std::hash<std::string>{}(key);
}

} // namespace tcp
} // namespace inet
