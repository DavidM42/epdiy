/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen.
 *
 * Write an image into a header file using a 3...2...1...0 format per pixel,
 * for 4 bits color (16 colors - well, greys.) MSB first.  At 80 MHz, screen
 * clears execute in 1.075 seconds and images are drawn in 1.531 seconds.
 */

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_types.h"

#include "EPD.h"
#include "firasans.h"
#include "font.h"
#include "image.h"
#include "shells.h"

/* Display State Machine */
enum ScreenState {
  CLEAR_SCREEN = 0,
  DRAW_SCREEN = 1,
  CLEAR_PARTIAL = 2,
  DRAW_FONTS = 3,
};


uint8_t *img_buf;

uint8_t *framebuffer;
uint8_t *original_image_ram;

void delay(uint32_t millis) { vTaskDelay(millis / portTICK_PERIOD_MS); }

uint32_t millis() { return esp_timer_get_time() / 1000; }

void loop() {
  // Variables to set one time.
  static enum ScreenState _state = CLEAR_SCREEN;

  delay(300);
  epd_poweron();

  uint32_t timestamp = 0;
  if (_state == CLEAR_SCREEN) {
    printf("Clear cycle.\n");
    timestamp = millis();
    epd_clear();

    int cursor_x = 100;
    int cursor_y = 100;
    unsigned char *string = (unsigned char *)"Hello World! *g*";
    writeln((GFXfont *)&FiraSans, string, &cursor_x, &cursor_y, framebuffer);
    cursor_y += FiraSans.advance_y;
    cursor_x = 850;
    cursor_y = 100;
    string = (unsigned char *)"Hello äöüßabcd/#{🚀";
    writeln((GFXfont *)&FiraSans, string, &cursor_x, &cursor_y, framebuffer);

    epd_draw_hline(50, 20, 1100, 0x00, framebuffer);
    epd_draw_hline(50, 800, 1100, 0x00, framebuffer);
    epd_draw_vline(50, 20, 781, 0x00, framebuffer);
    epd_draw_vline(1150, 20, 781, 0x00, framebuffer);

    _state = DRAW_FONTS;

  } else if (_state == DRAW_FONTS) {
    printf("Squares cycle.\n");
    Rect_t area = {
        .x = -100,
        .y = -100,
        .width = shells_width,
        .height = shells_height,
    };
    //epd_copy_to_framebuffer(area, (uint8_t*)shells_data, framebuffer);
    epd_draw_grayscale_image(area, (uint8_t*)shells_data);

    timestamp = millis();
    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    int cursor_x = 100;
    int cursor_y = 600;
    unsigned char* string = (unsigned char *)"Hello äöüßabcd/#{🚀";
    writeln((GFXfont *)&FiraSans, string, &cursor_x, &cursor_y, NULL);
    _state = DRAW_SCREEN;

  } else if (_state == DRAW_SCREEN) {
    printf("Draw cycle.\n");
    timestamp = millis();
    Rect_t area = {
        .x = 100,
        .y = 100,
        .width = shells_width,
        .height = shells_height,
    };
    epd_draw_grayscale_image(area, (uint8_t*)shells_data);
    //draw_image_unary_coded(epd_full_screen(), img_buf);
    _state = CLEAR_PARTIAL;

  } else if (_state == CLEAR_PARTIAL) {
    printf("Partial clear cycle.\n");
    timestamp = millis();
    Rect_t area = {
        .x = 100,
        .y = 100,
        .width = 1200 - 200,
        .height = 825 - 200,
    };
    epd_clear_area(area);
    _state = CLEAR_SCREEN;
  }
  timestamp = millis() - timestamp;
  epd_poweroff();
  // Print out the benchmark
  printf("Took %d ms to redraw the screen.\n", timestamp);

  // Wait 4 seconds then do it again
  delay(2000);
  printf("Going active again.\n");
}

void epd_task() {
  epd_init();

  ESP_LOGW("main", "allocating...\n");

  original_image_ram =
      (uint8_t *)heap_caps_malloc(1200 * 825 / 2, MALLOC_CAP_SPIRAM);
  framebuffer =
      (uint8_t *)heap_caps_malloc(1200 * 825 / 2, MALLOC_CAP_SPIRAM);
  memset(framebuffer, 0xFF, 1200 * 825 / 2);

  volatile uint32_t t = millis();
  memcpy(original_image_ram, img_bytes, 1200 * 825 / 2);
  volatile uint32_t t2 = millis();
  printf("original copy to PSRAM took %dms.\n", t2 - t);

  //img_buf = (uint8_t *)heap_caps_malloc(1200 * 825 * 2, MALLOC_CAP_SPIRAM);

  //t = millis();
  //img_8bit_to_unary_image(img_buf, original_image_ram, 1200, 825);
  //t2 = millis();
  //printf("converting took %dms.\n", t2 - t);

  while (1) {
    loop();
  };
}

void app_main() {
  ESP_LOGW("main", "Hello World!\n");

  heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
  heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);

  xTaskCreatePinnedToCore(&epd_task, "epd task", 10000, NULL, 2, NULL, 1);
}