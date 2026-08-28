// Puts the build's signing certificate into the machine's trust stores.
//
// Windows checks a driver package against two of them, and needs it in both:
// Root, to make the certificate chain validate at all, and TrustedPublisher,
// so that installation does not stop to ask whether the publisher is trusted.
//
// The certificate this trusts was generated inside the build that produced the
// driver, and its private key was destroyed with that build's runner -- it was
// never exported, so nothing anywhere can sign anything new with it. Trusting
// it therefore permits exactly this driver and nothing else.

#pragma once

#include <string>

namespace visual4k {

struct Result {
    bool ok = false;
    std::wstring detail;

    static Result Success() { return Result{true, std::wstring()}; }
    static Result Failure(const std::wstring& why) { return Result{false, why}; }
};

// Accepts both encodings: Export-Certificate writes DER, but a certificate
// that has been through a text editor or an email is usually PEM.
Result TrustCertificate(const std::wstring& cerPath);

// Takes it back out of both stores, matched by thumbprint so that only the
// certificate in this file is removed.
Result UntrustCertificate(const std::wstring& cerPath);

}  // namespace visual4k
