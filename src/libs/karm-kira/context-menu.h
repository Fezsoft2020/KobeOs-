#pragma once

#include <karm-ui/input.h>

#include "_prelude.h"

namespace Karm::Kira {

Ui::Child contextMenu(Ui::Child child, Ui::Slot menu);

static inline auto contextMenu(Ui::Slot menu) {
    return [menu = std::move(menu)](Ui::Child child) mutable {
        return contextMenu(child, std::move(menu));
    };
}

Ui::Child contextMenuContent(Ui::Children children);

Ui::Child contextMenuItem(Ui::OnPress onPress, Opt<Mdi::Icon> i, Str t);

} // namespace Karm::Kira
