// tests/drive/test_nav.py's Checkout fixture, never built: three calls of
// files::Staged::publish, the lines that case finds and pins, whatever the
// product's own tests call. clangd reads it over the analysis preset's
// database, its command taken from the nearest file's there.

#include "platform/files.hpp"
#include "support/fake_platform/fake_platform.hpp"

namespace qiven::test::nav_fixture {

void publish_three(qiven3::platform::fake::FakeStaged& first, qiven3::platform::fake::FakeStaged& second, qiven3::platform::Seconds deadline) {
    first.publish("bag-1", deadline);
    second.publish("bag-2", deadline);
    first.publish("bag-3", deadline);
}

}  // namespace qiven::test::nav_fixture
