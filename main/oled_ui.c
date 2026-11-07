#include "oled_ui.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SDA_PIN GPIO_NUM_8
#define SCL_PIN GPIO_NUM_9
#define OLED_ADDR 0x3C /* Try 0x3D if needed */
#define W 128
#define H 64
#define COL_OFFSET 2 /* SH1106 RAM column offset */

static const char *TAG = "oled_ui";
static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t dev;
static uint8_t fb[W * 8];
static bool ready;

typedef struct { char c; uint8_t b[5]; } glyph_t;
/* Compact 5x7 font for the characters used by this UI. */
static const glyph_t font[] = {
{' ',{0,0,0,0,0}}, {':',{0,0x36,0x36,0,0}}, {'-',{8,8,8,8,8}},
{'*',{0x14,8,0x3e,8,0x14}}, {'.',{0,0x60,0x60,0,0}}, {'/',{0x20,0x10,8,4,2}},
{'0',{0x3e,0x51,0x49,0x45,0x3e}}, {'1',{0,0x42,0x7f,0x40,0}},
{'2',{0x42,0x61,0x51,0x49,0x46}}, {'3',{0x21,0x41,0x45,0x4b,0x31}},
{'4',{0x18,0x14,0x12,0x7f,0x10}}, {'5',{0x27,0x45,0x45,0x45,0x39}},
{'6',{0x3c,0x4a,0x49,0x49,0x30}}, {'7',{1,0x71,9,5,3}},
{'8',{0x36,0x49,0x49,0x49,0x36}}, {'9',{6,0x49,0x49,0x29,0x1e}},
{'A',{0x7e,0x11,0x11,0x11,0x7e}}, {'B',{0x7f,0x49,0x49,0x49,0x36}},
{'C',{0x3e,0x41,0x41,0x41,0x22}}, {'D',{0x7f,0x41,0x41,0x22,0x1c}},
{'E',{0x7f,0x49,0x49,0x49,0x41}}, {'F',{0x7f,9,9,9,1}},
{'G',{0x3e,0x41,0x49,0x49,0x7a}}, {'H',{0x7f,8,8,8,0x7f}},
{'I',{0,0x41,0x7f,0x41,0}}, {'J',{0x20,0x40,0x41,0x3f,1}},
{'K',{0x7f,8,0x14,0x22,0x41}}, {'L',{0x7f,0x40,0x40,0x40,0x40}},
{'M',{0x7f,2,0x0c,2,0x7f}}, {'N',{0x7f,4,8,0x10,0x7f}},
{'O',{0x3e,0x41,0x41,0x41,0x3e}}, {'P',{0x7f,9,9,9,6}},
{'Q',{0x3e,0x41,0x51,0x21,0x5e}}, {'R',{0x7f,9,0x19,0x29,0x46}},
{'S',{0x46,0x49,0x49,0x49,0x31}}, {'T',{1,1,0x7f,1,1}},
{'U',{0x3f,0x40,0x40,0x40,0x3f}}, {'V',{0x1f,0x20,0x40,0x20,0x1f}},
{'W',{0x3f,0x40,0x38,0x40,0x3f}}, {'X',{0x63,0x14,8,0x14,0x63}},
{'Y',{7,8,0x70,8,7}}, {'Z',{0x61,0x51,0x49,0x45,0x43}},
};
static const glyph_t *glyph(char c) {
    c = (char)toupper((unsigned char)c);
    for (unsigned i=0; i<sizeof(font)/sizeof(font[0]); i++) if(font[i].c==c) return &font[i];
    return &font[0];
}
static esp_err_t send_bytes(const uint8_t *p, size_t n) { return i2c_master_transmit(dev,p,n,100); }
static esp_err_t cmd(uint8_t c) { uint8_t p[2]={0x00,c}; return send_bytes(p,2); }
static void pixel(int x,int y) { if(x>=0&&x<W&&y>=0&&y<H) fb[(y/8)*W+x] |= (uint8_t)(1U<<(y&7)); }
static void text(int x,int y,const char *s) {
    while(s && *s && x<W) { const glyph_t *g=glyph(*s++); for(int a=0;a<5;a++) for(int b=0;b<7;b++) if(g->b[a]&(1U<<b)) pixel(x+a,y+b); x+=6; }
}
static void line(int y) { for(int x=0;x<W;x++) pixel(x,y); }
static esp_err_t flush(void) {
    uint8_t p[W+1];
    for(int page=0;page<8;page++) {
        if(cmd((uint8_t)(0xB0|page))!=ESP_OK || cmd((uint8_t)(COL_OFFSET&0x0f))!=ESP_OK ||
           cmd((uint8_t)(0x10|(COL_OFFSET>>4)) )!=ESP_OK) return ESP_FAIL;
        p[0]=0x40; memcpy(p+1,fb+page*W,W);
        esp_err_t e=send_bytes(p,sizeof(p)); if(e!=ESP_OK) return e;
    }
    return ESP_OK;
}
static void header(const char *title) { memset(fb,0,sizeof(fb)); text(14,1,"REC DIAL"); line(11); text(0,16,title); }
static void name_only(const char *filename,int y) {
    if(!filename) return;
    const char *b=strrchr(filename,'/'); b=b?b+1:filename;
    char t[22]; snprintf(t,sizeof(t),"%.20s",b); text(0,y,t);
}
esp_err_t oled_ui_init(void) {
    i2c_master_bus_config_t bc={.i2c_port=I2C_NUM_0,.sda_io_num=SDA_PIN,.scl_io_num=SCL_PIN,
      .clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=true};
    esp_err_t e=i2c_new_master_bus(&bc,&bus); if(e!=ESP_OK) return e;
    i2c_device_config_t dc={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=OLED_ADDR,.scl_speed_hz=400000};
    e=i2c_master_bus_add_device(bus,&dc,&dev); if(e!=ESP_OK) return e;
    vTaskDelay(pdMS_TO_TICKS(50));
    const uint8_t init[]={0xAE,0xD5,0x80,0xA8,0x3F,0xD3,0,0x40,0xAD,0x8B,0xA1,0xC8,0xDA,0x12,
      0x81,0x7F,0xD9,0x22,0xDB,0x35,0xA4,0xA6,0xAF};
    for(unsigned i=0;i<sizeof(init);i++) { e=cmd(init[i]); if(e!=ESP_OK) return e; }
    ready=true; oled_ui_show_ready(); ESP_LOGI(TAG,"OLED initialized at 0x%02X",OLED_ADDR); return ESP_OK;
}
void oled_ui_show_ready(void) { if(!ready)return; header("STATUS: READY"); text(0,29,"FLASH STORAGE"); text(0,43,"PRESS REC TO START"); (void)flush(); }
void oled_ui_show_recording(uint32_t sec,const char *filename,uint8_t level) {
    if (!ready) {
        return;
    }

    char t[24];
    snprintf(t, sizeof(t), "TIME %02lu:%02lu",
             (unsigned long)(sec / 60), (unsigned long)(sec % 60));

    header("* RECORDING *");
    text(0, 27, t);
    name_only(filename, 36);
    text(0, 47, "LEVEL");

    if (level > 10) {
        level = 10;
    }
    for (int i = 0; i < 10; i++) {
        for (int y = 0; y < (i < level ? 12 : 3); y++) {
            for (int x = 0; x < 4; x++) {
                pixel(38 + i * 8 + x, 61 - y);
            }
        }
    }

    (void)flush();
}
void oled_ui_show_stopped(const char *filename) { if(!ready)return; header("SAVED"); name_only(filename,30); text(0,47,"READY FOR NEXT"); (void)flush(); }
void oled_ui_show_error(const char *message) {
    if (!ready) {
        return;
    }

    header("ERROR");
    if (message) {
        char a[21] = {0};
        char b[21] = {0};
        size_t n = strlen(message);
        size_t n1 = n > 20 ? 20 : n;
        memcpy(a, message, n1);
        if (n > 20) {
            memcpy(b, message + 20, (n - 20) > 20 ? 20 : n - 20);
        }
        text(0, 29, a);
        text(0, 39, b);
    }
    text(0, 53, "CHECK HARDWARE");
    (void)flush();
}
