#pragma once

#include "audio/fingerprinting/stem_fingerprint.hpp"

#include <CommonCrypto/CommonDigest.h>

#include <span>
#include <string_view>

namespace playset::detail {

class Sha256 {
public:
  Sha256();
  void update(std::span<const std::uint8_t> bytes);
  void update(std::string_view text);
  Sha256Digest finish();

private:
  CC_SHA256_CTX context_{};
};

} // namespace playset::detail
