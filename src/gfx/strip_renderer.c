#include "gfx/strip_renderer.h"

#include <stddef.h>

static int min_of(int first, int second)
{
    return first < second ? first : second;
}

static void render_strip(const StripRenderer *renderer, Color *pixels, Rect strip,
                         FrameDrawer drawer, FrameSink sink)
{
    Canvas canvas = canvas_make(pixels, strip);
    if (!renderer->drawer_paints_everything) {
        canvas_clear(&canvas, renderer->background);
    }
    drawer.draw(drawer.context, &canvas);
    sink.write(sink.context, strip, pixels);
}

void strip_renderer_render(const StripRenderer *renderer, FrameDrawer drawer, FrameSink sink)
{
    Color *pixels = renderer->strip_pixels;
    for (int top = 0; top < renderer->screen_height; top += renderer->strip_height) {
        int height = min_of(renderer->strip_height, renderer->screen_height - top);
        Rect strip = {0, top, renderer->screen_width, height};
        render_strip(renderer, pixels, strip, drawer, sink);
        if (renderer->other_pixels != NULL) {
            pixels =
                pixels == renderer->strip_pixels ? renderer->other_pixels : renderer->strip_pixels;
        }
    }
}
