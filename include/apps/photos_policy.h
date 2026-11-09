#pragma once
#include <stdint.h>

namespace apps {
namespace photos {
namespace policy {

enum class Action { Delete, Escape };

inline int moveIndex(int index, int delta, int count) {
    if (count <= 0) return 0;
    const int64_t next = (int64_t(index) + delta) % count;
    return int(next < 0 ? next + count : next);
}


constexpr Action moveAction(Action current, int delta) {
    return delta < 0 ? Action::Delete : delta > 0 ? Action::Escape : current;
}

constexpr int indexAfterRemoval(int index, int remaining) {
    return remaining <= 0 ? 0 : index >= remaining ? remaining - 1 : index;
}

}  // namespace policy
}  // namespace photos
}  // namespace apps
