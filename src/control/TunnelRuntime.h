#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "control/ControlTypes.h"

namespace control {

// Adapters depend only on this lifecycle contract, not on a particular process
// or threading model.
class TunnelRuntime {
public:
    virtual ~TunnelRuntime() = default;

    virtual void start(std::string_view alias) = 0;
    virtual void stop(std::string_view alias) = 0;
    virtual void restart(std::string_view alias) = 0;
    virtual std::vector<TunnelSnapshot> snapshots() const = 0;
};

} // namespace control
