#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _objects_t {
    lv_obj_t *lock_screen;
    lv_obj_t *main;
    lv_obj_t *digit1;
    lv_obj_t *digit6;
    lv_obj_t *digit5;
    lv_obj_t *digit4;
    lv_obj_t *digit3;
    lv_obj_t *digit2;
    lv_obj_t *arc_encoder;
    lv_obj_t *selected_number;
    lv_obj_t *status_message;
    lv_obj_t *obj0;
    lv_obj_t *obj1;
    lv_obj_t *obj2;
    lv_obj_t *obj3;
    lv_obj_t *obj4;
    lv_obj_t *obj5;
    lv_obj_t *obj6;
    lv_obj_t *obj7;
    lv_obj_t *obj8;
    lv_obj_t *obj9;
    lv_obj_t *obj10;
    lv_obj_t *obj11;
    lv_obj_t *eco;
    lv_obj_t *obj12;
    lv_obj_t *normal;
    lv_obj_t *obj13;
    lv_obj_t *ultra;
    lv_obj_t *obj14;
    lv_obj_t *demo;
    lv_obj_t *obj15;
    lv_obj_t *sport;
    lv_obj_t *obj16;
    lv_obj_t *arc_fan;
    lv_obj_t *fan_label;
    lv_obj_t *arc_temp;
    lv_obj_t *temp_label;
    lv_obj_t *obj17;
    lv_obj_t *obj18;
    lv_obj_t *obj19;
    lv_obj_t *on_time;
    lv_obj_t *bat_status;
    lv_obj_t *obj20;
    lv_obj_t *obj21;
    lv_obj_t *bat_percent;
} objects_t;

extern objects_t objects;

enum ScreensEnum {
    SCREEN_ID_LOCK_SCREEN = 1,
    SCREEN_ID_MAIN = 2,
};

void create_screen_lock_screen();
void delete_screen_lock_screen();
void tick_screen_lock_screen();

void create_screen_main();
void delete_screen_main();
void tick_screen_main();

void create_screen_by_id(enum ScreensEnum screenId);
void delete_screen_by_id(enum ScreensEnum screenId);
void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();


#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/