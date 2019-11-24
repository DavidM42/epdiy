#include "EPD.hpp"
#include "ed097oc4.hpp"

#define CLEAR_BYTE    0B10101010
#define DARK_BYTE     0B01010101

struct __attribute__((__packed__)) bmp_file_header_t {
  uint16_t signature;
  uint32_t file_size;
  uint16_t reserved[2];
  uint32_t image_offset;
};

struct __attribute__((__packed__)) bmp_image_header_t {
  uint32_t header_size;
  uint32_t image_width;
  uint32_t image_height;
  uint16_t color_planes;
  uint16_t bits_per_pixel;
  uint32_t compression_method;
  uint32_t image_size;
  uint32_t horizontal_resolution;
  uint32_t vertical_resolution;
  uint32_t colors_in_palette;
  uint32_t important_colors;
};


/* Contrast cycles in order of contrast (Darkest first).  */
const uint32_t contrast_cycles[15] = {
    3, 4, 4,
    4, 4, 5, 5,
    5, 8, 10, 10,
    10, 30, 40, 50
};

EPD::EPD(uint16_t width, uint16_t height) {
    this->width = width;
    this->height = height;
    this->skipping = 0;
    this->null_row = (uint8_t*)malloc(this->width/4);
    for (int i = 0; i < this->width/4; i++) {
        this->null_row[i] = 255;
    }
    //memset(this->null_row, 0, this->width/4);
    init_gpios();
}

EPD::~EPD() {
    free(this->null_row);
}

void EPD::poweron() {
    epd_poweron();
}

void EPD::poweroff() {
    epd_poweroff();
}

void EPD::skip_row() {
    if (this->skipping < 1) {
        output_row(10, this->null_row, this->width);
    } else {
        output_row(10, this->null_row, this->width);
        //skip(this->width);
    }
    this->skipping++;
}

void EPD::write_row(uint32_t output_time_us, uint8_t* data) {
    this->skipping = 0;
    output_row(output_time_us, data, this->width);
}

void EPD::draw_byte(Rect_t* area, short time, uint8_t byte) {

    uint8_t* row = (uint8_t*)malloc(this->width/4);
    for (int i = 0; i < this->width/4; i++) {
        if (i*4 + 3 < area->x || i*4 >= area->x + area->width) {
            row[i] = 0;
        } else {
            // undivisible pixel values
            if (area->x > i*4) {
                row[i] = byte & (0B11111111 >> (2 * (area->x % 4)));
            } else if (i*4 + 4 > area->x + area->width) {
                row[i] = byte & (0B11111111 << (8 - 2 * ((area->x + area->width) % 4)));
            } else {
                row[i] = byte;
            }
        }
    }
    start_frame();
    for (int i = 0; i < this->height; i++) {
        // before are of interest: skip
        if (i < area->y) {
            this->skip_row();
        // start area of interest: set row data
        } else if (i == area->y) {
            this->write_row(time, row);
        // load nop row if done with area
        } else if (i >= area->y + area->height) {
            this->skip_row();
        // output the same as before
        } else {
            this->write_row(time, NULL);
        }
    }
    end_frame();
    free(row);

}

void EPD::clear_area(Rect_t area) {
    const short white_time = 80;
    const short dark_time = 40;

    for (int i=0; i<8; i++) {
        draw_byte(&area, white_time, CLEAR_BYTE);
    }
    for (int i=0; i<6; i++) {
        draw_byte(&area, dark_time, DARK_BYTE);
    }
    for (int i=0; i<8; i++) {
        draw_byte(&area, white_time, CLEAR_BYTE);
    }
}

Rect_t EPD::full_screen() {
    Rect_t full_screen = { .x = 0, .y = 0, .width = this->width, .height = this->height };
    return full_screen;
}

void EPD::clear_screen() {
    clear_area(this->full_screen());
}

/* shift row bitwise by bits to the right.
 * only touch bytes start to end (inclusive).
 * insert zeroes where gaps are created.
 * information of the end byte is lost.
 *
 * Possible improvement: use larger chunks.
 * */
void shift_row_r(uint8_t* row, uint8_t bits, uint16_t start, uint16_t end) {
    uint8_t carry = 0;
    uint8_t mask = ~(0B11111111 << bits);
    for (uint16_t i=end; i>=start; i--) {
        carry = (row[i - 1] & mask) << (8 - bits);
        row[i] = row[i] >> bits | carry;
    }
}

void IRAM_ATTR EPD::draw_picture(Rect_t area, uint8_t* data) {
    uint8_t* row = (uint8_t*)malloc(this->width/4);

    for (uint8_t k = 15; k > 0; k--) {
        uint8_t* ptr = data;
        yield();
        start_frame();
        // initialize with null row to avoid artifacts
        for (int i = 0; i < this->height; i++) {
            if (i < area.y || i >= area.y + area.height) {
                this->skip_row();
                continue;
            }

            uint32_t aligned_end = 4 * (area.x / 4) + area.width;
            uint8_t pixel = 0B00000000;
            for (uint32_t j = 0; j < this->width; j++) {
                if (j % 4 == 0) {
                    pixel = 0B00000000;
                }
                pixel = pixel << 2;
                if (j >= area.x && j < area.x + area.width) {
                    uint8_t value = *(ptr++);
                    pixel |= ((value >> 4) < k);
                }
                if (j % 4 == 3) {
                    row[j / 4] = pixel;
                }
            }
            this->write_row(contrast_cycles[15 - k], row);
        }
        end_frame();
    }
    free(row);
}

int EPD::draw_sd_image(File* file) {

    uint8_t* row = (uint8_t*)malloc(this->width/4);
    uint8_t* img_row = (uint8_t*)malloc(this->width);
    bmp_file_header_t fileHeader;
    bmp_image_header_t imageHeader;

    // Read the file header
    file->read((uint8_t*)&fileHeader, sizeof(fileHeader));
    // Check signature
    if (fileHeader.signature != 0x4D42) {
        return 1;
    }
    // Read the image header
    file->read((uint8_t*)&imageHeader, sizeof(imageHeader));
    if (imageHeader.image_height != 825) {
        return 2;
    }
    if (imageHeader.image_width != 1200) {
        return 3;
    }
    if (imageHeader.compression_method != 0) {
        return 4;
    }
    if (imageHeader.colors_in_palette != 256 && imageHeader.colors_in_palette != 0) {
        return 5;
    }

    for (uint8_t k = 15; k > 0; k--) {
        yield();
        start_frame();
        file->seek(fileHeader.image_offset);
        // initialize with null row to avoid artifacts
        for (int i = 0; i < this->height; i++) {
            file->read(img_row, imageHeader.image_width);
            uint8_t* ptr = img_row;
            uint8_t pixel = 0B00000000;
            for (uint32_t j = 0; j < this->width; j++) {
                if (j % 4 == 0) {
                    pixel = 0B00000000;
                }
                pixel = pixel << 2;
                uint8_t value = *(ptr++);
                pixel |= ((value >> 4) < k);
                if (j % 4 == 3) {
                    row[j / 4] = pixel;
                }
            }
            this->write_row(contrast_cycles[15 - k], row);
        }
        end_frame();
    }
    free(img_row);
    free(row);
}
