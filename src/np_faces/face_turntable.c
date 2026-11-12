#include "core/resident.h"
#include "face_text.h"
#include "faces.h"
#include "np_faces/face_shapes.h"
#include "ui/label.h"
#include "ui/rippl_logo.h"

#define WALNUT_TOP COLOR_RGB(72, 44, 24)
#define WALNUT_BOTTOM COLOR_RGB(40, 24, 16)
#define VINYL COLOR_RGB(16, 16, 24)
#define ALU COLOR_RGB(200, 200, 192)
#define ALU_DARK COLOR_RGB(104, 104, 96)
#define CREAM COLOR_RGB(248, 236, 208)
#define TAN COLOR_RGB(216, 184, 136)
#define TAN_DIM COLOR_RGB(192, 160, 120)
#define AMBER COLOR_RGB(248, 164, 40)
#define LABEL_RED COLOR_RGB(176, 36, 32)

enum {
    Q4 = SHAPE_Q4,
    CENTRE_X = 77,
    CENTRE_DROP = 73,
    ARM_DX = 75,
    ARM_DY = -47,
    LAMP_DX = 80,
    LAMP_DY = 59,
    RIM_OUTER = 67,
    RIM_INNER = 64,
    LABEL_RADIUS = 28,
    LABEL_MASK = 40,
    COVER_FIXED_BITS = 16,
    GROOVE_FIRST = 45,
    GROOVE_STEP = 7,
    GROOVE_COUNT = 3,
    TURN_STEPS = 18,
    SPINDLE = 48,
    BADGE_DISC = 320,
    BADGE_SIDE = 18,
    ARM_REST = 140,
    ARM_STEPS = 12,
    ARM_REACH = 84,
    ARM_HEAD_TURN = 10,
    HALF_TURN = 128,
    LINE_BIG = 22,
    LINE_SMALL = 16,
    BAR_HEIGHT = 3,
    PIXEL_CENTRE = 8,
    LAMP_RADIUS = 48
};

typedef struct Deck {
    Canvas *canvas;
    const NowPlayingFrame *frame;
    Point centre;
} Deck;

typedef struct Span {
    int first;
    int end;
} Span;

typedef struct Rows {
    int x;
    int width;
    FaceAlign align;
    int title;
    int artist;
    int album;
    int format;
    int detail;
    int modes;
    int bar;
    int times;
    bool has_detail;
} Rows;

typedef struct Slot {
    const Font *font;
    int row;
    int height;
    Color color;
} Slot;

RIPPL_NP_FACES_DATA static const Rows LANDSCAPE_ROWS = {
    170, 140, FACE_ALIGN_LEFT, 4, 26, 46, 73, 92, 107, 124, 129, true};
RIPPL_NP_FACES_DATA static const Rows PORTRAIT_ROWS = {
    10, 150, FACE_ALIGN_CENTRE, 144, 166, 188, 212, 0, -42, -23, -18, false};

RIPPL_NP_FACES_CODE static ShapeRing ring_of(Point at, int outer, int inner)
{
    return (ShapeRing){(int16_t)(at.x * Q4 + PIXEL_CENTRE), (int16_t)(at.y * Q4 + PIXEL_CENTRE),
                       (int16_t)outer, (int16_t)inner};
}

RIPPL_NP_FACES_CODE static void ring(const Deck *deck, int outer, int inner, Color color)
{
    shape_ring(deck->canvas, ring_of(deck->centre, outer, inner), color);
}

RIPPL_NP_FACES_CODE static void dot(Canvas *canvas, Point at, int radius, Color color)
{
    shape_ring(canvas, ring_of(at, radius, 0), color);
}

RIPPL_NP_FACES_CODE static void spoke(Canvas *canvas, ShapeSpoke geometry, Color color)
{
    shape_spoke(canvas, &geometry, color);
}

RIPPL_NP_FACES_CODE static void plate(const Deck *deck)
{
    canvas_fill_gradient(deck->canvas, deck->frame->area, WALNUT_TOP, WALNUT_BOTTOM);
    ring(deck, RIM_OUTER * Q4, RIM_INNER * Q4, ALU);
    ring(deck, RIM_INNER * Q4, 0, VINYL);
}

RIPPL_NP_FACES_CODE static uint8_t label_angle(const Deck *deck)
{
    return (uint8_t)(deck->frame->tick % TURN_STEPS * SHAPE_TURN / TURN_STEPS);
}

RIPPL_NP_FACES_CODE static Span span_in(int origin, int length, int clip_start, int clip_length)
{
    int first = clip_start - origin;
    int end = clip_start + clip_length - origin;
    return (Span){first < 0 ? 0 : first, end > length ? length : end};
}

enum {
    COVER_SPREAD_MASK = 0x07E0F81F,
    COVER_ROUND_HALF = 0x01004008
};

RIPPL_NP_FACES_CODE static uint32_t spread_pixel(Color color)
{
    return ((uint32_t)color | ((uint32_t)color << 16)) & (uint32_t)COVER_SPREAD_MASK;
}

RIPPL_NP_FACES_CODE static void turned_cover(const Deck *deck, Image cover, uint8_t angle)
{
    int radius = LABEL_RADIUS;
    int span = 2 * radius + 1;
    Point origin = {deck->centre.x - radius, deck->centre.y - radius};
    Canvas *canvas = deck->canvas;
    Rect clip = canvas->clip;
    Span rows = span_in(origin.y, span, clip.y, clip.height);
    Span columns = span_in(origin.x, span, clip.x, clip.width);
    if (rows.first < rows.end && columns.first < columns.end && cover.width > 0 &&
        cover.height > 0) {
        int sine = shape_sine(angle);
        int cosine = shape_cosine(angle);
        int reach = radius * SHAPE_UNIT;
        int limit = radius * radius + radius;
        int step_u = (cover.width << COVER_FIXED_BITS) / (2 * reach);
        int step_v = (cover.height << COVER_FIXED_BITS) / (2 * reach);
        int du_x = cosine * step_u;
        int du_y = sine * step_u;
        int dv_x = -sine * step_v;
        int dv_y = cosine * step_v;
        int width = cover.width;
        int max_x = width - 1;
        int max_y = cover.height - 1;
        int max_u = max_x << COVER_FIXED_BITS;
        int max_v = max_y << COVER_FIXED_BITS;
        int mid_u = (max_x << (COVER_FIXED_BITS - 1)) + (1 << (COVER_FIXED_BITS - 5));
        int mid_v = (max_y << (COVER_FIXED_BITS - 1)) + (1 << (COVER_FIXED_BITS - 5));
        const Color *src = cover.pixels;
        for (int row = rows.first; row < rows.end; row++) {
            int dy = row - radius;
            int room = limit - dy * dy;
            if (room < 0) {
                continue;
            }
            int half = (int)shape_isqrt((uint32_t)room);
            int first = radius - half;
            int end = radius + half + 1;
            if (first < columns.first) {
                first = columns.first;
            }
            if (end > columns.end) {
                end = columns.end;
            }
            if (first >= end) {
                continue;
            }
            int dx0 = first - radius;
            int u = mid_u + dx0 * du_x + dy * du_y;
            int v = mid_v + dx0 * dv_x + dy * dv_y;
            Color *dst =
                &canvas->pixels[(origin.y + row - canvas->bounds.y) * canvas->bounds.width +
                                origin.x + first - canvas->bounds.x];
            for (int column = first; column < end; column++) {
                int cu = u < 0 ? 0 : u > max_u ? max_u : u;
                int cv = v < 0 ? 0 : v > max_v ? max_v : v;
                int sx = cu >> COVER_FIXED_BITS;
                int sy = cv >> COVER_FIXED_BITS;
                const Color *p0 = src + sy * width + sx;
                const Color *p1 = sy < max_y ? p0 + width : p0;
                int dx = sx < max_x ? 1 : 0;
                Color c00 = p0[0];
                Color c10 = p0[dx];
                Color c01 = p1[0];
                Color c11 = p1[dx];
                if (((c00 ^ c10) | (c00 ^ c01) | (c00 ^ c11)) == 0) {
                    *dst++ = c00;
                } else {
                    uint32_t fx = (uint32_t)((cu >> (COVER_FIXED_BITS - 4)) & 15);
                    uint32_t fy = (uint32_t)((cv >> (COVER_FIXED_BITS - 4)) & 15);
                    uint32_t inv_x = 16u - fx;
                    uint32_t inv_y = 16u - fy;
                    uint32_t top =
                        ((spread_pixel(c00) * inv_x + spread_pixel(c10) * fx + COVER_ROUND_HALF) >>
                         4) &
                        COVER_SPREAD_MASK;
                    uint32_t bot =
                        ((spread_pixel(c01) * inv_x + spread_pixel(c11) * fx + COVER_ROUND_HALF) >>
                         4) &
                        COVER_SPREAD_MASK;
                    uint32_t mix =
                        ((top * inv_y + bot * fy + COVER_ROUND_HALF) >> 4) & COVER_SPREAD_MASK;
                    *dst++ = (Color)(mix | (mix >> 16));
                }
                u += du_x;
                v += dv_x;
            }
        }
    }
    ring(deck, LABEL_MASK * Q4, radius * Q4, VINYL);
}

RIPPL_NP_FACES_CODE static void label_art(const Deck *deck)
{
    Image cover = deck->frame->cover;
    if (cover.pixels == NULL) {
        cover = face_logo_cover(deck->frame->scratch, deck->frame->scratch_size, LABEL_RED, CREAM);
    }
    turned_cover(deck, cover, label_angle(deck));
}

RIPPL_NP_FACES_CODE static void grooves(const Deck *deck)
{
    for (int index = 0; index < GROOVE_COUNT; index++) {
        int radius = GROOVE_FIRST + GROOVE_STEP * index;
        ring(deck, (radius + 1) * Q4, radius * Q4, ALU_DARK);
    }
    ring(deck, SPINDLE, 0, ALU);
}

RIPPL_NP_FACES_CODE static void badge(const Deck *deck)
{
    if (face_badge_kind(deck->frame) == FACE_ICON_NONE) {
        return;
    }
    Point at = deck->centre;
    ring(deck, BADGE_DISC, 0, VINYL);
    face_badge_draw(deck->canvas, deck->frame,
                    (Rect){at.x - BADGE_SIDE / 2, at.y - BADGE_SIDE / 2, BADGE_SIDE, BADGE_SIDE},
                    CREAM);
}

RIPPL_NP_FACES_CODE static void arm(const Deck *deck)
{
    const NowPlayingFrame *frame = deck->frame;
    int steps = face_progress_width(frame->shown_ms, frame->status->duration_ms, ARM_STEPS);
    uint8_t angle = (uint8_t)(ARM_REST + steps);
    uint8_t head = (uint8_t)(angle + ARM_HEAD_TURN);
    Point pivot = {deck->centre.x + ARM_DX, deck->centre.y + ARM_DY};
    Point stylus = shape_polar(pivot, angle, ARM_REACH);
    Canvas *canvas = deck->canvas;
    spoke(canvas, (ShapeSpoke){pivot, (uint8_t)(angle + HALF_TURN), 160, 7, 21}, ALU_DARK);
    dot(canvas, pivot, 160, ALU_DARK);
    dot(canvas, pivot, 112, ALU);
    spoke(canvas, (ShapeSpoke){pivot, angle, 64, 0, 72}, ALU);
    spoke(canvas, (ShapeSpoke){stylus, head, 112, -14, 3}, ALU);
    spoke(canvas, (ShapeSpoke){stylus, head, 48, -11, 0}, VINYL);
    dot(canvas, pivot, 48, ALU_DARK);
}

RIPPL_NP_FACES_CODE static void lamp(const Deck *deck)
{
    Point at = {deck->centre.x + LAMP_DX, deck->centre.y + LAMP_DY};
    dot(deck->canvas, at, LAMP_RADIUS, deck->frame->playing ? AMBER : ALU_DARK);
}

RIPPL_NP_FACES_CODE static const Rows *rows_of(const NowPlayingFrame *frame)
{
    return frame->area.height > frame->area.width ? &PORTRAIT_ROWS : &LANDSCAPE_ROWS;
}

RIPPL_NP_FACES_CODE static FaceLine line_of(const NowPlayingFrame *frame, Slot slot)
{
    const Rows *rows = rows_of(frame);
    Rect area = frame->area;
    int y = slot.row >= 0 ? area.y + slot.row : area.y + area.height + slot.row;
    return (FaceLine){
        slot.font, {rows->x, y, rows->width, slot.height}, slot.color, slot.color, rows->align};
}

RIPPL_NP_FACES_CODE static void note(const Deck *deck, FaceLine line, const char *text)
{
    if (text != NULL) {
        Rect fitted = face_fitted_area(line.font, line.area, text, line.align);
        label_draw(deck->canvas, (TextStyle){line.font, line.color}, (Label){fitted, text});
    }
}

RIPPL_NP_FACES_CODE static void heading(const Deck *deck)
{
    const NowPlayingFrame *frame = deck->frame;
    const Rows *rows = rows_of(frame);
    const char *lyric = frame->lyric;
    bool sung = lyric != NULL && lyric[0] != '\0';
    FaceLine title =
        line_of(frame, (Slot){frame->theme->fonts.title, rows->title, LINE_BIG, CREAM});
    FaceLine artist = line_of(frame, (Slot){frame->theme->fonts.body, rows->artist, LINE_BIG, TAN});
    FaceLine album = line_of(
        frame, (Slot){frame->theme->fonts.body, rows->album, LINE_BIG, sung ? CREAM : TAN_DIM});
    face_line(deck->canvas, frame, &title, frame->song);
    face_line(deck->canvas, frame, &artist, frame->artist);
    face_line(deck->canvas, frame, &album, sung ? lyric : frame->album);
}

RIPPL_NP_FACES_CODE static void modes(const Deck *deck)
{
    const NowPlayingFrame *frame = deck->frame;
    char buffer[FACE_FEEDBACK_CAPACITY];
    const char *feedback = face_feedback_text(frame, buffer, sizeof buffer);
    Slot slot = {frame->theme->fonts.small, rows_of(frame)->modes, LINE_SMALL,
                 feedback != NULL ? CREAM : TAN_DIM};
    note(deck, line_of(frame, slot), feedback != NULL ? feedback : frame->modes);
}

RIPPL_NP_FACES_CODE static void details(const Deck *deck)
{
    const NowPlayingFrame *frame = deck->frame;
    const Rows *rows = rows_of(frame);
    const Font *small = frame->theme->fonts.small;
    FaceLine format = line_of(frame, (Slot){small, rows->format, LINE_SMALL, TAN});
    face_format_row(deck->canvas, frame, &format);
    if (rows->has_detail) {
        note(deck, line_of(frame, (Slot){small, rows->detail, LINE_SMALL, TAN_DIM}), frame->detail);
    }
    modes(deck);
}

RIPPL_NP_FACES_CODE static void progress(const Deck *deck)
{
    const NowPlayingFrame *frame = deck->frame;
    const Rows *rows = rows_of(frame);
    const Font *small = frame->theme->fonts.small;
    FaceLine bar = line_of(frame, (Slot){small, rows->bar, BAR_HEIGHT, TAN});
    face_bar_draw(deck->canvas, frame, &(FaceBar){bar.area, ALU_DARK, CREAM, AMBER});
    FaceLine times = line_of(frame, (Slot){small, rows->times, LINE_SMALL, TAN});
    face_times_draw(deck->canvas, frame, &times);
}

RIPPL_NP_FACES_CODE void face_turntable_draw(const NowPlayingFrame *frame, Canvas *canvas)
{
    Deck deck = {canvas, frame, {CENTRE_X, frame->area.y + CENTRE_DROP}};
    plate(&deck);
    label_art(&deck);
    grooves(&deck);
    badge(&deck);
    arm(&deck);
    lamp(&deck);
    heading(&deck);
    details(&deck);
    progress(&deck);
}
