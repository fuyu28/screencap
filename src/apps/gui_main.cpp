#include "gui/gui.h"

#include <shellscalingapi.h>

int main() {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  return sc::RunGui();
}
