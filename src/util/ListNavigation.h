#pragma once

namespace ListNavigation {

/// Move selector to the next item, wrapping around
inline int nextItem(int current, int total) { return (current + 1) % total; }

/// Move selector to the previous item, wrapping around
inline int prevItem(int current, int total) { return (current + total - 1) % total; }

/// Skip forward by one page worth of items, wrapping around
inline int nextPage(int current, int pageSize, int total) { return ((current / pageSize + 1) * pageSize) % total; }

/// Skip backward by one page worth of items, wrapping around
inline int prevPage(int current, int pageSize, int total) {
  return ((current / pageSize - 1) * pageSize + total) % total;
}

}  // namespace ListNavigation
