// qiven3-child's instantiation file: location's main on the Windows
// adapter, which init's children run (init.cpp), P20's two
// init runs at once. Elsewhere the child runs no location command and
// this file instantiates nothing.

#if defined(_WIN32)
#include <span>
#include <string_view>

#include "qiven/platform/windows/windows.hpp"
#include "qiven3/location_impl.hpp"

namespace qiven3 {

template int location::main<qiven::platform::win::WindowsPlatform>(std::span<const std::string_view>,
                                                            qiven::platform::win::WindowsPlatform&,
                                                            const location::Hooks&) noexcept;

}  // namespace qiven3
#endif
