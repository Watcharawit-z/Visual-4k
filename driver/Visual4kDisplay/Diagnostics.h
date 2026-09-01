// Makes the driver say why it failed, instead of leaving Windows to say only
// that it did.
//
// A driver whose EvtDeviceAdd returns a failure produces exactly one piece of
// evidence: problem code 31, CM_PROB_FAILED_ADD. That code says the callback
// failed. It does not say which call inside it failed, or with what status,
// and there are three candidates, so the difference between a diagnosis and a
// guess is entirely in information the driver has and throws away.
//
// Two rounds were spent guessing at this from the outside, each costing a
// release and an install on someone else's machine, and each wrong. So the
// driver now writes down what happened.
//
// Two channels, because neither is guaranteed:
//
//   * The debugger output stream, which always works but needs DebugView open
//     at the moment of failure to be seen.
//   * A registry value, which survives the failure and can be read afterwards
//     by the setup program. Whether the driver host has permission to write it
//     depends on the account it runs under, which is why setup creates the key
//     in advance and grants that account access.

#pragma once

#include <windows.h>

namespace visual4k {

// NTSTATUS spelled as the LONG it is, so this file needs none of the driver
// framework headers. Diagnostics that fail to build for a reason connected to
// what they are diagnosing are worth less than none, and the ordering those
// headers require is delicate enough to have already broken this file once.
using StatusCode = LONG;

// Where the record goes. Setup creates this key with an ACL that lets the
// driver host write to it, and reads it back after a failed start.
inline constexpr wchar_t kDiagnosticsKey[] = L"SOFTWARE\\Visual-4k";

// Records the outcome of one named step. `stage` is the call that was made,
// spelled as it appears in the source so it can be found.
//
// Called on success too: a record saying the last step reached was
// IddCxDeviceInitialize and it succeeded is what distinguishes "the driver
// never ran" from "the driver ran and Windows still produced no display".
void RecordStage(const wchar_t* stage, StatusCode status);

// Records a free-text detail alongside the stage, for facts that are numbers
// rather than outcomes.
//
// STATUS_INVALID_PARAMETER from a framework call says an argument was wrong
// and nothing about which one. The sizes of the structures being passed, and
// the version they were compiled for, are the arguments most likely to be
// wrong in a way that produces exactly that and nothing else -- and they are
// knowable from inside the driver, where the compiler has already decided
// them.
void RecordDetail(const wchar_t* name, const wchar_t* text);

}  // namespace visual4k
