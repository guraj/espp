#pragma once

#include "display_drivers.hpp"

namespace espp {
  /**
   * @brief Display driver for the ILI9341 display controller.
   *
   *   This code is modified from
   *   https://github.com/lvgl/lvgl_esp32_drivers/blob/master/lvgl_tft/ili9341.c
   *   and
   *   https://github.com/espressif/esp-dev-kits/blob/master/esp32-s2-hmi-devkit-1/components/screen/controller_driver/ili9341/ili9341.c
   *
   *   See also:
   *   https://github.com/espressif/esp-bsp/blob/master/components/lcd/esp_lcd_ili9341/esp_lcd_ili9341.c
   *
   * \section ili9341_ex1 ili9341 Example
   * \snippet display_drivers_example.cpp ili9341 example
   */
  class Ili9341 {
  public:
    enum class Command : uint8_t {
      invoff = 0x20, // display inversion off
      invon = 0x21,  // display inversion on
      gamset = 0x26, // gamma set
      dispoff = 0x28,// display off
      dispon = 0x29, // display on
      caset = 0x2a,  // column address set
      raset = 0x2b,  // row address set
      ramwr = 0x2c,  // ram write
      rgbset = 0x2d, // color setting for 4096, 64k and 262k colors
      ramrd = 0x2e,  // ram read
    };
    /**
     * @brief Store the config data and send the initialization commands to the
     *        display controller.
     * @param config display_drivers::Config structure
     */
    static void initialize(const display_drivers::Config& config) {
      // update the static members
      lcd_write_ = config.lcd_write;
      reset_pin_ = config.reset_pin;
      dc_pin_ = config.data_command_pin;
      backlight_pin_ = config.backlight_pin;
      offset_x_ = config.offset_x;
      offset_y_ = config.offset_y;

      // Initialize display pins
      display_drivers::init_pins(reset_pin_, dc_pin_, backlight_pin_, config.backlight_on_value);

      // init the display
      display_drivers::LcdInitCmd ili_init_cmds[]={
        {0xCF, {0x00, 0x83, 0X30}, 3},
        {0xED, {0x64, 0x03, 0X12, 0X81}, 4},
        {0xE8, {0x85, 0x01, 0x79}, 3},
        {0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02}, 5},
        {0xF7, {0x20}, 1},
        {0xEA, {0x00, 0x00}, 2},
        {0xC0, {0x26}, 1},
        {0xC1, {0x11}, 1},
        {0xC5, {0x35, 0x3E}, 2},
        {0xC7, {0xBE}, 1},
        {0x36, {0x28}, 1},
        {0x3A, {0x55}, 1},
        {0xB1, {0x00, 0x1B}, 2},
        {0xF2, {0x08}, 1},
        {0x26, {0x01}, 1},
        {0xE0, {0x1F, 0x1A, 0x18, 0x0A, 0x0F, 0x06, 0x45, 0X87, 0x32, 0x0A, 0x07, 0x02, 0x07, 0x05, 0x00}, 15},
        {0XE1, {0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3A, 0x78, 0x4D, 0x05, 0x18, 0x0D, 0x38, 0x3A, 0x1F}, 15},
        {0x2A, {0x00, 0x00, 0x00, 0xEF}, 4},
        {0x2B, {0x00, 0x00, 0x01, 0x3f}, 4},
        {0x2C, {0}, 0},
        {0xB7, {0x07}, 1},
        {0xB6, {0x0A, 0x82, 0x27, 0x00}, 4},
        {0x11, {0}, 0x80},
        {0x29, {0}, 0x80},
        {0, {0}, 0xff},
      };

      // send the init commands
      send_commands(ili_init_cmds);

      // configure the display color configuration
      if (config.invert_colors) {
        send_command(0x21);
      } else {
        send_command(0x20);
      }
    }

    /**
     * @brief Flush the pixel data for the provided area to the display.
     * @param *drv Pointer to the LVGL display driver.
     * @param *area Pointer to the structure describing the pixel area.
     * @param *color_map Pointer to array of colors to flush to the display.
     */
    static void flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
      fill(drv, area, color_map, (uint32_t)Display::Signal::FLUSH);
    }

    /**
     * @brief Flush the pixel data for the provided area to the display.
     * @param *drv Pointer to the LVGL display driver.
     * @param *area Pointer to the structure describing the pixel area.
     * @param *color_map Pointer to array of colors to flush to the display.
     */
    static void fill(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map, uint32_t flags=(uint32_t)Display::Signal::NONE) {
      uint8_t data[4];

      uint16_t start_x = area->x1 + offset_x_;
      uint16_t end_x = area->x2 + offset_x_;
      uint16_t start_y = area->y1 + offset_y_;
      uint16_t end_y = area->y2 + offset_y_;

      // Set start and end column addresses
      send_command((uint8_t)Command::caset);
      data[0] = (start_x >> 8) & 0xFF;
      data[1] = start_x & 0xFF;
      data[2] = (end_x >> 8) & 0xFF;
      data[3] = end_x & 0xFF;
      send_data(data, 4);

      // Set start and end row addresses
      send_command((uint8_t)Command::raset);
      data[0] = (start_y >> 8) & 0xFF;
      data[1] = start_y & 0xFF;
      data[2] = (end_y >> 8) & 0xFF;
      data[3] = end_y & 0xFF;
      send_data(data, 4);

      // Write the color data to the configured section of controller memory
      send_command((uint8_t)Command::ramwr);
      uint32_t size = lv_area_get_width(area) * lv_area_get_height(area);
      send_data((uint8_t*)color_map, size * 2, flags);
    }

    /**
     * @param Clear the display area, filling it with the provided color.
     * @param x X coordinate of the upper left corner of the display area.
     * @param y Y coordinate of the upper left corner of the display area.
     * @param width Width of the display area to clear.
     * @param height Height of the display area to clear.
     * @param color 16 bit color (default 0x0000) to fill with.
     */
    static void clear(size_t x, size_t y, size_t width, size_t height, uint16_t color=0x0000) {
      uint8_t data[4] = {0};

      uint16_t start_x = x + offset_x_;
      uint16_t end_x = (x+width) + offset_x_;
      uint16_t start_y = y + offset_y_;
      uint16_t end_y = (y+width) + offset_y_;

      // Set the column (x) start / end addresses
      send_command((uint8_t)Command::caset);
      data[0] = (start_x >> 8) & 0xFF;
      data[1] = start_x & 0xFF;
      data[2] = (end_x >> 8) & 0xFF;
      data[3] = end_x & 0xFF;
      send_data(data, 4);

      // Set the row (y) start / end addresses
      send_command((uint8_t)Command::raset);
      data[0] = (start_y >> 8) & 0xFF;
      data[1] = start_y & 0xFF;
      data[2] = (end_y >> 8) & 0xFF;
      data[3] = end_y & 0xFF;
      send_data(data, 4);

      // Write the color data to controller RAM
      send_command((uint8_t)Command::ramwr);
      uint32_t size = width * height;
      static constexpr int max_bytes_to_send = 1024 * 2;
      uint16_t color_data[max_bytes_to_send];
      memset(color_data, color, max_bytes_to_send * sizeof(uint16_t));
      for (int i=0; i<size; i+=max_bytes_to_send) {
        int num_bytes = std::min((int)(size-i), (int)(max_bytes_to_send));
        send_data((uint8_t *)color_data, num_bytes * 2);
      }
    }

    /**
     * @brief Sets the DC pin to command and sends the command code.
     * @param command Command code to send
     */
    static void send_command(uint8_t command) {
      gpio_set_level(dc_pin_, (uint8_t)display_drivers::Mode::COMMAND);
      lcd_write_(&command, 1, (uint32_t)Display::Signal::NONE);
    }

    /**
     * @brief Sets the DC pin to data and sends the data, with optional flags.
     * @param data Pointer to array of bytes to be sent
     * @param length Number of bytes of data to send.
     * @param flags Optional (default = Display::Signal::NONE) flags associated with transfer.
     */
    static void send_data(uint8_t* data, size_t length, uint32_t flags=(uint32_t)Display::Signal::NONE) {
      gpio_set_level(dc_pin_, (uint8_t)display_drivers::Mode::DATA);
      lcd_write_(data, length, flags);
    }

    static void send_commands(display_drivers::LcdInitCmd *commands) {
      using namespace std::chrono_literals;
      //Send all the commands
      uint16_t cmd = 0;
      while (commands[cmd].length!=0xff) {
        send_command(commands[cmd].command);
        send_data(commands[cmd].data, commands[cmd].length&0x1F);
        if (commands[cmd].length & 0x80) {
          std::this_thread::sleep_for(100ms);
        }
        cmd++;
      }
    }

    static void set_offset(int x, int y) {
      offset_x_ = x;
      offset_y_ = y;
    }

    static void get_offset(int &x, int &y) {
      x = offset_x_;
      y = offset_y_;
    }

  protected:
    static Display::write_fn lcd_write_;
    static gpio_num_t reset_pin_;
    static gpio_num_t dc_pin_;
    static gpio_num_t backlight_pin_;
    static int offset_x_;
    static int offset_y_;
  };
}