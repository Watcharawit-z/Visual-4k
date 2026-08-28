#include "CertTrust.h"

#include <windows.h>
#include <wincrypt.h>

#include <cstring>
#include <fstream>
#include <vector>

#include "Ui.h"

namespace visual4k {
namespace {

bool ReadFileBytes(const std::wstring& path, std::vector<BYTE>* out)
{
    std::ifstream file(path.c_str(), std::ios::binary | std::ios::ate);
    if (!file)
        return false;
    const std::streamoff size = file.tellg();
    if (size <= 0)
        return false;
    out->resize(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(out->data()), size);
    return file.good() || file.eof();
}

// PEM arrives as base64 between BEGIN/END lines; CryptStringToBinary strips
// the armour itself given CRYPT_STRING_BASE64HEADER.
bool DecodeIfPem(std::vector<BYTE>* bytes)
{
    static const char kMarker[] = "-----BEGIN";
    if (bytes->size() < sizeof(kMarker) - 1)
        return true;
    if (std::memcmp(bytes->data(), kMarker, sizeof(kMarker) - 1) != 0)
        return true;  // already DER

    const char* text = reinterpret_cast<const char*>(bytes->data());
    DWORD needed = 0;
    if (!CryptStringToBinaryA(text, static_cast<DWORD>(bytes->size()),
                              CRYPT_STRING_BASE64HEADER, nullptr, &needed,
                              nullptr, nullptr))
        return false;

    std::vector<BYTE> decoded(needed);
    if (!CryptStringToBinaryA(text, static_cast<DWORD>(bytes->size()),
                              CRYPT_STRING_BASE64HEADER, decoded.data(),
                              &needed, nullptr, nullptr))
        return false;

    decoded.resize(needed);
    bytes->swap(decoded);
    return true;
}

const wchar_t* const kStores[] = {L"Root", L"TrustedPublisher"};

}  // namespace

Result TrustCertificate(const std::wstring& cerPath)
{
    std::vector<BYTE> bytes;
    if (!ReadFileBytes(cerPath, &bytes))
        return Result::Failure(L"could not read " + cerPath);
    if (!DecodeIfPem(&bytes))
        return Result::Failure(L"the certificate file is neither DER nor PEM");

    for (const wchar_t* storeName : kStores) {
        HCERTSTORE store = CertOpenStore(
            CERT_STORE_PROV_SYSTEM_W, 0, 0,
            CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_OPEN_EXISTING_FLAG,
            storeName);
        if (store == nullptr) {
            return Result::Failure(std::wstring(L"could not open the machine ") +
                                   storeName + L" store: " +
                                   DescribeError(GetLastError()));
        }

        // ADD_REPLACE_EXISTING rather than ADD_NEW: re-running setup must not
        // fail merely because the certificate is already trusted.
        const BOOL added = CertAddEncodedCertificateToStore(
            store, X509_ASN_ENCODING, bytes.data(),
            static_cast<DWORD>(bytes.size()), CERT_STORE_ADD_REPLACE_EXISTING,
            nullptr);
        const DWORD error = GetLastError();
        CertCloseStore(store, 0);

        if (!added) {
            return Result::Failure(std::wstring(L"could not add the certificate to ") +
                                   storeName + L": " + DescribeError(error));
        }
        Line(std::wstring(L"  trusted in LocalMachine\\") + storeName);
    }
    return Result::Success();
}

Result UntrustCertificate(const std::wstring& cerPath)
{
    std::vector<BYTE> bytes;
    if (!ReadFileBytes(cerPath, &bytes))
        return Result::Failure(L"could not read " + cerPath);
    if (!DecodeIfPem(&bytes))
        return Result::Failure(L"the certificate file is neither DER nor PEM");

    PCCERT_CONTEXT wanted = CertCreateCertificateContext(
        X509_ASN_ENCODING, bytes.data(), static_cast<DWORD>(bytes.size()));
    if (wanted == nullptr)
        return Result::Failure(L"the certificate file could not be parsed");

    for (const wchar_t* storeName : kStores) {
        HCERTSTORE store = CertOpenStore(
            CERT_STORE_PROV_SYSTEM_W, 0, 0,
            CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_OPEN_EXISTING_FLAG,
            storeName);
        if (store == nullptr)
            continue;

        // Matching on the encoded certificate rather than on the subject name:
        // a subject match would delete any certificate that happened to share
        // the name, and the name here is not unique by construction.
        PCCERT_CONTEXT found = CertFindCertificateInStore(
            store, X509_ASN_ENCODING, 0, CERT_FIND_EXISTING, wanted, nullptr);
        if (found != nullptr) {
            CertDeleteCertificateFromStore(found);
            Line(std::wstring(L"  removed from LocalMachine\\") + storeName);
        }
        CertCloseStore(store, 0);
    }

    CertFreeCertificateContext(wanted);
    return Result::Success();
}

}  // namespace visual4k
