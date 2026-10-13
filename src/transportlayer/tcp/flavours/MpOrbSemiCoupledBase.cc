//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//

#include "MpOrbSemiCoupledBase.h"

#include <functional>
#include <string>

#include "../MpOrbSubflowConnection.h"
#include "../../../../../mptcp/src/transportlayer/tcp/MpTcpConnection.h"

namespace inet {
namespace tcp {

MpTcpConnection *MpOrbSemiCoupledBase::getMetaConnection() const
{
    auto *subflow = dynamic_cast<MpOrbSubflowConnection *>(conn);
    return subflow != nullptr ? subflow->getMetaConnection() : nullptr;
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
