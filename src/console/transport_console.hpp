#pragma once

#include <iosfwd>

namespace playset {
class Transport;

void run_transport_console(Transport &transport, std::istream &commands,
                           std::ostream &output, std::ostream &errors);

} // namespace playset
