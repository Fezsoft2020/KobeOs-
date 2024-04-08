#pragma once

#include <karm-base/checked.h>
#include <karm-base/ctype.h>
#include <karm-base/string.h>

#include "sscan.h"

namespace Karm::Re {

template <typename T>
concept Expr = requires(T expr, Io::SScan &scan) {
    { expr(scan) } -> Meta::Same<bool>;
};

/// Match a string against an Expr
///
/// Returns
///  - Match::YES : If the Expr match the whole string
///  - Match::NO : If the Expr doesn't match the string
///  - Match::PARTIAL : If the Expr matches but the end of string is not reached
template <StaticEncoding E>
Match match(Re::Expr auto expr, _Str<E> input) {
    Io::_SScan<E> scan(input);
    if (not expr(scan))
        return Match::NO;
    return scan.ended()
               ? Match::YES
               : Match::PARTIAL;
}

/* --- Combinators ---------------------------------------------------------- */

/// Returns true if either of the expressions match pass as parameters
inline constexpr Expr auto either(Expr auto... exprs) {
    return [=](auto &scan) {
        return (exprs(scan) or ...);
    };
}

inline constexpr Expr auto operator|(Expr auto a, Expr auto b) {
    return either(a, b);
}

/// Returns true if all of the expressions match.
/// The expressions are evaluated in order.
/// If any expression fails, the scanner is rewound to the state before the first expression.
inline constexpr Expr auto chain(Expr auto... exprs) {
    return [=](auto &scan) {
        auto saved = scan;
        if ((exprs(scan) and ...))
            return true;
        scan = saved;
        return false;
    };
}

inline constexpr Expr auto operator&(Expr auto a, Expr auto b) {
    return chain(a, b);
}

/// Inverts the result of the expression.
inline constexpr Expr auto negate(Expr auto expr) {
    return [=](auto &scan) {
        auto saved = scan;
        if (not expr(scan)) {
            if (scan.ended())
                return false;
            scan.next();
            return true;
        }
        scan = saved;
        return false;
    };
}

inline constexpr Expr auto operator~(Expr auto expr) {
    return negate(expr);
}

/// Consumes until the expression matches or the end of the input is reached.
/// scanner is rewound to the last unmatched rune.
inline constexpr Expr auto until(Expr auto expr) {
    return [=](auto &scan) {
        auto saved = scan;
        while (not expr(scan) and not scan.ended()) {
            scan.next();
            saved = scan;
        }
        scan = saved;
        return true;
    };
}

/// Consumes until the expression matches or the end of the input is reached.
inline constexpr Expr auto untilAndConsume(Expr auto expr) {
    return [=](auto &scan) {
        while (not expr(scan) and not scan.ended())
            scan.next();
        return true;
    };
}

/* --- Quantifiers ---------------------------------------------------------- */

/// Try to match an expression `atLeast` times and and stops when `atMost` times
/// is reached, return true if atLeast is reached otherwise rewind the scanner.
inline constexpr Expr auto nOrN(usize atLeast, usize atMost, Expr auto expr) {
    return [=](auto &scan) {
        usize count = 0;
        auto saved = scan;
        saved = scan;
        while (expr(scan)) {
            count++;
            saved = scan;
            if (count == atMost)
                break;
        }
        if (count >= atLeast) {
            scan = saved;
            return true;
        }
        return false;
    };
}

/// Returns true if the expression matches exactly n times.
inline constexpr Expr auto exactly(usize n, Expr auto expr) {
    return nOrN(n, n, expr);
}

/// Returns true if the expression matches at least n times.
inline constexpr Expr auto atLeast(usize n, Expr auto expr) {
    return nOrN(n, MAX<usize>, expr);
}

/// Returns true if the expression matches at most n times.
inline constexpr Expr auto atMost(usize n, Expr auto expr) {
    return nOrN(0, n, expr);
}

/// Returns true if the expression matches zero or more times.
inline constexpr Expr auto zeroOrMore(Expr auto expr) {
    return nOrN(0, MAX<usize>, expr);
}

/// Returns true if the expression matches one or more times.
inline constexpr Expr auto oneOrMore(Expr auto expr) {
    return nOrN(1, MAX<usize>, expr);
}

/// Returns true if the expression matches zero or one times.
inline constexpr Expr auto zeroOrOne(Expr auto expr) {
    return nOrN(0, 1, expr);
}

// Returns true if the expression matches at most n times.
// If the expression matches more than n times, the scanner is rewound to the start of the first match.
inline constexpr Expr auto atMost(usize n, auto expr) {
    return nOrN(0, n, expr);
}

/// Returns true if the expression matches exactly one time and saves the result.
inline Expr auto token(Str &out, Expr auto expr) {
    return [=, &out](auto &scan) {
        scan.begin();
        if (expr(scan)) {
            out = scan.end();
            return true;
        }
        return false;
    };
}

/// If the expression matches, the callback is called with the matching scanner.
/// The scanner is rewound to the state before the expression was matched.
inline Expr auto trap(Expr auto expr, auto cb) {
    return [=](auto &scan) {
        auto saved = scan;
        if (expr(scan)) {
            scan = saved;
            cb(saved);
            return false;
        }
        return true;
    };
}

/* --- Tokens --------------------------------------------------------------- */

/// Match nothing and return true.
inline constexpr Expr auto nothing() {
    return [](auto &) {
        return true;
    };
}

inline constexpr Expr auto any() {
    return [](auto &scan) {
        scan.next();
        return true;
    };
}

/// Match the end of the input.
inline constexpr Expr auto eof() {
    return [](auto &scan) {
        return scan.ended();
    };
}

/// Match a word and consume it.
inline constexpr Expr auto word(Str word) {
    return [=](auto &scan) {
        return scan.skip(word);
    };
}

/// Match a single character and consume it.
/// Multiple characters can be passed to match any of them.
inline constexpr Expr auto single(auto... c) {
    return [=](auto &scan) {
        if (((scan.curr() == (Rune)c) or ...)) {
            scan.next();
            return true;
        }
        return false;
    };
}

/// Match a single character against a ctype function and consume it.
inline constexpr Expr auto ctype(CType auto ctype) {
    return [=](auto &scan) {
        if (ctype(scan.curr())) {
            scan.next();
            return true;
        }
        return false;
    };
}

/// Match a character range and consume it if it lies within the range.
inline constexpr Expr auto range(Rune start, Rune end) {
    return [=](auto &scan) {
        if (scan.curr() >= start and scan.curr() <= end) {
            scan.next();
            return true;
        }
        return false;
    };
}

/* --- Posix Classes -------------------------------------------------------- */

/// Match an ASCII Character and consume it.
inline constexpr Expr auto ascii() {
    return ctype(isAscii);
}

/// Match an ASCII upper case letter and consume it.
inline constexpr Expr auto upper() {
    return ctype(isAsciiUpper);
}

/// Match an ASCII lower case letter and consume it.
inline constexpr Expr auto lower() {
    return ctype(isAsciiLower);
}

/// Match an ASCII letter and consume it.
inline constexpr Expr auto alpha() {
    return upper() | lower();
}

/// Match an ASCII digit and consume it.
inline constexpr Expr auto digit() {
    return ctype(isAsciiDecDigit);
}

/// Match an ASCII hexadecimal digit and consume it.
inline constexpr Expr auto xdigit() {
    return ctype(isAsciiHexDigit);
}

/// Match an ASCII alphanumeric character and consume it.
inline constexpr Expr auto alnum() {
    return ctype(isAsciiAlphaNum);
}

/// Match a work made of ASCII letters and underscores and consume it.
inline constexpr Expr auto word() {
    return alnum() | single('_');
}

/// Match punctuation and consume it.
inline constexpr Expr auto punct() {
    return single(
        '!', '"', '#', '$', '%', '&', '\'', '(',
        ')', '*', '+', ',', '-', '.', '/', ':',
        ';', '<', '=', '>', '?', '@', '[', '\\',
        ']', '^', '_', '`', '{', '|', '}', '~'
    );
}

/// Match ascii whitespace and consume it.
inline constexpr Expr auto space() {
    return single(' ', '\t', '\n', '\r');
}

/// Match a blank space and consume it.
inline constexpr Expr auto blank() {
    return single(' ', '\t');
}

/* --- Utils ---------------------------------------------------------------- */

/// Match a separator and consume it.
/// A separator is a expr surrounded by spaces.
inline constexpr Expr auto separator(Expr auto expr) {
    return zeroOrMore(space()) &
           expr &
           zeroOrMore(space());
}

/// Match an optional separator and consume it.
/// A separator is an expression surrounded by spaces.
/// If the separator is not found, the expression still matches.
/// And whitespaces are consumed.
inline constexpr Expr auto optSeparator(Expr auto expr) {
    return zeroOrMore(space()) &
           zeroOrOne(expr) &
           zeroOrMore(space());
}

} // namespace Karm::Re

inline constexpr Karm::Re::Expr auto operator""_re(char const *str, usize len) {
    return Karm::Re::word(Str{str, len});
}

inline constexpr Karm::Re::Expr auto operator""_re(char c) {
    return Karm::Re::single(c);
}
