// qiven3-child's instantiation file: location's main on the Windows
// adapter, which platform init's children run (platform.cpp), P20's two
// init runs at once. Elsewhere the child runs no location command and
// this file instantiates nothing.

#if defined(_WIN32)
#include <span>
#include <string_view>

#include "platform/win/windows.hpp"
#include "qiven3/location_impl.hpp"

namespace qiven3 {

template int location::main<platform::win::WindowsPlatform>(std::span<const std::string_view>,
                                                            platform::win::WindowsPlatform&,
                                                            const location::Hooks&) noexcept;

}  // namespace qiven3
#endif
