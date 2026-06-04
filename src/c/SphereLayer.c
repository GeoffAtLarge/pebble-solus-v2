#include <pebble.h>
#include "SphereLayer.h"

typedef struct {
  GColor color;
  int radius;
} CircleData;

static void layer_draw(Layer *layer, GContext *ctx) {
  CircleData *data = (CircleData*)layer_get_data(layer);
  
  graphics_context_set_fill_color(ctx, data->color);
  graphics_fill_circle (ctx,GPoint(data->radius,data->radius), data->radius - 1);
}

Layer* sphere_layer_create(GPoint pos, GColor color, int radius)
{
  Layer *layer = layer_create_with_data(GRect(pos.x - radius, pos.y - radius, radius * 2, radius * 2), sizeof(CircleData));
  
  CircleData *data = (CircleData*)layer_get_data(layer);
  data->color = color;
  data->radius = radius;
  
  layer_set_update_proc(layer, layer_draw);
  
  return layer;
}

void sphere_layer_change_color(Layer *layer, GColor color)
{
  CircleData *data = (CircleData*)layer_get_data(layer);
  data->color = color;
}