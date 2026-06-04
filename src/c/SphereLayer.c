/**
 * SphereLayer — custom circular Layer for Solus Watchface
 *
 * Originally created by Graham-Bryan
 * https://github.com/Graham-Bryan/pebble-solus
 *
 * Modified as part of the pebble-solus-v2 fork
 * Developed with Claude (Anthropic).
 */

#include <pebble.h>
#include "SphereLayer.h"

// Per-layer data stored inside each sphere Layer.
typedef struct {
  GColor color;
  int radius;
} SphereData;

// Layer update callback — fills a circle centered within the layer's own frame.
static void sphere_layer_draw(Layer *layer, GContext *ctx) {
  SphereData *data = (SphereData *)layer_get_data(layer);
  graphics_context_set_fill_color(ctx, data->color);
  // Center of our own frame is always (radius, radius) because the frame is
  // sized to exactly 2*radius × 2*radius by sphere_layer_create().
  graphics_fill_circle(ctx, GPoint(data->radius, data->radius), data->radius - 1);
}

Layer *sphere_layer_create(GPoint pos, GColor color, int radius) {
  // Frame is positioned so the center of the circle is at pos.
  Layer *layer = layer_create_with_data(
      GRect(pos.x - radius, pos.y - radius, radius * 2, radius * 2),
      sizeof(SphereData));
  SphereData *data = (SphereData *)layer_get_data(layer);
  data->color = color;
  data->radius = radius;
  layer_set_update_proc(layer, sphere_layer_draw);
  return layer;
}

void sphere_layer_change_color(Layer *layer, GColor color) {
  SphereData *data = (SphereData *)layer_get_data(layer);
  // Only repaint if the color actually changed.
  if (!gcolor_equal(data->color, color)) {
    data->color = color;
    layer_mark_dirty(layer);
  }
}
