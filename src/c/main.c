#include <pebble.h>
#include "SphereLayer.h"
  
#define KEY_SUNRISE 0
#define KEY_SUNSET 1
#define STARS 250

static GRect window_bounds;
static GPoint window_center;

static Window *s_app_window;
static Layer *s_daylight_layer;
static Layer *s_minute_layer;
static Layer *s_hour_layer;
static Layer *s_pivot_layer;
static BitmapLayer *s_star_layer;


static int32_t minute_angle;
static Animation *s_minute_hand_animation;

static int32_t hour_angle;
static Animation *s_hour_hand_animation;

static int16_t minute_orbit_dist = 65;
static int16_t hour_orbit_dist = 40;

static int minute_radius = 8;
static int hour_radius = 8;
static int pivot_radius = 26;
static int second_radius = 50;

static int32_t sunrise_t = 6 * 60 * 60;
static int32_t sunset_t = 18 * 60 * 60;
static float daylight_t = 0.0f;

static bool requested = false;
static GColor colors[6];

static bool testMode = false;
static int currentHour = 0;
static int currentMin = 0;

static GPoint get_orbital_position(int angle, int dist, int size)
{
    GPoint orbit_pos = {
    .x = ((int16_t)(sin_lookup(angle) * (int32_t)dist / TRIG_MAX_RATIO) + window_center.x) - size,
    .y = ((int16_t)(-cos_lookup(angle) * (int32_t)dist / TRIG_MAX_RATIO) + window_center.y) - size,
  };
  
  return orbit_pos;
}

static void request_daylight()
{
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);

    dict_write_uint8(iter, 0, 0);

    app_message_outbox_send();  
}


static int anim_percentage(AnimationProgress dist_normalized, int max) {
  return (int)(float)(((float)dist_normalized / (float)ANIMATION_NORMALIZED_MAX) * (float)max);
}


void minute_anim_callback(Animation *anim, AnimationProgress dist_normalized)
{
  int step = anim_percentage(dist_normalized, 360);
  float t = (float)step / 360.0f;

  int anim_angle = (int)((float)TRIG_MAX_ANGLE * t);
  anim_angle += minute_angle;
  GPoint minute_hand = get_orbital_position(anim_angle, minute_orbit_dist, minute_radius);
  layer_set_frame((Layer *)s_minute_layer, GRect(minute_hand.x,minute_hand.y, minute_radius * 2, minute_radius * 2));
}

void hour_anim_callback(Animation *anim, AnimationProgress dist_normalized)
{
  int step = anim_percentage(dist_normalized, 720);
  float t = (float)step / 360.0f;

  int anim_angle = (int)((float)TRIG_MAX_ANGLE * t);
  anim_angle += hour_angle;
  GPoint hour_hand = get_orbital_position(anim_angle, hour_orbit_dist, hour_radius);
  layer_set_frame((Layer *)s_hour_layer, GRect(hour_hand.x,hour_hand.y, hour_radius * 2, hour_radius * 2));
}

static const PropertyAnimationImplementation minute_animimp = {
  .base = {
    .update = (AnimationUpdateImplementation) minute_anim_callback,
  },
};

static const PropertyAnimationImplementation hour_animimp = {
  .base = {
    .update = (AnimationUpdateImplementation) hour_anim_callback,
  },
};

static void animate_minute_hand()
{
  s_minute_hand_animation = (Animation*)property_animation_create(&minute_animimp, s_minute_layer, NULL, NULL);
  animation_set_duration((Animation*)s_minute_hand_animation, 4000);

  animation_set_curve((Animation*)s_minute_hand_animation, AnimationCurveEaseInOut);
  animation_schedule((Animation*)s_minute_hand_animation);  
}

static void animate_hour_hand()
{
  s_hour_hand_animation = (Animation*)property_animation_create(&hour_animimp, s_hour_layer, NULL, NULL);
  animation_set_duration((Animation*)s_hour_hand_animation, 4000);

  animation_set_curve((Animation*)s_hour_hand_animation, AnimationCurveEaseInOut);
  animation_schedule((Animation*)s_hour_hand_animation);  
}

static float animation_curve_value(float t)
{
  t *= 2.0f;
  t -=1.0f;
  float output = ((t * t * t * t * t * t * t) + 1.0f) / 2.0f;
  return output;
}

static void update_time()
{ 
  if (!requested)
      request_daylight();
  
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);
  
  if (testMode)
  {
    if ((currentHour == 0)&&(currentMin == 0))
    {
      currentHour = 17;
      currentMin = 0;
    }else{
      currentMin += 1;
      if (currentMin == 60)
      {
        currentHour += 1;
        currentMin = 0;
        if (currentHour == 24)
          currentHour = 0;
      }  
    }
  }else{
      currentHour = tick_time->tm_hour;
      currentMin = tick_time->tm_min;
  }

  int t = currentMin * 60;
  t += currentHour * 60 * 60;

  float a = (t - sunrise_t);
  float b = (sunset_t - sunrise_t);
  daylight_t =  animation_curve_value(a / b);
  int start = -100;
  int end = window_bounds.size.h;

  if ((daylight_t < 0.0f)||(daylight_t > 1.0f))
  {
      sphere_layer_change_color(s_pivot_layer, GColorWhite);
      sphere_layer_change_color(s_minute_layer, GColorWhite);
      sphere_layer_change_color(s_hour_layer, GColorWhite);  
  }else{
      sphere_layer_change_color(s_pivot_layer, GColorBlack);
      sphere_layer_change_color(s_minute_layer, GColorBlack);
      sphere_layer_change_color(s_hour_layer, GColorBlack);       
  }

  if ((daylight_t <= -0.5f)||(daylight_t >= 1.5f))
  {
      window_set_background_color(s_app_window, colors[0]);  
      layer_insert_below_sibling((Layer*)s_star_layer, s_daylight_layer); 
  }else if ((daylight_t <= -0.1f)||(daylight_t >= 1.1f))
  {
      window_set_background_color(s_app_window, colors[1]); 
      layer_insert_below_sibling((Layer*)s_star_layer, s_daylight_layer); 
  }else if ((daylight_t < 0.0f)||(daylight_t > 1.0f)){
      window_set_background_color(s_app_window, colors[2]); 
      layer_remove_from_parent((Layer*)s_star_layer); 
    }else if ((daylight_t < 0.1f)||(daylight_t > 0.9f)){  
      layer_remove_from_parent((Layer*)s_star_layer); 
      window_set_background_color(s_app_window, colors[3]); 
    }else if ((daylight_t < 0.15f)||(daylight_t > 0.85f)){  
      layer_remove_from_parent((Layer*)s_star_layer); 
      window_set_background_color(s_app_window, colors[4]); 
    }else{   
      layer_remove_from_parent((Layer*)s_star_layer); 
      window_set_background_color(s_app_window, colors[5]); 
    }


          APP_LOG(APP_LOG_LEVEL_INFO, "T  %i",  (int)(daylight_t * 1000.0f));

  
  GRect from_frame = layer_get_frame(s_daylight_layer);
  from_frame.origin.y = start + ((end - start) * daylight_t);
   layer_set_frame(s_daylight_layer, from_frame);  
  


  minute_angle = TRIG_MAX_ANGLE * currentMin / 60;
  GRect minute_frame = layer_get_frame(s_minute_layer);
  GPoint minute_hand = get_orbital_position(minute_angle, minute_orbit_dist, minute_radius);  

  minute_frame.origin = minute_hand;
  layer_set_frame(s_minute_layer, minute_frame); 

  hour_angle = (TRIG_MAX_ANGLE * currentHour / 12) + (minute_angle / 12);
  GRect hour_frame = layer_get_frame(s_hour_layer);
  GPoint hour_hand = get_orbital_position(hour_angle, hour_orbit_dist, hour_radius);

  hour_frame.origin = hour_hand;
  layer_set_frame(s_hour_layer, hour_frame);   
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) 
{



  update_time();
  
  if (!testMode)
  {
    animate_minute_hand();  
    animate_hour_hand();
  }
}




static void main_window_load(Window *window) {
  
  window_bounds = layer_get_frame(window_get_root_layer(window));
  window_center = grect_center_point(&window_bounds);
  
  s_star_layer = bitmap_layer_create(window_bounds);
  bitmap_layer_set_background_color (s_star_layer, GColorClear);
  bitmap_layer_set_compositing_mode(s_star_layer, GCompOpSet );
  bitmap_layer_set_bitmap(s_star_layer,   gbitmap_create_with_resource(RESOURCE_ID_IMAGE_STARS));

  s_daylight_layer = sphere_layer_create(GPoint((window_bounds.size.w / 2),(window_bounds.size.h / 2)), GColorWhite, second_radius);
  layer_add_child(window_get_root_layer(window), s_daylight_layer);  

  s_pivot_layer = sphere_layer_create(GPoint((window_bounds.size.w / 2), (window_bounds.size.h / 2)), GColorBlack, pivot_radius);
  layer_add_child(window_get_root_layer(window), s_pivot_layer);  
  
  s_minute_layer = sphere_layer_create(GPoint((window_bounds.size.w / 2), (window_bounds.size.h / 2)- minute_orbit_dist) , GColorBlack, minute_radius);
  layer_add_child(window_get_root_layer(window), s_minute_layer);  
  
  s_hour_layer = sphere_layer_create(GPoint((window_bounds.size.w / 2), (window_bounds.size.h / 2)+ hour_orbit_dist) , GColorBlack, hour_radius);
  layer_add_child(window_get_root_layer(window), s_hour_layer);   
  

  
}

static void main_window_unload(Window *window) {
  layer_destroy(s_daylight_layer);
  layer_destroy(s_pivot_layer);
  layer_destroy(s_minute_layer);  
  layer_destroy(s_hour_layer);    
}


static void inbox_received_callback(DictionaryIterator *iterator, void *context) {

  // Read first item
  Tuple *t = dict_read_first(iterator);
  
  if (t == NULL)
    APP_LOG(APP_LOG_LEVEL_DEBUG, "NO DATA");

  // For all items
  while(t != NULL) {
    // Which key was received?

    switch(t->key) {
    case KEY_SUNRISE:{
        sunrise_t = t->value->int32;
        APP_LOG(APP_LOG_LEVEL_INFO, "KEY_SUNRISE  %li",  sunrise_t);
    }break;
      case KEY_SUNSET:{
         sunset_t = t->value->int32;
        APP_LOG(APP_LOG_LEVEL_INFO, "KEY_SUNSET  %li",  sunset_t);
        requested = true;
        update_time();
    }break;      
    default:
 
      APP_LOG(APP_LOG_LEVEL_ERROR, "Key %d not recognized!", (int)t->key);
      break;
    }

    // Look for next item
    t = dict_read_next(iterator);
  }
 
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped!");
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed! %d", reason);
  request_daylight();
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
}


void handle_init(void) {

  colors[0] = GColorBlack;
  colors[1] = GColorImperialPurple;
  colors[2] = GColorPurple;
  colors[3] = GColorFashionMagenta;
  colors[4] = GColorChromeYellow;
  colors[5] = GColorYellow;
  
  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);
  
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
  
  s_app_window = window_create();
  window_set_background_color(s_app_window, GColorLavenderIndigo );
  
    window_set_window_handlers(s_app_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });

  window_stack_push(s_app_window, true);

  update_time();
         
  
  
  tick_timer_service_subscribe((testMode)?SECOND_UNIT:MINUTE_UNIT, tick_handler);
}

void handle_deinit(void) {

  window_destroy(s_app_window);
}

int main(void) {
  handle_init();
  app_event_loop();
  handle_deinit();
}
