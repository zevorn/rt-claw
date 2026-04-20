#include "ui_control.h"
#include "ui.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"



#define TEXT_REFRESH_MS  1200     /* 1.2秒切换下一段 */
static char  s_full_text[UI_TEXT_LENGTH];   
static int   s_text_offset = 0;   
static lv_timer_t *s_text_timer = NULL;  


static int first_run = 1; 


static QueueHandle_t s_ui_queue = NULL;


static lv_obj_t *get_ui_object(ui_obj_id_t id)
{
    switch(id) {
        case UI_OBJ_TEXTAREA2:  return ui_TextArea2;
        case UI_OBJ_TEXTAREA3:  return ui_TextArea3;
        case UI_OBJ_CONTAINER1: return ui_Container1;
        case UI_OBJ_CONTAINER2: return ui_Container2;
        case UI_OBJ_IMAGE1:     return ui_Image1;
        default:                return NULL;
    }
}

static void ui_text_scroll_timer(lv_timer_t *timer)
{
    static int scroll_y = 0;
    static int need_change = 0;
    int total_len = strlen(s_full_text);

    lv_obj_set_scroll_dir(ui_TextArea3, LV_DIR_VER);
    lv_obj_scroll_to_y(ui_TextArea3, 0, LV_ANIM_OFF);


    if (s_text_offset >= total_len) {
    lv_obj_scroll_to_y(ui_TextArea3, scroll_y, LV_ANIM_OFF);
    scroll_y += 1;
    
    if (scroll_y > 120) {
        lv_timer_del(s_text_timer);
        s_text_timer = NULL;
        scroll_y = 0;
        first_run = 1;
        need_change = 0;
        lv_textarea_set_text(ui_TextArea3, "                              「·～·」          ");
        lv_obj_scroll_to_y(ui_TextArea3, 0, LV_ANIM_OFF);
        lv_obj_invalidate(ui_TextArea3);
    }
    return;
}


    if (first_run) {
        lv_obj_scroll_to_y(ui_TextArea3, scroll_y, LV_ANIM_OFF);
        scroll_y += 1;

        if (scroll_y > 120) {
            first_run = 0;
            scroll_y = 0;
            need_change = 1;
        }
        return;
    }

    if (need_change) {
        while (s_text_offset < total_len) {
            char c = s_full_text[s_text_offset];
            if (c == '\n' || c == '\r' || c == ' ') s_text_offset++;
            else break;
        }

        int end = s_text_offset;
        while (end < total_len && s_full_text[end] != '\n' && s_full_text[end] != '\r') end++;

        int line_len = end - s_text_offset;
        char line[512] = {0};
        if (line_len > 0) strncpy(line, s_full_text + s_text_offset, line_len);

        lv_textarea_set_text(ui_TextArea3, line);
        lv_obj_scroll_to_y(ui_TextArea3, 0, LV_ANIM_OFF);
        scroll_y = 0;
        s_text_offset = end;
        need_change = 0;
        return;
    }

    lv_obj_scroll_to_y(ui_TextArea3, scroll_y, LV_ANIM_OFF);
    scroll_y += 1;
    if (scroll_y > 120) {
        need_change = 1;
    }
}



static void ui_execute_command(const ui_msg_t *msg)
{
    if (!msg) return;
    lv_obj_t *obj = get_ui_object(msg->obj_id);
    if (!obj) return;

    switch (msg->cmd) {
        case UI_CMD_SET_TEXT: {
            if (lv_obj_check_type(obj, &lv_textarea_class)) {
                if (msg->obj_id == UI_OBJ_TEXTAREA3) {
                    if (s_text_timer) {
                        lv_timer_del(s_text_timer);
                        s_text_timer = NULL;
                    }

                    memset(s_full_text, 0, sizeof(s_full_text));
                    strncpy(s_full_text, msg->param.text, sizeof(s_full_text)-1);
                    s_text_offset = 0;
                    first_run = 1;

                    int total_len = strlen(s_full_text);
                    while (s_text_offset < total_len) {
                        char c = s_full_text[s_text_offset];
                        if (c == '\n' || c == '\r' || c == ' ') s_text_offset++;
                        else break;
                    }
                    int end = s_text_offset;
                    while (end < total_len && s_full_text[end] != '\n' && s_full_text[end] != '\r') end++;

                    char first_line[512] = {0};
                    int line_len = end - s_text_offset;
                    if (line_len > 0) {
                        line_len = LV_CLAMP(line_len, 0, sizeof(first_line) - 1);
                        strncpy(first_line, s_full_text + s_text_offset, line_len);
                    }

                    lv_textarea_set_text(obj, "");          
                    lv_obj_scroll_to_y(obj, 0, LV_ANIM_OFF); 
                    lv_textarea_set_text(obj, first_line);   
                    lv_obj_invalidate(obj);
                    s_text_offset = end;

                    s_text_timer = lv_timer_create(ui_text_scroll_timer, 30, NULL);
                } else {
                    lv_textarea_set_text(obj, msg->param.text);
                }
            }
            break;
        }

        case UI_CMD_SET_FONT_SIZE:
            lv_obj_set_style_text_font(obj, LV_FONT_DEFAULT, LV_PART_MAIN | LV_STATE_DEFAULT);
            break;

        case UI_CMD_SET_TEXT_COLOR:
            lv_obj_set_style_text_color(obj, lv_color_hex(msg->param.color), 0);
            break;

        case UI_CMD_SET_VISIBLE:
            if (msg->param.visible) {
                lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            }
            break;

         case UI_CMD_SET_ENABLE:
            if (msg->param.enable)
                lv_obj_clear_state(obj, LV_STATE_DISABLED);
            else
                lv_obj_add_state(obj, LV_STATE_DISABLED);
            break;
    }
}


static void ui_control_task(void *arg)
{
    ui_msg_t msg;
    while (1) {
        if (xQueueReceive(s_ui_queue, &msg, portMAX_DELAY) == pdPASS) {
            ui_execute_command(&msg);
        }
    }
}


void ui_controller_init(void)
{
    s_ui_queue = xQueueCreate(10, sizeof(ui_msg_t));
    xTaskCreate(ui_control_task, "ui_ctrl", 4096, NULL, 10, NULL);
}

bool ui_send_command(ui_msg_t *msg)
{
    if (!s_ui_queue || !msg) return false;
    return xQueueSend(s_ui_queue, msg, 0) == pdPASS;
}


bool ui_set_status_text(const char *text)
{
    if (!text) return false;

    ui_msg_t msg = {0};
    msg.cmd = UI_CMD_SET_TEXT;
    msg.obj_id = UI_OBJ_TEXTAREA2;  
    snprintf(msg.param.text, sizeof(msg.param.text), "%s", text);

    return ui_send_command(&msg);
}

bool ui_set_reply_text(const char *text)
{
    if (!text) return false;

    ui_msg_t msg = {0};
    msg.cmd = UI_CMD_SET_TEXT;
    msg.obj_id = UI_OBJ_TEXTAREA3;  
    snprintf(msg.param.text, sizeof(msg.param.text), "%s", text);

    return ui_send_command(&msg);
}