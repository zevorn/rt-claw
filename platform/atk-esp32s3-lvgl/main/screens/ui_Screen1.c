#include "../ui.h"

LV_FONT_DECLARE(lv_siyuan_16);

lv_obj_t * ui_Screen1 = NULL;
lv_obj_t * ui_Container1 = NULL;
lv_obj_t * ui_Image1 = NULL;
lv_obj_t * ui_Container2 = NULL;
lv_obj_t * ui_TextArea2 = NULL;
lv_obj_t * ui_TextArea3 = NULL;


void ui_Screen1_screen_init(void)
{
    ui_Screen1 = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_Screen1, LV_OBJ_FLAG_SCROLLABLE);      

    ui_Container1 = lv_obj_create(ui_Screen1);
    lv_obj_remove_style_all(ui_Container1);
    lv_obj_set_width(ui_Container1, 558);
    lv_obj_set_height(ui_Container1, 432);
    lv_obj_set_x(ui_Container1, -9);
    lv_obj_set_y(ui_Container1, -21);
    lv_obj_set_align(ui_Container1, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_Container1, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);      
    lv_obj_set_style_bg_color(ui_Container1, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Container1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_Image1 = lv_img_create(ui_Container1);
    lv_img_set_src(ui_Image1, &ui_img_logo_png);
    lv_obj_set_width(ui_Image1, LV_SIZE_CONTENT);   
    lv_obj_set_height(ui_Image1, LV_SIZE_CONTENT);    
    lv_obj_set_x(ui_Image1, 5);
    lv_obj_set_y(ui_Image1, -12);
    lv_obj_set_align(ui_Image1, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_Image1, LV_OBJ_FLAG_ADV_HITTEST);     
    lv_obj_clear_flag(ui_Image1, LV_OBJ_FLAG_SCROLLABLE);      
    lv_img_set_zoom(ui_Image1, 240);

    ui_Container2 = lv_obj_create(ui_Screen1);
    lv_obj_remove_style_all(ui_Container2);
    lv_obj_set_width(ui_Container2, 506);
    lv_obj_set_height(ui_Container2, 32);
    lv_obj_set_x(ui_Container2, 2);
    lv_obj_set_y(ui_Container2, -103);
    lv_obj_set_align(ui_Container2, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_Container2, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);     
    lv_obj_set_style_bg_color(ui_Container2, lv_color_hex(0xC5A62B), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Container2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_TextArea2 = lv_textarea_create(ui_Container2);
    lv_obj_set_width(ui_TextArea2, 220);
    lv_obj_set_height(ui_TextArea2, 56);
    lv_obj_set_x(ui_TextArea2, -4);
    lv_obj_set_y(ui_TextArea2, 9);
    lv_obj_set_align(ui_TextArea2, LV_ALIGN_CENTER);
    lv_textarea_set_text(ui_TextArea2, "welcome to use rt-claw");
    lv_textarea_set_placeholder_text(ui_TextArea2, "Placeholder...");
    lv_obj_clear_flag(ui_TextArea2, LV_OBJ_FLAG_SCROLLABLE);     
    lv_obj_set_style_text_color(ui_TextArea2, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_TextArea2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_TextArea2, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_TextArea2, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_TextArea2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_TextArea2, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_TextArea2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_TextArea3 = lv_textarea_create(ui_Screen1);
    lv_obj_set_style_text_font(ui_TextArea3, &lv_siyuan_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_width(ui_TextArea3, 308);
    lv_obj_set_height(ui_TextArea3, 88);
    lv_obj_set_x(ui_TextArea3, 3);
    lv_obj_set_y(ui_TextArea3, 61);
    lv_obj_set_align(ui_TextArea3, LV_ALIGN_CENTER);

    lv_textarea_set_one_line(ui_TextArea3, false);  

    lv_textarea_set_text(ui_TextArea3, "                              「·～·」          ");
    lv_textarea_set_placeholder_text(ui_TextArea3, "Placeholder...");

    
}

void ui_Screen1_screen_destroy(void)
{
    if(ui_Screen1) lv_obj_del(ui_Screen1);

    ui_Screen1 = NULL;
    ui_Container1 = NULL;
    ui_Image1 = NULL;
    ui_Container2 = NULL;
    ui_TextArea2 = NULL;
    ui_TextArea3 = NULL;

}
