// names' unit for the analysis preset: list, remove and rename on the
// fake's system, the one System beside the Windows adapter's.

#include "platform/names.hpp"

#include "support/fake_platform/fake_files.hpp"

namespace qiven3::platform::files {

using Fake = fake::FakeFiles;

template void list(Fake&, std::string_view, Vector<String>&);
template void remove(Fake&, std::string_view);
template void rename(Fake&, std::string_view, std::string_view, Seconds);

}  // namespace qiven3::platform::files
