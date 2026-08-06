// No-op status_display for every board without a display -- see
// status_display.h for the build_src_filter reasoning.
#include "status_display.h"

void statusDisplayBegin() {}

void statusDisplayShowIP(const String &ip) {
  (void)ip;
}
