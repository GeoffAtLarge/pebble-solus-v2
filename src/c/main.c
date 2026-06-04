/**
 * Solus Watchface — v2.1
 *
 * Originally created by Graham-Bryan
 * https://github.com/Graham-Bryan/pebble-solus
 *
 * This version is a fork developed with Claude (Anthropic).
 * Modifications include:
 *   - Pebble Time 2 (emery, 200×228) and Pebble Round 2 (gabbro, 260×260) support
 *   - Procedural vector starfield (replaces bitmap resource)
 *   - Animation configuration screen (always on / daytime only / always off)
 *   - Battery and memory optimisations throughout
 *
 * Original work © Graham-Bryan. Fork © NetNoise.
 * Released under the same licence as the original project.
 *
 * An orbital solar-system clock: a central "sun" sphere rises and sets with
 * real sunrise/sunset data, while minute and hour "planets" orbit it.
 *
 * Targets: emery (Pebble Time 2, 200×228, 64-color rectangular display).
 *
 * Vector graphics notes:
 *  - The starfield is drawn procedurally in a LayerUpdateProc using a seeded
 *    linear-congruential PRNG.  No bitmap resource is required; the field
 *    automatically fills whatever screen size layer_get_bounds() returns.
 *  - All orbital spheres are drawn with graphics_fill_circle() — already vector.
 *  - The only remaining PNG is the app-store icon, which Pebble requires as PNG.
 *
 * Best-practice notes (developer.repebble.com/tutorials/watchface-tutorial/part1/):
 *  - Static file-scope variables are prefixed with s_.
 *  - init() / deinit() / main() structure.
 *  - Window load/unload handlers create and destroy all sub-layers.
 *  - layer_get_bounds() used everywhere; no hardcoded screen dimensions.
 *  - MINUTE_UNIT tick timer for battery efficiency.
 *  - All resources destroyed in the unload handler.
 *  - Fixed-point trig via sin_lookup() / cos_lookup() + TRIG_MAX_RATIO.
 *  - AppMessage callbacks registered before app_message_open().
 *  - Persistent storage used for animation-mode preference.
 */

#include <pebble.h>
#include "SphereLayer.h"

// ─── AppMessage keys ──────────────────────────────────────────────────────────
// CloudPebble "Automatic assignment" assigns integers alphabetically by key name.
// The SDK generates MESSAGE_KEY_KEY_* constants in appinfo.auto.c, but those are
// emitted as 'const int' variables which C does not allow as case labels.
// We therefore define our own #define constants with the same alphabetical values.
// If you add or rename a key in CloudPebble Settings, re-derive the order and
// update these values, index.js, and appinfo.json to match.
//   KEY_ANIM_MODE = 0  (A before S)
//   KEY_SUNRISE   = 1
//   KEY_SUNSET    = 2
#define KEY_ANIM_MODE 0
#define KEY_SUNRISE   1
#define KEY_SUNSET    2

// ─── Persistent-storage key ──────────────────────────────────────────────────
#define PERSIST_KEY_ANIM_MODE 100

// ─── Animation-mode constants (must match config page values) ────────────────
#define ANIM_MODE_ALL_ON       0
#define ANIM_MODE_DAYTIME_ONLY 1
#define ANIM_MODE_ALL_OFF      2

// ─── Orbit geometry — expressed as fractions of screen width so the layout
//     works correctly for emery (200 px wide) and any future platform.
//     Reference base is 144 px (basalt / original watchface).
#define ORBIT_SCALE_BASE 144

// ─── File-scope statics ──────────────────────────────────────────────────────

// Layout (populated in main_window_load from layer_get_bounds())
static GRect  s_window_bounds;
static GPoint s_window_center;

// Orbit geometry (computed from s_window_bounds in main_window_load)
static int16_t s_minute_orbit_dist;
static int16_t s_hour_orbit_dist;
static int     s_minute_radius;
static int     s_hour_radius;
static int     s_pivot_radius;
static int     s_daylight_radius;

// Layers
static Window *s_main_window;
static Layer  *s_daylight_layer;
static Layer  *s_minute_layer;
static Layer  *s_hour_layer;
static Layer  *s_pivot_layer;
static Layer  *s_star_layer;   // procedurally-drawn starfield (no bitmap resource)

// Animation handles
static Animation *s_minute_hand_anim;
static Animation *s_hour_hand_anim;

// Current orbit angles (TRIG_MAX_ANGLE units)
static int32_t s_minute_angle;
static int32_t s_hour_angle;

// Daylight data (seconds from midnight)
static int32_t s_sunrise_t = 6  * 60 * 60;  // default 06:00
static int32_t s_sunset_t  = 18 * 60 * 60;  // default 18:00

// Fractional daylight progress in Q16 fixed-point (0 = sunrise, 65536 = sunset)
static int32_t s_daylight_fp = 0;

// Daylight colours: deep night → twilight → dawn → golden-hour → full day
static GColor s_colors[6];

// Communication state
static bool s_daylight_requested = false;
// Retry backoff for failed outbox sends (minutes between retries: 1, 2, 4, 8, …)
#define RETRY_MAX_INTERVAL_MINS 32
static int  s_retry_interval    = 1;   // current backoff interval in minutes
static int  s_retry_countdown   = 0;   // ticks remaining before next retry

// Animation mode — loaded from persistent storage
static int s_anim_mode = ANIM_MODE_ALL_ON;

// Current time (set in update_time so should_animate() can read it)
static int s_current_hour = 0;
static int s_current_min  = 0;

// Dedup guards — track what was last rendered to skip no-op repaints
// Background band: 0=deep-night 1=night 2=twilight 3=golden 4=day  -1=uninitialised
static int     s_last_bg_band       = -1;
static bool    s_last_stars_visible = true;   // matches main_window_load which adds the layer unconditionally
static bool    s_star_in_hierarchy  = false;  // true when s_star_layer has a parent
// Last daylight-sphere Y origin — INT16_MIN forces the first frame to always draw
static int16_t s_last_daylight_y    = INT16_MIN;

// ─── Procedural starfield ─────────────────────────────────────────────────────
//
// Stars are placed using a seeded linear-congruential PRNG so positions are
// identical on every frame, yet require zero resource bytes.  The seed is fixed
// so the field is deterministic across watch reboots.
//
// Each "star" is independently sized:
//   weight 0–3 → single pixel  (dim, common)
//   weight 4–5 → radius-1 circle (medium)
//   weight 6   → radius-2 circle (bright, rare)
//
// Colours are chosen from a purple/blue palette that matches the night sky
// background, graduating to white for the brightest stars.

#define STAR_COUNT 180
#define STAR_SEED  0xDEADC0DEu

static uint32_t prng_step(uint32_t state) {
  // Knuth multiplicative LCG — fast, no division, good distribution.
  return state * 1664525u + 1013904223u;
}

static void star_layer_draw(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int w = bounds.size.w;
  int h = bounds.size.h;

  uint32_t rng = STAR_SEED;

  for (int i = 0; i < STAR_COUNT; i++) {
    rng = prng_step(rng);
    int x = (int)(rng & 0xFFFF) * w / 65536;

    rng = prng_step(rng);
    int y = (int)(rng & 0xFFFF) * h / 65536;

    rng = prng_step(rng);
    int weight = (int)(rng % 7);  // 0–6

    // Pick colour based on weight: dim purple → mid lavender → white
    GColor color;
    if (weight <= 1) {
      color = GColorImperialPurple;
    } else if (weight <= 3) {
      color = GColorLavenderIndigo;
    } else if (weight <= 5) {
      color = GColorLightGray;
    } else {
      color = GColorWhite;
    }
    graphics_context_set_fill_color(ctx, color);

    if (weight <= 3) {
      // Single pixel — use a 1×1 filled rect (graphics_draw_pixel respects AA)
      graphics_fill_rect(ctx, GRect(x, y, 1, 1), 0, GCornerNone);
    } else if (weight <= 5) {
      graphics_fill_circle(ctx, GPoint(x, y), 1);
    } else {
      graphics_fill_circle(ctx, GPoint(x, y), 2);
    }
  }
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

/**
 * Computes the pixel position of a point that orbits the screen center
 * at distance `dist`, at the given `angle` (TRIG_MAX_ANGLE units).
 * Returns the top-left corner of a square of side 2*size.
 */
static GPoint get_orbital_position(int32_t angle, int16_t dist, int size) {
  return (GPoint){
    .x = (int16_t)(sin_lookup(angle)  * (int32_t)dist / TRIG_MAX_RATIO)
         + s_window_center.x - size,
    .y = (int16_t)(-cos_lookup(angle) * (int32_t)dist / TRIG_MAX_RATIO)
         + s_window_center.y - size,
  };
}

/** Sends a zero-byte message to the phone to trigger a daylight data fetch. */
static void request_daylight(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, 0, 0);
    app_message_outbox_send();
  }
}

/**
 * Returns true if animations should run right now given the current
 * animation-mode setting and the time of day.
 */
static bool should_animate(void) {
  switch (s_anim_mode) {
    case ANIM_MODE_ALL_ON:
      return true;
    case ANIM_MODE_DAYTIME_ONLY: {
      int32_t now_secs = s_current_hour * 3600 + s_current_min * 60;
      return (now_secs >= s_sunrise_t && now_secs <= s_sunset_t);
    }
    case ANIM_MODE_ALL_OFF:
      return false;
    default:
      return true;
  }
}

// ─── Animation callbacks ─────────────────────────────────────────────────────

// Easing helper — maps a normalised animation progress to [0, max].
static int anim_percentage(AnimationProgress dist_normalized, int max) {
  return (int)(((int64_t)dist_normalized * max) / ANIMATION_NORMALIZED_MAX);
}

static void minute_anim_update(Animation *anim, AnimationProgress dist_normalized) {
  // Rotate the minute planet one full revolution (360°) from its start angle.
  int step = anim_percentage(dist_normalized, TRIG_MAX_ANGLE);
  int32_t anim_angle = s_minute_angle + step;
  GPoint pos = get_orbital_position(anim_angle, s_minute_orbit_dist, s_minute_radius);
  layer_set_frame(s_minute_layer,
                  GRect(pos.x, pos.y, s_minute_radius * 2, s_minute_radius * 2));
}

static void hour_anim_update(Animation *anim, AnimationProgress dist_normalized) {
  // The hour planet does two full revolutions for visual drama.
  int step = anim_percentage(dist_normalized, TRIG_MAX_ANGLE * 2);
  int32_t anim_angle = s_hour_angle + step;
  GPoint pos = get_orbital_position(anim_angle, s_hour_orbit_dist, s_hour_radius);
  layer_set_frame(s_hour_layer,
                  GRect(pos.x, pos.y, s_hour_radius * 2, s_hour_radius * 2));
}

static const PropertyAnimationImplementation s_minute_anim_impl = {
  .base = { .update = (AnimationUpdateImplementation)minute_anim_update },
};
static const PropertyAnimationImplementation s_hour_anim_impl = {
  .base = { .update = (AnimationUpdateImplementation)hour_anim_update },
};

// Animation stopped callbacks — the SDK automatically destroys animation objects
// when they complete naturally, invalidating the handle.  We null out our static
// pointers here so the next tick's guard (if (s_*_hand_anim)) correctly skips
// the unschedule/destroy path on an already-freed handle.
static void minute_anim_stopped(Animation *anim, bool finished, void *context) {
  s_minute_hand_anim = NULL;
}

static void hour_anim_stopped(Animation *anim, bool finished, void *context) {
  s_hour_hand_anim = NULL;
}

static void animate_minute_hand(void) {
  // Unschedule then destroy only if the animation is still running (handle is
  // non-NULL).  If it completed naturally, minute_anim_stopped already cleared
  // the pointer and the SDK has freed the object.
  if (s_minute_hand_anim) {
    animation_unschedule(s_minute_hand_anim);
    animation_destroy(s_minute_hand_anim);
    s_minute_hand_anim = NULL;
  }
  s_minute_hand_anim = (Animation *)property_animation_create(
      &s_minute_anim_impl, s_minute_layer, NULL, NULL);
  animation_set_duration(s_minute_hand_anim, 4000);
  animation_set_curve(s_minute_hand_anim, AnimationCurveEaseInOut);
  animation_set_handlers(s_minute_hand_anim,
      (AnimationHandlers){ .stopped = minute_anim_stopped }, NULL);
  animation_schedule(s_minute_hand_anim);
}

static void animate_hour_hand(void) {
  if (s_hour_hand_anim) {
    animation_unschedule(s_hour_hand_anim);
    animation_destroy(s_hour_hand_anim);
    s_hour_hand_anim = NULL;
  }
  s_hour_hand_anim = (Animation *)property_animation_create(
      &s_hour_anim_impl, s_hour_layer, NULL, NULL);
  animation_set_duration(s_hour_hand_anim, 4000);
  animation_set_curve(s_hour_hand_anim, AnimationCurveEaseInOut);
  animation_set_handlers(s_hour_hand_anim,
      (AnimationHandlers){ .stopped = hour_anim_stopped }, NULL);
  animation_schedule(s_hour_hand_anim);
}

// ─── Daylight curve ──────────────────────────────────────────────────────────

/**
 * Smoothstep-like easing for the daylight-sphere vertical travel.
 * Input t is in Q16 (0 = start, 65536 = end of daylight window).
 * Returns a Q16 value in [0, 65536].
 */
static int32_t daylight_curve_q16(int32_t t_q16) {
  // Clamp to valid range first — inputs outside [0, 65536] (e.g. before sunrise
  // or after sunset) would cause int64_t overflow in the u7 chain below.
  if (t_q16 <= 0)     return 0;
  if (t_q16 >= 65536) return 65536;
  // Map to [-65536, 65536] centred on zero, apply 7th-power ease, map back.
  int64_t u = (int64_t)t_q16 * 2 - 65536; // [-65536, 65536]
  // u^7 / 65536^6  (keep result in Q16)
  int64_t u2 = u * u / 65536;
  int64_t u4 = u2 * u2 / 65536;
  int64_t u7 = u4 * u2 / 65536 * u / 65536;
  return (int32_t)((u7 + 65536) / 2);
}

// ─── Main time-update logic ───────────────────────────────────────────────────

static void update_time(void) {
  // Request daylight data on the first tick.
  if (!s_daylight_requested) {
    request_daylight();
  }

  // Get current wall-clock time.
  time_t now_epoch = time(NULL);
  struct tm *tick_time = localtime(&now_epoch);
  s_current_hour = tick_time->tm_hour;
  s_current_min  = tick_time->tm_min;

  int32_t now_secs = s_current_hour * 3600 + s_current_min * 60;

  // Compute fractional daylight position (Q16).
  int32_t daylight_window = s_sunset_t - s_sunrise_t;
  int32_t raw_q16;
  if (daylight_window <= 0) {
    raw_q16 = 0;
  } else {
    // Scale: 0 at sunrise, 65536 at sunset.
    raw_q16 = (int32_t)(((int64_t)(now_secs - s_sunrise_t) * 65536)
                        / daylight_window);
  }
  s_daylight_fp = daylight_curve_q16(raw_q16);

  // ── Background colour & star visibility ──────────────────────────────────
  bool is_night = (now_secs < s_sunrise_t || now_secs > s_sunset_t);
  bool is_twilight = !is_night
      && (raw_q16 < 6553 || raw_q16 > 58982);   // within ~10% of sunrise/set
  bool is_golden  = !is_night && !is_twilight
      && (raw_q16 < 13107 || raw_q16 > 52428);  // within ~20%

  // Classify into a numbered band (0–4) so we can skip no-op repaints.
  int bg_band;
  bool stars_visible;
  GColor bg_color;
  if (now_secs < s_sunrise_t - 5400 || now_secs > s_sunset_t + 5400) {
    bg_band = 0; bg_color = s_colors[0]; stars_visible = true;
  } else if (is_night) {
    bg_band = 1; bg_color = s_colors[1]; stars_visible = true;
  } else if (is_twilight) {
    bg_band = 2; bg_color = s_colors[2]; stars_visible = false;
  } else if (is_golden) {
    bg_band = 3; bg_color = s_colors[3]; stars_visible = false;
  } else {
    bg_band = 4; bg_color = s_colors[4]; stars_visible = false;
  }

  // Only touch the layer tree when something actually changed.
  if (bg_band != s_last_bg_band) {
    window_set_background_color(s_main_window, bg_color);
    s_last_bg_band = bg_band;
  }
  if (stars_visible != s_last_stars_visible) {
    if (stars_visible) {
      // Guard against double-add using our own flag — layer_get_parent() does
      // not exist in the Pebble SDK.
      if (!s_star_in_hierarchy) {
        layer_insert_below_sibling(s_star_layer, s_daylight_layer);
        s_star_in_hierarchy = true;
      }
    } else {
      layer_remove_from_parent(s_star_layer);
      s_star_in_hierarchy = false;
    }
    s_last_stars_visible = stars_visible;
  }

  // ── Sphere colours (white at night, dark during day) ─────────────────────
  GColor sphere_color = is_night ? GColorWhite : GColorBlack;
  // sphere_layer_change_color already guards against no-op repaints internally.
  sphere_layer_change_color(s_pivot_layer,  sphere_color);
  sphere_layer_change_color(s_minute_layer, sphere_color);
  sphere_layer_change_color(s_hour_layer,   sphere_color);

  // ── Daylight sphere vertical position ────────────────────────────────────
  int screen_top    = -s_daylight_radius * 2;
  int screen_bottom = s_window_bounds.size.h + s_daylight_radius * 2;
  int16_t new_daylight_y = (int16_t)(
      screen_top
      + (int32_t)((int64_t)(screen_bottom - screen_top) * s_daylight_fp / 65536)
      - s_daylight_radius);
  // Only call layer_set_frame (which marks dirty) when the pixel row changed.
  if (new_daylight_y != s_last_daylight_y) {
    GRect daylight_frame = layer_get_frame(s_daylight_layer);
    daylight_frame.origin.y = new_daylight_y;
    layer_set_frame(s_daylight_layer, daylight_frame);
    s_last_daylight_y = new_daylight_y;
  }

  // ── Minute hand static position ──────────────────────────────────────────
  int32_t new_minute_angle = (int32_t)TRIG_MAX_ANGLE * s_current_min / 60;
  if (new_minute_angle != s_minute_angle) {
    s_minute_angle = new_minute_angle;
    GPoint minute_pos = get_orbital_position(s_minute_angle,
                                             s_minute_orbit_dist, s_minute_radius);
    layer_set_frame(s_minute_layer,
                    GRect(minute_pos.x, minute_pos.y,
                          s_minute_radius * 2, s_minute_radius * 2));
  }

  // ── Hour hand static position ─────────────────────────────────────────────
  int32_t new_hour_angle = (int32_t)TRIG_MAX_ANGLE * (s_current_hour % 12) / 12
                           + s_minute_angle / 12;
  if (new_hour_angle != s_hour_angle) {
    s_hour_angle = new_hour_angle;
    GPoint hour_pos = get_orbital_position(s_hour_angle,
                                           s_hour_orbit_dist, s_hour_radius);
    layer_set_frame(s_hour_layer,
                    GRect(hour_pos.x, hour_pos.y,
                          s_hour_radius * 2, s_hour_radius * 2));
  }
}

// ─── Tick handler ─────────────────────────────────────────────────────────────

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  // Service the backoff retry counter for daylight requests.
  if (s_retry_countdown > 0) {
    s_retry_countdown--;
    if (s_retry_countdown == 0) {
      APP_LOG(APP_LOG_LEVEL_INFO, "Retrying daylight request");
      request_daylight();
    }
  }
  update_time();
  if (should_animate()) {
    animate_minute_hand();
    animate_hour_hand();
  }
}

// ─── Window lifecycle ────────────────────────────────────────────────────────

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  s_window_bounds = layer_get_bounds(window_layer);
  s_window_center = grect_center_point(&s_window_bounds);

  // Scale orbit geometry proportionally from the basalt reference (144 px wide).
  int w = s_window_bounds.size.w;
  s_minute_orbit_dist = (int16_t)(65 * w / ORBIT_SCALE_BASE);
  s_hour_orbit_dist   = (int16_t)(40 * w / ORBIT_SCALE_BASE);
  s_minute_radius     = 8  * w / ORBIT_SCALE_BASE;
  s_hour_radius       = 8  * w / ORBIT_SCALE_BASE;
  s_pivot_radius      = 26 * w / ORBIT_SCALE_BASE;
  s_daylight_radius   = 50 * w / ORBIT_SCALE_BASE;

  // Clamp radii to at least 4 px so they're always visible.
  if (s_minute_radius < 4) s_minute_radius = 4;
  if (s_hour_radius   < 4) s_hour_radius   = 4;
  if (s_pivot_radius  < 8) s_pivot_radius   = 8;
  if (s_daylight_radius < 12) s_daylight_radius = 12;

  // Procedural starfield layer (replaces the old PNG bitmap).
  // The update proc draws all stars from a seeded PRNG; no resource file needed.
  // The layer fills the full screen so stars always cover the entire background.
  s_star_layer = layer_create(s_window_bounds);
  layer_set_update_proc(s_star_layer, star_layer_draw);
  layer_add_child(window_layer, s_star_layer);
  s_star_in_hierarchy = true;

  // Daylight sphere (the "sun").
  s_daylight_layer = sphere_layer_create(
      s_window_center, GColorWhite, s_daylight_radius);
  layer_add_child(window_layer, s_daylight_layer);

  // Central pivot.
  s_pivot_layer = sphere_layer_create(
      s_window_center, GColorBlack, s_pivot_radius);
  layer_add_child(window_layer, s_pivot_layer);

  // Minute planet.
  GPoint minute_start = {
    .x = s_window_center.x,
    .y = s_window_center.y - s_minute_orbit_dist,
  };
  s_minute_layer = sphere_layer_create(minute_start, GColorBlack, s_minute_radius);
  layer_add_child(window_layer, s_minute_layer);

  // Hour planet.
  GPoint hour_start = {
    .x = s_window_center.x,
    .y = s_window_center.y + s_hour_orbit_dist,
  };
  s_hour_layer = sphere_layer_create(hour_start, GColorBlack, s_hour_radius);
  layer_add_child(window_layer, s_hour_layer);
}

static void main_window_unload(Window *window) {
  // Destroy all layers created in main_window_load.
  layer_destroy(s_star_layer);
  layer_destroy(s_daylight_layer);
  layer_destroy(s_pivot_layer);
  layer_destroy(s_minute_layer);
  layer_destroy(s_hour_layer);
}

// ─── AppMessage callbacks ─────────────────────────────────────────────────────

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  Tuple *t = dict_read_first(iterator);
  while (t != NULL) {
    switch (t->key) {
      case KEY_SUNRISE:
        s_sunrise_t = t->value->int32;
#ifdef DEBUG
        APP_LOG(APP_LOG_LEVEL_INFO, "KEY_SUNRISE: %ld s", (long)s_sunrise_t);
#endif
        break;
      case KEY_SUNSET:
        s_sunset_t = t->value->int32;
        s_daylight_requested = true;
#ifdef DEBUG
        APP_LOG(APP_LOG_LEVEL_INFO, "KEY_SUNSET: %ld s", (long)s_sunset_t);
#endif
        update_time();
        break;
      case KEY_ANIM_MODE: {
        int mode = (int)t->value->int32;
        // Clamp to valid range in case of corrupt or future-version values.
        s_anim_mode = (mode >= ANIM_MODE_ALL_ON && mode <= ANIM_MODE_ALL_OFF)
                      ? mode : ANIM_MODE_ALL_ON;
        persist_write_int(PERSIST_KEY_ANIM_MODE, s_anim_mode);
#ifdef DEBUG
        APP_LOG(APP_LOG_LEVEL_INFO, "KEY_ANIM_MODE: %d", s_anim_mode);
#endif
        break;
      }
      default:
        APP_LOG(APP_LOG_LEVEL_WARNING, "Unrecognised key: %d", (int)t->key);
        break;
    }
    t = dict_read_next(iterator);
  }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "AppMessage dropped: %d", (int)reason);
}

static void outbox_failed_callback(DictionaryIterator *iterator,
                                   AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox failed: %d — will retry in %d min(s)",
          (int)reason, s_retry_interval);
  // Schedule a retry after an exponential backoff, rather than retrying
  // immediately (which hammers the Bluetooth radio when the phone is out of range).
  s_retry_countdown = s_retry_interval;
  s_retry_interval  = (s_retry_interval * 2 > RETRY_MAX_INTERVAL_MINS)
                      ? RETRY_MAX_INTERVAL_MINS
                      : s_retry_interval * 2;
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
#ifdef DEBUG
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Outbox send OK");
#endif
  // Reset backoff so the next failure starts from 1-minute retry again.
  s_retry_interval  = 1;
  s_retry_countdown = 0;
}

// ─── App lifecycle ────────────────────────────────────────────────────────────

static void init(void) {
  // Daylight colour ramp, from deepest night through to full day:
  s_colors[0] = GColorBlack;           // deep night (>90 min outside daylight)
  s_colors[1] = GColorImperialPurple;  // night
  s_colors[2] = GColorPurple;          // twilight (within ~10% of sunrise/set)
  s_colors[3] = GColorChromeYellow;    // golden hour (warm orange, within ~20%)
  s_colors[4] = GColorVividCerulean;   // full daytime sky blue
  s_colors[5] = GColorCeleste;         // (reserved — bright midday, future use)

  // Load persisted animation-mode preference (default: all animations on).
  // Clamp to the valid range in case the stored value is corrupt or from an
  // incompatible future version.
  if (persist_exists(PERSIST_KEY_ANIM_MODE)) {
    int stored = persist_read_int(PERSIST_KEY_ANIM_MODE);
    s_anim_mode = (stored >= ANIM_MODE_ALL_ON && stored <= ANIM_MODE_ALL_OFF)
                  ? stored : ANIM_MODE_ALL_ON;
  }

  // Register AppMessage callbacks BEFORE opening the inbox.
  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);
  // Size AppMessage buffers to our actual payloads, not the SDK maximum.
  // Inbox: two int32 values (KEY_SUNRISE + KEY_SUNSET) = dict header (8B) + 2×
  //        tuple (7B each) ≈ 30B.  We round up to 64B for safety.
  // Outbox: a single uint8 "ping" ≈ 15B.  We use 32B.
  app_message_open(64, 32);

  // Create the main window.
  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorLavenderIndigo);
  window_set_window_handlers(s_main_window, (WindowHandlers){
    .load   = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_main_window, true /* animated */);

  // Display the time immediately (before the first tick fires).
  update_time();

  // Subscribe to minute-unit ticks for battery efficiency.
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  // Only unschedule/destroy if the animation is still running — if it completed
  // naturally the stopped handler already nulled the pointer and the SDK freed it.
  if (s_minute_hand_anim) {
    animation_unschedule(s_minute_hand_anim);
    animation_destroy(s_minute_hand_anim);
  }
  if (s_hour_hand_anim) {
    animation_unschedule(s_hour_hand_anim);
    animation_destroy(s_hour_hand_anim);
  }
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
