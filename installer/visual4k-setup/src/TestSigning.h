// The boot flag that lets Windows load a driver signed by a certificate that
// no public authority vouches for.
//
// Without it Windows refuses this driver with code 52 no matter how correctly
// it is signed and installed. With it, Windows will load *any* driver signed
// by a certificate in the machine's trust stores -- which is why turning it on
// is a decision the user makes explicitly, and why setup offers to turn it
// back off again afterwards.

#pragma once

namespace visual4k {

enum class TestSigningState { Off, On, Unknown };

TestSigningState QueryTestSigning();

// Both shell out to bcdedit, which is the only supported way to change this.
// They take effect on the next boot.
bool EnableTestSigning();
bool DisableTestSigning();

// Reboots after a countdown the user can cancel.
bool RebootNow();

}  // namespace visual4k
