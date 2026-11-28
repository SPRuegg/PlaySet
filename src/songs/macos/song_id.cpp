#include "song_id.hpp"

#include <uuid/uuid.h>

namespace playset::detail {

std::string create_song_id() {
  uuid_t uuid;
  uuid_generate_random(uuid);
  uuid_string_t text;
  uuid_unparse_lower(uuid, text);
  return text;
}

} // namespace playset::detail
