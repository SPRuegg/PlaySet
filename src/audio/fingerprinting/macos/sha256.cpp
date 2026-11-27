#include "sha256.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace playset::detail {
namespace {
void check(int result) {
  if (result != 1) throw std::runtime_error("Could not calculate audio fingerprint digest.");
}
}

Sha256::Sha256() { check(CC_SHA256_Init(&context_)); }

void Sha256::update(std::span<const std::uint8_t> bytes) {
  while (!bytes.empty()) {
    const auto count = std::min<std::size_t>(bytes.size(), std::numeric_limits<CC_LONG>::max());
    check(CC_SHA256_Update(&context_, bytes.data(), static_cast<CC_LONG>(count)));
    bytes = bytes.subspan(count);
  }
}
void Sha256::update(std::string_view text) {
  update(std::span(reinterpret_cast<const std::uint8_t *>(text.data()), text.size()));
}
Sha256Digest Sha256::finish() {
  Sha256Digest digest;
  check(CC_SHA256_Final(digest.data(), &context_));
  return digest;
}

} // namespace playset::detail
