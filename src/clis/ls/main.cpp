#include <karm-main/main.h>
#include <karm-sys/dir.h>

Res<> entryPoint(CliArgs) {
    Sys::Dir dir = try$(Sys::Dir::open("."));
    for (auto const &entry : dir.entries()) {
        Sys::println(entry.name);
    }
    return Ok();
}
