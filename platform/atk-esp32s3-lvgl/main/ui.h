#ifndef _SQUARELINE_PROJECT_UI_H
#define _SQUARELINE_PROJECT_UI_H

#ifdef __cplusplus
extern "C" {
#endif


#include "lvgl.h"

#include "ui_helpers.h"
#include "ui_events.h"




#include "screens/ui_Screen1.h"

extern lv_obj_t * ui____initial_actions0;


LV_IMG_DECLARE(ui_img_logo_png);   


void ui_init(void);
void ui_destroy(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
