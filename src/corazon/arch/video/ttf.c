/******************************************************************************
 * @file            ttf.c
 *****************************************************************************/
#include    <corazon/utils/misc.h>
#include    <corazon/video/ttf.h>
#include    <corazon/console.h>

#include    <stdint.h>
#include    <string.h>

extern const uint8_t font8x16[256][16];
uint8_t *ttf_font_bitmap = 0;

uint32_t ttf_font_width = 0;
uint32_t ttf_font_height = 0;
uint32_t ttf_font_pitch = 0;

int ttf_font_loaded = 0;

#define     TTF_MAX_POINTS              1024
#define     TTF_MAX_CONTOURS            64
#define     TTF_MAX_EDGES               4096

typedef struct {

    const uint8_t *data;
    size_t length;
    
    uint32_t cmap, cmap_length;
    uint32_t glyf, glyf_length;
    uint32_t loca, loca_length;
    uint32_t hhea, hhea_length;
    uint32_t hmtx, hmtx_length;
    
    uint16_t num_glyphs;
    uint16_t num_metrics;
    uint16_t advance_max;
    
    int16_t ascent, descent;
    int16_t loca_format;
    
    uint32_t unicode_map, unicode_length;
    uint16_t unicode_format;

} ttf_t;

typedef struct {

    int32_t x, y;
    uint8_t on;

} ttf_point_t;

typedef struct { int32_t x0, y0, x1, y1; } ttf_edge_t;

typedef struct {

    ttf_point_t *points;
    ttf_edge_t *edges;
    
    uint16_t ends[TTF_MAX_CONTOURS];
    unsigned count;
    
    uint32_t scale_x, scale_y;
    int32_t x_offset, y_offset;

} ttf_raster_t;

static uint16_t ttf_u16 (const uint8_t *p) {
    return (uint16_t) ((uint16_t) p[0] << 8 | p[1]);
}

static int16_t ttf_s16 (const uint8_t *p) {
    return (int16_t) ttf_u16 (p);
}

static uint32_t ttf_u32 (const uint8_t *p) {
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static int ttf_bounds (size_t total, size_t offset, size_t bytes) {
    return offset <= total && bytes <= total - offset;
}

static int ttf_table (ttf_t *font, uint32_t tag, uint32_t *offset, uint32_t *length) {

    const uint8_t *data = font->data;
    
    unsigned i, count;
    size_t entry;
    
    if (!ttf_bounds (font->length, 0, 12)) {
        return 0;
    }
    
    count = ttf_u16 (data + 4);
    
    if (!ttf_bounds (font->length, 12, (size_t) count * 16)) {
        return 0;
    }
    
    for (i = 0; i < count; i++) {
    
        entry = 12 + (size_t) i * 16;
        
        if (ttf_u32 (data + entry) == tag) {
        
            *offset = ttf_u32 (data + entry + 8);
            *length = ttf_u32 (data + entry + 12);
            
            if (!ttf_bounds (font->length, *offset, *length)) {
            
                kprintf ("[ttf] table %08x exceeds file: offset=%lu length=%lu size=%lu\n", (unsigned) tag, (unsigned long) *offset, (unsigned long) *length, (unsigned long) font->length);
                return 0;
            
            }
            
            return 1;
        
        }
    
    }
    
    kprintf ("[ttf] table %08x not found\n", (unsigned) tag);
    return 0;

}

static int ttf_initialize (ttf_t *font, const void *data, size_t length) {

    const uint8_t *p = (const uint8_t *) data;
    
    uint32_t head, head_length, maxp, maxp_length;
    uint32_t i, count, sub, sub_length;
    
    uint16_t platform, encoding, format;
    int priority, best = 0;
    
    memset (font, 0, sizeof (*font));
    
    font->data = p;
    font->length = length;
    
    if (!p || !ttf_bounds (length, 0, 12)) {
    
        kprintf ("[ttf] invalid input: data=%p length=%lu\n", data, (unsigned long) length);
        return 0;
    
    }
    
    if (ttf_u32 (p) != 0x00010000 && ttf_u32 (p) != 0x74727565) {
    
        kprintf ("[ttf] invalid signature: %08x length=%lu\n", (unsigned) ttf_u32 (p), (unsigned long) length);
        return 0;
    
    }
    
    if (!ttf_table (font, 0x68656164, &head, &head_length) || head_length < 54 ||
        !ttf_table (font, 0x6d617870, &maxp, &maxp_length) || maxp_length < 6 ||
        !ttf_table (font, 0x636d6170, &font->cmap, &font->cmap_length) || font->cmap_length < 4 ||
        !ttf_table (font, 0x676c7966, &font->glyf, &font->glyf_length) ||
        !ttf_table (font, 0x6c6f6361, &font->loca, &font->loca_length) ||
        !ttf_table (font, 0x68686561, &font->hhea, &font->hhea_length) || font->hhea_length < 36 ||
        !ttf_table (font, 0x686d7478, &font->hmtx, &font->hmtx_length)) {
        return 0;
    }
    
    font->loca_format = ttf_s16 (p + head + 50);
    font->num_glyphs = ttf_u16 (p + maxp + 4);
    font->ascent = ttf_s16 (p + font->hhea + 4);
    font->descent = ttf_s16 (p + font->hhea + 6);
    font->advance_max = ttf_u16 (p + font->hhea + 10);
    font->num_metrics = ttf_u16 (p + font->hhea + 34);
    
    if (!font->num_glyphs) {
    
        kprintf ("[ttf] invalid glyph count\n");
        return 0;
    
    }
    
    if (!font->num_metrics) {
    
        kprintf ("[ttf] invalid horizontal metric count\n");
        return 0;
    
    }
    
    if (font->num_metrics > font->num_glyphs) {
    
        kprintf ("[ttf] horizontal metric count exceeds glyph count\n");
        return 0;
    
    }
    
    if (!font->advance_max) {
    
        kprintf ("[ttf] invalid advance width\n");
        return 0;
    
    }
    
    if (((int32_t) font->ascent - (int32_t) font->descent) <= 0) {
    
        kprintf ("[ttf] invalid ascent/descent\n");
        return 0;
    
    }
    
    if (font->loca_format != 0 && font->loca_format != 1) {
    
        kprintf ("[ttf] invalid loca format\n");
        return 0;
    
    }
    
    if ((size_t) (font->num_glyphs + 1) * (font->loca_format ? 4 : 2) > font->loca_length) {
    
        kprintf ("[ttf] loca table too short\n");
        return 0;
    
    }
    
    if ((size_t) font->num_metrics * 4 > font->hmtx_length) {
    
        kprintf ("[ttf] hmtx table too short\n");
        return 0;
    
    }
    
    count = ttf_u16 (p + font->cmap + 2);
    
    if (!ttf_bounds (font->cmap_length, 4, (size_t) count * 8)) {
    
        kprintf ("[ttf] cmap directory out of bounds: count=%u length=%lu\n", (unsigned) count, (unsigned long) font->cmap_length);
        return 0;
    
    }
    
    for (i = 0; i < count; i++) {
    
        const uint8_t *entry = p + font->cmap + 4 + i * 8;
        uint32_t relative = ttf_u32 (entry + 4);
        
        platform = ttf_u16 (entry);
        encoding = ttf_u16 (entry + 2);
        
        if (platform != 0 && (platform != 3 || (encoding != 1 && encoding != 10))) {
            continue;
        }
        
        if (!ttf_bounds (font->cmap_length, relative, 4)) {
            continue;
        }
        
        sub = font->cmap + relative;
        format = ttf_u16 (p + sub);
        
        if (format == 4) {
        
            sub_length = ttf_u16 (p + sub + 2);
            priority = 1;
        
        } else if (format == 12 && ttf_bounds (font->cmap_length, relative, 16)) {
        
            sub_length = ttf_u32 (p + sub + 4);
            priority = 2;
        
        } else {
            continue;
        }
        
        if (!ttf_bounds (font->cmap_length, relative, sub_length) || sub_length < 16 || priority <= best) {
            continue;
        }
        
        font->unicode_map = sub;
        font->unicode_length = sub_length;
        font->unicode_format = format;
        
        best = priority;
    
    }
    
    return best != 0;

}

static uint16_t ttf_character (const ttf_t *font, uint16_t ch) {

    const uint8_t *p = font->data + font->unicode_map;
    
    uint32_t count, i, value, off;
    uint16_t start, end, glyph, delta, range;
    
    if (font->unicode_format == 12) {
    
        count = ttf_u32 (p + 12);
        
        if (!ttf_bounds (font->unicode_length, 16, (size_t) count * 12)) {
            return 0;
        }
        
        for (i = 0; i < count; i++) {
        
            const uint8_t *group = p + 16 + (size_t) i * 12;
            
            uint32_t first = ttf_u32 (group);
            uint32_t last = ttf_u32 (group + 4);
            
            if ((uint32_t) ch >= first && (uint32_t) ch <= last) {
            
                value = ttf_u32 (group + 8) + (uint32_t) ch - first;
                return value < font->num_glyphs ? (uint16_t) value : 0;
            
            }
        
        }
        
        return 0;
    
    }
    
    count = ttf_u16 (p + 6) / 2;
    
    if (!count || !ttf_bounds (font->unicode_length, 14, (size_t) count * 8 + 2)) {
        return 0;
    }
    
    for (i = 0; i < count; i++) {
    
        end = ttf_u16 (p + 14 + i * 2);
        start = ttf_u16 (p + 16 + count * 2 + i * 2);
        
        if (ch < start || ch > end) {
            continue;
        }
        
        delta = ttf_u16 (p + 16 + count * 4 + i * 2);
        range = ttf_u16 (p + 16 + count * 6 + i * 2);
        
        if (!range) {
            glyph = (uint16_t) (ch + delta);
        } else {
        
            off = 16 + count * 6 + i * 2 + range + (uint32_t) (ch - start) * 2;
            
            if (!ttf_bounds (font->unicode_length, off, 2)) {
                return 0;
            }
            
            glyph = ttf_u16 (p + off);
            
            if (glyph) {
                glyph = (uint16_t) (glyph + delta);
            }
        
        }
        
        return glyph < font->num_glyphs ? glyph : 0;
    
    }
    
    return 0;

}

static int ttf_edge (ttf_raster_t *raster, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {

    ttf_edge_t *edge;
    
    if (x0 == x1 && y0 == y1) {
        return 1;
    }
    
    if (raster->count >= TTF_MAX_EDGES) {
        return 0;
    }
    
    edge = &raster->edges[raster->count++];
    
    edge->x0 = x0;
    edge->y0 = y0;
    
    edge->x1 = x1;
    edge->y1 = y1;
    
    return 1;

}

static int ttf_curve (ttf_raster_t *raster, ttf_point_t a, ttf_point_t b, ttf_point_t c) {

    int32_t x = a.x, y = a.y;
    int32_t next_x, next_y;
    
    unsigned t;
    
    for (t = 1; t <= 8; t++) {
    
        int32_t u = 8 - (int32_t) t;
        
        next_x = (u * u * a.x + 2 * u * (int32_t) t * b.x + (int32_t) t * (int32_t) t * c.x) / 64;
        next_y = (u * u * a.y + 2 * u * (int32_t) t * b.y + (int32_t) t * (int32_t) t * c.y) / 64;
        
        if (!ttf_edge (raster, x, y, next_x, next_y)) {
            return 0;
        }
        
        x = next_x;
        y = next_y;
    
    }
    
    return 1;

}

static ttf_point_t ttf_midpoint (ttf_point_t a, ttf_point_t b) {

    ttf_point_t mid;
    
    mid.x = (a.x + b.x) / 2;
    mid.y = (a.y + b.y) / 2;
    
    mid.on = 1;
    return mid;

}

static int ttf_contour (ttf_raster_t *raster, unsigned first, unsigned last) {

    ttf_point_t start, current, control, point;
    
    int pending = 0;
    unsigned i;
    
    if (raster->points[first].on) {
        start = raster->points[first];
    } else if (raster->points[last].on) {
        start = raster->points[last];
    } else {
        start = ttf_midpoint (raster->points[first], raster->points[last]);
    }
    
    current = start;
    
    for (i = first; i <= last; i++) {
    
        point = raster->points[i];
        
        if (point.on) {
        
            if (pending) {
            
                if (!ttf_curve (raster, current, control, point)) {
                    return 0;
                }
                
                pending = 0;
            
            } else if (!ttf_edge (raster, current.x, current.y, point.x, point.y)) {
                return 0;
            }
            
            current = point;
        
        } else {
        
            if (pending) {
            
                ttf_point_t mid = ttf_midpoint (control, point);
                
                if (!ttf_curve (raster, current, control, mid)) {
                    return 0;
                }
                
                current = mid;
            
            }
            
            control = point;
            pending = 1;
        
        }
    
    }
    
    if (pending) {
        return ttf_curve (raster, current, control, start);
    }
    
    return ttf_edge (raster, current.x, current.y, start.x, start.y);

}

static int ttf_outline (const ttf_t *font, ttf_raster_t *raster, uint16_t glyph) {

    const uint8_t *p = font->data;
    
    uint32_t begin, end;
    size_t cursor, size;
    
    unsigned i, count, contours, first;
    
    int32_t x = 0, y = 0;
    uint8_t flags[TTF_MAX_POINTS];
    
    if (font->loca_format) {
    
        begin = ttf_u32 (p + font->loca + (size_t) glyph * 4);
        end = ttf_u32 (p + font->loca + (size_t) (glyph + 1) * 4);
    
    } else {
    
        begin = (uint32_t) ttf_u16 (p + font->loca + (size_t) glyph * 2) * 2;
        end = (uint32_t) ttf_u16 (p + font->loca + (size_t) (glyph + 1) * 2) * 2;
    
    }
    
    if (end < begin || end > font->glyf_length) {
        return 0;
    }
    
    if (begin == end) {
        return 1;
    }
    
    size = end - begin;
    p += font->glyf + begin;
    
    if (!ttf_bounds (size, 0, 10)) {
        return 0;
    }
    
    contours = (unsigned) ttf_s16 (p);
    
    /* A composite glyph is left in the built-in font until supported. */
    if (ttf_s16 (p) < 0 || contours > TTF_MAX_CONTOURS) {
        return 0;
    }
    
    cursor = 10;
    
    if (!ttf_bounds (size, cursor, (size_t) contours * 2 + 2)) {
        return 0;
    }
    
    count = 0;
    
    for (i = 0; i < contours; i++) {
    
        raster->ends[i] = ttf_u16 (p + cursor);
        cursor += 2;
        
        if (raster->ends[i] < count || raster->ends[i] >= TTF_MAX_POINTS) {
            return 0;
        }
        
        count = (unsigned) raster->ends[i] + 1;
    
    }
    
    cursor += 2 + ttf_u16 (p + cursor);
    
    if (!ttf_bounds (size, cursor, 0)) {
        return 0;
    }
    
    for (i = 0; i < count; i++) {
    
        uint8_t flag, repeat;
        
        if (!ttf_bounds (size, cursor, 1)) {
            return 0;
        }
        
        flag = p[cursor++];
        flags[i] = flag;
        
        if (flag & 0x08) {
        
            if (!ttf_bounds (size, cursor, 1)) {
                return 0;
            }
            
            repeat = p[cursor++];
            
            if (repeat > count - i - 1) {
                return 0;
            }
            
            while (repeat--) {
                flags[++i] = flag;
            }
        
        }
    
    }
    
    for (i = 0; i < count; i++) {
    
        uint8_t flag = flags[i];
        int delta = 0;
        
        if (flag & 0x02) {
        
            if (!ttf_bounds (size, cursor, 1)) {
                return 0;
            }
            
            delta = p[cursor++];
            
            if (!(flag & 0x10)) {
                delta = -delta;
            }
        
        } else if (!(flag & 0x10)) {
        
            if (!ttf_bounds (size, cursor, 2)) {
                return 0;
            }
            
            delta = ttf_s16 (p + cursor);
            cursor += 2;
        
        }
        
        x += delta;
        
        raster->points[i].x = x;
        raster->points[i].on = (uint8_t) (flag & 1);
    
    }
    
    for (i = 0; i < count; i++) {
    
        uint8_t flag = flags[i];
        int delta = 0;
        
        if (flag & 0x04) {
        
            if (!ttf_bounds (size, cursor, 1)) {
                return 0;
            }
            
            delta = p[cursor++];
            
            if (!(flag & 0x20)) {
                delta = -delta;
            }
        
        } else if (!(flag & 0x20)) {
        
            if (!ttf_bounds (size, cursor, 2)) {
                return 0;
            }
            
            delta = ttf_s16 (p + cursor);
            cursor += 2;
        
        }
        
        y += delta;
        raster->points[i].y = y;
    
    }
    
    for (i = 0; i < count; i++) {
    
        int64_t sx = (int64_t) raster->points[i].x * raster->scale_x;
        int64_t sy = (int64_t) raster->points[i].y * raster->scale_y;
        
        raster->points[i].x = raster->x_offset + (int32_t) (sx >> 10);
        raster->points[i].y = raster->y_offset - (int32_t) (sy >> 10);
    
    }
    
    first = 0;
    
    for (i = 0; i < contours; i++) {
    
        if (!ttf_contour (raster, first, raster->ends[i])) {
            return 0;
        }
        
        first = (unsigned) raster->ends[i] + 1;
    
    }
    
    return 1;

}

static int ttf_inside (const ttf_raster_t *raster, int32_t x, int32_t y) {

    int inside = 0;
    unsigned i;
    
    for (i = 0; i < raster->count; i++) {
    
        const ttf_edge_t *edge = &raster->edges[i];
        
        int32_t dy = edge->y1 - edge->y0;
        int64_t cross;
        
        if ((edge->y0 > y) == (edge->y1 > y)) {
            continue;
        }
        
        cross = (int64_t) (x - edge->x0) * dy - (int64_t) (y - edge->y0) * (edge->x1 - edge->x0);
        
        if ((dy > 0 && cross < 0) || (dy < 0 && cross > 0)) {
            inside = !inside;
        }
    
    }
    
    return inside;

}

static int ttf_character_bitmap (const ttf_t *font, ttf_raster_t *raster, uint16_t ch, uint8_t *bitmap, unsigned width, unsigned height) {

    uint16_t glyph = ttf_character (font, ch);
    uint16_t advance;
    
    uint32_t line_height;
    int32_t scaled_advance;
    
    unsigned pitch = (width + 7) / 8, row, col;
    
    if (!glyph) {
        return 0;
    }
    
    advance = ttf_u16 (font->data + font->hmtx + (size_t) (glyph < font->num_metrics ? glyph : font->num_metrics - 1) * 4);
    line_height = (uint32_t) (font->ascent - font->descent);
    
    raster->scale_x = (uint32_t) (((uint32_t) width << 16) / font->advance_max);
    raster->scale_y = (uint32_t) (((uint32_t) height << 16) / line_height);
    
    scaled_advance = (int32_t) (((uint64_t) advance * raster->scale_x) >> 10);
    
    raster->x_offset = ((int32_t) width * 64 - scaled_advance) / 2;
    raster->y_offset = ((int32_t) height * 64 - (int32_t) (((uint64_t) line_height * raster->scale_y) >> 10)) / 2
        + (int32_t) (((int64_t) font->ascent * raster->scale_y) >> 10);
    
    raster->count = 0;
    
    if (!ttf_outline (font, raster, glyph)) {
        return 0;
    }
    
    memset (bitmap, 0, (size_t) height * pitch);
    
    for (row = 0; row < height; row++) {
    
        for (col = 0; col < width; col++) {
        
            if (ttf_inside (raster, (int32_t) col * 64 + 32, (int32_t) row * 64 + 32)) {
                bitmap[(size_t) row * pitch + (col >> 3)] |= (uint8_t) (0x80 >> (col & 7));
            }
        
        }
    
    }
    
    return 1;

}

/******************************************************************************
 * Rasterize an already-loaded TrueType font into the console glyph tables.
 *****************************************************************************/
int console_font_load_ttf (const void *data, size_t length) {

    ttf_raster_t raster;
    ttf_t font;
    
    size_t glyph_size;
    uint8_t *bitmap;
    
    uint32_t width, height, pitch, screen_height;
    uint32_t ch, row, col;
    
    int success = 0;
    
    if (ttf_font_loaded || !ttf_initialize (&font, data, length)) {
        return 1;
    }
    
    screen_height = console_get_pixel_height ();
    
    if (console_get_type () == CONSOLE_TYPE_CGA || !screen_height) {
        return 1;
    }
    
    height = 32;
    
    if (screen_height < 768) {
        height = 24;
    }
    
    if (screen_height < 600) {
        height = 16;
    }
    
    width = ((height / 2) + 8) & ~8;
    pitch = (width + 7) / 8;
    
    if ((glyph_size = (size_t) height * pitch) > SIZE_MAX / 256) {
        return 1;
    }
    
    if (!(bitmap = kmalloc (glyph_size * 256))) {
        return 1;
    }
    
    /* Keep the built-in glyphs as fallbacks at the selected cell size. */
    memset (bitmap, 0, glyph_size * 256);
    
    for (ch = 0; ch < 256; ch++) {
    
        uint8_t *glyph_bitmap = bitmap + (size_t) ch * glyph_size;
        
        for (row = 0; row < height; row++) {
        
            uint8_t bits = font8x16[ch][row * 16 / height];
            
            for (col = 0; col < width; col++) {
            
                if (bits & (0x80 >> (col * 8 / width))) {
                    glyph_bitmap[(size_t) row * pitch + (col >> 3)] |= (uint8_t) (0x80 >> (col & 7));
                }
            
            }
        
        }
    
    }
    
    if (!(raster.points = kmalloc (TTF_MAX_POINTS * sizeof (*raster.points)))) {
    
        kfree (bitmap);
        return 1;
    
    }
    
    if (!(raster.edges = kmalloc (TTF_MAX_EDGES * sizeof (*raster.edges)))) {
    
        kfree (raster.points);
        kfree (bitmap);
        
        return 1;
    
    }
    
    for (ch = 32; ch < 127; ch++) {
    
        if (ttf_character_bitmap (&font, &raster, (uint16_t) ch, bitmap + (size_t) ch * glyph_size, width, height)) {
            success = 1;
        }
    
    }
    
    kfree (raster.edges);
    kfree (raster.points);
    
    if (!success) {
    
        kfree (bitmap);
        return 1;
    
    }
    
    ttf_font_bitmap = bitmap;
    ttf_font_width = width;
    ttf_font_height = height;
    ttf_font_pitch = pitch;
    
    console_font_activate ();
    return 0;

}
