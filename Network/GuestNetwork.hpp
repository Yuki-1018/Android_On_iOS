#pragma once
#include <string>
namespace emu {
// libslirp outbound NAT only: no hostfwd, guestfwd or listening ADB socket.
inline std::string guestNICOption() {
    return "user,model=smc91c111,ipv6=off,net=10.0.2.0/24,host=10.0.2.2,dns=10.0.2.3,dhcpstart=10.0.2.15";
}
}
