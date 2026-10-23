// files' unit for the analysis preset: the durable writes and the lock on
// the fake's system, the one System the code gives them yet, every member
// of each handle instantiated.

#include "platform/files.hpp"

#include "support/fake_platform/fake_files.hpp"

namespace qiven3::platform::files {

using Fake = fake::FakeFiles;

template class Sink<Fake>;
template class Staged<Fake>;
template class Lock<Fake>;
template void check(Fake&, std::string_view, std::string_view, bool);
template Digits drawn(Fake&);
template void replace(Fake&, std::string_view, std::string_view, Seconds);
template Staged<Fake> stage(Fake&, std::string_view);
template Lock<Fake> lock(Fake&, std::string_view, Seconds);

}  // namespace qiven3::platform::files
