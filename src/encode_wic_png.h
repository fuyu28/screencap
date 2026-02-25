#pragma once

#include "common.h"

namespace sc {

bool SavePngWic(const ImageBuffer &img, const std::wstring &out_path,
                bool overwrite, ErrorInfo *err);

} // namespace sc
