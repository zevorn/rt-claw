#ifndef __UI_CONTROL_H__
#define __UI_CONTROL_H__

#include <stdbool.h>
#include <stdint.h>

#define UI_TEXT_LENGTH 1024

typedef enum {
    UI_OBJ_TEXTAREA2,
    UI_OBJ_TEXTAREA3,
    UI_OBJ_CONTAINER1,
    UI_OBJ_CONTAINER2,
    UI_OBJ_IMAGE1,
} ui_obj_id_t;


typedef enum {
    UI_CMD_SET_TEXT,
    UI_CMD_SET_FONT_SIZE,
    UI_CMD_SET_TEXT_COLOR,
    UI_CMD_SET_VISIBLE,
    UI_CMD_SET_ENABLE,
} ui_cmd_t;


typedef struct {
    ui_cmd_t cmd;
    ui_obj_id_t obj_id;
    union {
        char     text[UI_TEXT_LENGTH];
        uint16_t font_size;
        uint32_t color;      
        bool     visible;
        bool     enable;
    } param;
} ui_msg_t;


void ui_controller_init(void);


bool ui_send_command(ui_msg_t *msg);
bool ui_set_status_text(const char *text);
bool ui_set_reply_text(const char *text);

#endif