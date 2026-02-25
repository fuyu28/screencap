#include "image_stats.h"

namespace sc {

ImageStats ComputeImageStats(const ImageBuffer& img) {
  ImageStats s;
  if (img.width <= 0 || img.height <= 0 || img.bgra.empty()) {
    return s;
  }

  const size_t pixels = static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
  size_t black = 0;
  size_t transparent = 0;
  double luma_sum = 0.0;

  for (int y = 0; y < img.height; ++y) {
    const uint8_t* row = img.bgra.data() + static_cast<size_t>(y * img.row_pitch);
    for (int x = 0; x < img.width; ++x) {
      const uint8_t b = row[x * 4 + 0];
      const uint8_t g = row[x * 4 + 1];
      const uint8_t r = row[x * 4 + 2];
      const uint8_t a = row[x * 4 + 3];
      if (r == 0 && g == 0 && b == 0) ++black;
      if (a == 0) ++transparent;
      luma_sum += (0.2126 * r + 0.7152 * g + 0.0722 * b);
    }
  }

  s.black_ratio = static_cast<double>(black) / static_cast<double>(pixels);
  s.transparent_ratio = static_cast<double>(transparent) / static_cast<double>(pixels);
  s.avg_luma = luma_sum / static_cast<double>(pixels);
  return s;
}

}  // namespace sc
