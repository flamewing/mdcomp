#include "mdcomp/unreachable.hh"

#include <version>

static_assert(noexcept(utils::unreachable()));
