#include "gfx/image.h"

#include <stdbool.h>

#include "core/resident.h"

typedef struct Column {
    int x;
    int offset;
    int top;
    int height;
} Column;

static int max_of(int first, int second)
{
    return first > second ? first : second;
}

static int min_of(int first, int second)
{
    return first < second ? first : second;
}

static int interpolate(int from, int to, int offset, int span)
{
    return from + (to - from) * offset / span;
}

static Column column_at(Quad quad, int x)
{
    int offset = x - quad.left;
    return (Column){
        .x = x,
        .offset = offset,
        .top = interpolate(quad.left_top, quad.right_top, offset, quad.width),
        .height = interpolate(quad.left_height, quad.right_height, offset, quad.width),
    };
}

static Color *pixel_address(Canvas *canvas, int x, int y)
{
    Rect bounds = canvas->bounds;
    return &canvas->pixels[(y - bounds.y) * bounds.width + (x - bounds.x)];
}

enum {
    STEP_SHIFT = 16
};

typedef struct Sampler {
    const Color *column;
    uint32_t position;
    uint32_t step;
} Sampler;

static Sampler sampler_at(const Column *column, const Image *image, int quad_width, int skip)
{
    uint32_t height = (uint32_t)column->height;
    uint32_t step = (((uint32_t)image->height << STEP_SHIFT) + height - 1) / height;
    return (Sampler){
        .column = &image->pixels[column->offset * image->width / quad_width],
        .position = step * (uint32_t)skip,
        .step = step,
    };
}

static void draw_column(Canvas *canvas, Column column, const Image *image, int quad_width)
{
    Rect clip = canvas->clip;
    int first = max_of(column.top, clip.y);
    int last = min_of(column.top + column.height, clip.y + clip.height);
    if (first >= last) {
        return;
    }
    int stride = canvas->bounds.width;
    int width = image->width;
    Sampler sampler = sampler_at(&column, image, quad_width, first - column.top);
    Color *target = pixel_address(canvas, column.x, first);
    for (int count = last - first; count > 0; count--) {
        *target = sampler.column[(int)(sampler.position >> STEP_SHIFT) * width];
        target += stride;
        sampler.position += sampler.step;
    }
}

static unsigned light_at(QuadLight light, Column column, int quad_width)
{
    return (unsigned)interpolate(light.left, light.right, column.offset, quad_width);
}

static void draw_lit_column(Canvas *canvas, Column column, const Image *image, int quad_width,
                            unsigned light)
{
    Rect clip = canvas->clip;
    int first = max_of(column.top, clip.y);
    int last = min_of(column.top + column.height, clip.y + clip.height);
    if (first >= last) {
        return;
    }
    int stride = canvas->bounds.width;
    int width = image->width;
    Sampler sampler = sampler_at(&column, image, quad_width, first - column.top);
    Color *target = pixel_address(canvas, column.x, first);
    for (int count = last - first; count > 0; count--) {
        *target = color_scale(sampler.column[(int)(sampler.position >> STEP_SHIFT) * width], light);
        target += stride;
        sampler.position += sampler.step;
    }
}

static void draw_reflected_column(Canvas *canvas, Column column, const Image *image, int quad_width,
                                  unsigned light, int rows)
{
    Rect clip = canvas->clip;
    int top = column.top + column.height;
    int first = max_of(top, clip.y);
    int last = min_of(top + rows, clip.y + clip.height);
    if (first >= last) {
        return;
    }
    int skip = first - top;
    int stride = canvas->bounds.width;
    int width = image->width;
    int last_row = image->height - 1;
    Sampler sampler = sampler_at(&column, image, quad_width, skip);
    uint32_t fade_step = ((uint32_t)light << COLOR_LIGHT_SHIFT) / (uint32_t)rows;
    uint32_t faded = ((uint32_t)light << COLOR_LIGHT_SHIFT) - fade_step * (uint32_t)skip;
    Color *target = pixel_address(canvas, column.x, first);
    for (int count = last - first; count > 0; count--) {
        int row = max_of(last_row - (int)(sampler.position >> STEP_SHIFT), 0);
        *target = color_scale(sampler.column[row * width], faded >> COLOR_LIGHT_SHIFT);
        target += stride;
        sampler.position += sampler.step;
        faded -= fade_step;
    }
}

static void fill_column(Canvas *canvas, Column column, Color color)
{
    Rect clip = canvas->clip;
    int first = max_of(column.top, clip.y);
    int last = min_of(column.top + column.height, clip.y + clip.height);
    for (int y = first; y < last; y++) {
        *pixel_address(canvas, column.x, y) = color;
    }
}

static bool quad_visible(const Canvas *canvas, Quad quad, int *first, int *last)
{
    Rect clip = canvas->clip;
    *first = max_of(quad.left, clip.x);
    *last = min_of(quad.left + quad.width, clip.x + clip.width);
    return quad.width > 0 && *first < *last;
}

void image_draw_quad(Canvas *canvas, Image image, Quad quad)
{
    int first = 0;
    int last = 0;
    if (image.width <= 0 || image.height <= 0 || !quad_visible(canvas, quad, &first, &last)) {
        return;
    }
    for (int x = first; x < last; x++) {
        Column column = column_at(quad, x);
        if (column.height > 0) {
            draw_column(canvas, column, &image, quad.width);
        }
    }
}

void image_draw_quad_lit(Canvas *canvas, Image image, Quad quad, QuadLight light)
{
    if (light.left >= COLOR_LIGHT_FULL && light.right >= COLOR_LIGHT_FULL) {
        image_draw_quad(canvas, image, quad);
        return;
    }
    int first = 0;
    int last = 0;
    if (image.width <= 0 || image.height <= 0 || !quad_visible(canvas, quad, &first, &last)) {
        return;
    }
    for (int x = first; x < last; x++) {
        Column column = column_at(quad, x);
        if (column.height > 0) {
            draw_lit_column(canvas, column, &image, quad.width,
                            light_at(light, column, quad.width));
        }
    }
}

void image_draw_reflection(Canvas *canvas, Image image, Quad quad, QuadLight light, int rows)
{
    int first = 0;
    int last = 0;
    if (rows <= 0 || image.width <= 0 || image.height <= 0 ||
        !quad_visible(canvas, quad, &first, &last)) {
        return;
    }
    for (int x = first; x < last; x++) {
        Column column = column_at(quad, x);
        if (column.height > 0) {
            draw_reflected_column(canvas, column, &image, quad.width,
                                  light_at(light, column, quad.width), rows);
        }
    }
}

void image_fill_quad(Canvas *canvas, Quad quad, Color color)
{
    int first = 0;
    int last = 0;
    if (!quad_visible(canvas, quad, &first, &last)) {
        return;
    }
    for (int x = first; x < last; x++) {
        Column column = column_at(quad, x);
        if (column.height > 0) {
            fill_column(canvas, column, color);
        }
    }
}

enum {
    TURN_SPREAD_MASK = 0x07E0F81F,
    TURN_ROUND_HALF = 0x01004008
};

static inline uint32_t spread_pixel(Color color)
{
    return ((uint32_t)color | ((uint32_t)color << 16)) & (uint32_t)TURN_SPREAD_MASK;
}

RIPPL_CODE2 void image_draw_turned_row(Color *dst, const Image *image, const ImageRowStep *step,
                                       int count)
{
    int width = image->width;
    int max_x = width - 1;
    int max_y = image->height - 1;
    int max_u = max_x << IMAGE_TURN_SHIFT;
    int max_v = max_y << IMAGE_TURN_SHIFT;
    const Color *src = image->pixels;
    int u = step->u;
    int v = step->v;
    int du = step->du;
    int dv = step->dv;
    for (; count > 0; count--) {
        int cu = u < 0 ? 0 : u > max_u ? max_u : u;
        int cv = v < 0 ? 0 : v > max_v ? max_v : v;
        int sx = cu >> IMAGE_TURN_SHIFT;
        int sy = cv >> IMAGE_TURN_SHIFT;
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
            uint32_t fx = (uint32_t)((cu >> (IMAGE_TURN_SHIFT - 4)) & 15);
            uint32_t fy = (uint32_t)((cv >> (IMAGE_TURN_SHIFT - 4)) & 15);
            uint32_t inv_x = 16u - fx;
            uint32_t inv_y = 16u - fy;
            uint32_t top =
                ((spread_pixel(c00) * inv_x + spread_pixel(c10) * fx + (uint32_t)TURN_ROUND_HALF) >>
                 4) &
                (uint32_t)TURN_SPREAD_MASK;
            uint32_t bot =
                ((spread_pixel(c01) * inv_x + spread_pixel(c11) * fx + (uint32_t)TURN_ROUND_HALF) >>
                 4) &
                (uint32_t)TURN_SPREAD_MASK;
            uint32_t mix = ((top * inv_y + bot * fy + (uint32_t)TURN_ROUND_HALF) >> 4) &
                           (uint32_t)TURN_SPREAD_MASK;
            *dst++ = (Color)(mix | (mix >> 16));
        }
        u += du;
        v += dv;
    }
}
