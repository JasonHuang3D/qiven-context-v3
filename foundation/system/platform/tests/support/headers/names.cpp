// names' unit for the analysis preset: list, remove and rename on the
// fake's system, the one System beside the Windows adapter's.

#include "qiven/platform/names.hpp"

#include "qiven/platform/fake/fake_files.hpp"

namespace qiven::platform::files {

using Fake = fake::FakeFiles;

template void list(Fake&, std::string_view, Vector<String>&);
template void remove(Fake&, std::string_view);
template void rename(Fake&, std::string_view, std::string_view, Seconds);

}  // namespace qiven::platform::files
