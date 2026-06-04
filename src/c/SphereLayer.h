#pragma once

#include <pebble.h>

/**
 * Creates a circular "sphere" layer at the given position with the given color
 * and radius. The layer's frame is sized to fit the circle exactly.
 *
 * @param pos     Center of the sphere, in parent-layer coordinates.
 * @param color   Initial fill color.
 * @param radius  Radius of the sphere in pixels.
 * @return        A Layer* that can be added to the window layer tree.
 *                Destroy with layer_destroy().
 */
Layer *sphere_layer_create(GPoint pos, GColor color, int radius);

/**
 * Changes the fill color of a sphere layer without recreating it.
 *
 * @param layer  A layer previously created with sphere_layer_create().
 * @param color  The new fill color.
 */
void sphere_layer_change_color(Layer *layer, GColor color);
