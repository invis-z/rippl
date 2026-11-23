#include "np_faces/face_shapes.h"

#include "core/resident.h"

enum {
    QUARTER_STEPS = 64,
    QUARTER_SHIFT = 6,
    QUARTER_MASK = 63,
    ODD_QUARTER = 1,
    NEGATIVE_QUARTER = 2,
    QUARTER_TURN = 64,
    HALF_PIXEL_Q4 = 8,
    FRACTION_MASK = 15,
    PIXEL_SHIFT = 4,
    SQRT_TOP_BIT = 1 << 30,
    SPOKE_Q4_UNIT = SHAPE_Q4 * SHAPE_UNIT,
    BAND_FRINGE = HALF_PIXEL_Q4 * SHAPE_UNIT,
    LATTICE_OFFSET = 2,
    DIGIT_SEGMENTS = 10,
    BAR_COUNT = 3,
    POST_COUNT = 4,
    COLON_DOTS = 2
};

RIPPL_NP_FACES_DATA static const uint8_t QUARTER[QUARTER_STEPS + 1] = {
    0,   6,   13,  19,  25,  31,  37,  44,  50,  56,  62,  68,  74,  80,  86,  92,  98,
    103, 109, 115, 120, 126, 131, 136, 142, 147, 152, 157, 162, 167, 171, 176, 180, 185,
    189, 193, 197, 201, 205, 208, 212, 215, 219, 222, 225, 228, 231, 233, 236, 238, 240,
    242, 244, 246, 247, 249, 250, 251, 252, 253, 254, 254, 255, 255, 255};

RIPPL_NP_FACES_DATA static const uint8_t DIGIT_MASKS[DIGIT_SEGMENTS] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

RIPPL_NP_FACES_DATA static const uint8_t BAR_BITS[BAR_COUNT] = {0, 6, 3};
RIPPL_NP_FACES_DATA static const uint8_t POST_BITS[POST_COUNT] = {5, 1, 4, 2};

typedef struct DigitPen {
    Canvas *canvas;
    const SegmentStyle *style;
    Point origin;
} DigitPen;

typedef struct DigitFrame {
    int left;
    int right;
    int top;
    int middle;
    int bottom;
    int half;
} DigitFrame;

typedef struct Run {
    int left;
    int right;
    int y;
} Run;

typedef struct Span {
    int first;
    int last;
} Span;

typedef struct InsetRow {
    int row;
    int inset;
} InsetRow;

typedef struct Post {
    int x;
    int top;
    int bottom;
} Post;

RIPPL_NP_FACES_CODE static int round_div(int numerator, int denominator)
{
    int half = denominator / 2;
    return (numerator + (numerator >= 0 ? half : -half)) / denominator;
}

RIPPL_NP_FACES_CODE static int magnitude(int value)
{
    return value < 0 ? -value : value;
}

RIPPL_NP_FACES_CODE static int smaller(int first, int second)
{
    return first < second ? first : second;
}

RIPPL_NP_FACES_CODE static int larger(int first, int second)
{
    return first > second ? first : second;
}

RIPPL_NP_FACES_CODE int shape_sine(uint8_t angle)
{
    unsigned quadrant = (unsigned)angle >> QUARTER_SHIFT;
    unsigned step = (unsigned)angle & QUARTER_MASK;
    int value = QUARTER[(quadrant & ODD_QUARTER) != 0 ? QUARTER_STEPS - step : step];
    return quadrant >= NEGATIVE_QUARTER ? -value : value;
}

RIPPL_NP_FACES_CODE int shape_cosine(uint8_t angle)
{
    return shape_sine((uint8_t)(angle + QUARTER_TURN));
}

RIPPL_NP_FACES_CODE int shape_sine_fine(uint16_t fine)
{
    uint8_t angle = (uint8_t)(fine / SHAPE_FINE_STEPS);
    int below = shape_sine(angle);
    int above = shape_sine((uint8_t)(angle + 1));
    return below + (above - below) * (fine % SHAPE_FINE_STEPS) / SHAPE_FINE_STEPS;
}

RIPPL_NP_FACES_CODE int shape_cosine_fine(uint16_t fine)
{
    return shape_sine_fine((uint16_t)(fine + QUARTER_TURN * SHAPE_FINE_STEPS));
}

RIPPL_NP_FACES_CODE uint32_t shape_isqrt(uint32_t value)
{
    if (value == 0) {
        return 0;
    }
    uint32_t root = 0;
    uint32_t bit = 1u << ((31u - (uint32_t)__builtin_clz(value)) & ~1u);
    for (; bit != 0; bit >>= 2) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
    }
    return root;
}

RIPPL_NP_FACES_CODE Point shape_polar(Point centre, uint8_t angle, int radius)
{
    return (Point){centre.x + round_div(radius * shape_sine(angle), SHAPE_UNIT),
                   centre.y - round_div(radius * shape_cosine(angle), SHAPE_UNIT)};
}

RIPPL_NP_FACES_CODE Point shape_polar_fine(Point centre, uint16_t fine, int radius)
{
    return (Point){centre.x + round_div(radius * shape_sine_fine(fine), SHAPE_UNIT),
                   centre.y - round_div(radius * shape_cosine_fine(fine), SHAPE_UNIT)};
}

RIPPL_NP_FACES_CODE static void blend_at(Canvas *canvas, Point point, Color color, uint8_t alpha)
{
    Rect clip = canvas->clip;
    bool inside = point.x >= clip.x && point.x < clip.x + clip.width && point.y >= clip.y &&
                  point.y < clip.y + clip.height;
    if (alpha == 0 || !inside) {
        return;
    }
    int offset = (point.y - canvas->bounds.y) * canvas->bounds.width + point.x - canvas->bounds.x;
    canvas->pixels[offset] = color_blend(canvas->pixels[offset], color, alpha);
}

RIPPL_NP_FACES_CODE static uint8_t coverage(int fraction_q4)
{
    return (uint8_t)(fraction_q4 * COLOR_ALPHA_OPAQUE / SHAPE_Q4);
}

RIPPL_NP_FACES_CODE void shape_span(Canvas *canvas, ShapeSpan span, Color color)
{
    Rect clip = canvas->clip;
    if (span.right_q4 <= span.left_q4 || span.y < clip.y || span.y >= clip.y + clip.height) {
        return;
    }
    int left = span.left_q4 >> PIXEL_SHIFT;
    int right = span.right_q4 >> PIXEL_SHIFT;
    int left_part = span.left_q4 & FRACTION_MASK;
    int right_part = span.right_q4 & FRACTION_MASK;
    if (left == right) {
        blend_at(canvas, (Point){left, span.y}, color, coverage(span.right_q4 - span.left_q4));
        return;
    }
    if (left_part != 0) {
        blend_at(canvas, (Point){left, span.y}, color, coverage(SHAPE_Q4 - left_part));
        left++;
    }
    canvas_fill_rect(canvas, (Rect){left, span.y, right - left, 1}, color);
    blend_at(canvas, (Point){right, span.y}, color, coverage(right_part));
}

RIPPL_NP_FACES_CODE static ShapeSpan span_of(int y, int left_q4, int right_q4)
{
    return (ShapeSpan){(int16_t)y, (int16_t)left_q4, (int16_t)right_q4};
}

typedef struct PixelRun {
    Canvas *canvas;
    int y;
    int start;
    Color color;
} PixelRun;

RIPPL_NP_FACES_CODE static void run_flush(PixelRun *run, int end)
{
    if (run->start < end) {
        canvas_fill_rect(run->canvas, (Rect){run->start, run->y, end - run->start, 1}, run->color);
    }
}

RIPPL_NP_FACES_CODE static void run_put(PixelRun *run, int x, uint8_t alpha)
{
    if (alpha >= COLOR_ALPHA_OPAQUE) {
        return;
    }
    run_flush(run, x);
    run->start = x + 1;
    blend_at(run->canvas, (Point){x, run->y}, run->color, alpha);
}

RIPPL_NP_FACES_CODE static uint8_t edge_alpha(int distance_squared, int radius_q4)
{
    int outside = radius_q4 + HALF_PIXEL_Q4;
    int inside = radius_q4 - HALF_PIXEL_Q4;
    if (radius_q4 <= 0 || distance_squared >= outside * outside) {
        return 0;
    }
    if (inside > 0 && distance_squared <= inside * inside) {
        return COLOR_ALPHA_OPAQUE;
    }
    return coverage(outside - (int)shape_isqrt((uint32_t)distance_squared));
}

RIPPL_NP_FACES_CODE static uint8_t ring_alpha(const ShapeRing *ring, int dx, int dy)
{
    int distance_squared = dx * dx + dy * dy;
    int alpha =
        edge_alpha(distance_squared, ring->outer_q4) - edge_alpha(distance_squared, ring->inner_q4);
    return (uint8_t)(alpha < 0 ? 0 : alpha);
}

RIPPL_NP_FACES_CODE static int floor_div(int numerator, int denominator)
{
    int quotient = numerator / denominator;
    return quotient * denominator > numerator ? quotient - 1 : quotient;
}

RIPPL_NP_FACES_CODE static Span reach_span(const ShapeRing *ring, int reach)
{
    int base = ring->x_q4 - HALF_PIXEL_Q4;
    return (Span){-floor_div(reach - base, SHAPE_Q4), floor_div(base + reach, SHAPE_Q4)};
}

RIPPL_NP_FACES_CODE static Span within(const ShapeRing *ring, int radius_q4, int dy)
{
    int room = radius_q4 * radius_q4 - dy * dy;
    return radius_q4 > 0 && room >= 0 ? reach_span(ring, (int)shape_isqrt((uint32_t)room))
                                      : (Span){1, 0};
}

RIPPL_NP_FACES_CODE static bool in_span(Span span, int x)
{
    return x >= span.first && x <= span.last;
}

RIPPL_NP_FACES_CODE static void ring_row(Canvas *canvas, const ShapeRing *ring, int y, Color color)
{
    int dy = y * SHAPE_Q4 + HALF_PIXEL_Q4 - ring->y_q4;
    int far = ring->outer_q4 + HALF_PIXEL_Q4;
    if (dy * dy >= far * far) {
        return;
    }
    int reach = (int)shape_isqrt((uint32_t)(far * far - dy * dy));
    int first = larger(((ring->x_q4 - reach) >> PIXEL_SHIFT) - 1, canvas->clip.x);
    int last =
        smaller(((ring->x_q4 + reach) >> PIXEL_SHIFT) + 1, canvas->clip.x + canvas->clip.width - 1);
    bool disc = ring->inner_q4 <= 0;
    Span skip = within(ring, (disc ? ring->outer_q4 : ring->inner_q4) - HALF_PIXEL_Q4, dy);
    PixelRun run = {canvas, y, first, color};
    for (int x = first; x <= last; x++) {
        if (in_span(skip, x)) {
            if (!disc) {
                run_flush(&run, x);
                run.start = skip.last + 1;
            }
            x = skip.last;
            continue;
        }
        int dx = x * SHAPE_Q4 + HALF_PIXEL_Q4 - ring->x_q4;
        uint8_t alpha = ring_alpha(ring, dx, dy);
        if (alpha == 0) {
            run_flush(&run, x);
            run.start = x + 1;
        } else {
            run_put(&run, x, alpha);
        }
    }
    run_flush(&run, last + 1);
}

RIPPL_NP_FACES_CODE void shape_ring(Canvas *canvas, ShapeRing ring, Color color)
{
    int top = larger((ring.y_q4 - ring.outer_q4) >> PIXEL_SHIFT, canvas->clip.y);
    int bottom = smaller(((ring.y_q4 + ring.outer_q4) >> PIXEL_SHIFT) + 1,
                         canvas->clip.y + canvas->clip.height);
    for (int y = top; y < bottom; y++) {
        ring_row(canvas, &ring, y, color);
    }
}

RIPPL_NP_FACES_CODE static void inset_row(Canvas *canvas, Rect area, InsetRow at, Color color)
{
    int left = area.x * SHAPE_Q4 + at.inset;
    int right = (area.x + area.width) * SHAPE_Q4 - at.inset;
    shape_span(canvas, span_of(area.y + at.row, left, right), color);
}

RIPPL_NP_FACES_CODE static int corner_distance(int position, int start, int length, int radius)
{
    int centre = position * SHAPE_Q4 + HALF_PIXEL_Q4;
    int near = (start + radius) * SHAPE_Q4;
    int far = (start + length - radius) * SHAPE_Q4;
    return centre < near ? near - centre : centre > far ? centre - far : 0;
}

RIPPL_NP_FACES_CODE void shape_rounded(Canvas *canvas, Rect area, int radius, Color color)
{
    radius = smaller(radius, smaller(area.width, area.height) / 2);
    Rect shown = rect_intersection(canvas->clip, area);
    int inside = radius * SHAPE_Q4 - HALF_PIXEL_Q4;
    int near = (area.x + radius) * SHAPE_Q4 - HALF_PIXEL_Q4;
    int far = (area.x + area.width - radius) * SHAPE_Q4 - HALF_PIXEL_Q4;
    for (int y = shown.y; y < shown.y + shown.height; y++) {
        int dy = corner_distance(y, area.y, area.height, radius);
        int room = inside * inside - dy * dy;
        int reach = inside > 0 && room >= 0 ? (int)shape_isqrt((uint32_t)room) : -1;
        Span solid = {-floor_div(reach - near, SHAPE_Q4), floor_div(far + reach, SHAPE_Q4)};
        PixelRun run = {canvas, y, shown.x, color};
        for (int x = shown.x; dy > 0 && x < shown.x + shown.width; x++) {
            if (reach >= 0 && in_span(solid, x)) {
                x = solid.last;
                continue;
            }
            int dx = corner_distance(x, area.x, area.width, radius);
            if (dx > 0) {
                uint8_t alpha = edge_alpha(dx * dx + dy * dy, radius * SHAPE_Q4);
                if (alpha == 0) {
                    run_flush(&run, x);
                    run.start = x + 1;
                } else {
                    run_put(&run, x, alpha);
                }
            }
        }
        run_flush(&run, shown.x + shown.width);
    }
}

RIPPL_NP_FACES_CODE void shape_trapezoid(Canvas *canvas, Rect area, int top_inset, Color color)
{
    int first = larger(0, canvas->clip.y - area.y);
    int last = smaller(area.height, canvas->clip.y + canvas->clip.height - area.y);
    for (int row = first; row < last; row++) {
        int inset = top_inset * SHAPE_Q4 * (2 * (area.height - row) - 1) / (2 * area.height);
        inset_row(canvas, area, (InsetRow){row, inset}, color);
    }
}

RIPPL_NP_FACES_CODE static int band_coverage(int inside)
{
    int level = inside / SHAPE_UNIT + HALF_PIXEL_Q4;
    return level <= 0 ? 0 : level >= SHAPE_Q4 ? SHAPE_Q4 : level;
}

RIPPL_NP_FACES_CODE static uint8_t spoke_alpha(const ShapeSpoke *spoke, int sine, int cosine,
                                               int dx, int dy)
{
    int across = dx * cosine + dy * sine;
    int along = dx * sine - dy * cosine;
    int half = spoke->width_q4 * SHAPE_UNIT / 2;
    int from = spoke->from * SPOKE_Q4_UNIT;
    int to = spoke->to * SPOKE_Q4_UNIT;
    int wide = band_coverage(half - (across < 0 ? -across : across));
    int deep = band_coverage(smaller(along - from, to - along));
    return coverage(smaller(wide, deep));
}

RIPPL_NP_FACES_CODE static void narrow(Span *span, int base, int step, Span open)
{
    if (step == 0) {
        if (base <= open.first || base >= open.last) {
            *span = (Span){1, 0};
        }
        return;
    }
    int low = open.first - base;
    int high = open.last - base;
    if (step < 0) {
        int swap = low;
        low = -high;
        high = -swap;
        step = -step;
    }
    span->first = larger(span->first, floor_div(low, step));
    span->last = smaller(span->last, floor_div(high, step));
}

RIPPL_NP_FACES_CODE static void spoke_draw(Canvas *canvas, const ShapeSpoke *spoke, int sine,
                                           int cosine, Color color)
{
    int near = -spoke->from * cosine / SHAPE_UNIT;
    int far = -spoke->to * cosine / SHAPE_UNIT;
    int spread = spoke->width_q4 / SHAPE_Q4 + 2;
    int top = larger(spoke->pivot.y + smaller(near, far) - spread, canvas->clip.y);
    int bottom = smaller(spoke->pivot.y + larger(near, far) + spread + 1,
                         canvas->clip.y + canvas->clip.height);
    int left_reach = spoke->from * sine / SHAPE_UNIT;
    int right_reach = spoke->to * sine / SHAPE_UNIT;
    int left = larger(spoke->pivot.x + smaller(left_reach, right_reach) - spread, canvas->clip.x);
    int right = smaller(spoke->pivot.x + larger(left_reach, right_reach) + spread + 1,
                        canvas->clip.x + canvas->clip.width);
    int reach = spoke->width_q4 * SHAPE_UNIT / 2 + BAND_FRINGE;
    int from = spoke->from * SPOKE_Q4_UNIT - BAND_FRINGE;
    int to = spoke->to * SPOKE_Q4_UNIT + BAND_FRINGE;
    for (int y = top; y < bottom; y++) {
        int dy = (y - spoke->pivot.y) * SHAPE_Q4;
        Span span = {left - spoke->pivot.x, right - 1 - spoke->pivot.x};
        narrow(&span, dy * sine, SHAPE_Q4 * cosine, (Span){-reach, reach});
        narrow(&span, -dy * cosine, SHAPE_Q4 * sine, (Span){from, to});
        PixelRun run = {canvas, y, spoke->pivot.x + span.first, color};
        for (int x = spoke->pivot.x + span.first; x <= spoke->pivot.x + span.last; x++) {
            uint8_t alpha = spoke_alpha(spoke, sine, cosine, (x - spoke->pivot.x) * SHAPE_Q4, dy);
            if (alpha == 0) {
                run_flush(&run, x);
                run.start = x + 1;
            } else {
                run_put(&run, x, alpha);
            }
        }
        run_flush(&run, spoke->pivot.x + span.last + 1);
    }
}

RIPPL_NP_FACES_CODE void shape_spoke(Canvas *canvas, const ShapeSpoke *spoke, Color color)
{
    spoke_draw(canvas, spoke, shape_sine(spoke->angle), shape_cosine(spoke->angle), color);
}

RIPPL_NP_FACES_CODE void shape_spoke_fine(Canvas *canvas, const ShapeSpoke *spoke, uint16_t fine,
                                          Color color)
{
    spoke_draw(canvas, spoke, shape_sine_fine(fine), shape_cosine_fine(fine), color);
}

RIPPL_NP_FACES_CODE static void run_draw(const DigitPen *pen, Run run, Color color)
{
    if (run.right < run.left) {
        return;
    }
    const SegmentStyle *style = pen->style;
    int lean = style->lean;
    int shift = lean == 0 ? 0 : (pen->origin.y + style->height - 1 - run.y) / lean;
    canvas_fill_rect(pen->canvas, (Rect){run.left + shift, run.y, run.right - run.left + 1, 1},
                     color);
}

RIPPL_NP_FACES_CODE static DigitFrame digit_frame(const SegmentStyle *style, Point origin)
{
    int half = style->thickness / 2;
    return (DigitFrame){.left = origin.x + half,
                        .right = origin.x + style->width - 1 - half,
                        .top = origin.y + half,
                        .middle = origin.y + (style->height - 1) / 2,
                        .bottom = origin.y + style->height - 1 - half,
                        .half = half};
}

RIPPL_NP_FACES_CODE static Color segment_color(const SegmentStyle *style, uint8_t mask, int bit)
{
    return ((mask >> bit) & 1) != 0 ? style->lit : style->unlit;
}

RIPPL_NP_FACES_CODE static void digit_bar(const DigitPen *pen, const DigitFrame *frame, int row,
                                          Color color)
{
    for (int offset = -frame->half; offset <= frame->half; offset++) {
        int slant = magnitude(offset) + 1;
        run_draw(pen, (Run){frame->left + slant, frame->right - slant, row + offset}, color);
    }
}

RIPPL_NP_FACES_CODE static void digit_post(const DigitPen *pen, const DigitFrame *frame, Post post,
                                           Color color)
{
    for (int row = post.top; row <= post.bottom; row++) {
        int reach = smaller(frame->half, smaller(row - post.top, post.bottom - row) - 1);
        if (reach >= 0) {
            run_draw(pen, (Run){post.x - reach, post.x + reach, row}, color);
        }
    }
}

RIPPL_NP_FACES_CODE static void digit_bars(const DigitPen *pen, const DigitFrame *frame,
                                           uint8_t mask)
{
    const int rows[BAR_COUNT] = {frame->top, frame->middle, frame->bottom};
    for (int index = 0; index < BAR_COUNT; index++) {
        digit_bar(pen, frame, rows[index], segment_color(pen->style, mask, BAR_BITS[index]));
    }
}

RIPPL_NP_FACES_CODE static void digit_posts(const DigitPen *pen, const DigitFrame *frame,
                                            uint8_t mask)
{
    const Post posts[POST_COUNT] = {{frame->left, frame->top, frame->middle},
                                    {frame->right, frame->top, frame->middle},
                                    {frame->left, frame->middle, frame->bottom},
                                    {frame->right, frame->middle, frame->bottom}};
    for (int index = 0; index < POST_COUNT; index++) {
        digit_post(pen, frame, posts[index], segment_color(pen->style, mask, POST_BITS[index]));
    }
}

RIPPL_NP_FACES_CODE void shape_digit(Canvas *canvas, const SegmentStyle *style, Point origin,
                                     uint8_t mask)
{
    DigitPen pen = {canvas, style, origin};
    DigitFrame frame = digit_frame(style, origin);
    digit_bars(&pen, &frame, mask);
    digit_posts(&pen, &frame, mask);
}

RIPPL_NP_FACES_CODE void shape_colon(Canvas *canvas, const SegmentStyle *style, Point origin,
                                     bool lit)
{
    DigitPen pen = {canvas, style, origin};
    DigitFrame frame = digit_frame(style, origin);
    int size = style->thickness - 1;
    int centres[COLON_DOTS] = {(frame.top + frame.middle) / 2,
                               (frame.middle + frame.bottom) / 2 + 1};
    for (int dot = 0; dot < COLON_DOTS; dot++) {
        int first = centres[dot] - size / 2;
        for (int row = first; row < first + size; row++) {
            run_draw(&pen, (Run){origin.x + 2, origin.x + 1 + size, row},
                     lit ? style->lit : style->unlit);
        }
    }
}

RIPPL_NP_FACES_CODE uint8_t shape_digit_mask(unsigned digit)
{
    return digit < DIGIT_SEGMENTS ? DIGIT_MASKS[digit] : 0;
}

RIPPL_NP_FACES_CODE void shape_lattice(Canvas *canvas, Rect area, int pitch, Color color)
{
    if (pitch <= 0) {
        return;
    }
    for (int x = area.x + LATTICE_OFFSET; x < area.x + area.width; x += pitch) {
        canvas_fill_rect(canvas, (Rect){x, area.y, 1, area.height}, color);
    }
    for (int y = area.y + LATTICE_OFFSET; y < area.y + area.height; y += pitch) {
        canvas_fill_rect(canvas, (Rect){area.x, y, area.width, 1}, color);
    }
}
