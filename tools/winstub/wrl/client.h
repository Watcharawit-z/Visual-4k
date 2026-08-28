// Minimal Microsoft::WRL::ComPtr -- see tools/winstub/README.md.
#pragma once

#include <windows.h>
#include <type_traits>

namespace Microsoft {
namespace WRL {

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(T* p) : ptr_(p) { if (ptr_) ptr_->AddRef(); }
    ComPtr(const ComPtr& o) : ptr_(o.ptr_) { if (ptr_) ptr_->AddRef(); }
    ~ComPtr() { if (ptr_) ptr_->Release(); }

    ComPtr& operator=(T* p) {
        if (ptr_) ptr_->Release();
        ptr_ = p;
        if (ptr_) ptr_->AddRef();
        return *this;
    }
    ComPtr& operator=(const ComPtr& o) { return *this = o.ptr_; }

    T* Get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

    T** GetAddressOf() { return &ptr_; }
    T** ReleaseAndGetAddressOf() {
        if (ptr_) { ptr_->Release(); ptr_ = nullptr; }
        return &ptr_;
    }
    void Reset() { if (ptr_) { ptr_->Release(); ptr_ = nullptr; } }

    template <typename U>
    HRESULT As(ComPtr<U>* out) const {
        return ptr_->QueryInterface(StubIidOf<U>::value,
                                    reinterpret_cast<void**>(out->ReleaseAndGetAddressOf()));
    }

private:
    T* ptr_ = nullptr;
};

}  // namespace WRL
}  // namespace Microsoft
