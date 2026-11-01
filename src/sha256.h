#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

/// SHA-256, built in so it works on every XP service pack (CryptoAPI only has it from SP3).
class Sha256 {
public:
    Sha256();
    void Update(const void* data, size_t len);
    /// Lowercase hex digest.
    std::string HexDigest();

private:
    void Block(const uint8_t* p);
    uint32_t h_[8];
    uint8_t buf_[64];
    size_t used_ = 0;
    uint64_t total_ = 0;
};
