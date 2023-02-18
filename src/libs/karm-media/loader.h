#pragma once

#include "font.h"
#include "image.h"

namespace Karm::Media {

Res<Strong<Fontface>> loadFontface(Str path);

Res<Font> loadFont(double size, Str path);

Res<Image> loadImage(Str path);

} // namespace Karm::Media
