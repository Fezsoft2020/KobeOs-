#include <karm-logger/logger.h>

#include "canvas.h"

namespace Karm::Pdf {

// MARK: Context Operations ------------------------------------------------

void Canvas::push() {
    logDebug("pdf: push() operation not implemented");
}

void Canvas::pop() {
    logDebug("pdf: pop() operation not implemented");
}

void Canvas::fillStyle(Gfx::Fill) {
    logDebug("pdf: fillStyle() operation not implemented");
}

void Canvas::strokeStyle(Gfx::Stroke) {
    logDebug("pdf: strokeStyle() operation not implemented");
}

void Canvas::transform(Math::Trans2f) {
    logDebug("pdf: transform() operation not implemented");
}

// MARK: Path Operations ---------------------------------------------------

void Canvas::beginPath() {
    emit().ln("q");
}

void Canvas::closePath() {
    emit().ln("Q");
}

void Canvas::moveTo(Math::Vec2f p, Math::Path::Flags flags) {
    p = _mapPointAndUpdate(p, flags);
    emit().ln("{} {} m", p.x, p.y);
}

void Canvas::lineTo(Math::Vec2f p, Math::Path::Flags flags) {
    p = _mapPointAndUpdate(p, flags);
    emit().ln("{} {} l", p.x, p.y);
}

void Canvas::hlineTo(f64 x, Math::Path::Flags flags) {
    auto p = _mapPoint({x, 0}, flags);
    emit().ln("{} 0 l", p.x);
}

void Canvas::vlineTo(f64 y, Math::Path::Flags flags) {
    auto p = _mapPoint({0, y}, flags);
    emit().ln("0 {} l", p.y);
}

void Canvas::cubicTo(Math::Vec2f cp1, Math::Vec2f cp2, Math::Vec2f p, Math::Path::Flags flags) {
    cp1 = _mapPoint(cp1, flags);
    cp2 = _mapPoint(cp2, flags);
    p = _mapPointAndUpdate(p, flags);
    emit().ln("{} {} {} {} {} {} c", cp1.x, cp1.y, cp2.x, cp2.y, p.x, p.y);
}

void Canvas::quadTo(Math::Vec2f cp, Math::Vec2f p, Math::Path::Flags flags) {
    cp = _mapPoint(cp, flags);
    p = _mapPointAndUpdate(p, flags);
    emit().ln("{} {} {} {} q", cp.x, cp.y, p.x, p.y);
}

void Canvas::arcTo(Math::Vec2f, f64, Math::Vec2f, Math::Path::Flags) {
    notImplemented();
}

void Canvas::line(Math::Edgef line) {
    moveTo(line.start, Math::Path::DEFAULT);
    lineTo(line.end, Math::Path::DEFAULT);
}

void Canvas::curve(Math::Curvef curve) {
    moveTo(curve.a, Math::Path::DEFAULT);
    cubicTo(curve.b, curve.c, curve.d, Math::Path::DEFAULT);
}

void Canvas::rect(Math::Rectf rect, Math::Radiif) {
    moveTo(rect.topStart(), Math::Path::DEFAULT);
    lineTo(rect.topEnd(), Math::Path::DEFAULT);
    lineTo(rect.bottomEnd(), Math::Path::DEFAULT);
    lineTo(rect.bottomStart(), Math::Path::DEFAULT);
    closePath();
}

void Canvas::ellipse(Math::Ellipsef) {
    notImplemented();
}

void Canvas::path(Math::Path const &) {
    logDebug("pdf: path() operation not implemented");
}

void Canvas::fill(Gfx::FillRule) {
    logDebug("pdf: fill() operation not implemented");
}

void Canvas::fill(Gfx::Fill fill, Gfx::FillRule rule) {
    auto color = fill.unwrap<Gfx::Color>();
    emit().ln("{} {} {} rg", color.red, color.green, color.blue);
    if (rule == Gfx::FillRule::NONZERO)
        emit().ln("f");
    else
        emit().ln("f*");
}

void Canvas::stroke() {
    logDebug("pdf: stroke() operation not implemented");
}

void Canvas::stroke(Gfx::Stroke style) {
    auto color = style.fill.unwrap<Gfx::Color>();
    emit().ln("{} {} {} RG", color.red, color.green, color.blue);

    emit().ln("{} w", style.width);

    if (style.cap == Gfx::ROUND_CAP)
        emit().ln("1 J");
    else if (style.cap == Gfx::SQUARE_CAP)
        emit().ln("2 J");
    else
        emit().ln("0 J");

    if (style.join == Gfx::ROUND_JOIN)
        emit().ln("1 j");
    else if (style.join == Gfx::BEVEL_JOIN)
        emit().ln("2 j");
    else
        emit().ln("0 j");

    emit().ln("S");
}

void Canvas::clip(Gfx::FillRule) {
    logDebug("pdf: clip() operation not implemented");
}

void Canvas::apply(Gfx::Filter) {
    logDebug("pdf: apply() operation not implemented");
};

// MARK: Shape Operations --------------------------------------------------

void Canvas::fill(Text::Font &, Text::Glyph, Math::Vec2f) {
    logDebug("pdf: fill() operation not implemented");
}

// MARK: Clear Operations --------------------------------------------------

void Canvas::clear(Gfx::Color) {
    logDebug("pdf: clear() operation not implemented");
}

void Canvas::clear(Math::Recti, Gfx::Color) {
    logDebug("pdf: clear() operation not implemented");
}

// MARK: Plot Operations ---------------------------------------------------

void Canvas::plot(Math::Vec2i, Gfx::Color) {
    logDebug("pdf: plot() operation not implemented");
}

void Canvas::plot(Math::Edgei, Gfx::Color) {
    logDebug("pdf: plot() operation not implemented");
}

void Canvas::plot(Math::Recti, Gfx::Color) {
    logDebug("pdf: plot() operation not implemented");
}

// MARK: Blit Operations ---------------------------------------------------

void Canvas::blit(Math::Recti, Math::Recti, Gfx::Pixels) {
    logDebug("pdf: blit() operation not implemented");
}

} // namespace Karm::Pdf
