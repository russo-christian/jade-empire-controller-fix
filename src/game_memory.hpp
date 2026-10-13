#pragma once

#include <cstddef>
#include <cstdint>

// Offsets and virtual slots refer to the signature-checked 32-bit game build.
using Byte = unsigned char;
template <class T> static T& field(void* object, size_t offset) {
    return *reinterpret_cast<T*>(static_cast<Byte*>(object) + offset);
}
template <class F> static F method(void* object, size_t offset) {
    return reinterpret_cast<F>(field<uintptr_t>(field<void*>(object, 0), offset));
}
