// `UiTestbed --selftest`: juce::UnitTest cases for the UI's pure logic.
#pragma once

#include <juce_core/juce_core.h>

namespace t3k::ui::testbed {

// Runs every "ui" unit test (or, with `only`, those whose name contains
// it: `UiTestbed --selftest "Rig view"`); 0 when all pass.
int runSelfTests(const juce::String& only = {});

}  // namespace t3k::ui::testbed
