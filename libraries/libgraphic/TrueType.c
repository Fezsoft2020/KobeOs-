// Base on stb truetype.

#include <libgraphic/TrueType.h>
#include <libmath/math.h>
#include <libsystem/Assert.h>
#include <libsystem/CString.h>

#define STBTT_MAX_OVERSAMPLE 8

static_assert(STBTT_MAX_OVERSAMPLE < 256, "STBTT_MAX_OVERSAMPLE cannot be > 255");
static_assert((STBTT_MAX_OVERSAMPLE & (STBTT_MAX_OVERSAMPLE - 1)) == 0, "Test oversample pow2");

//////////////////////////////////////////////////////////////////////////
//
// stbtt__buf helpers to parse data from file
//

static uint8_t stbtt__buf_get8(stbtt__buf *b)
{
    if (b->cursor >= b->size)
        return 0;

    return b->data[b->cursor++];
}

static uint8_t stbtt__buf_peek8(stbtt__buf *b)
{
    if (b->cursor >= b->size)
        return 0;

    return b->data[b->cursor];
}

static void stbtt__buf_seek(stbtt__buf *b, int o)
{
    assert(!(o > b->size || o < 0));
    b->cursor = (o > b->size || o < 0) ? b->size : o;
}

static void stbtt__buf_skip(stbtt__buf *b, int o)
{
    stbtt__buf_seek(b, b->cursor + o);
}

static uint32_t stbtt__buf_get(stbtt__buf *b, int n)
{
    assert(n >= 1 && n <= 4);

    uint32_t v = 0;

    for (int i = 0; i < n; i++)
    {
        v = (v << 8) | stbtt__buf_get8(b);
    }

    return v;
}

static stbtt__buf stbtt__new_buf(const void *p, size_t size)
{
    assert(size < 0x40000000);

    return (stbtt__buf){
        .data = (uint8_t *)p,
        .size = (int)size,
        .cursor = 0,
    };
}

#define stbtt__buf_get16(b) stbtt__buf_get((b), 2)
#define stbtt__buf_get32(b) stbtt__buf_get((b), 4)

static stbtt__buf stbtt__buf_range(const stbtt__buf *b, int o, int s)
{
    stbtt__buf r = stbtt__new_buf(NULL, 0);

    if (o < 0 || s < 0 || o > b->size || s > b->size - o)
        return r;

    r.data = b->data + o;
    r.size = s;

    return r;
}

static stbtt__buf stbtt__cff_get_index(stbtt__buf *b)
{
    int count, start, offsize;
    start = b->cursor;
    count = stbtt__buf_get16(b);
    if (count)
    {
        offsize = stbtt__buf_get8(b);
        assert(offsize >= 1 && offsize <= 4);
        stbtt__buf_skip(b, offsize * count);
        stbtt__buf_skip(b, stbtt__buf_get(b, offsize) - 1);
    }
    return stbtt__buf_range(b, start, b->cursor - start);
}

static uint32_t stbtt__cff_int(stbtt__buf *b)
{
    int b0 = stbtt__buf_get8(b);
    if (b0 >= 32 && b0 <= 246)
        return b0 - 139;
    else if (b0 >= 247 && b0 <= 250)
        return (b0 - 247) * 256 + stbtt__buf_get8(b) + 108;
    else if (b0 >= 251 && b0 <= 254)
        return -(b0 - 251) * 256 - stbtt__buf_get8(b) - 108;
    else if (b0 == 28)
        return stbtt__buf_get16(b);
    else if (b0 == 29)
        return stbtt__buf_get32(b);

    ASSERT_NOT_REACHED();
}

static void stbtt__cff_skip_operand(stbtt__buf *b)
{
    int v, b0 = stbtt__buf_peek8(b);
    assert(b0 >= 28);
    if (b0 == 30)
    {
        stbtt__buf_skip(b, 1);
        while (b->cursor < b->size)
        {
            v = stbtt__buf_get8(b);
            if ((v & 0xF) == 0xF || (v >> 4) == 0xF)
                break;
        }
    }
    else
    {
        stbtt__cff_int(b);
    }
}

static stbtt__buf stbtt__dict_get(stbtt__buf *b, int key)
{
    stbtt__buf_seek(b, 0);
    while (b->cursor < b->size)
    {
        int start = b->cursor, end, op;
        while (stbtt__buf_peek8(b) >= 28)
            stbtt__cff_skip_operand(b);
        end = b->cursor;
        op = stbtt__buf_get8(b);
        if (op == 12)
            op = stbtt__buf_get8(b) | 0x100;
        if (op == key)
            return stbtt__buf_range(b, start, end - start);
    }
    return stbtt__buf_range(b, 0, 0);
}

static void stbtt__dict_get_ints(stbtt__buf *b, int key, int outcount, uint32_t *out)
{
    int i;
    stbtt__buf operands = stbtt__dict_get(b, key);
    for (i = 0; i < outcount && operands.cursor < operands.size; i++)
        out[i] = stbtt__cff_int(&operands);
}

static int stbtt__cff_index_count(stbtt__buf *b)
{
    stbtt__buf_seek(b, 0);
    return stbtt__buf_get16(b);
}

static stbtt__buf stbtt__cff_index_get(stbtt__buf b, int i)
{
    int count, offsize, start, end;
    stbtt__buf_seek(&b, 0);
    count = stbtt__buf_get16(&b);
    offsize = stbtt__buf_get8(&b);
    assert(i >= 0 && i < count);
    assert(offsize >= 1 && offsize <= 4);
    stbtt__buf_skip(&b, i * offsize);
    start = stbtt__buf_get(&b, offsize);
    end = stbtt__buf_get(&b, offsize);
    return stbtt__buf_range(&b, 2 + (count + 1) * offsize + start, end - start);
}

//////////////////////////////////////////////////////////////////////////
//
// accessors to parse data from file
//

// on platforms that don't allow misaligned reads, if we want to allow
// truetype fonts that aren't padded to alignment, define ALLOW_UNALIGNED_TRUETYPE

#define ttBYTE(p) (*(uint8_t *)(p))
#define ttCHAR(p) (*(int8_t *)(p))
#define ttFixed(p) ttLONG(p)

static uint16_t ttUSHORT(uint8_t *p)
{
    return p[0] * 256 + p[1];
}
static int16_t ttSHORT(uint8_t *p) { return p[0] * 256 + p[1]; }
static uint32_t ttULONG(uint8_t *p) { return (p[0] << 24) + (p[1] << 16) + (p[2] << 8) + p[3]; }
static int32_t ttLONG(uint8_t *p) { return (p[0] << 24) + (p[1] << 16) + (p[2] << 8) + p[3]; }

#define stbtt_tag4(p, c0, c1, c2, c3) ((p)[0] == (c0) && (p)[1] == (c1) && (p)[2] == (c2) && (p)[3] == (c3))
#define stbtt_tag(p, str) stbtt_tag4(p, str[0], str[1], str[2], str[3])

static int stbtt__isfont(uint8_t *font)
{
    // check the version number
    if (stbtt_tag4(font, '1', 0, 0, 0))
        return 1; // TrueType 1

    if (stbtt_tag(font, "typ1"))
        return 1; // TrueType with type 1 font -- we don't support this!

    if (stbtt_tag(font, "OTTO"))
        return 1; // OpenType with CFF

    if (stbtt_tag4(font, 0, 1, 0, 0))
        return 1; // OpenType 1.0

    if (stbtt_tag(font, "true"))
        return 1; // Apple specification for TrueType fonts

    return 0;
}

// @OPTIMIZE: binary search
static uint32_t stbtt__find_table(uint8_t *data, uint32_t fontstart, const char *tag)
{
    int32_t num_tables = ttUSHORT(data + fontstart + 4);
    uint32_t tabledir = fontstart + 12;
    int32_t i;
    for (i = 0; i < num_tables; ++i)
    {
        uint32_t loc = tabledir + 16 * i;
        if (stbtt_tag(data + loc + 0, tag))
            return ttULONG(data + loc + 8);
    }
    return 0;
}

static int stbtt_GetFontOffsetForIndex_internal(unsigned char *font_collection, int index)
{
    // if it's just a font, there's only one valid index
    if (stbtt__isfont(font_collection))
        return index == 0 ? 0 : -1;

    // check if it's a TTC
    if (stbtt_tag(font_collection, "ttcf"))
    {
        // version 1?
        if (ttULONG(font_collection + 4) == 0x00010000 || ttULONG(font_collection + 4) == 0x00020000)
        {
            int32_t n = ttLONG(font_collection + 8);
            if (index >= n)
                return -1;
            return ttULONG(font_collection + 12 + index * 4);
        }
    }
    return -1;
}

static int stbtt_GetNumberOfFonts_internal(unsigned char *font_collection)
{
    // if it's just a font, there's only one valid font
    if (stbtt__isfont(font_collection))
        return 1;

    // check if it's a TTC
    if (stbtt_tag(font_collection, "ttcf"))
    {
        // version 1?
        if (ttULONG(font_collection + 4) == 0x00010000 || ttULONG(font_collection + 4) == 0x00020000)
        {
            return ttLONG(font_collection + 8);
        }
    }
    return 0;
}

static stbtt__buf stbtt__get_subrs(stbtt__buf cff, stbtt__buf fontdict)
{
    uint32_t subrsoff = 0, private_loc[2] = {0, 0};
    stbtt__buf pdict;
    stbtt__dict_get_ints(&fontdict, 18, 2, private_loc);
    if (!private_loc[1] || !private_loc[0])
        return stbtt__new_buf(NULL, 0);
    pdict = stbtt__buf_range(&cff, private_loc[1], private_loc[0]);
    stbtt__dict_get_ints(&pdict, 19, 1, &subrsoff);
    if (!subrsoff)
        return stbtt__new_buf(NULL, 0);
    stbtt__buf_seek(&cff, private_loc[1] + subrsoff);
    return stbtt__cff_get_index(&cff);
}

// since most people won't use this, find this table the first time it's needed
static int stbtt__get_svg(stbtt_fontinfo *info)
{
    uint32_t t;
    if (info->svg < 0)
    {
        t = stbtt__find_table(info->data, info->fontstart, "SVG ");
        if (t)
        {
            uint32_t offset = ttULONG(info->data + t + 2);
            info->svg = t + offset;
        }
        else
        {
            info->svg = 0;
        }
    }
    return info->svg;
}

static int stbtt_InitFont_internal(stbtt_fontinfo *info, unsigned char *data, int fontstart)
{
    uint32_t cmap, t;
    int32_t i, numTables;

    info->data = data;
    info->fontstart = fontstart;
    info->cff = stbtt__new_buf(NULL, 0);

    cmap = stbtt__find_table(data, fontstart, "cmap");       // required
    info->loca = stbtt__find_table(data, fontstart, "loca"); // required
    info->head = stbtt__find_table(data, fontstart, "head"); // required
    info->glyf = stbtt__find_table(data, fontstart, "glyf"); // required
    info->hhea = stbtt__find_table(data, fontstart, "hhea"); // required
    info->hmtx = stbtt__find_table(data, fontstart, "hmtx"); // required
    info->kern = stbtt__find_table(data, fontstart, "kern"); // not required
    info->gpos = stbtt__find_table(data, fontstart, "GPOS"); // not required

    if (!cmap || !info->head || !info->hhea || !info->hmtx)
        return 0;
    if (info->glyf)
    {
        // required for truetype
        if (!info->loca)
            return 0;
    }
    else
    {
        // initialization for CFF / Type2 fonts (OTF)
        stbtt__buf b, topdict, topdictidx;
        uint32_t cstype = 2, charstrings = 0, fdarrayoff = 0, fdselectoff = 0;
        uint32_t cff;

        cff = stbtt__find_table(data, fontstart, "CFF ");
        if (!cff)
            return 0;

        info->fontdicts = stbtt__new_buf(NULL, 0);
        info->fdselect = stbtt__new_buf(NULL, 0);

        // @TODO this should use size from table (not 512MB)
        info->cff = stbtt__new_buf(data + cff, 512 * 1024 * 1024);
        b = info->cff;

        // read the header
        stbtt__buf_skip(&b, 2);
        stbtt__buf_seek(&b, stbtt__buf_get8(&b)); // hdrsize

        // @TODO the name INDEX could list multiple fonts,
        // but we just use the first one.
        stbtt__cff_get_index(&b); // name INDEX
        topdictidx = stbtt__cff_get_index(&b);
        topdict = stbtt__cff_index_get(topdictidx, 0);
        stbtt__cff_get_index(&b); // string INDEX
        info->gsubrs = stbtt__cff_get_index(&b);

        stbtt__dict_get_ints(&topdict, 17, 1, &charstrings);
        stbtt__dict_get_ints(&topdict, 0x100 | 6, 1, &cstype);
        stbtt__dict_get_ints(&topdict, 0x100 | 36, 1, &fdarrayoff);
        stbtt__dict_get_ints(&topdict, 0x100 | 37, 1, &fdselectoff);
        info->subrs = stbtt__get_subrs(b, topdict);

        // we only support Type 2 charstrings
        if (cstype != 2)
            return 0;
        if (charstrings == 0)
            return 0;

        if (fdarrayoff)
        {
            // looks like a CID font
            if (!fdselectoff)
                return 0;
            stbtt__buf_seek(&b, fdarrayoff);
            info->fontdicts = stbtt__cff_get_index(&b);
            info->fdselect = stbtt__buf_range(&b, fdselectoff, b.size - fdselectoff);
        }

        stbtt__buf_seek(&b, charstrings);
        info->charstrings = stbtt__cff_get_index(&b);
    }

    t = stbtt__find_table(data, fontstart, "maxp");
    if (t)
        info->numGlyphs = ttUSHORT(data + t + 4);
    else
        info->numGlyphs = 0xffff;

    info->svg = -1;

    // find a cmap encoding table we understand *now* to avoid searching
    // later. (todo: could make this installable)
    // the same regardless of glyph.
    numTables = ttUSHORT(data + cmap + 2);
    info->index_map = 0;
    for (i = 0; i < numTables; ++i)
    {
        uint32_t encoding_record = cmap + 4 + 8 * i;
        // find an encoding we understand:
        switch (ttUSHORT(data + encoding_record))
        {
        case STBTT_PLATFORM_ID_MICROSOFT:
            switch (ttUSHORT(data + encoding_record + 2))
            {
            case STBTT_MS_EID_UNICODE_BMP:
            case STBTT_MS_EID_UNICODE_FULL:
                // MS/Unicode
                info->index_map = cmap + ttULONG(data + encoding_record + 4);
                break;
            }
            break;
        case STBTT_PLATFORM_ID_UNICODE:
            // Mac/iOS has these
            // all the encodingIDs are unicode, so we don't bother to check it
            info->index_map = cmap + ttULONG(data + encoding_record + 4);
            break;
        }
    }
    if (info->index_map == 0)
        return 0;

    info->indexToLocFormat = ttUSHORT(data + info->head + 50);
    return 1;
}

int stbtt_FindGlyphIndex(const stbtt_fontinfo *info, int unicode_codepoint)
{
    uint8_t *data = info->data;
    uint32_t index_map = info->index_map;

    uint16_t format = ttUSHORT(data + index_map + 0);
    if (format == 0)
    { // apple byte encoding
        int32_t bytes = ttUSHORT(data + index_map + 2);
        if (unicode_codepoint < bytes - 6)
            return ttBYTE(data + index_map + 6 + unicode_codepoint);
        return 0;
    }
    else if (format == 6)
    {
        uint32_t first = ttUSHORT(data + index_map + 6);
        uint32_t count = ttUSHORT(data + index_map + 8);
        if ((uint32_t)unicode_codepoint >= first && (uint32_t)unicode_codepoint < first + count)
            return ttUSHORT(data + index_map + 10 + (unicode_codepoint - first) * 2);
        return 0;
    }
    else if (format == 2)
    {
        // @TODO: high-byte mapping for japanese/chinese/korean
        ASSERT_NOT_REACHED();
    }
    else if (format == 4)
    { // standard mapping for windows fonts: binary search collection of ranges
        uint16_t segcount = ttUSHORT(data + index_map + 6) >> 1;
        uint16_t searchRange = ttUSHORT(data + index_map + 8) >> 1;
        uint16_t entrySelector = ttUSHORT(data + index_map + 10);
        uint16_t rangeShift = ttUSHORT(data + index_map + 12) >> 1;

        // do a binary search of the segments
        uint32_t endCount = index_map + 14;
        uint32_t search = endCount;

        if (unicode_codepoint > 0xffff)
            return 0;

        // they lie from endCount .. endCount + segCount
        // but searchRange is the nearest power of two, so...
        if (unicode_codepoint >= ttUSHORT(data + search + rangeShift * 2))
            search += rangeShift * 2;

        // now decrement to bias correctly to find smallest
        search -= 2;
        while (entrySelector)
        {
            uint16_t end;
            searchRange >>= 1;
            end = ttUSHORT(data + search + searchRange * 2);
            if (unicode_codepoint > end)
                search += searchRange * 2;
            --entrySelector;
        }
        search += 2;

        {
            uint16_t offset, start;
            uint16_t item = (uint16_t)((search - endCount) >> 1);

            assert(unicode_codepoint <= ttUSHORT(data + endCount + 2 * item));
            start = ttUSHORT(data + index_map + 14 + segcount * 2 + 2 + 2 * item);
            if (unicode_codepoint < start)
                return 0;

            offset = ttUSHORT(data + index_map + 14 + segcount * 6 + 2 + 2 * item);
            if (offset == 0)
                return (uint16_t)(unicode_codepoint + ttSHORT(data + index_map + 14 + segcount * 4 + 2 + 2 * item));

            return ttUSHORT(data + offset + (unicode_codepoint - start) * 2 + index_map + 14 + segcount * 6 + 2 + 2 * item);
        }
    }
    else if (format == 12 || format == 13)
    {
        uint32_t ngroups = ttULONG(data + index_map + 12);
        int32_t low, high;
        low = 0;
        high = (int32_t)ngroups;
        // Binary search the right group.
        while (low < high)
        {
            int32_t mid = low + ((high - low) >> 1); // rounds down, so low <= mid < high
            uint32_t start_char = ttULONG(data + index_map + 16 + mid * 12);
            uint32_t end_char = ttULONG(data + index_map + 16 + mid * 12 + 4);
            if ((uint32_t)unicode_codepoint < start_char)
                high = mid;
            else if ((uint32_t)unicode_codepoint > end_char)
                low = mid + 1;
            else
            {
                uint32_t start_glyph = ttULONG(data + index_map + 16 + mid * 12 + 8);
                if (format == 12)
                    return start_glyph + unicode_codepoint - start_char;
                else // format == 13
                    return start_glyph;
            }
        }
        return 0; // not found
    }
    // @TODO
    ASSERT_NOT_REACHED();
}

int stbtt_GetCodepointShape(const stbtt_fontinfo *info, int unicode_codepoint, stbtt_vertex **vertices)
{
    return stbtt_GetGlyphShape(info, stbtt_FindGlyphIndex(info, unicode_codepoint), vertices);
}

static void stbtt_setvertex(stbtt_vertex *v, uint8_t type, int32_t x, int32_t y, int32_t cx, int32_t cy)
{
    v->type = type;
    v->x = (int16_t)x;
    v->y = (int16_t)y;
    v->cx = (int16_t)cx;
    v->cy = (int16_t)cy;
}

static int stbtt__GetGlyfOffset(const stbtt_fontinfo *info, int glyph_index)
{
    int g1, g2;

    assert(!info->cff.size);

    if (glyph_index >= info->numGlyphs)
        return -1; // glyph index out of range
    if (info->indexToLocFormat >= 2)
        return -1; // unknown index->glyph map format

    if (info->indexToLocFormat == 0)
    {
        g1 = info->glyf + ttUSHORT(info->data + info->loca + glyph_index * 2) * 2;
        g2 = info->glyf + ttUSHORT(info->data + info->loca + glyph_index * 2 + 2) * 2;
    }
    else
    {
        g1 = info->glyf + ttULONG(info->data + info->loca + glyph_index * 4);
        g2 = info->glyf + ttULONG(info->data + info->loca + glyph_index * 4 + 4);
    }

    return g1 == g2 ? -1 : g1; // if length is 0, return -1
}

static int stbtt__GetGlyphInfoT2(const stbtt_fontinfo *info, int glyph_index, int *x0, int *y0, int *x1, int *y1);

int stbtt_GetGlyphBox(const stbtt_fontinfo *info, int glyph_index, int *x0, int *y0, int *x1, int *y1)
{
    if (info->cff.size)
    {
        stbtt__GetGlyphInfoT2(info, glyph_index, x0, y0, x1, y1);
    }
    else
    {
        int g = stbtt__GetGlyfOffset(info, glyph_index);
        if (g < 0)
            return 0;

        if (x0)
            *x0 = ttSHORT(info->data + g + 2);
        if (y0)
            *y0 = ttSHORT(info->data + g + 4);
        if (x1)
            *x1 = ttSHORT(info->data + g + 6);
        if (y1)
            *y1 = ttSHORT(info->data + g + 8);
    }
    return 1;
}

int stbtt_GetCodepointBox(const stbtt_fontinfo *info, int codepoint, int *x0, int *y0, int *x1, int *y1)
{
    return stbtt_GetGlyphBox(info, stbtt_FindGlyphIndex(info, codepoint), x0, y0, x1, y1);
}

int stbtt_IsGlyphEmpty(const stbtt_fontinfo *info, int glyph_index)
{
    int16_t numberOfContours;
    int g;
    if (info->cff.size)
        return stbtt__GetGlyphInfoT2(info, glyph_index, NULL, NULL, NULL, NULL) == 0;
    g = stbtt__GetGlyfOffset(info, glyph_index);
    if (g < 0)
        return 1;
    numberOfContours = ttSHORT(info->data + g);
    return numberOfContours == 0;
}

static int stbtt__close_shape(stbtt_vertex *vertices, int num_vertices, int was_off, int start_off,
                              int32_t sx, int32_t sy, int32_t scx, int32_t scy, int32_t cx, int32_t cy)
{
    if (start_off)
    {
        if (was_off)
            stbtt_setvertex(&vertices[num_vertices++], STBTT_vcurve, (cx + scx) >> 1, (cy + scy) >> 1, cx, cy);
        stbtt_setvertex(&vertices[num_vertices++], STBTT_vcurve, sx, sy, scx, scy);
    }
    else
    {
        if (was_off)
            stbtt_setvertex(&vertices[num_vertices++], STBTT_vcurve, sx, sy, cx, cy);
        else
            stbtt_setvertex(&vertices[num_vertices++], STBTT_vline, sx, sy, 0, 0);
    }
    return num_vertices;
}

static int stbtt__GetGlyphShapeTT(const stbtt_fontinfo *info, int glyph_index, stbtt_vertex **pvertices)
{
    int16_t numberOfContours;
    uint8_t *endPtsOfContours;
    uint8_t *data = info->data;
    stbtt_vertex *vertices = 0;
    int num_vertices = 0;
    int g = stbtt__GetGlyfOffset(info, glyph_index);

    *pvertices = NULL;

    if (g < 0)
        return 0;

    numberOfContours = ttSHORT(data + g);

    if (numberOfContours > 0)
    {
        uint8_t flags = 0, flagcount;
        int32_t ins, i, j = 0, m, n, next_move, was_off = 0, off, start_off = 0;
        int32_t x, y, cx, cy, sx, sy, scx, scy;
        uint8_t *points;
        endPtsOfContours = (data + g + 10);
        ins = ttUSHORT(data + g + 10 + numberOfContours * 2);
        points = data + g + 10 + numberOfContours * 2 + 2 + ins;

        n = 1 + ttUSHORT(endPtsOfContours + numberOfContours * 2 - 2);

        m = n + 2 * numberOfContours; // a loose bound on how many vertices we might need
        vertices = (stbtt_vertex *)malloc(m * sizeof(vertices[0]));
        if (vertices == 0)
            return 0;

        next_move = 0;
        flagcount = 0;

        // in first pass, we load uninterpreted data into the allocated array
        // above, shifted to the end of the array so we won't overwrite it when
        // we create our final data starting from the front

        off = m - n; // starting offset for uninterpreted data, regardless of how m ends up being calculated

        // first load flags

        for (i = 0; i < n; ++i)
        {
            if (flagcount == 0)
            {
                flags = *points++;
                if (flags & 8)
                    flagcount = *points++;
            }
            else
                --flagcount;
            vertices[off + i].type = flags;
        }

        // now load x coordinates
        x = 0;
        for (i = 0; i < n; ++i)
        {
            flags = vertices[off + i].type;
            if (flags & 2)
            {
                int16_t dx = *points++;
                x += (flags & 16) ? dx : -dx; // ???
            }
            else
            {
                if (!(flags & 16))
                {
                    x = x + (int16_t)(points[0] * 256 + points[1]);
                    points += 2;
                }
            }
            vertices[off + i].x = (int16_t)x;
        }

        // now load y coordinates
        y = 0;
        for (i = 0; i < n; ++i)
        {
            flags = vertices[off + i].type;
            if (flags & 4)
            {
                int16_t dy = *points++;
                y += (flags & 32) ? dy : -dy; // ???
            }
            else
            {
                if (!(flags & 32))
                {
                    y = y + (int16_t)(points[0] * 256 + points[1]);
                    points += 2;
                }
            }
            vertices[off + i].y = (int16_t)y;
        }

        // now convert them to our format
        num_vertices = 0;
        sx = sy = cx = cy = scx = scy = 0;
        for (i = 0; i < n; ++i)
        {
            flags = vertices[off + i].type;
            x = (int16_t)vertices[off + i].x;
            y = (int16_t)vertices[off + i].y;

            if (next_move == i)
            {
                if (i != 0)
                    num_vertices = stbtt__close_shape(vertices, num_vertices, was_off, start_off, sx, sy, scx, scy, cx, cy);

                // now start the new one
                start_off = !(flags & 1);
                if (start_off)
                {
                    // if we start off with an off-curve point, then when we need to find a point on the curve
                    // where we can start, and we need to save some state for when we wraparound.
                    scx = x;
                    scy = y;
                    if (!(vertices[off + i + 1].type & 1))
                    {
                        // next point is also a curve point, so interpolate an on-point curve
                        sx = (x + (int32_t)vertices[off + i + 1].x) >> 1;
                        sy = (y + (int32_t)vertices[off + i + 1].y) >> 1;
                    }
                    else
                    {
                        // otherwise just use the next point as our start point
                        sx = (int32_t)vertices[off + i + 1].x;
                        sy = (int32_t)vertices[off + i + 1].y;
                        ++i; // we're using point i+1 as the starting point, so skip it
                    }
                }
                else
                {
                    sx = x;
                    sy = y;
                }
                stbtt_setvertex(&vertices[num_vertices++], STBTT_vmove, sx, sy, 0, 0);
                was_off = 0;
                next_move = 1 + ttUSHORT(endPtsOfContours + j * 2);
                ++j;
            }
            else
            {
                if (!(flags & 1))
                {                // if it's a curve
                    if (was_off) // two off-curve control points in a row means interpolate an on-curve midpoint
                        stbtt_setvertex(&vertices[num_vertices++], STBTT_vcurve, (cx + x) >> 1, (cy + y) >> 1, cx, cy);
                    cx = x;
                    cy = y;
                    was_off = 1;
                }
                else
                {
                    if (was_off)
                        stbtt_setvertex(&vertices[num_vertices++], STBTT_vcurve, x, y, cx, cy);
                    else
                        stbtt_setvertex(&vertices[num_vertices++], STBTT_vline, x, y, 0, 0);
                    was_off = 0;
                }
            }
        }
        num_vertices = stbtt__close_shape(vertices, num_vertices, was_off, start_off, sx, sy, scx, scy, cx, cy);
    }
    else if (numberOfContours < 0)
    {
        // Compound shapes.
        int more = 1;
        uint8_t *comp = data + g + 10;
        num_vertices = 0;
        vertices = 0;
        while (more)
        {
            uint16_t flags, gidx;
            int comp_num_verts = 0, i;
            stbtt_vertex *comp_verts = 0, *tmp = 0;
            float mtx[6] = {1, 0, 0, 1, 0, 0}, m, n;

            flags = ttSHORT(comp);
            comp += 2;
            gidx = ttSHORT(comp);
            comp += 2;

            if (flags & 2)
            { // XY values
                if (flags & 1)
                { // shorts
                    mtx[4] = ttSHORT(comp);
                    comp += 2;
                    mtx[5] = ttSHORT(comp);
                    comp += 2;
                }
                else
                {
                    mtx[4] = ttCHAR(comp);
                    comp += 1;
                    mtx[5] = ttCHAR(comp);
                    comp += 1;
                }
            }
            else
            {
                // @TODO handle matching point
                ASSERT_NOT_REACHED();
            }
            if (flags & (1 << 3))
            { // WE_HAVE_A_SCALE
                mtx[0] = mtx[3] = ttSHORT(comp) / 16384.0f;
                comp += 2;
                mtx[1] = mtx[2] = 0;
            }
            else if (flags & (1 << 6))
            { // WE_HAVE_AN_X_AND_YSCALE
                mtx[0] = ttSHORT(comp) / 16384.0f;
                comp += 2;
                mtx[1] = mtx[2] = 0;
                mtx[3] = ttSHORT(comp) / 16384.0f;
                comp += 2;
            }
            else if (flags & (1 << 7))
            { // WE_HAVE_A_TWO_BY_TWO
                mtx[0] = ttSHORT(comp) / 16384.0f;
                comp += 2;
                mtx[1] = ttSHORT(comp) / 16384.0f;
                comp += 2;
                mtx[2] = ttSHORT(comp) / 16384.0f;
                comp += 2;
                mtx[3] = ttSHORT(comp) / 16384.0f;
                comp += 2;
            }

            // Find transformation scales.
            m = (float)sqrt(mtx[0] * mtx[0] + mtx[1] * mtx[1]);
            n = (float)sqrt(mtx[2] * mtx[2] + mtx[3] * mtx[3]);

            // Get indexed glyph.
            comp_num_verts = stbtt_GetGlyphShape(info, gidx, &comp_verts);
            if (comp_num_verts > 0)
            {
                // Transform vertices.
                for (i = 0; i < comp_num_verts; ++i)
                {
                    stbtt_vertex *v = &comp_verts[i];
                    short x, y;
                    x = v->x;
                    y = v->y;
                    v->x = (short)(m * (mtx[0] * x + mtx[2] * y + mtx[4]));
                    v->y = (short)(n * (mtx[1] * x + mtx[3] * y + mtx[5]));
                    x = v->cx;
                    y = v->cy;
                    v->cx = (short)(m * (mtx[0] * x + mtx[2] * y + mtx[4]));
                    v->cy = (short)(n * (mtx[1] * x + mtx[3] * y + mtx[5]));
                }
                // Append vertices.
                tmp = (stbtt_vertex *)malloc((num_vertices + comp_num_verts) * sizeof(stbtt_vertex));
                if (!tmp)
                {
                    if (vertices)
                        free(vertices);
                    if (comp_verts)
                        free(comp_verts);
                    return 0;
                }
                if (num_vertices > 0)
                    memcpy(tmp, vertices, num_vertices * sizeof(stbtt_vertex));
                memcpy(tmp + num_vertices, comp_verts, comp_num_verts * sizeof(stbtt_vertex));
                if (vertices)
                    free(vertices);
                vertices = tmp;
                free(comp_verts);
                num_vertices += comp_num_verts;
            }
            // More components ?
            more = flags & (1 << 5);
        }
    }
    else
    {
        // numberOfCounters == 0, do nothing
    }

    *pvertices = vertices;
    return num_vertices;
}

typedef struct
{
    int bounds;
    int started;
    float first_x, first_y;
    float x, y;
    int32_t min_x, max_x, min_y, max_y;

    stbtt_vertex *pvertices;
    int num_vertices;
} stbtt__csctx;

#define STBTT__CSCTX_INIT(bounds)                  \
    {                                              \
        bounds, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL, 0 \
    }

static void stbtt__track_vertex(stbtt__csctx *c, int32_t x, int32_t y)
{
    if (x > c->max_x || !c->started)
        c->max_x = x;
    if (y > c->max_y || !c->started)
        c->max_y = y;
    if (x < c->min_x || !c->started)
        c->min_x = x;
    if (y < c->min_y || !c->started)
        c->min_y = y;
    c->started = 1;
}

static void stbtt__csctx_v(stbtt__csctx *c, uint8_t type, int32_t x, int32_t y, int32_t cx, int32_t cy, int32_t cx1, int32_t cy1)
{
    if (c->bounds)
    {
        stbtt__track_vertex(c, x, y);
        if (type == STBTT_vcubic)
        {
            stbtt__track_vertex(c, cx, cy);
            stbtt__track_vertex(c, cx1, cy1);
        }
    }
    else
    {
        stbtt_setvertex(&c->pvertices[c->num_vertices], type, x, y, cx, cy);
        c->pvertices[c->num_vertices].cx1 = (int16_t)cx1;
        c->pvertices[c->num_vertices].cy1 = (int16_t)cy1;
    }
    c->num_vertices++;
}

static void stbtt__csctx_close_shape(stbtt__csctx *ctx)
{
    if (ctx->first_x != ctx->x || ctx->first_y != ctx->y)
        stbtt__csctx_v(ctx, STBTT_vline, (int)ctx->first_x, (int)ctx->first_y, 0, 0, 0, 0);
}

static void stbtt__csctx_rmove_to(stbtt__csctx *ctx, float dx, float dy)
{
    stbtt__csctx_close_shape(ctx);
    ctx->first_x = ctx->x = ctx->x + dx;
    ctx->first_y = ctx->y = ctx->y + dy;
    stbtt__csctx_v(ctx, STBTT_vmove, (int)ctx->x, (int)ctx->y, 0, 0, 0, 0);
}

static void stbtt__csctx_rline_to(stbtt__csctx *ctx, float dx, float dy)
{
    ctx->x += dx;
    ctx->y += dy;
    stbtt__csctx_v(ctx, STBTT_vline, (int)ctx->x, (int)ctx->y, 0, 0, 0, 0);
}

static void stbtt__csctx_rccurve_to(stbtt__csctx *ctx, float dx1, float dy1, float dx2, float dy2, float dx3, float dy3)
{
    float cx1 = ctx->x + dx1;
    float cy1 = ctx->y + dy1;
    float cx2 = cx1 + dx2;
    float cy2 = cy1 + dy2;
    ctx->x = cx2 + dx3;
    ctx->y = cy2 + dy3;
    stbtt__csctx_v(ctx, STBTT_vcubic, (int)ctx->x, (int)ctx->y, (int)cx1, (int)cy1, (int)cx2, (int)cy2);
}

static stbtt__buf stbtt__get_subr(stbtt__buf idx, int n)
{
    int count = stbtt__cff_index_count(&idx);
    int bias = 107;
    if (count >= 33900)
        bias = 32768;
    else if (count >= 1240)
        bias = 1131;
    n += bias;
    if (n < 0 || n >= count)
        return stbtt__new_buf(NULL, 0);
    return stbtt__cff_index_get(idx, n);
}

static stbtt__buf stbtt__cid_get_glyph_subrs(const stbtt_fontinfo *info, int glyph_index)
{
    stbtt__buf fdselect = info->fdselect;
    int nranges, start, end, v, fmt, fdselector = -1, i;

    stbtt__buf_seek(&fdselect, 0);
    fmt = stbtt__buf_get8(&fdselect);
    if (fmt == 0)
    {
        // untested
        stbtt__buf_skip(&fdselect, glyph_index);
        fdselector = stbtt__buf_get8(&fdselect);
    }
    else if (fmt == 3)
    {
        nranges = stbtt__buf_get16(&fdselect);
        start = stbtt__buf_get16(&fdselect);
        for (i = 0; i < nranges; i++)
        {
            v = stbtt__buf_get8(&fdselect);
            end = stbtt__buf_get16(&fdselect);
            if (glyph_index >= start && glyph_index < end)
            {
                fdselector = v;
                break;
            }
            start = end;
        }
    }
    if (fdselector == -1)
        stbtt__new_buf(NULL, 0);
    return stbtt__get_subrs(info->cff, stbtt__cff_index_get(info->fontdicts, fdselector));
}

static int stbtt__run_charstring(const stbtt_fontinfo *info, int glyph_index, stbtt__csctx *c)
{
    int in_header = 1, maskbits = 0, subr_stack_height = 0, sp = 0, v, i, b0;
    int has_subrs = 0, clear_stack;
    float s[48];
    stbtt__buf subr_stack[10], subrs = info->subrs, b;
    float f;

#define STBTT__CSERR(s) (0)

    // this currently ignores the initial width value, which isn't needed if we have hmtx
    b = stbtt__cff_index_get(info->charstrings, glyph_index);
    while (b.cursor < b.size)
    {
        i = 0;
        clear_stack = 1;
        b0 = stbtt__buf_get8(&b);
        switch (b0)
        {
        // @TODO implement hinting
        case 0x13: // hintmask
        case 0x14: // cntrmask
            if (in_header)
                maskbits += (sp / 2); // implicit "vstem"
            in_header = 0;
            stbtt__buf_skip(&b, (maskbits + 7) / 8);
            break;

        case 0x01: // hstem
        case 0x03: // vstem
        case 0x12: // hstemhm
        case 0x17: // vstemhm
            maskbits += (sp / 2);
            break;

        case 0x15: // rmoveto
            in_header = 0;
            if (sp < 2)
                return STBTT__CSERR("rmoveto stack");
            stbtt__csctx_rmove_to(c, s[sp - 2], s[sp - 1]);
            break;
        case 0x04: // vmoveto
            in_header = 0;
            if (sp < 1)
                return STBTT__CSERR("vmoveto stack");
            stbtt__csctx_rmove_to(c, 0, s[sp - 1]);
            break;
        case 0x16: // hmoveto
            in_header = 0;
            if (sp < 1)
                return STBTT__CSERR("hmoveto stack");
            stbtt__csctx_rmove_to(c, s[sp - 1], 0);
            break;

        case 0x05: // rlineto
            if (sp < 2)
                return STBTT__CSERR("rlineto stack");
            for (; i + 1 < sp; i += 2)
                stbtt__csctx_rline_to(c, s[i], s[i + 1]);
            break;

            // hlineto/vlineto and vhcurveto/hvcurveto alternate horizontal and vertical
            // starting from a different place.

        case 0x07: // vlineto
            if (sp < 1)
                return STBTT__CSERR("vlineto stack");
            goto vlineto;
        case 0x06: // hlineto
            if (sp < 1)
                return STBTT__CSERR("hlineto stack");
            for (;;)
            {
                if (i >= sp)
                    break;
                stbtt__csctx_rline_to(c, s[i], 0);
                i++;
            vlineto:
                if (i >= sp)
                    break;
                stbtt__csctx_rline_to(c, 0, s[i]);
                i++;
            }
            break;

        case 0x1F: // hvcurveto
            if (sp < 4)
                return STBTT__CSERR("hvcurveto stack");
            goto hvcurveto;
        case 0x1E: // vhcurveto
            if (sp < 4)
                return STBTT__CSERR("vhcurveto stack");
            for (;;)
            {
                if (i + 3 >= sp)
                    break;
                stbtt__csctx_rccurve_to(c, 0, s[i], s[i + 1], s[i + 2], s[i + 3], (sp - i == 5) ? s[i + 4] : 0.0f);
                i += 4;
            hvcurveto:
                if (i + 3 >= sp)
                    break;
                stbtt__csctx_rccurve_to(c, s[i], 0, s[i + 1], s[i + 2], (sp - i == 5) ? s[i + 4] : 0.0f, s[i + 3]);
                i += 4;
            }
            break;

        case 0x08: // rrcurveto
            if (sp < 6)
                return STBTT__CSERR("rcurveline stack");
            for (; i + 5 < sp; i += 6)
                stbtt__csctx_rccurve_to(c, s[i], s[i + 1], s[i + 2], s[i + 3], s[i + 4], s[i + 5]);
            break;

        case 0x18: // rcurveline
            if (sp < 8)
                return STBTT__CSERR("rcurveline stack");
            for (; i + 5 < sp - 2; i += 6)
                stbtt__csctx_rccurve_to(c, s[i], s[i + 1], s[i + 2], s[i + 3], s[i + 4], s[i + 5]);
            if (i + 1 >= sp)
                return STBTT__CSERR("rcurveline stack");
            stbtt__csctx_rline_to(c, s[i], s[i + 1]);
            break;

        case 0x19: // rlinecurve
            if (sp < 8)
                return STBTT__CSERR("rlinecurve stack");
            for (; i + 1 < sp - 6; i += 2)
                stbtt__csctx_rline_to(c, s[i], s[i + 1]);
            if (i + 5 >= sp)
                return STBTT__CSERR("rlinecurve stack");
            stbtt__csctx_rccurve_to(c, s[i], s[i + 1], s[i + 2], s[i + 3], s[i + 4], s[i + 5]);
            break;

        case 0x1A: // vvcurveto
        case 0x1B: // hhcurveto
            if (sp < 4)
                return STBTT__CSERR("(vv|hh)curveto stack");
            f = 0.0;
            if (sp & 1)
            {
                f = s[i];
                i++;
            }
            for (; i + 3 < sp; i += 4)
            {
                if (b0 == 0x1B)
                    stbtt__csctx_rccurve_to(c, s[i], f, s[i + 1], s[i + 2], s[i + 3], 0.0);
                else
                    stbtt__csctx_rccurve_to(c, f, s[i], s[i + 1], s[i + 2], 0.0, s[i + 3]);
                f = 0.0;
            }
            break;

        case 0x0A: // callsubr
            if (!has_subrs)
            {
                if (info->fdselect.size)
                    subrs = stbtt__cid_get_glyph_subrs(info, glyph_index);
                has_subrs = 1;
            }
            // fallthrough
        case 0x1D: // callgsubr
            if (sp < 1)
                return STBTT__CSERR("call(g|)subr stack");
            v = (int)s[--sp];
            if (subr_stack_height >= 10)
                return STBTT__CSERR("recursion limit");
            subr_stack[subr_stack_height++] = b;
            b = stbtt__get_subr(b0 == 0x0A ? subrs : info->gsubrs, v);
            if (b.size == 0)
                return STBTT__CSERR("subr not found");
            b.cursor = 0;
            clear_stack = 0;
            break;

        case 0x0B: // return
            if (subr_stack_height <= 0)
                return STBTT__CSERR("return outside subr");
            b = subr_stack[--subr_stack_height];
            clear_stack = 0;
            break;

        case 0x0E: // endchar
            stbtt__csctx_close_shape(c);
            return 1;

        case 0x0C:
        { // two-byte escape
            float dx1, dx2, dx3, dx4, dx5, dx6, dy1, dy2, dy3, dy4, dy5, dy6;
            float dx, dy;
            int b1 = stbtt__buf_get8(&b);
            switch (b1)
            {
            // @TODO These "flex" implementations ignore the flex-depth and resolution,
            // and always draw beziers.
            case 0x22: // hflex
                if (sp < 7)
                    return STBTT__CSERR("hflex stack");
                dx1 = s[0];
                dx2 = s[1];
                dy2 = s[2];
                dx3 = s[3];
                dx4 = s[4];
                dx5 = s[5];
                dx6 = s[6];
                stbtt__csctx_rccurve_to(c, dx1, 0, dx2, dy2, dx3, 0);
                stbtt__csctx_rccurve_to(c, dx4, 0, dx5, -dy2, dx6, 0);
                break;

            case 0x23: // flex
                if (sp < 13)
                    return STBTT__CSERR("flex stack");
                dx1 = s[0];
                dy1 = s[1];
                dx2 = s[2];
                dy2 = s[3];
                dx3 = s[4];
                dy3 = s[5];
                dx4 = s[6];
                dy4 = s[7];
                dx5 = s[8];
                dy5 = s[9];
                dx6 = s[10];
                dy6 = s[11];
                //fd is s[12]
                stbtt__csctx_rccurve_to(c, dx1, dy1, dx2, dy2, dx3, dy3);
                stbtt__csctx_rccurve_to(c, dx4, dy4, dx5, dy5, dx6, dy6);
                break;

            case 0x24: // hflex1
                if (sp < 9)
                    return STBTT__CSERR("hflex1 stack");
                dx1 = s[0];
                dy1 = s[1];
                dx2 = s[2];
                dy2 = s[3];
                dx3 = s[4];
                dx4 = s[5];
                dx5 = s[6];
                dy5 = s[7];
                dx6 = s[8];
                stbtt__csctx_rccurve_to(c, dx1, dy1, dx2, dy2, dx3, 0);
                stbtt__csctx_rccurve_to(c, dx4, 0, dx5, dy5, dx6, -(dy1 + dy2 + dy5));
                break;

            case 0x25: // flex1
                if (sp < 11)
                    return STBTT__CSERR("flex1 stack");
                dx1 = s[0];
                dy1 = s[1];
                dx2 = s[2];
                dy2 = s[3];
                dx3 = s[4];
                dy3 = s[5];
                dx4 = s[6];
                dy4 = s[7];
                dx5 = s[8];
                dy5 = s[9];
                dx6 = dy6 = s[10];
                dx = dx1 + dx2 + dx3 + dx4 + dx5;
                dy = dy1 + dy2 + dy3 + dy4 + dy5;
                if (fabs(dx) > fabs(dy))
                    dy6 = -dy;
                else
                    dx6 = -dx;
                stbtt__csctx_rccurve_to(c, dx1, dy1, dx2, dy2, dx3, dy3);
                stbtt__csctx_rccurve_to(c, dx4, dy4, dx5, dy5, dx6, dy6);
                break;

            default:
                return STBTT__CSERR("unimplemented");
            }
        }
        break;

        default:
            if (b0 != 255 && b0 != 28 && (b0 < 32 || b0 > 254))
                return STBTT__CSERR("reserved operator");

            // push immediate
            if (b0 == 255)
            {
                f = (float)(int32_t)stbtt__buf_get32(&b) / 0x10000;
            }
            else
            {
                stbtt__buf_skip(&b, -1);
                f = (float)(int16_t)stbtt__cff_int(&b);
            }
            if (sp >= 48)
                return STBTT__CSERR("push stack overflow");
            s[sp++] = f;
            clear_stack = 0;
            break;
        }
        if (clear_stack)
            sp = 0;
    }
    return STBTT__CSERR("no endchar");

#undef STBTT__CSERR
}

static int stbtt__GetGlyphShapeT2(const stbtt_fontinfo *info, int glyph_index, stbtt_vertex **pvertices)
{
    // runs the charstring twice, once to count and once to output (to avoid realloc)
    stbtt__csctx count_ctx = STBTT__CSCTX_INIT(1);
    stbtt__csctx output_ctx = STBTT__CSCTX_INIT(0);
    if (stbtt__run_charstring(info, glyph_index, &count_ctx))
    {
        *pvertices = (stbtt_vertex *)malloc(count_ctx.num_vertices * sizeof(stbtt_vertex));
        output_ctx.pvertices = *pvertices;
        if (stbtt__run_charstring(info, glyph_index, &output_ctx))
        {
            assert(output_ctx.num_vertices == count_ctx.num_vertices);
            return output_ctx.num_vertices;
        }
    }
    *pvertices = NULL;
    return 0;
}

static int stbtt__GetGlyphInfoT2(const stbtt_fontinfo *info, int glyph_index, int *x0, int *y0, int *x1, int *y1)
{
    stbtt__csctx c = STBTT__CSCTX_INIT(1);
    int r = stbtt__run_charstring(info, glyph_index, &c);
    if (x0)
        *x0 = r ? c.min_x : 0;
    if (y0)
        *y0 = r ? c.min_y : 0;
    if (x1)
        *x1 = r ? c.max_x : 0;
    if (y1)
        *y1 = r ? c.max_y : 0;
    return r ? c.num_vertices : 0;
}

int stbtt_GetGlyphShape(const stbtt_fontinfo *info, int glyph_index, stbtt_vertex **pvertices)
{
    if (!info->cff.size)
        return stbtt__GetGlyphShapeTT(info, glyph_index, pvertices);
    else
        return stbtt__GetGlyphShapeT2(info, glyph_index, pvertices);
}

void stbtt_GetGlyphHMetrics(const stbtt_fontinfo *info, int glyph_index, int *advanceWidth, int *leftSideBearing)
{
    uint16_t numOfLongHorMetrics = ttUSHORT(info->data + info->hhea + 34);
    if (glyph_index < numOfLongHorMetrics)
    {
        if (advanceWidth)
            *advanceWidth = ttSHORT(info->data + info->hmtx + 4 * glyph_index);
        if (leftSideBearing)
            *leftSideBearing = ttSHORT(info->data + info->hmtx + 4 * glyph_index + 2);
    }
    else
    {
        if (advanceWidth)
            *advanceWidth = ttSHORT(info->data + info->hmtx + 4 * (numOfLongHorMetrics - 1));
        if (leftSideBearing)
            *leftSideBearing = ttSHORT(info->data + info->hmtx + 4 * numOfLongHorMetrics + 2 * (glyph_index - numOfLongHorMetrics));
    }
}

int stbtt_GetKerningTableLength(const stbtt_fontinfo *info)
{
    uint8_t *data = info->data + info->kern;

    // we only look at the first table. it must be 'horizontal' and format 0.
    if (!info->kern)
        return 0;
    if (ttUSHORT(data + 2) < 1) // number of tables, need at least 1
        return 0;
    if (ttUSHORT(data + 8) != 1) // horizontal flag must be set in format
        return 0;

    return ttUSHORT(data + 10);
}

int stbtt_GetKerningTable(const stbtt_fontinfo *info, stbtt_kerningentry *table, int table_length)
{
    uint8_t *data = info->data + info->kern;
    int k, length;

    // we only look at the first table. it must be 'horizontal' and format 0.
    if (!info->kern)
        return 0;
    if (ttUSHORT(data + 2) < 1) // number of tables, need at least 1
        return 0;
    if (ttUSHORT(data + 8) != 1) // horizontal flag must be set in format
        return 0;

    length = ttUSHORT(data + 10);
    if (table_length < length)
        length = table_length;

    for (k = 0; k < length; k++)
    {
        table[k].glyph1 = ttUSHORT(data + 18 + (k * 6));
        table[k].glyph2 = ttUSHORT(data + 20 + (k * 6));
        table[k].advance = ttSHORT(data + 22 + (k * 6));
    }

    return length;
}

static int stbtt__GetGlyphKernInfoAdvance(const stbtt_fontinfo *info, int glyph1, int glyph2)
{
    uint8_t *data = info->data + info->kern;
    uint32_t needle, straw;
    int l, r, m;

    // we only look at the first table. it must be 'horizontal' and format 0.
    if (!info->kern)
        return 0;
    if (ttUSHORT(data + 2) < 1) // number of tables, need at least 1
        return 0;
    if (ttUSHORT(data + 8) != 1) // horizontal flag must be set in format
        return 0;

    l = 0;
    r = ttUSHORT(data + 10) - 1;
    needle = glyph1 << 16 | glyph2;
    while (l <= r)
    {
        m = (l + r) >> 1;
        straw = ttULONG(data + 18 + (m * 6)); // note: unaligned read
        if (needle < straw)
            r = m - 1;
        else if (needle > straw)
            l = m + 1;
        else
            return ttSHORT(data + 22 + (m * 6));
    }
    return 0;
}

static int32_t stbtt__GetCoverageIndex(uint8_t *coverageTable, int glyph)
{
    uint16_t coverageFormat = ttUSHORT(coverageTable);
    switch (coverageFormat)
    {
    case 1:
    {
        uint16_t glyphCount = ttUSHORT(coverageTable + 2);

        // Binary search.
        int32_t l = 0, r = glyphCount - 1, m;
        int straw, needle = glyph;
        while (l <= r)
        {
            uint8_t *glyphArray = coverageTable + 4;
            uint16_t glyphID;
            m = (l + r) >> 1;
            glyphID = ttUSHORT(glyphArray + 2 * m);
            straw = glyphID;
            if (needle < straw)
                r = m - 1;
            else if (needle > straw)
                l = m + 1;
            else
            {
                return m;
            }
        }
    }
    break;

    case 2:
    {
        uint16_t rangeCount = ttUSHORT(coverageTable + 2);
        uint8_t *rangeArray = coverageTable + 4;

        // Binary search.
        int32_t l = 0, r = rangeCount - 1, m;
        int strawStart, strawEnd, needle = glyph;
        while (l <= r)
        {
            uint8_t *rangeRecord;
            m = (l + r) >> 1;
            rangeRecord = rangeArray + 6 * m;
            strawStart = ttUSHORT(rangeRecord);
            strawEnd = ttUSHORT(rangeRecord + 2);
            if (needle < strawStart)
                r = m - 1;
            else if (needle > strawEnd)
                l = m + 1;
            else
            {
                uint16_t startCoverageIndex = ttUSHORT(rangeRecord + 4);
                return startCoverageIndex + glyph - strawStart;
            }
        }
    }
    break;

    default:
    {
        // There are no other cases.
        ASSERT_NOT_REACHED();
    }
    break;
    }

    return -1;
}

static int32_t stbtt__GetGlyphClass(uint8_t *classDefTable, int glyph)
{
    uint16_t classDefFormat = ttUSHORT(classDefTable);
    switch (classDefFormat)
    {
    case 1:
    {
        uint16_t startGlyphID = ttUSHORT(classDefTable + 2);
        uint16_t glyphCount = ttUSHORT(classDefTable + 4);
        uint8_t *classDef1ValueArray = classDefTable + 6;

        if (glyph >= startGlyphID && glyph < startGlyphID + glyphCount)
            return (int32_t)ttUSHORT(classDef1ValueArray + 2 * (glyph - startGlyphID));

        classDefTable = classDef1ValueArray + 2 * glyphCount;
    }
    break;

    case 2:
    {
        uint16_t classRangeCount = ttUSHORT(classDefTable + 2);
        uint8_t *classRangeRecords = classDefTable + 4;

        // Binary search.
        int32_t l = 0, r = classRangeCount - 1, m;
        int strawStart, strawEnd, needle = glyph;
        while (l <= r)
        {
            uint8_t *classRangeRecord;
            m = (l + r) >> 1;
            classRangeRecord = classRangeRecords + 6 * m;
            strawStart = ttUSHORT(classRangeRecord);
            strawEnd = ttUSHORT(classRangeRecord + 2);
            if (needle < strawStart)
                r = m - 1;
            else if (needle > strawEnd)
                l = m + 1;
            else
                return (int32_t)ttUSHORT(classRangeRecord + 4);
        }

        classDefTable = classRangeRecords + 6 * classRangeCount;
    }
    break;

    default:
    {
        // There are no other cases.
        ASSERT_NOT_REACHED();
    }
    break;
    }

    return -1;
}

// Define to assert(x) if you want to break on unimplemented formats.
#define STBTT_GPOS_TODO_assert(x)

static int32_t stbtt__GetGlyphGPOSInfoAdvance(const stbtt_fontinfo *info, int glyph1, int glyph2)
{
    uint16_t lookupListOffset;
    uint8_t *lookupList;
    uint16_t lookupCount;
    uint8_t *data;
    int32_t i;

    if (!info->gpos)
        return 0;

    data = info->data + info->gpos;

    if (ttUSHORT(data + 0) != 1)
        return 0; // Major version 1
    if (ttUSHORT(data + 2) != 0)
        return 0; // Minor version 0

    lookupListOffset = ttUSHORT(data + 8);
    lookupList = data + lookupListOffset;
    lookupCount = ttUSHORT(lookupList);

    for (i = 0; i < lookupCount; ++i)
    {
        uint16_t lookupOffset = ttUSHORT(lookupList + 2 + 2 * i);
        uint8_t *lookupTable = lookupList + lookupOffset;

        uint16_t lookupType = ttUSHORT(lookupTable);
        uint16_t subTableCount = ttUSHORT(lookupTable + 4);
        uint8_t *subTableOffsets = lookupTable + 6;
        switch (lookupType)
        {
        case 2:
        { // Pair Adjustment Positioning Subtable
            int32_t sti;
            for (sti = 0; sti < subTableCount; sti++)
            {
                uint16_t subtableOffset = ttUSHORT(subTableOffsets + 2 * sti);
                uint8_t *table = lookupTable + subtableOffset;
                uint16_t posFormat = ttUSHORT(table);
                uint16_t coverageOffset = ttUSHORT(table + 2);
                int32_t coverageIndex = stbtt__GetCoverageIndex(table + coverageOffset, glyph1);
                if (coverageIndex == -1)
                    continue;

                switch (posFormat)
                {
                case 1:
                {
                    int32_t l, r, m;
                    int straw, needle;
                    uint16_t valueFormat1 = ttUSHORT(table + 4);
                    uint16_t valueFormat2 = ttUSHORT(table + 6);
                    int32_t valueRecordPairSizeInBytes = 2;
                    uint16_t pairSetCount = ttUSHORT(table + 8);
                    uint16_t pairPosOffset = ttUSHORT(table + 10 + 2 * coverageIndex);
                    uint8_t *pairValueTable = table + pairPosOffset;
                    uint16_t pairValueCount = ttUSHORT(pairValueTable);
                    uint8_t *pairValueArray = pairValueTable + 2;
                    // TODO: Support more formats.
                    STBTT_GPOS_TODO_assert(valueFormat1 == 4);
                    if (valueFormat1 != 4)
                        return 0;
                    STBTT_GPOS_TODO_assert(valueFormat2 == 0);
                    if (valueFormat2 != 0)
                        return 0;

                    assert(coverageIndex < pairSetCount);
                    __unused(pairSetCount);

                    needle = glyph2;
                    r = pairValueCount - 1;
                    l = 0;

                    // Binary search.
                    while (l <= r)
                    {
                        uint16_t secondGlyph;
                        uint8_t *pairValue;
                        m = (l + r) >> 1;
                        pairValue = pairValueArray + (2 + valueRecordPairSizeInBytes) * m;
                        secondGlyph = ttUSHORT(pairValue);
                        straw = secondGlyph;
                        if (needle < straw)
                            r = m - 1;
                        else if (needle > straw)
                            l = m + 1;
                        else
                        {
                            int16_t xAdvance = ttSHORT(pairValue + 2);
                            return xAdvance;
                        }
                    }
                }
                break;

                case 2:
                {
                    uint16_t valueFormat1 = ttUSHORT(table + 4);
                    uint16_t valueFormat2 = ttUSHORT(table + 6);

                    uint16_t classDef1Offset = ttUSHORT(table + 8);
                    uint16_t classDef2Offset = ttUSHORT(table + 10);
                    int glyph1class = stbtt__GetGlyphClass(table + classDef1Offset, glyph1);
                    int glyph2class = stbtt__GetGlyphClass(table + classDef2Offset, glyph2);

                    uint16_t class1Count = ttUSHORT(table + 12);
                    uint16_t class2Count = ttUSHORT(table + 14);
                    assert(glyph1class < class1Count);
                    assert(glyph2class < class2Count);

                    // TODO: Support more formats.
                    STBTT_GPOS_TODO_assert(valueFormat1 == 4);
                    if (valueFormat1 != 4)
                        return 0;
                    STBTT_GPOS_TODO_assert(valueFormat2 == 0);
                    if (valueFormat2 != 0)
                        return 0;

                    if (glyph1class >= 0 && glyph1class < class1Count && glyph2class >= 0 && glyph2class < class2Count)
                    {
                        uint8_t *class1Records = table + 16;
                        uint8_t *class2Records = class1Records + 2 * (glyph1class * class2Count);
                        int16_t xAdvance = ttSHORT(class2Records + 2 * glyph2class);
                        return xAdvance;
                    }
                }
                break;

                default:
                {
                    // There are no other cases.
                    ASSERT_NOT_REACHED();
                    break;
                };
                }
            }
            break;
        };

        default:
            // TODO: Implement other stuff.
            break;
        }
    }

    return 0;
}

int stbtt_GetGlyphKernAdvance(const stbtt_fontinfo *info, int g1, int g2)
{
    int xAdvance = 0;

    if (info->gpos)
        xAdvance += stbtt__GetGlyphGPOSInfoAdvance(info, g1, g2);
    else if (info->kern)
        xAdvance += stbtt__GetGlyphKernInfoAdvance(info, g1, g2);

    return xAdvance;
}

int stbtt_GetCodepointKernAdvance(const stbtt_fontinfo *info, int ch1, int ch2)
{
    if (!info->kern && !info->gpos) // if no kerning table, don't waste time looking up both codepoint->glyphs
        return 0;

    return stbtt_GetGlyphKernAdvance(info, stbtt_FindGlyphIndex(info, ch1), stbtt_FindGlyphIndex(info, ch2));
}

void stbtt_GetCodepointHMetrics(const stbtt_fontinfo *info, int codepoint, int *advanceWidth, int *leftSideBearing)
{
    stbtt_GetGlyphHMetrics(info, stbtt_FindGlyphIndex(info, codepoint), advanceWidth, leftSideBearing);
}

void stbtt_GetFontVMetrics(const stbtt_fontinfo *info, int *ascent, int *descent, int *lineGap)
{
    if (ascent)
        *ascent = ttSHORT(info->data + info->hhea + 4);

    if (descent)
        *descent = ttSHORT(info->data + info->hhea + 6);

    if (lineGap)
        *lineGap = ttSHORT(info->data + info->hhea + 8);
}

int stbtt_GetFontVMetricsOS2(const stbtt_fontinfo *info, int *typoAscent, int *typoDescent, int *typoLineGap)
{
    int tab = stbtt__find_table(info->data, info->fontstart, "OS/2");

    if (!tab)
        return 0;

    if (typoAscent)
        *typoAscent = ttSHORT(info->data + tab + 68);

    if (typoDescent)
        *typoDescent = ttSHORT(info->data + tab + 70);

    if (typoLineGap)
        *typoLineGap = ttSHORT(info->data + tab + 72);

    return 1;
}

void stbtt_GetFontBoundingBox(const stbtt_fontinfo *info, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = ttSHORT(info->data + info->head + 36);
    *y0 = ttSHORT(info->data + info->head + 38);
    *x1 = ttSHORT(info->data + info->head + 40);
    *y1 = ttSHORT(info->data + info->head + 42);
}

float stbtt_ScaleForPixelHeight(const stbtt_fontinfo *info, float height)
{
    int fheight = ttSHORT(info->data + info->hhea + 4) - ttSHORT(info->data + info->hhea + 6);
    return (float)height / fheight;
}

float stbtt_ScaleForMappingEmToPixels(const stbtt_fontinfo *info, float pixels)
{
    int unitsPerEm = ttUSHORT(info->data + info->head + 18);
    return pixels / unitsPerEm;
}

void freeShape(const stbtt_fontinfo *info, stbtt_vertex *v)
{
    __unused(info);

    free(v);
}

uint8_t *stbtt_FindSVGDoc(const stbtt_fontinfo *info, int gl)
{
    int i;
    uint8_t *data = info->data;
    uint8_t *svg_doc_list = data + stbtt__get_svg((stbtt_fontinfo *)info);

    int numEntries = ttUSHORT(svg_doc_list);
    uint8_t *svg_docs = svg_doc_list + 2;

    for (i = 0; i < numEntries; i++)
    {
        uint8_t *svg_doc = svg_docs + (12 * i);
        if ((gl >= ttUSHORT(svg_doc)) && (gl <= ttUSHORT(svg_doc + 2)))
            return svg_doc;
    }
    return 0;
}

int stbtt_GetGlyphSVG(const stbtt_fontinfo *info, int gl, const char **svg)
{
    uint8_t *data = info->data;
    uint8_t *svg_doc;

    if (info->svg == 0)
        return 0;

    svg_doc = stbtt_FindSVGDoc(info, gl);
    if (svg_doc != NULL)
    {
        *svg = (char *)data + info->svg + ttULONG(svg_doc + 4);
        return ttULONG(svg_doc + 8);
    }
    else
    {
        return 0;
    }
}

int stbtt_GetCodepointSVG(const stbtt_fontinfo *info, int unicode_codepoint, const char **svg)
{
    return stbtt_GetGlyphSVG(info, stbtt_FindGlyphIndex(info, unicode_codepoint), svg);
}

//////////////////////////////////////////////////////////////////////////////
//
// antialiasing software rasterizer
//

void stbtt_GetGlyphBitmapBoxSubpixel(const stbtt_fontinfo *font, int glyph, float scale_x, float scale_y, float shift_x, float shift_y, int *ix0, int *iy0, int *ix1, int *iy1)
{
    int x0 = 0, y0 = 0, x1, y1; // =0 suppresses compiler warning
    if (!stbtt_GetGlyphBox(font, glyph, &x0, &y0, &x1, &y1))
    {
        // e.g. space character
        if (ix0)
            *ix0 = 0;
        if (iy0)
            *iy0 = 0;
        if (ix1)
            *ix1 = 0;
        if (iy1)
            *iy1 = 0;
    }
    else
    {
        // move to integral bboxes (treating pixels as little squares, what pixels get touched)?
        if (ix0)
            *ix0 = floor(x0 * scale_x + shift_x);

        if (iy0)
            *iy0 = floor(-y1 * scale_y + shift_y);

        if (ix1)
            *ix1 = ceil(x1 * scale_x + shift_x);

        if (iy1)
            *iy1 = ceil(-y0 * scale_y + shift_y);
    }
}

void stbtt_GetGlyphBitmapBox(const stbtt_fontinfo *font, int glyph, float scale_x, float scale_y, int *ix0, int *iy0, int *ix1, int *iy1)
{
    stbtt_GetGlyphBitmapBoxSubpixel(font, glyph, scale_x, scale_y, 0.0f, 0.0f, ix0, iy0, ix1, iy1);
}

void stbtt_GetCodepointBitmapBoxSubpixel(const stbtt_fontinfo *font, int codepoint, float scale_x, float scale_y, float shift_x, float shift_y, int *ix0, int *iy0, int *ix1, int *iy1)
{
    stbtt_GetGlyphBitmapBoxSubpixel(font, stbtt_FindGlyphIndex(font, codepoint), scale_x, scale_y, shift_x, shift_y, ix0, iy0, ix1, iy1);
}

void stbtt_GetCodepointBitmapBox(const stbtt_fontinfo *font, int codepoint, float scale_x, float scale_y, int *ix0, int *iy0, int *ix1, int *iy1)
{
    stbtt_GetCodepointBitmapBoxSubpixel(font, codepoint, scale_x, scale_y, 0.0f, 0.0f, ix0, iy0, ix1, iy1);
}

//////////////////////////////////////////////////////////////////////////////
//
//  Rasterizer

typedef struct stbtt__hheap_chunk
{
    struct stbtt__hheap_chunk *next;
} stbtt__hheap_chunk;

typedef struct stbtt__hheap
{
    struct stbtt__hheap_chunk *head;
    void *first_free;
    int num_remaining_in_head_chunk;
} stbtt__hheap;

static void *stbtt__hheap_alloc(stbtt__hheap *hh, size_t size)
{
    if (hh->first_free)
    {
        void *p = hh->first_free;
        hh->first_free = *(void **)p;
        return p;
    }
    else
    {
        if (hh->num_remaining_in_head_chunk == 0)
        {
            int count = (size < 32 ? 2000 : size < 128 ? 800 : 100);
            stbtt__hheap_chunk *c = (stbtt__hheap_chunk *)malloc(sizeof(stbtt__hheap_chunk) + size * count);
            if (c == NULL)
                return NULL;
            c->next = hh->head;
            hh->head = c;
            hh->num_remaining_in_head_chunk = count;
        }
        --hh->num_remaining_in_head_chunk;
        return (char *)(hh->head) + sizeof(stbtt__hheap_chunk) + size * hh->num_remaining_in_head_chunk;
    }
}

static void stbtt__hheap_free(stbtt__hheap *hh, void *p)
{
    *(void **)p = hh->first_free;
    hh->first_free = p;
}

static void stbtt__hheap_cleanup(stbtt__hheap *hh)
{
    stbtt__hheap_chunk *c = hh->head;
    while (c)
    {
        stbtt__hheap_chunk *n = c->next;
        free(c);
        c = n;
    }
}

typedef struct stbtt__edge
{
    float x0, y0, x1, y1;
    int invert;
} stbtt__edge;

typedef struct stbtt__active_edge
{
    struct stbtt__active_edge *next;
    float fx, fdx, fdy;
    float direction;
    float sy;
    float ey;
} stbtt__active_edge;

static stbtt__active_edge *stbtt__new_active(stbtt__hheap *hh, stbtt__edge *e, int off_x, float start_point)
{
    stbtt__active_edge *z = (stbtt__active_edge *)stbtt__hheap_alloc(hh, sizeof(*z));
    float dxdy = (e->x1 - e->x0) / (e->y1 - e->y0);
    assert(z != NULL);
    //assert(e->y0 <= start_point);
    if (!z)
        return z;

    z->fdx = dxdy;
    z->fdy = dxdy != 0.0f ? (1.0f / dxdy) : 0.0f;
    z->fx = e->x0 + dxdy * (start_point - e->y0);
    z->fx -= off_x;
    z->direction = e->invert ? 1.0f : -1.0f;
    z->sy = e->y0;
    z->ey = e->y1;
    z->next = 0;

    return z;
}

// the edge passed in here does not cross the vertical line at x or the vertical line at x+1
// (i.e. it has already been clipped to those)
static void stbtt__handle_clipped_edge(float *scanline, int x, stbtt__active_edge *e, float x0, float y0, float x1, float y1)
{
    if (y0 == y1)
        return;
    assert(y0 < y1);
    assert(e->sy <= e->ey);
    if (y0 > e->ey)
        return;
    if (y1 < e->sy)
        return;
    if (y0 < e->sy)
    {
        x0 += (x1 - x0) * (e->sy - y0) / (y1 - y0);
        y0 = e->sy;
    }
    if (y1 > e->ey)
    {
        x1 += (x1 - x0) * (e->ey - y1) / (y1 - y0);
        y1 = e->ey;
    }

    if (x0 == x)
        assert(x1 <= x + 1);
    else if (x0 == x + 1)
        assert(x1 >= x);
    else if (x0 <= x)
        assert(x1 <= x);
    else if (x0 >= x + 1)
        assert(x1 >= x + 1);
    else
        assert(x1 >= x && x1 <= x + 1);

    if (x0 <= x && x1 <= x)
        scanline[x] += e->direction * (y1 - y0);
    else if (x0 >= x + 1 && x1 >= x + 1)
        ;
    else
    {
        assert(x0 >= x && x0 <= x + 1 && x1 >= x && x1 <= x + 1);
        scanline[x] += e->direction * (y1 - y0) * (1 - ((x0 - x) + (x1 - x)) / 2); // coverage = 1 - average x position
    }
}

static void stbtt__fill_active_edges_new(float *scanline, float *scanline_fill, int len, stbtt__active_edge *e, float y_top)
{
    float y_bottom = y_top + 1;

    while (e)
    {
        // brute force every pixel

        // compute intersection points with top & bottom
        assert(e->ey >= y_top);

        if (e->fdx == 0)
        {
            float x0 = e->fx;
            if (x0 < len)
            {
                if (x0 >= 0)
                {
                    stbtt__handle_clipped_edge(scanline, (int)x0, e, x0, y_top, x0, y_bottom);
                    stbtt__handle_clipped_edge(scanline_fill - 1, (int)x0 + 1, e, x0, y_top, x0, y_bottom);
                }
                else
                {
                    stbtt__handle_clipped_edge(scanline_fill - 1, 0, e, x0, y_top, x0, y_bottom);
                }
            }
        }
        else
        {
            float x0 = e->fx;
            float dx = e->fdx;
            float xb = x0 + dx;
            float x_top, x_bottom;
            float sy0, sy1;
            float dy = e->fdy;
            assert(e->sy <= y_bottom && e->ey >= y_top);

            // compute endpoints of line segment clipped to this scanline (if the
            // line segment starts on this scanline. x0 is the intersection of the
            // line with y_top, but that may be off the line segment.
            if (e->sy > y_top)
            {
                x_top = x0 + dx * (e->sy - y_top);
                sy0 = e->sy;
            }
            else
            {
                x_top = x0;
                sy0 = y_top;
            }
            if (e->ey < y_bottom)
            {
                x_bottom = x0 + dx * (e->ey - y_top);
                sy1 = e->ey;
            }
            else
            {
                x_bottom = xb;
                sy1 = y_bottom;
            }

            if (x_top >= 0 && x_bottom >= 0 && x_top < len && x_bottom < len)
            {
                // from here on, we don't have to range check x values

                if ((int)x_top == (int)x_bottom)
                {
                    float height;
                    // simple case, only spans one pixel
                    int x = (int)x_top;
                    height = sy1 - sy0;
                    assert(x >= 0 && x < len);
                    scanline[x] += e->direction * (1 - ((x_top - x) + (x_bottom - x)) / 2) * height;
                    scanline_fill[x] += e->direction * height; // everything right of this pixel is filled
                }
                else
                {
                    int x, x1, x2;
                    float y_crossing, step, sign, area;
                    // covers 2+ pixels
                    if (x_top > x_bottom)
                    {
                        // flip scanline vertically; signed area is the same
                        float t;
                        sy0 = y_bottom - (sy0 - y_top);
                        sy1 = y_bottom - (sy1 - y_top);
                        t = sy0, sy0 = sy1, sy1 = t;
                        t = x_bottom, x_bottom = x_top, x_top = t;
                        dx = -dx;
                        dy = -dy;
                        t = x0, x0 = xb, xb = t;
                    }

                    x1 = (int)x_top;
                    x2 = (int)x_bottom;
                    // compute intersection with y axis at x1+1
                    y_crossing = (x1 + 1 - x0) * dy + y_top;

                    sign = e->direction;
                    // area of the rectangle covered from y0..y_crossing
                    area = sign * (y_crossing - sy0);
                    // area of the triangle (x_top,y0), (x+1,y0), (x+1,y_crossing)
                    scanline[x1] += area * (1 - ((x_top - x1) + (x1 + 1 - x1)) / 2);

                    step = sign * dy;
                    for (x = x1 + 1; x < x2; ++x)
                    {
                        scanline[x] += area + step / 2;
                        area += step;
                    }
                    y_crossing += dy * (x2 - (x1 + 1));

                    assert(fabs(area) <= 1.01f);

                    scanline[x2] += area + sign * (1 - ((x2 - x2) + (x_bottom - x2)) / 2) * (sy1 - y_crossing);

                    scanline_fill[x2] += sign * (sy1 - sy0);
                }
            }
            else
            {
                // if edge goes outside of box we're drawing, we require
                // clipping logic. since this does not match the intended use
                // of this library, we use a different, very slow brute
                // force implementation
                int x;
                for (x = 0; x < len; ++x)
                {
                    // cases:
                    //
                    // there can be up to two intersections with the pixel. any intersection
                    // with left or right edges can be handled by splitting into two (or three)
                    // regions. intersections with top & bottom do not necessitate case-wise logic.
                    //
                    // the old way of doing this found the intersections with the left & right edges,
                    // then used some simple logic to produce up to three segments in sorted order
                    // from top-to-bottom. however, this had a problem: if an x edge was epsilon
                    // across the x border, then the corresponding y position might not be distinct
                    // from the other y segment, and it might ignored as an empty segment. to avoid
                    // that, we need to explicitly produce segments based on x positions.

                    // rename variables to clearly-defined pairs
                    float y0 = y_top;
                    float x1 = (float)(x);
                    float x2 = (float)(x + 1);
                    float x3 = xb;
                    float y3 = y_bottom;

                    // x = e->x + e->dx * (y-y_top)
                    // (y-y_top) = (x - e->x) / e->dx
                    // y = (x - e->x) / e->dx + y_top
                    float y1 = (x - x0) / dx + y_top;
                    float y2 = (x + 1 - x0) / dx + y_top;

                    if (x0 < x1 && x3 > x2)
                    { // three segments descending down-right
                        stbtt__handle_clipped_edge(scanline, x, e, x0, y0, x1, y1);
                        stbtt__handle_clipped_edge(scanline, x, e, x1, y1, x2, y2);
                        stbtt__handle_clipped_edge(scanline, x, e, x2, y2, x3, y3);
                    }
                    else if (x3 < x1 && x0 > x2)
                    { // three segments descending down-left
                        stbtt__handle_clipped_edge(scanline, x, e, x0, y0, x2, y2);
                        stbtt__handle_clipped_edge(scanline, x, e, x2, y2, x1, y1);
                        stbtt__handle_clipped_edge(scanline, x, e, x1, y1, x3, y3);
                    }
                    else if (x0 < x1 && x3 > x1)
                    { // two segments across x, down-right
                        stbtt__handle_clipped_edge(scanline, x, e, x0, y0, x1, y1);
                        stbtt__handle_clipped_edge(scanline, x, e, x1, y1, x3, y3);
                    }
                    else if (x3 < x1 && x0 > x1)
                    { // two segments across x, down-left
                        stbtt__handle_clipped_edge(scanline, x, e, x0, y0, x1, y1);
                        stbtt__handle_clipped_edge(scanline, x, e, x1, y1, x3, y3);
                    }
                    else if (x0 < x2 && x3 > x2)
                    { // two segments across x+1, down-right
                        stbtt__handle_clipped_edge(scanline, x, e, x0, y0, x2, y2);
                        stbtt__handle_clipped_edge(scanline, x, e, x2, y2, x3, y3);
                    }
                    else if (x3 < x2 && x0 > x2)
                    { // two segments across x+1, down-left
                        stbtt__handle_clipped_edge(scanline, x, e, x0, y0, x2, y2);
                        stbtt__handle_clipped_edge(scanline, x, e, x2, y2, x3, y3);
                    }
                    else
                    { // one segment
                        stbtt__handle_clipped_edge(scanline, x, e, x0, y0, x3, y3);
                    }
                }
            }
        }
        e = e->next;
    }
}

// directly AA rasterize edges w/o supersampling
static void stbtt__rasterize_sorted_edges(stbtt__bitmap *result, stbtt__edge *e, int n, int vsubsample, int off_x, int off_y)
{
    stbtt__hheap hh = {0, 0, 0};
    stbtt__active_edge *active = NULL;
    int y, j = 0, i;
    float scanline_data[129], *scanline, *scanline2;

    __unused(vsubsample);

    if (result->w > 64)
        scanline = (float *)malloc((result->w * 2 + 1) * sizeof(float));
    else
        scanline = scanline_data;

    scanline2 = scanline + result->w;

    y = off_y;
    e[n].y0 = (float)(off_y + result->h) + 1;

    while (j < result->h)
    {
        // find center of pixel for this scanline
        float scan_y_top = y + 0.0f;
        float scan_y_bottom = y + 1.0f;
        stbtt__active_edge **step = &active;

        memset(scanline, 0, result->w * sizeof(scanline[0]));
        memset(scanline2, 0, (result->w + 1) * sizeof(scanline[0]));

        // update all active edges;
        // remove all active edges that terminate before the top of this scanline
        while (*step)
        {
            stbtt__active_edge *z = *step;
            if (z->ey <= scan_y_top)
            {
                *step = z->next; // delete from list
                assert(z->direction);
                z->direction = 0;
                stbtt__hheap_free(&hh, z);
            }
            else
            {
                step = &((*step)->next); // advance through list
            }
        }

        // insert all edges that start before the bottom of this scanline
        while (e->y0 <= scan_y_bottom)
        {
            if (e->y0 != e->y1)
            {
                stbtt__active_edge *z = stbtt__new_active(&hh, e, off_x, scan_y_top);
                if (z != NULL)
                {
                    if (j == 0 && off_y != 0)
                    {
                        if (z->ey < scan_y_top)
                        {
                            // this can happen due to subpixel positioning and some kind of fp rounding error i think
                            z->ey = scan_y_top;
                        }
                    }
                    assert(z->ey >= scan_y_top); // if we get really unlucky a tiny bit of an edge can be out of bounds
                    // insert at front
                    z->next = active;
                    active = z;
                }
            }
            ++e;
        }

        // now process all active edges
        if (active)
            stbtt__fill_active_edges_new(scanline, scanline2 + 1, result->w, active, scan_y_top);

        {
            float sum = 0;
            for (i = 0; i < result->w; ++i)
            {
                float k;
                int m;
                sum += scanline2[i];
                k = scanline[i] + sum;
                k = (float)fabs(k) * 255 + 0.5f;
                m = (int)k;
                if (m > 255)
                    m = 255;
                result->pixels[j * result->stride + i] = (unsigned char)m;
            }
        }
        // advance all the edges
        step = &active;
        while (*step)
        {
            stbtt__active_edge *z = *step;
            z->fx += z->fdx;         // advance to position for current scanline
            step = &((*step)->next); // advance through list
        }

        ++y;
        ++j;
    }

    stbtt__hheap_cleanup(&hh);

    if (scanline != scanline_data)
        free(scanline);
}

#define STBTT__COMPARE(a, b) ((a)->y0 < (b)->y0)

static void stbtt__sort_edges_ins_sort(stbtt__edge *p, int n)
{
    int i, j;
    for (i = 1; i < n; ++i)
    {
        stbtt__edge t = p[i], *a = &t;
        j = i;
        while (j > 0)
        {
            stbtt__edge *b = &p[j - 1];
            int c = STBTT__COMPARE(a, b);
            if (!c)
                break;
            p[j] = p[j - 1];
            --j;
        }
        if (i != j)
            p[j] = t;
    }
}

static void stbtt__sort_edges_quicksort(stbtt__edge *p, int n)
{
    /* threshold for transitioning to insertion sort */
    while (n > 12)
    {
        stbtt__edge t;
        int c01, c12, c, m, i, j;

        /* compute median of three */
        m = n >> 1;
        c01 = STBTT__COMPARE(&p[0], &p[m]);
        c12 = STBTT__COMPARE(&p[m], &p[n - 1]);
        /* if 0 >= mid >= end, or 0 < mid < end, then use mid */
        if (c01 != c12)
        {
            /* otherwise, we'll need to swap something else to middle */
            int z;
            c = STBTT__COMPARE(&p[0], &p[n - 1]);
            /* 0>mid && mid<n:  0>n => n; 0<n => 0 */
            /* 0<mid && mid>n:  0>n => 0; 0<n => n */
            z = (c == c12) ? 0 : n - 1;
            t = p[z];
            p[z] = p[m];
            p[m] = t;
        }
        /* now p[m] is the median-of-three */
        /* swap it to the beginning so it won't move around */
        t = p[0];
        p[0] = p[m];
        p[m] = t;

        /* partition loop */
        i = 1;
        j = n - 1;
        for (;;)
        {
            /* handling of equality is crucial here */
            /* for sentinels & efficiency with duplicates */
            for (;; ++i)
            {
                if (!STBTT__COMPARE(&p[i], &p[0]))
                    break;
            }
            for (;; --j)
            {
                if (!STBTT__COMPARE(&p[0], &p[j]))
                    break;
            }
            /* make sure we haven't crossed */
            if (i >= j)
                break;
            t = p[i];
            p[i] = p[j];
            p[j] = t;

            ++i;
            --j;
        }
        /* recurse on smaller side, iterate on larger */
        if (j < (n - i))
        {
            stbtt__sort_edges_quicksort(p, j);
            p = p + i;
            n = n - i;
        }
        else
        {
            stbtt__sort_edges_quicksort(p + i, n - i);
            n = j;
        }
    }
}

static void stbtt__sort_edges(stbtt__edge *p, int n)
{
    stbtt__sort_edges_quicksort(p, n);
    stbtt__sort_edges_ins_sort(p, n);
}

typedef struct
{
    float x, y;
} stbtt__point;

static void stbtt__rasterize(stbtt__bitmap *result, stbtt__point *pts, int *wcount, int windings, float scale_x, float scale_y, float shift_x, float shift_y, int off_x, int off_y, int invert)
{
    float y_scale_inv = invert ? -scale_y : scale_y;
    stbtt__edge *e;
    int n, i, j, k, m;
    int vsubsample = 1;

    // vsubsample should divide 255 evenly; otherwise we won't reach full opacity

    // now we have to blow out the windings into explicit edge lists
    n = 0;
    for (i = 0; i < windings; ++i)
        n += wcount[i];

    e = (stbtt__edge *)malloc(sizeof(*e) * (n + 1)); // add an extra one as a sentinel
    if (e == 0)
        return;
    n = 0;

    m = 0;
    for (i = 0; i < windings; ++i)
    {
        stbtt__point *p = pts + m;
        m += wcount[i];
        j = wcount[i] - 1;
        for (k = 0; k < wcount[i]; j = k++)
        {
            int a = k, b = j;
            // skip the edge if horizontal
            if (p[j].y == p[k].y)
                continue;
            // add edge from j to k to the list
            e[n].invert = 0;
            if (invert ? p[j].y > p[k].y : p[j].y < p[k].y)
            {
                e[n].invert = 1;
                a = j, b = k;
            }
            e[n].x0 = p[a].x * scale_x + shift_x;
            e[n].y0 = (p[a].y * y_scale_inv + shift_y) * vsubsample;
            e[n].x1 = p[b].x * scale_x + shift_x;
            e[n].y1 = (p[b].y * y_scale_inv + shift_y) * vsubsample;
            ++n;
        }
    }

    // now sort the edges by their highest point (should snap to integer, and then by x)
    //STBTT_sort(e, n, sizeof(e[0]), stbtt__edge_compare);
    stbtt__sort_edges(e, n);

    // now, traverse the scanlines and find the intersections on each scanline, use xor winding rule
    stbtt__rasterize_sorted_edges(result, e, n, vsubsample, off_x, off_y);

    free(e);
}

static void stbtt__add_point(stbtt__point *points, int n, float x, float y)
{
    if (!points)
        return; // during first pass, it's unallocated
    points[n].x = x;
    points[n].y = y;
}

// tessellate until threshold p is happy... @TODO warped to compensate for non-linear stretching
static int stbtt__tesselate_curve(stbtt__point *points, int *num_points, float x0, float y0, float x1, float y1, float x2, float y2, float objspace_flatness_squared, int n)
{
    // midpoint
    float mx = (x0 + 2 * x1 + x2) / 4;
    float my = (y0 + 2 * y1 + y2) / 4;
    // versus directly drawn line
    float dx = (x0 + x2) / 2 - mx;
    float dy = (y0 + y2) / 2 - my;
    if (n > 16) // 65536 segments on one curve better be enough!
        return 1;
    if (dx * dx + dy * dy > objspace_flatness_squared)
    { // half-pixel error allowed... need to be smaller if AA
        stbtt__tesselate_curve(points, num_points, x0, y0, (x0 + x1) / 2.0f, (y0 + y1) / 2.0f, mx, my, objspace_flatness_squared, n + 1);
        stbtt__tesselate_curve(points, num_points, mx, my, (x1 + x2) / 2.0f, (y1 + y2) / 2.0f, x2, y2, objspace_flatness_squared, n + 1);
    }
    else
    {
        stbtt__add_point(points, *num_points, x2, y2);
        *num_points = *num_points + 1;
    }
    return 1;
}

static void stbtt__tesselate_cubic(stbtt__point *points, int *num_points, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float objspace_flatness_squared, int n)
{
    // @TODO this "flatness" calculation is just made-up nonsense that seems to work well enough
    float dx0 = x1 - x0;
    float dy0 = y1 - y0;
    float dx1 = x2 - x1;
    float dy1 = y2 - y1;
    float dx2 = x3 - x2;
    float dy2 = y3 - y2;
    float dx = x3 - x0;
    float dy = y3 - y0;
    float longlen = (float)(sqrt(dx0 * dx0 + dy0 * dy0) + sqrt(dx1 * dx1 + dy1 * dy1) + sqrt(dx2 * dx2 + dy2 * dy2));
    float shortlen = (float)sqrt(dx * dx + dy * dy);
    float flatness_squared = longlen * longlen - shortlen * shortlen;

    if (n > 16) // 65536 segments on one curve better be enough!
        return;

    if (flatness_squared > objspace_flatness_squared)
    {
        float x01 = (x0 + x1) / 2;
        float y01 = (y0 + y1) / 2;
        float x12 = (x1 + x2) / 2;
        float y12 = (y1 + y2) / 2;
        float x23 = (x2 + x3) / 2;
        float y23 = (y2 + y3) / 2;

        float xa = (x01 + x12) / 2;
        float ya = (y01 + y12) / 2;
        float xb = (x12 + x23) / 2;
        float yb = (y12 + y23) / 2;

        float mx = (xa + xb) / 2;
        float my = (ya + yb) / 2;

        stbtt__tesselate_cubic(points, num_points, x0, y0, x01, y01, xa, ya, mx, my, objspace_flatness_squared, n + 1);
        stbtt__tesselate_cubic(points, num_points, mx, my, xb, yb, x23, y23, x3, y3, objspace_flatness_squared, n + 1);
    }
    else
    {
        stbtt__add_point(points, *num_points, x3, y3);
        *num_points = *num_points + 1;
    }
}

// returns number of contours
static stbtt__point *stbtt_FlattenCurves(stbtt_vertex *vertices, int num_verts, float objspace_flatness, int **contour_lengths, int *num_contours)
{
    stbtt__point *points = 0;
    int num_points = 0;

    float objspace_flatness_squared = objspace_flatness * objspace_flatness;
    int i, n = 0, start = 0, pass;

    // count how many "moves" there are to get the contour count
    for (i = 0; i < num_verts; ++i)
        if (vertices[i].type == STBTT_vmove)
            ++n;

    *num_contours = n;
    if (n == 0)
        return 0;

    *contour_lengths = (int *)malloc(sizeof(**contour_lengths) * n);

    if (*contour_lengths == 0)
    {
        *num_contours = 0;
        return 0;
    }

    // make two passes through the points so we don't need to realloc
    for (pass = 0; pass < 2; ++pass)
    {
        float x = 0, y = 0;
        if (pass == 1)
        {
            points = (stbtt__point *)malloc(num_points * sizeof(points[0]));
            if (points == NULL)
                goto error;
        }
        num_points = 0;
        n = -1;
        for (i = 0; i < num_verts; ++i)
        {
            switch (vertices[i].type)
            {
            case STBTT_vmove:
                // start the next contour
                if (n >= 0)
                    (*contour_lengths)[n] = num_points - start;
                ++n;
                start = num_points;

                x = vertices[i].x, y = vertices[i].y;
                stbtt__add_point(points, num_points++, x, y);
                break;
            case STBTT_vline:
                x = vertices[i].x, y = vertices[i].y;
                stbtt__add_point(points, num_points++, x, y);
                break;
            case STBTT_vcurve:
                stbtt__tesselate_curve(points, &num_points, x, y,
                                       vertices[i].cx, vertices[i].cy,
                                       vertices[i].x, vertices[i].y,
                                       objspace_flatness_squared, 0);
                x = vertices[i].x, y = vertices[i].y;
                break;
            case STBTT_vcubic:
                stbtt__tesselate_cubic(points, &num_points, x, y,
                                       vertices[i].cx, vertices[i].cy,
                                       vertices[i].cx1, vertices[i].cy1,
                                       vertices[i].x, vertices[i].y,
                                       objspace_flatness_squared, 0);
                x = vertices[i].x, y = vertices[i].y;
                break;
            }
        }
        (*contour_lengths)[n] = num_points - start;
    }

    return points;
error:
    free(points);
    free(*contour_lengths);
    *contour_lengths = 0;
    *num_contours = 0;
    return NULL;
}

void stbtt_Rasterize(stbtt__bitmap *result, float flatness_in_pixels, stbtt_vertex *vertices, int num_verts, float scale_x, float scale_y, float shift_x, float shift_y, int x_off, int y_off, int invert)
{
    float scale = scale_x > scale_y ? scale_y : scale_x;
    int winding_count = 0;
    int *winding_lengths = NULL;
    stbtt__point *windings = stbtt_FlattenCurves(vertices, num_verts, flatness_in_pixels / scale, &winding_lengths, &winding_count);
    if (windings)
    {
        stbtt__rasterize(result, windings, winding_lengths, winding_count, scale_x, scale_y, shift_x, shift_y, x_off, y_off, invert);
        free(winding_lengths);
        free(windings);
    }
}

void freeBitmap(unsigned char *bitmap)
{
    free(bitmap);
}

unsigned char *stbtt_GetGlyphBitmapSubpixel(const stbtt_fontinfo *info, float scale_x, float scale_y, float shift_x, float shift_y, int glyph, int *width, int *height, int *xoff, int *yoff)
{
    int ix0, iy0, ix1, iy1;
    stbtt__bitmap gbm;
    stbtt_vertex *vertices;
    int num_verts = stbtt_GetGlyphShape(info, glyph, &vertices);

    if (scale_x == 0)
        scale_x = scale_y;
    if (scale_y == 0)
    {
        if (scale_x == 0)
        {
            free(vertices);
            return NULL;
        }
        scale_y = scale_x;
    }

    stbtt_GetGlyphBitmapBoxSubpixel(info, glyph, scale_x, scale_y, shift_x, shift_y, &ix0, &iy0, &ix1, &iy1);

    // now we get the size
    gbm.w = (ix1 - ix0);
    gbm.h = (iy1 - iy0);
    gbm.pixels = NULL; // in case we error

    if (width)
        *width = gbm.w;
    if (height)
        *height = gbm.h;
    if (xoff)
        *xoff = ix0;
    if (yoff)
        *yoff = iy0;

    if (gbm.w && gbm.h)
    {
        gbm.pixels = (unsigned char *)malloc(gbm.w * gbm.h);
        if (gbm.pixels)
        {
            gbm.stride = gbm.w;

            stbtt_Rasterize(&gbm, 0.35f, vertices, num_verts, scale_x, scale_y, shift_x, shift_y, ix0, iy0, 1);
        }
    }
    free(vertices);
    return gbm.pixels;
}

unsigned char *stbtt_GetGlyphBitmap(const stbtt_fontinfo *info, float scale_x, float scale_y, int glyph, int *width, int *height, int *xoff, int *yoff)
{
    return stbtt_GetGlyphBitmapSubpixel(info, scale_x, scale_y, 0.0f, 0.0f, glyph, width, height, xoff, yoff);
}

void stbtt_MakeGlyphBitmapSubpixel(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x, float shift_y, int glyph)
{
    stbtt_vertex *vertices = NULL;
    int num_verts = stbtt_GetGlyphShape(info, glyph, &vertices);

    int ix0, iy0;
    stbtt_GetGlyphBitmapBoxSubpixel(info, glyph, scale_x, scale_y, shift_x, shift_y, &ix0, &iy0, 0, 0);

    stbtt__bitmap gbm;
    gbm.pixels = output;
    gbm.w = out_w;
    gbm.h = out_h;
    gbm.stride = out_stride;

    if (gbm.w && gbm.h)
        stbtt_Rasterize(&gbm, 0.35f, vertices, num_verts, scale_x, scale_y, shift_x, shift_y, ix0, iy0, 1);

    if (vertices)
        free(vertices);
}

void stbtt_MakeGlyphBitmap(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, int glyph)
{
    stbtt_MakeGlyphBitmapSubpixel(info, output, out_w, out_h, out_stride, scale_x, scale_y, 0.0f, 0.0f, glyph);
}

unsigned char *stbtt_GetCodepointBitmapSubpixel(const stbtt_fontinfo *info, float scale_x, float scale_y, float shift_x, float shift_y, int codepoint, int *width, int *height, int *xoff, int *yoff)
{
    return stbtt_GetGlyphBitmapSubpixel(info, scale_x, scale_y, shift_x, shift_y, stbtt_FindGlyphIndex(info, codepoint), width, height, xoff, yoff);
}

void stbtt_MakeCodepointBitmapSubpixelPrefilter(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x, float shift_y, int oversample_x, int oversample_y, float *sub_x, float *sub_y, int codepoint)
{
    stbtt_MakeGlyphBitmapSubpixelPrefilter(info, output, out_w, out_h, out_stride, scale_x, scale_y, shift_x, shift_y, oversample_x, oversample_y, sub_x, sub_y, stbtt_FindGlyphIndex(info, codepoint));
}

void stbtt_MakeCodepointBitmapSubpixel(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x, float shift_y, int codepoint)
{
    stbtt_MakeGlyphBitmapSubpixel(info, output, out_w, out_h, out_stride, scale_x, scale_y, shift_x, shift_y, stbtt_FindGlyphIndex(info, codepoint));
}

unsigned char *stbtt_GetCodepointBitmap(const stbtt_fontinfo *info, float scale_x, float scale_y, int codepoint, int *width, int *height, int *xoff, int *yoff)
{
    return stbtt_GetCodepointBitmapSubpixel(info, scale_x, scale_y, 0.0f, 0.0f, codepoint, width, height, xoff, yoff);
}

void stbtt_MakeCodepointBitmap(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, int codepoint)
{
    stbtt_MakeCodepointBitmapSubpixel(info, output, out_w, out_h, out_stride, scale_x, scale_y, 0.0f, 0.0f, codepoint);
}

//////////////////////////////////////////////////////////////////////////////
//
// bitmap baking
//
// This is SUPER-CRAPPY packing to keep source code small

static int stbtt_BakeFontBitmap_internal(const unsigned char *data, int offset, // font location (use offset=0 for plain .ttf)
                                         float pixel_height,                    // height of font in pixels
                                         unsigned char *pixels, int pw, int ph, // bitmap to be filled in
                                         int first_char, int num_chars,         // characters to bake
                                         stbtt_bakedchar *chardata)
{
    float scale;
    int x, y, bottom_y, i;
    stbtt_fontinfo f;

    if (!stbtt_InitFont(&f, data, offset))
        return -1;
    memset(pixels, 0, pw * ph); // background of 0 around pixels
    x = y = 1;
    bottom_y = 1;

    scale = stbtt_ScaleForPixelHeight(&f, pixel_height);

    for (i = 0; i < num_chars; ++i)
    {
        int advance, lsb, x0, y0, x1, y1, gw, gh;
        int g = stbtt_FindGlyphIndex(&f, first_char + i);
        stbtt_GetGlyphHMetrics(&f, g, &advance, &lsb);
        stbtt_GetGlyphBitmapBox(&f, g, scale, scale, &x0, &y0, &x1, &y1);
        gw = x1 - x0;
        gh = y1 - y0;
        if (x + gw + 1 >= pw)
            y = bottom_y, x = 1; // advance to next row
        if (y + gh + 1 >= ph)    // check if it fits vertically AFTER potentially moving to next row
            return -i;
        assert(x + gw < pw);
        assert(y + gh < ph);
        stbtt_MakeGlyphBitmap(&f, pixels + x + y * pw, gw, gh, pw, scale, scale, g);
        chardata[i].x0 = (int16_t)x;
        chardata[i].y0 = (int16_t)y;
        chardata[i].x1 = (int16_t)(x + gw);
        chardata[i].y1 = (int16_t)(y + gh);
        chardata[i].xadvance = scale * advance;
        chardata[i].xoff = (float)x0;
        chardata[i].yoff = (float)y0;
        x = x + gw + 1;
        if (y + gh + 1 > bottom_y)
            bottom_y = y + gh + 1;
    }
    return bottom_y;
}

void stbtt_GetBakedQuad(const stbtt_bakedchar *chardata, int pw, int ph, int char_index, float *xpos, float *ypos, stbtt_aligned_quad *q, int opengl_fillrule)
{
    float d3d_bias = opengl_fillrule ? 0 : -0.5f;
    float ipw = 1.0f / pw, iph = 1.0f / ph;
    const stbtt_bakedchar *b = chardata + char_index;
    int round_x = floor((*xpos + b->xoff) + 0.5f);
    int round_y = floor((*ypos + b->yoff) + 0.5f);

    q->x0 = round_x + d3d_bias;
    q->y0 = round_y + d3d_bias;
    q->x1 = round_x + b->x1 - b->x0 + d3d_bias;
    q->y1 = round_y + b->y1 - b->y0 + d3d_bias;

    q->s0 = b->x0 * ipw;
    q->t0 = b->y0 * iph;
    q->s1 = b->x1 * ipw;
    q->t1 = b->y1 * iph;

    *xpos += b->xadvance;
}

//////////////////////////////////////////////////////////////////////////////
//
// rectangle packing replacement routines if you don't have stb_rect_pack.h
//

#ifndef STB_RECT_PACK_VERSION

typedef int stbrp_coord;

typedef struct
{
    int width, height;
    int x, y, bottom_y;
} stbrp_context;

typedef struct
{
    unsigned char x;
} stbrp_node;

struct stbrp_rect
{
    stbrp_coord x, y;
    int id, w, h, was_packed;
};

static void stbrp_init_target(stbrp_context *con, int pw, int ph, stbrp_node *nodes, int num_nodes)
{
    con->width = pw;
    con->height = ph;
    con->x = 0;
    con->y = 0;
    con->bottom_y = 0;
    __unused(nodes);
    __unused(num_nodes);
}

static void stbrp_pack_rects(stbrp_context *con, stbrp_rect *rects, int num_rects)
{
    int i;

    for (i = 0; i < num_rects; ++i)
    {
        if (con->x + rects[i].w > con->width)
        {
            con->x = 0;
            con->y = con->bottom_y;
        }
        if (con->y + rects[i].h > con->height)
            break;
        rects[i].x = con->x;
        rects[i].y = con->y;
        rects[i].was_packed = 1;
        con->x += rects[i].w;
        if (con->y + rects[i].h > con->bottom_y)
            con->bottom_y = con->y + rects[i].h;
    }

    for (; i < num_rects; ++i)
        rects[i].was_packed = 0;
}
#endif

//////////////////////////////////////////////////////////////////////////////
//
// bitmap baking
//
// This is SUPER-AWESOME (tm Ryan Gordon) packing using stb_rect_pack.h. If
// stb_rect_pack.h isn't available, it uses the BakeFontBitmap strategy.

int stbtt_PackBegin(stbtt_pack_context *spc, unsigned char *pixels, int pw, int ph, int stride_in_bytes, int padding)
{
    stbrp_context *context = (stbrp_context *)malloc(sizeof(*context));
    int num_nodes = pw - padding;
    stbrp_node *nodes = (stbrp_node *)malloc(sizeof(*nodes) * num_nodes);

    if (context == NULL || nodes == NULL)
    {
        if (context != NULL)
            free(context);
        if (nodes != NULL)
            free(nodes);
        return 0;
    }

    spc->width = pw;
    spc->height = ph;
    spc->pixels = pixels;
    spc->pack_info = context;
    spc->nodes = nodes;
    spc->padding = padding;
    spc->stride_in_bytes = stride_in_bytes != 0 ? stride_in_bytes : pw;
    spc->h_oversample = 1;
    spc->v_oversample = 1;
    spc->skip_missing = 0;

    stbrp_init_target(context, pw - padding, ph - padding, nodes, num_nodes);

    if (pixels)
        memset(pixels, 0, pw * ph); // background of 0 around pixels

    return 1;
}

void stbtt_PackEnd(stbtt_pack_context *spc)
{
    free(spc->nodes);
    free(spc->pack_info);
}

void stbtt_PackSetOversampling(stbtt_pack_context *spc, unsigned int h_oversample, unsigned int v_oversample)
{
    assert(h_oversample <= STBTT_MAX_OVERSAMPLE);
    assert(v_oversample <= STBTT_MAX_OVERSAMPLE);
    if (h_oversample <= STBTT_MAX_OVERSAMPLE)
        spc->h_oversample = h_oversample;
    if (v_oversample <= STBTT_MAX_OVERSAMPLE)
        spc->v_oversample = v_oversample;
}

void stbtt_PackSetSkipMissingCodepoints(stbtt_pack_context *spc, int skip)
{
    spc->skip_missing = skip;
}

#define STBTT__OVER_MASK (STBTT_MAX_OVERSAMPLE - 1)

void stbtt__h_prefilter(unsigned char *pixels, int w, int h, int stride_in_bytes, unsigned int kernel_width)
{
    unsigned char buffer[STBTT_MAX_OVERSAMPLE] = {};
    int safe_w = w - kernel_width;

    for (int j = 0; j < h; ++j)
    {
        int i;
        unsigned int total;
        memset(buffer, 0, kernel_width);

        total = 0;

        for (i = 0; i <= safe_w; ++i)
        {
            total += pixels[i] - buffer[i & STBTT__OVER_MASK];
            buffer[(i + kernel_width) & STBTT__OVER_MASK] = pixels[i];
            pixels[i] = (unsigned char)(total / kernel_width);
        }

        for (; i < w; ++i)
        {
            assert(pixels[i] == 0);
            total -= buffer[i & STBTT__OVER_MASK];
            pixels[i] = (unsigned char)(total / kernel_width);
        }

        pixels += stride_in_bytes;
    }
}

void stbtt__v_prefilter(unsigned char *pixels, int w, int h, int stride_in_bytes, unsigned int kernel_width)
{
    unsigned char buffer[STBTT_MAX_OVERSAMPLE] = {};
    int safe_h = h - kernel_width;

    for (int j = 0; j < w; ++j)
    {
        int i;
        unsigned int total;
        memset(buffer, 0, kernel_width);

        total = 0;

        for (i = 0; i <= safe_h; ++i)
        {
            total += pixels[i * stride_in_bytes] - buffer[i & STBTT__OVER_MASK];
            buffer[(i + kernel_width) & STBTT__OVER_MASK] = pixels[i * stride_in_bytes];
            pixels[i * stride_in_bytes] = (unsigned char)(total / kernel_width);
        }

        for (; i < h; ++i)
        {
            assert(pixels[i * stride_in_bytes] == 0);
            total -= buffer[i & STBTT__OVER_MASK];
            pixels[i * stride_in_bytes] = (unsigned char)(total / kernel_width);
        }

        pixels += 1;
    }
}

static float stbtt__oversample_shift(int oversample)
{
    if (!oversample)
        return 0.0f;

    // The prefilter is a box filter of width "oversample",
    // which shifts phase by (oversample - 1)/2 pixels in
    // oversampled space. We want to shift in the opposite
    // direction to counter this.
    return (float)-(oversample - 1) / (2.0f * (float)oversample);
}

// rects array must be big enough to accommodate all characters in the given ranges
int stbtt_PackFontGatherRects(stbtt_pack_context *spc, const stbtt_fontinfo *info, stbtt_pack_range *range, stbrp_rect *rects)
{
    int j, k;
    int missing_glyph_added = 0;

    k = 0;

    float fh = range->font_size;
    float scale = fh > 0 ? stbtt_ScaleForPixelHeight(info, fh) : stbtt_ScaleForMappingEmToPixels(info, -fh);
    range->h_oversample = (unsigned char)spc->h_oversample;
    range->v_oversample = (unsigned char)spc->v_oversample;
    for (j = 0; j < range->num_chars; ++j)
    {
        int x0, y0, x1, y1;
        int codepoint = range->array_of_unicode_codepoints == NULL ? range->first_unicode_codepoint_in_range + j : range->array_of_unicode_codepoints[j];
        int glyph = stbtt_FindGlyphIndex(info, codepoint);
        if (glyph == 0 && (spc->skip_missing || missing_glyph_added))
        {
            rects[k].w = rects[k].h = 0;
        }
        else
        {
            stbtt_GetGlyphBitmapBoxSubpixel(info, glyph,
                                            scale * spc->h_oversample,
                                            scale * spc->v_oversample,
                                            0, 0,
                                            &x0, &y0, &x1, &y1);
            rects[k].w = (stbrp_coord)(x1 - x0 + spc->padding + spc->h_oversample - 1);
            rects[k].h = (stbrp_coord)(y1 - y0 + spc->padding + spc->v_oversample - 1);
            if (glyph == 0)
                missing_glyph_added = 1;
        }
        ++k;
    }

    return k;
}

void stbtt_MakeGlyphBitmapSubpixelPrefilter(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x, float shift_y, int prefilter_x, int prefilter_y, float *sub_x, float *sub_y, int glyph)
{
    stbtt_MakeGlyphBitmapSubpixel(info,
                                  output,
                                  out_w - (prefilter_x - 1),
                                  out_h - (prefilter_y - 1),
                                  out_stride,
                                  scale_x,
                                  scale_y,
                                  shift_x,
                                  shift_y,
                                  glyph);

    if (prefilter_x > 1)
        stbtt__h_prefilter(output, out_w, out_h, out_stride, prefilter_x);

    if (prefilter_y > 1)
        stbtt__v_prefilter(output, out_w, out_h, out_stride, prefilter_y);

    *sub_x = stbtt__oversample_shift(prefilter_x);
    *sub_y = stbtt__oversample_shift(prefilter_y);
}

// rects array must be big enough to accommodate all characters in the given ranges
int stbtt_PackFontRenderIntoRects(stbtt_pack_context *spc, const stbtt_fontinfo *info, stbtt_pack_range *range, stbrp_rect *rects)
{
    int j, k, missing_glyph = -1, return_value = 1;

    // save current values
    int old_h_over = spc->h_oversample;
    int old_v_over = spc->v_oversample;

    k = 0;

    float fh = range->font_size;
    float scale = fh > 0 ? stbtt_ScaleForPixelHeight(info, fh) : stbtt_ScaleForMappingEmToPixels(info, -fh);
    float recip_h, recip_v, sub_x, sub_y;
    spc->h_oversample = range->h_oversample;
    spc->v_oversample = range->v_oversample;
    recip_h = 1.0f / spc->h_oversample;
    recip_v = 1.0f / spc->v_oversample;
    sub_x = stbtt__oversample_shift(spc->h_oversample);
    sub_y = stbtt__oversample_shift(spc->v_oversample);

    for (j = 0; j < range->num_chars; ++j)
    {
        stbrp_rect *r = &rects[k];
        if (r->was_packed && r->w != 0 && r->h != 0)
        {
            stbtt_packedchar *bc = &range->chardata_for_range[j];
            int advance, lsb, x0, y0, x1, y1;
            int codepoint = range->array_of_unicode_codepoints == NULL ? range->first_unicode_codepoint_in_range + j : range->array_of_unicode_codepoints[j];
            int glyph = stbtt_FindGlyphIndex(info, codepoint);
            stbrp_coord pad = (stbrp_coord)spc->padding;

            // pad on left and top
            r->x += pad;
            r->y += pad;
            r->w -= pad;
            r->h -= pad;

            stbtt_GetGlyphHMetrics(info, glyph, &advance, &lsb);
            stbtt_GetGlyphBitmapBox(info, glyph,
                                    scale * spc->h_oversample,
                                    scale * spc->v_oversample,
                                    &x0, &y0, &x1, &y1);

            stbtt_MakeGlyphBitmapSubpixel(info,
                                          spc->pixels + r->x + r->y * spc->stride_in_bytes,
                                          r->w - spc->h_oversample + 1,
                                          r->h - spc->v_oversample + 1,
                                          spc->stride_in_bytes,
                                          scale * spc->h_oversample,
                                          scale * spc->v_oversample,
                                          0, 0,
                                          glyph);

            if (spc->h_oversample > 1)
                stbtt__h_prefilter(spc->pixels + r->x + r->y * spc->stride_in_bytes,
                                   r->w, r->h, spc->stride_in_bytes,
                                   spc->h_oversample);

            if (spc->v_oversample > 1)
                stbtt__v_prefilter(spc->pixels + r->x + r->y * spc->stride_in_bytes,
                                   r->w, r->h, spc->stride_in_bytes,
                                   spc->v_oversample);

            bc->x0 = (int16_t)r->x;
            bc->y0 = (int16_t)r->y;
            bc->x1 = (int16_t)(r->x + r->w);
            bc->y1 = (int16_t)(r->y + r->h);
            bc->xadvance = scale * advance;
            bc->xoff = (float)x0 * recip_h + sub_x;
            bc->yoff = (float)y0 * recip_v + sub_y;
            bc->xoff2 = (x0 + r->w) * recip_h + sub_x;
            bc->yoff2 = (y0 + r->h) * recip_v + sub_y;

            if (glyph == 0)
                missing_glyph = j;
        }
        else if (spc->skip_missing)
        {
            return_value = 0;
        }
        else if (r->was_packed && r->w == 0 && r->h == 0 && missing_glyph >= 0)
        {
            range->chardata_for_range[j] = range->chardata_for_range[missing_glyph];
        }
        else
        {
            return_value = 0; // if any fail, report failure
        }

        ++k;
    }

    // restore original values
    spc->h_oversample = old_h_over;
    spc->v_oversample = old_v_over;

    return return_value;
}

void stbtt_PackFontRangesPackRects(stbtt_pack_context *spc, stbrp_rect *rects, int num_rects)
{
    stbrp_pack_rects((stbrp_context *)spc->pack_info, rects, num_rects);
}

int stbtt_PackFontRange_internal(stbtt_pack_context *spc, const unsigned char *fontdata, int font_index, stbtt_pack_range *range)
{
    memset(range->chardata_for_range, 0, range->num_chars * sizeof(stbtt_packedchar));

    __cleanup_malloc stbrp_rect *rects = (stbrp_rect *)malloc(sizeof(*rects) * range->num_chars);

    stbtt_fontinfo info = {};
    stbtt_InitFont(&info, fontdata, stbtt_GetFontOffsetForIndex(fontdata, font_index));

    int n = stbtt_PackFontGatherRects(spc, &info, range, rects);

    stbtt_PackFontRangesPackRects(spc, rects, n);

    return stbtt_PackFontRenderIntoRects(spc, &info, range, rects);
}

int stbtt_PackFontRange(stbtt_pack_context *spc, const unsigned char *fontdata, int font_index, float font_size,
                        int first_unicode_codepoint_in_range, int num_chars_in_range, stbtt_packedchar *chardata_for_range)
{
    stbtt_pack_range range;

    range.first_unicode_codepoint_in_range = first_unicode_codepoint_in_range;
    range.array_of_unicode_codepoints = NULL;
    range.num_chars = num_chars_in_range;
    range.chardata_for_range = chardata_for_range;
    range.font_size = font_size;

    return stbtt_PackFontRange_internal(spc, fontdata, font_index, &range);
}

void stbtt_GetScaledFontVMetrics(const unsigned char *fontdata, int index, float size, float *ascent, float *descent, float *lineGap)
{
    int i_ascent, i_descent, i_lineGap;
    float scale;
    stbtt_fontinfo info;
    stbtt_InitFont(&info, fontdata, stbtt_GetFontOffsetForIndex(fontdata, index));
    scale = size > 0 ? stbtt_ScaleForPixelHeight(&info, size) : stbtt_ScaleForMappingEmToPixels(&info, -size);
    stbtt_GetFontVMetrics(&info, &i_ascent, &i_descent, &i_lineGap);
    *ascent = (float)i_ascent * scale;
    *descent = (float)i_descent * scale;
    *lineGap = (float)i_lineGap * scale;
}

void stbtt_GetPackedQuad(const stbtt_packedchar *chardata, int pw, int ph, int char_index, float *xpos, float *ypos, stbtt_aligned_quad *q, int align_to_integer)
{
    float ipw = 1.0f / pw, iph = 1.0f / ph;
    const stbtt_packedchar *b = chardata + char_index;

    if (align_to_integer)
    {
        float x = (float)floor((*xpos + b->xoff) + 0.5f);
        float y = (float)floor((*ypos + b->yoff) + 0.5f);
        q->x0 = x;
        q->y0 = y;
        q->x1 = x + b->xoff2 - b->xoff;
        q->y1 = y + b->yoff2 - b->yoff;
    }
    else
    {
        q->x0 = *xpos + b->xoff;
        q->y0 = *ypos + b->yoff;
        q->x1 = *xpos + b->xoff2;
        q->y1 = *ypos + b->yoff2;
    }

    q->s0 = b->x0 * ipw;
    q->t0 = b->y0 * iph;
    q->s1 = b->x1 * ipw;
    q->t1 = b->y1 * iph;

    *xpos += b->xadvance;
}

//////////////////////////////////////////////////////////////////////////////
//
// font name matching -- recommended not to use this
//

// check if a utf8 string contains a prefix which is the utf16 string; if so return length of matching utf8 string
static int32_t stbtt__CompareUTF8toUTF16_bigendian_prefix(uint8_t *s1, int32_t len1, uint8_t *s2, int32_t len2)
{
    int32_t i = 0;

    // convert utf16 to utf8 and compare the results while converting
    while (len2)
    {
        uint16_t ch = s2[0] * 256 + s2[1];
        if (ch < 0x80)
        {
            if (i >= len1)
                return -1;
            if (s1[i++] != ch)
                return -1;
        }
        else if (ch < 0x800)
        {
            if (i + 1 >= len1)
                return -1;
            if (s1[i++] != 0xc0 + (ch >> 6))
                return -1;
            if (s1[i++] != 0x80 + (ch & 0x3f))
                return -1;
        }
        else if (ch >= 0xd800 && ch < 0xdc00)
        {
            uint32_t c;
            uint16_t ch2 = s2[2] * 256 + s2[3];
            if (i + 3 >= len1)
                return -1;
            c = ((ch - 0xd800) << 10) + (ch2 - 0xdc00) + 0x10000;
            if (s1[i++] != 0xf0 + (c >> 18))
                return -1;
            if (s1[i++] != 0x80 + ((c >> 12) & 0x3f))
                return -1;
            if (s1[i++] != 0x80 + ((c >> 6) & 0x3f))
                return -1;
            if (s1[i++] != 0x80 + ((c)&0x3f))
                return -1;
            s2 += 2; // plus another 2 below
            len2 -= 2;
        }
        else if (ch >= 0xdc00 && ch < 0xe000)
        {
            return -1;
        }
        else
        {
            if (i + 2 >= len1)
                return -1;
            if (s1[i++] != 0xe0 + (ch >> 12))
                return -1;
            if (s1[i++] != 0x80 + ((ch >> 6) & 0x3f))
                return -1;
            if (s1[i++] != 0x80 + ((ch)&0x3f))
                return -1;
        }
        s2 += 2;
        len2 -= 2;
    }
    return i;
}

static int stbtt_CompareUTF8toUTF16_bigendian_internal(char *s1, int len1, char *s2, int len2)
{
    return len1 == stbtt__CompareUTF8toUTF16_bigendian_prefix((uint8_t *)s1, len1, (uint8_t *)s2, len2);
}

// returns results in whatever encoding you request... but note that 2-byte encodings
// will be BIG-ENDIAN... use stbtt_CompareUTF8toUTF16_bigendian() to compare
const char *stbtt_GetFontNameString(const stbtt_fontinfo *font, int *length, int platformID, int encodingID, int languageID, int nameID)
{
    uint8_t *fc = font->data;
    uint32_t offset = font->fontstart;
    uint32_t name_table = stbtt__find_table(fc, offset, "name");

    if (!name_table)
        return NULL;

    int32_t count = ttUSHORT(fc + name_table + 2);
    int32_t stringOffset = name_table + ttUSHORT(fc + name_table + 4);

    for (int i = 0; i < count; i++)
    {
        uint32_t loc = name_table + 6 + 12 * i;
        if (platformID == ttUSHORT(fc + loc + 0) && encodingID == ttUSHORT(fc + loc + 2) && languageID == ttUSHORT(fc + loc + 4) && nameID == ttUSHORT(fc + loc + 6))
        {
            *length = ttUSHORT(fc + loc + 8);
            return (const char *)(fc + stringOffset + ttUSHORT(fc + loc + 10));
        }
    }
    return NULL;
}

static int stbtt__matchpair(uint8_t *fc, uint32_t nm, uint8_t *name, int32_t nlen, int32_t target_id, int32_t next_id)
{
    int32_t count = ttUSHORT(fc + nm + 2);
    int32_t stringOffset = nm + ttUSHORT(fc + nm + 4);

    for (int32_t i = 0; i < count; ++i)
    {
        uint32_t loc = nm + 6 + 12 * i;

        int32_t id = ttUSHORT(fc + loc + 6);

        if (id != target_id)
        {
            continue;
        }

        // find the encoding
        int32_t platform = ttUSHORT(fc + loc + 0), encoding = ttUSHORT(fc + loc + 2), language = ttUSHORT(fc + loc + 4);

        // is this a Unicode encoding?
        if (platform == 0 ||
            (platform == 3 && encoding == 1) ||
            (platform == 3 && encoding == 10))
        {
            int32_t slen = ttUSHORT(fc + loc + 8);
            int32_t off = ttUSHORT(fc + loc + 10);

            // check if there's a prefix match
            int32_t matchlen = stbtt__CompareUTF8toUTF16_bigendian_prefix(name, nlen, fc + stringOffset + off, slen);
            if (matchlen < 0)
            {
                continue;
            }

            // check for target_id+1 immediately following, with same encoding & language
            if (i + 1 < count && ttUSHORT(fc + loc + 12 + 6) == next_id && ttUSHORT(fc + loc + 12) == platform && ttUSHORT(fc + loc + 12 + 2) == encoding && ttUSHORT(fc + loc + 12 + 4) == language)
            {
                slen = ttUSHORT(fc + loc + 12 + 8);
                off = ttUSHORT(fc + loc + 12 + 10);
                if (slen == 0)
                {
                    if (matchlen == nlen)
                        return 1;
                }
                else if (matchlen < nlen && name[matchlen] == ' ')
                {
                    ++matchlen;
                    if (stbtt_CompareUTF8toUTF16_bigendian_internal((char *)(name + matchlen), nlen - matchlen, (char *)(fc + stringOffset + off), slen))
                        return 1;
                }
            }
            else
            {
                // if nothing immediately following
                if (matchlen == nlen)
                    return 1;
            }
        }

        // @TODO handle other encodings
    }
    return 0;
}

static int stbtt__matches(uint8_t *fc, uint32_t offset, uint8_t *name, int32_t flags)
{
    int32_t nlen = (int32_t)strlen((char *)name);
    uint32_t nm, hd;
    if (!stbtt__isfont(fc + offset))
        return 0;

    // check italics/bold/underline flags in macStyle...
    if (flags)
    {
        hd = stbtt__find_table(fc, offset, "head");
        if ((ttUSHORT(fc + hd + 44) & 7) != (flags & 7))
            return 0;
    }

    nm = stbtt__find_table(fc, offset, "name");
    if (!nm)
        return 0;

    if (flags)
    {
        // if we checked the macStyle flags, then just check the family and ignore the subfamily
        if (stbtt__matchpair(fc, nm, name, nlen, 16, -1))
            return 1;
        if (stbtt__matchpair(fc, nm, name, nlen, 1, -1))
            return 1;
        if (stbtt__matchpair(fc, nm, name, nlen, 3, -1))
            return 1;
    }
    else
    {
        if (stbtt__matchpair(fc, nm, name, nlen, 16, 17))
            return 1;
        if (stbtt__matchpair(fc, nm, name, nlen, 1, 2))
            return 1;
        if (stbtt__matchpair(fc, nm, name, nlen, 3, -1))
            return 1;
    }

    return 0;
}

static int stbtt_FindMatchingFont_internal(unsigned char *font_collection, char *name_utf8, int32_t flags)
{
    int32_t i;
    for (i = 0;; ++i)
    {
        int32_t off = stbtt_GetFontOffsetForIndex(font_collection, i);
        if (off < 0)
            return off;
        if (stbtt__matches((uint8_t *)font_collection, off, (uint8_t *)name_utf8, flags))
            return off;
    }
}

int stbtt_BakeFontBitmap(const unsigned char *data, int offset,
                         float pixel_height, unsigned char *pixels, int pw, int ph,
                         int first_char, int num_chars, stbtt_bakedchar *chardata)
{
    return stbtt_BakeFontBitmap_internal((unsigned char *)data, offset, pixel_height, pixels, pw, ph, first_char, num_chars, chardata);
}

int stbtt_GetFontOffsetForIndex(const unsigned char *data, int index)
{
    return stbtt_GetFontOffsetForIndex_internal((unsigned char *)data, index);
}

int stbtt_GetNumberOfFonts(const unsigned char *data)
{
    return stbtt_GetNumberOfFonts_internal((unsigned char *)data);
}

int stbtt_InitFont(stbtt_fontinfo *info, const unsigned char *data, int offset)
{
    return stbtt_InitFont_internal(info, (unsigned char *)data, offset);
}

int stbtt_FindMatchingFont(const unsigned char *fontdata, const char *name, int flags)
{
    return stbtt_FindMatchingFont_internal((unsigned char *)fontdata, (char *)name, flags);
}

int stbtt_CompareUTF8toUTF16_bigendian(const char *s1, int len1, const char *s2, int len2)
{
    return stbtt_CompareUTF8toUTF16_bigendian_internal((char *)s1, len1, (char *)s2, len2);
}
