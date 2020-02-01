/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ivan Belokobylskiy
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#define __ST7789_VERSION__  "0.1.4"

#include "py/obj.h"
#include "py/runtime.h"
#include "py/builtin.h"
#include "py/mphal.h"
#include "extmod/machine_spi.h"

#include "st7789.h"

#define _swap_int16_t(a, b) { int16_t t = a; a = b; b = t; }
#define ABS(N) (((N)<0)?(-(N)):(N))
#define mp_hal_delay_ms(delay)  (mp_hal_delay_us(delay * 1000))

#define CS_LOW()     { if(self->cs) {mp_hal_pin_write(self->cs, 0);} }
#define CS_HIGH()    { if(self->cs) {mp_hal_pin_write(self->cs, 1);} }
#define DC_LOW()     (mp_hal_pin_write(self->dc, 0))
#define DC_HIGH()    (mp_hal_pin_write(self->dc, 1))
#define RESET_LOW()  (mp_hal_pin_write(self->reset, 0))
#define RESET_HIGH() (mp_hal_pin_write(self->reset, 1))
#define DISP_HIGH() (mp_hal_pin_write(self->backlight, 1))
#define DISP_LOW() (mp_hal_pin_write(self->backlight, 0))


STATIC void write_spi(mp_obj_base_t *spi_obj, const uint8_t *buf, int len) {
    mp_machine_spi_p_t *spi_p = (mp_machine_spi_p_t*)spi_obj->type->protocol;
    spi_p->transfer(spi_obj, len, buf, NULL);
}

// this is the actual C-structure for our new object
typedef struct _st7789_ST7789_obj_t {
    mp_obj_base_t base;

    mp_obj_base_t *spi_obj;
    uint8_t width;
    uint8_t height;
    uint8_t xstart;
    uint8_t ystart;
    mp_hal_pin_obj_t reset;
    mp_hal_pin_obj_t dc;
    mp_hal_pin_obj_t cs;
    mp_hal_pin_obj_t backlight;
} st7789_ST7789_obj_t;


// just a definition
mp_obj_t st7789_ST7789_make_new( const mp_obj_type_t *type,
                                  size_t n_args,
                                  size_t n_kw,
                                  const mp_obj_t *args );
STATIC void st7789_ST7789_print( const mp_print_t *print,
                                  mp_obj_t self_in,
                                  mp_print_kind_t kind ) {
    (void)kind;
    st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<ST7789 width=%u, height=%u, spi=%p>", self->width, self->height, self->spi_obj);
}

/* methods start */

STATIC void write_cmd(st7789_ST7789_obj_t *self, uint8_t cmd, const uint8_t *data, int len) {
    CS_LOW()
    if (cmd) {
        DC_LOW();
        write_spi(self->spi_obj, &cmd, 1);
    }
    if (len > 0) {
        DC_HIGH();
        write_spi(self->spi_obj, data, len);
    }
    CS_HIGH()
}

STATIC void set_window(st7789_ST7789_obj_t *self, uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1) {
    if (x0 > x1 || x1 >= self->width) {
        return;
    }
    if (y0 > y1 || y1 >= self->height) {
        return;
    }
    uint8_t bufx[4] = {(x0+self->xstart) >> 8, (x0+self->xstart) & 0xFF, (x1+self->xstart) >> 8, (x1+self->xstart) & 0xFF};
    uint8_t bufy[4] = {(y0+self->ystart) >> 8, (y0+self->ystart) & 0xFF, (y1+self->ystart) >> 8, (y1+self->ystart) & 0xFF};
    write_cmd(self, ST7789_CASET, bufx, 4);
    write_cmd(self, ST7789_RASET, bufy, 4);
    write_cmd(self, ST7789_RAMWR, NULL, 0);
}

STATIC void fill_color_buffer(mp_obj_base_t* spi_obj, uint16_t color, int length) {
    uint8_t hi = color >> 8, lo = color;
    const int buffer_pixel_size = 128;
    int chunks = length / buffer_pixel_size;
    int rest = length % buffer_pixel_size;

    uint8_t buffer[buffer_pixel_size * 2]; // 128 pixels
    // fill buffer with color data
    for (int i = 0; i < length && i < buffer_pixel_size; i++) {
        buffer[i*2] = hi;
        buffer[i*2 + 1] = lo;
    }

    if (chunks) {
        for (int j = 0; j < chunks; j ++) {
            write_spi(spi_obj, buffer, buffer_pixel_size*2);
        }
    }
    if (rest) {
        write_spi(spi_obj, buffer, rest*2);
    }
}


STATIC void draw_pixel(st7789_ST7789_obj_t *self, uint8_t x, uint8_t y, uint16_t color) {
    uint8_t hi = color >> 8, lo = color;
    set_window(self, x, y, x, y);
    DC_HIGH();
    CS_LOW();
    write_spi(self->spi_obj, &hi, 1);
    write_spi(self->spi_obj, &lo, 1);
    CS_HIGH();
}


STATIC void fast_hline(st7789_ST7789_obj_t *self, uint8_t x, uint8_t y, uint16_t w, uint16_t color) {
    set_window(self, x, y, x + w - 1, y);
    DC_HIGH();
    CS_LOW();
    fill_color_buffer(self->spi_obj, color, w);
    CS_HIGH();
}


STATIC void fast_vline(st7789_ST7789_obj_t *self, uint8_t x, uint8_t y, uint16_t w, uint16_t color) {
    set_window(self, x, y, x, y + w - 1);
    DC_HIGH();
    CS_LOW();
    fill_color_buffer(self->spi_obj, color, w);
    CS_HIGH();
}


STATIC mp_obj_t st7789_ST7789_hard_reset(mp_obj_t self_in) {
    st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);

    CS_LOW();
    RESET_HIGH();
    mp_hal_delay_ms(50);
    RESET_LOW();
    mp_hal_delay_ms(50);
    RESET_HIGH();
    mp_hal_delay_ms(150);
    CS_HIGH();
    return mp_const_none;
}

STATIC mp_obj_t st7789_ST7789_soft_reset(mp_obj_t self_in) {
    st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);

    write_cmd(self, ST7789_SWRESET, NULL, 0);
    mp_hal_delay_ms(150);
    return mp_const_none;
}

// do not expose extra method to reduce size
#ifdef EXPOSE_EXTRA_METHODS
STATIC mp_obj_t st7789_ST7789_write(mp_obj_t self_in, mp_obj_t command, mp_obj_t data) {
    st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);

    mp_buffer_info_t src;
    if (data == mp_const_none) {
        write_cmd(self, (uint8_t)mp_obj_get_int(command), NULL, 0);
    } else {
        mp_get_buffer_raise(data, &src, MP_BUFFER_READ);
        write_cmd(self, (uint8_t)mp_obj_get_int(command), (const uint8_t*)src.buf, src.len);
    }

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_3(st7789_ST7789_write_obj, st7789_ST7789_write);

MP_DEFINE_CONST_FUN_OBJ_1(st7789_ST7789_hard_reset_obj, st7789_ST7789_hard_reset);
MP_DEFINE_CONST_FUN_OBJ_1(st7789_ST7789_soft_reset_obj, st7789_ST7789_soft_reset);

STATIC mp_obj_t st7789_ST7789_sleep_mode(mp_obj_t self_in, mp_obj_t value) {
    st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if(mp_obj_is_true(value)) {
        write_cmd(self, ST7789_SLPIN, NULL, 0);
    } else {
        write_cmd(self, ST7789_SLPOUT, NULL, 0);
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(st7789_ST7789_sleep_mode_obj, st7789_ST7789_sleep_mode);

STATIC mp_obj_t st7789_ST7789_set_window(size_t n_args, const mp_obj_t *args) {
    st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t x0 = mp_obj_get_int(args[1]);
    mp_int_t x1 = mp_obj_get_int(args[2]);
    mp_int_t y0 = mp_obj_get_int(args[3]);
    mp_int_t y1 = mp_obj_get_int(args[4]);

    set_window(self, x0, y0, x1, y1);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7789_ST7789_set_window_obj, 5, 5, st7789_ST7789_set_window);

#endif

STATIC mp_obj_t st7789_ST7789_inversion_mode(mp_obj_t self_in, mp_obj_t value) {
    st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if(mp_obj_is_true(value)) {
        write_cmd(self, ST7789_INVON, NULL, 0);
    } else {
        write_cmd(self, ST7789_INVOFF, NULL, 0);
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_2(st7789_ST7789_inversion_mode_obj, st7789_ST7789_inversion_mode);


STATIC mp_obj_t st7789_ST7789_fill_rect(size_t n_args, const mp_obj_t *args) {
    st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t x = mp_obj_get_int(args[1]);
    mp_int_t y = mp_obj_get_int(args[2]);
    mp_int_t w = mp_obj_get_int(args[3]);
    mp_int_t h = mp_obj_get_int(args[4]);
    mp_int_t color = mp_obj_get_int(args[5]);

    set_window(self, x, y, x + w - 1, y + h - 1);
    DC_HIGH();
    CS_LOW();
    fill_color_buffer(self->spi_obj, color, w * h);
    CS_HIGH();

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7789_ST7789_fill_rect_obj, 6, 6, st7789_ST7789_fill_rect);


STATIC mp_obj_t st7789_ST7789_fill(mp_obj_t self_in, mp_obj_t _color) {
    st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_int_t color = mp_obj_get_int(_color);

    set_window(self, 0, 0, self->width - 1, self->height - 1);
    DC_HIGH();
    CS_LOW();
    fill_color_buffer(self->spi_obj, color, self->width * self->height);
    CS_HIGH();

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(st7789_ST7789_fill_obj, st7789_ST7789_fill);


STATIC mp_obj_t st7789_ST7789_pixel(size_t n_args, const mp_obj_t *args) {
    st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t x = mp_obj_get_int(args[1]);
    mp_int_t y = mp_obj_get_int(args[2]);
    mp_int_t color = mp_obj_get_int(args[3]);

    draw_pixel(self, x, y, color);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7789_ST7789_pixel_obj, 4, 4, st7789_ST7789_pixel);


STATIC mp_obj_t st7789_ST7789_line(size_t n_args, const mp_obj_t *args) {
    st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t x0 = mp_obj_get_int(args[1]);
    mp_int_t y0 = mp_obj_get_int(args[2]);
    mp_int_t x1 = mp_obj_get_int(args[3]);
    mp_int_t y1 = mp_obj_get_int(args[4]);
    mp_int_t color = mp_obj_get_int(args[5]);

    bool steep = ABS(y1 - y0) > ABS(x1 - x0);
    if (steep) {
        _swap_int16_t(x0, y0);
        _swap_int16_t(x1, y1);
    }

    if (x0 > x1) {
        _swap_int16_t(x0, x1);
        _swap_int16_t(y0, y1);
    }

    int16_t dx = x1 - x0, dy = ABS(y1 - y0);
    int16_t err = dx >> 1, ystep = -1, xs = x0, dlen = 0;

    if (y0 < y1) ystep = 1;

    // Split into steep and not steep for FastH/V separation
    if (steep) {
        for (; x0 <= x1; x0++) {
        dlen++;
        err -= dy;
        if (err < 0) {
            err += dx;
            if (dlen == 1) draw_pixel(self, y0, xs, color);
            else fast_vline(self, y0, xs, dlen, color);
            dlen = 0; y0 += ystep; xs = x0 + 1;
        }
        }
        if (dlen) fast_vline(self, y0, xs, dlen, color);
    }
    else
    {
        for (; x0 <= x1; x0++) {
        dlen++;
        err -= dy;
        if (err < 0) {
            err += dx;
            if (dlen == 1) draw_pixel(self, xs, y0, color);
            else fast_hline(self, xs, y0, dlen, color);
            dlen = 0; y0 += ystep; xs = x0 + 1;
        }
        }
        if (dlen) fast_hline(self, xs, y0, dlen, color);
    }
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7789_ST7789_line_obj, 6, 6, st7789_ST7789_line);


STATIC mp_obj_t st7789_ST7789_blit_buffer(size_t n_args, const mp_obj_t *args) {
    st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_buffer_info_t buf_info;
    mp_get_buffer_raise(args[1], &buf_info, MP_BUFFER_READ);
    mp_int_t x = mp_obj_get_int(args[2]);
    mp_int_t y = mp_obj_get_int(args[3]);
    mp_int_t w = mp_obj_get_int(args[4]);
    mp_int_t h = mp_obj_get_int(args[5]);

    set_window(self, x, y, x + w - 1, y + h - 1);
    DC_HIGH();
    CS_LOW();

    const int buf_size = 256;
    int limit = MIN(buf_info.len, w * h * 2);
    int chunks = limit / buf_size;
    int rest = limit % buf_size;
    int i = 0;
    for (; i < chunks; i ++) {
        write_spi(self->spi_obj, (const uint8_t*)buf_info.buf + i*buf_size, buf_size);
    }
    if (rest) {
        write_spi(self->spi_obj, (const uint8_t*)buf_info.buf + i*buf_size, rest);
    }
    CS_HIGH();

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7789_ST7789_blit_buffer_obj, 6, 6, st7789_ST7789_blit_buffer);


STATIC mp_obj_t st7789_ST7789_init(mp_obj_t self_in) {
    st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);
    st7789_ST7789_hard_reset(self_in);
    st7789_ST7789_soft_reset(self_in);
    write_cmd(self, ST7789_SLPOUT, NULL, 0);

    const uint8_t color_mode[] = { COLOR_MODE_65K | COLOR_MODE_16BIT};
    write_cmd(self, ST7789_COLMOD, color_mode, 1);
    mp_hal_delay_ms(10);
    const uint8_t madctl[] = { ST7789_MADCTL_ML | ST7789_MADCTL_RGB };
    write_cmd(self, ST7789_MADCTL, madctl, 1);

    write_cmd(self, ST7789_INVON, NULL, 0);
    mp_hal_delay_ms(10);
    write_cmd(self, ST7789_NORON, NULL, 0);
    mp_hal_delay_ms(10);

    const mp_obj_t args[] = {
        self_in,
        mp_obj_new_int(0),
        mp_obj_new_int(0),
        mp_obj_new_int(self->width),
        mp_obj_new_int(self->height),
        mp_obj_new_int(BLACK)
    };
    st7789_ST7789_fill_rect(6, args);
    write_cmd(self, ST7789_DISPON, NULL, 0);
    mp_hal_delay_ms(100);

    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(st7789_ST7789_init_obj, st7789_ST7789_init);

STATIC mp_obj_t st7789_ST7789_on(mp_obj_t self_in) {
    st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);
    DISP_HIGH();
    mp_hal_delay_ms(10);   
    
    return mp_const_none; 
}
MP_DEFINE_CONST_FUN_OBJ_1(st7789_ST7789_on_obj, st7789_ST7789_on);

STATIC mp_obj_t st7789_ST7789_off(mp_obj_t self_in) {
    st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);
    DISP_LOW();
    mp_hal_delay_ms(10);
    
    return mp_const_none;    
}
MP_DEFINE_CONST_FUN_OBJ_1(st7789_ST7789_off_obj, st7789_ST7789_off);

STATIC mp_obj_t st7789_ST7789_hline(size_t n_args, const mp_obj_t *args) {
    st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t x = mp_obj_get_int(args[1]);
    mp_int_t y = mp_obj_get_int(args[2]);
    mp_int_t w = mp_obj_get_int(args[3]);
    mp_int_t color = mp_obj_get_int(args[4]);

    fast_hline(self, x, y, w, color);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7789_ST7789_hline_obj, 5, 5, st7789_ST7789_hline);


STATIC mp_obj_t st7789_ST7789_vline(size_t n_args, const mp_obj_t *args) {
    st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t x = mp_obj_get_int(args[1]);
    mp_int_t y = mp_obj_get_int(args[2]);
    mp_int_t w = mp_obj_get_int(args[3]);
    mp_int_t color = mp_obj_get_int(args[4]);

    fast_vline(self, x, y, w, color);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7789_ST7789_vline_obj, 5, 5, st7789_ST7789_vline);


STATIC mp_obj_t st7789_ST7789_rect(size_t n_args, const mp_obj_t *args) {
    st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_int_t x = mp_obj_get_int(args[1]);
    mp_int_t y = mp_obj_get_int(args[2]);
    mp_int_t w = mp_obj_get_int(args[3]);
    mp_int_t h = mp_obj_get_int(args[4]);
    mp_int_t color = mp_obj_get_int(args[5]);

    fast_hline(self, x, y, w, color);
    fast_vline(self, x, y, h, color);
    fast_hline(self, x, y + h - 1, w, color);
    fast_vline(self, x + w - 1, y, h, color);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7789_ST7789_rect_obj, 6, 6, st7789_ST7789_rect);


STATIC const mp_rom_map_elem_t st7789_ST7789_locals_dict_table[] = {
    // Do not expose internal functions to fit iram_0 section
#ifdef EXPOSE_EXTRA_METHODS
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&st7789_ST7789_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_hard_reset), MP_ROM_PTR(&st7789_ST7789_hard_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_soft_reset), MP_ROM_PTR(&st7789_ST7789_soft_reset_obj) },
    { MP_ROM_QSTR(MP_QSTR_sleep_mode), MP_ROM_PTR(&st7789_ST7789_sleep_mode_obj) },
    { MP_ROM_QSTR(MP_QSTR_inversion_mode), MP_ROM_PTR(&st7789_ST7789_inversion_mode_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_window), MP_ROM_PTR(&st7789_ST7789_set_window_obj) },
#endif
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&st7789_ST7789_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_on), MP_ROM_PTR(&st7789_ST7789_on_obj) },
    { MP_ROM_QSTR(MP_QSTR_off), MP_ROM_PTR(&st7789_ST7789_off_obj) },
    { MP_ROM_QSTR(MP_QSTR_pixel), MP_ROM_PTR(&st7789_ST7789_pixel_obj) },
    { MP_ROM_QSTR(MP_QSTR_line), MP_ROM_PTR(&st7789_ST7789_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_blit_buffer), MP_ROM_PTR(&st7789_ST7789_blit_buffer_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill_rect), MP_ROM_PTR(&st7789_ST7789_fill_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_fill), MP_ROM_PTR(&st7789_ST7789_fill_obj) },
    { MP_ROM_QSTR(MP_QSTR_hline), MP_ROM_PTR(&st7789_ST7789_hline_obj) },
    { MP_ROM_QSTR(MP_QSTR_vline), MP_ROM_PTR(&st7789_ST7789_vline_obj) },
    { MP_ROM_QSTR(MP_QSTR_rect), MP_ROM_PTR(&st7789_ST7789_rect_obj) },
};

STATIC MP_DEFINE_CONST_DICT(st7789_ST7789_locals_dict, st7789_ST7789_locals_dict_table);
/* methods end */


const mp_obj_type_t st7789_ST7789_type = {
    { &mp_type_type },
    .name = MP_QSTR_ST7789,
    .print = st7789_ST7789_print,
    .make_new = st7789_ST7789_make_new,
    .locals_dict = (mp_obj_dict_t*)&st7789_ST7789_locals_dict,
};

mp_obj_t st7789_ST7789_make_new(const mp_obj_type_t *type,
                                size_t n_args,
                                size_t n_kw,
                                const mp_obj_t *all_args ) {
    enum {
        ARG_spi, ARG_width, ARG_height, ARG_reset, ARG_dc, ARG_cs,
        ARG_backlight, ARG_xstart, ARG_ystart
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_spi, MP_ARG_OBJ | MP_ARG_REQUIRED, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_width, MP_ARG_INT | MP_ARG_REQUIRED, {.u_int = 0} },
        { MP_QSTR_height, MP_ARG_INT | MP_ARG_REQUIRED, {.u_int = 0} },
        { MP_QSTR_reset, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_dc, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_cs, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_backlight, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_xstart, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_ystart, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = -1} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // create new object
    st7789_ST7789_obj_t *self = m_new_obj(st7789_ST7789_obj_t);
    self->base.type = &st7789_ST7789_type;

    // set parameters
    mp_obj_base_t *spi_obj = (mp_obj_base_t*)MP_OBJ_TO_PTR(args[ARG_spi].u_obj);
    self->spi_obj = spi_obj;
    self->width = args[ARG_width].u_int;
    self->height = args[ARG_height].u_int;

    if (args[ARG_xstart].u_int >= 0 && args[ARG_ystart].u_int >= 0) {
        self->xstart = args[ARG_xstart].u_int;
        self->ystart = args[ARG_ystart].u_int;
    } else if (self->width == 240 && self->height == 240) {
        self->xstart = ST7789_240x240_XSTART;
        self->ystart = ST7789_240x240_YSTART;
    } else if (self->width == 135 && self->height == 240) {
        self->xstart = ST7789_135x240_XSTART;
        self->ystart = ST7789_135x240_YSTART;
    } else {
        mp_raise_ValueError("Unsupported display. Only 240x240 and 135x240 are supported without xstart and ystart provided");
    }

    if (args[ARG_reset].u_obj == MP_OBJ_NULL
        || args[ARG_dc].u_obj == MP_OBJ_NULL) {
        mp_raise_ValueError("must specify all of reset/dc pins");
    }

    self->reset = mp_hal_get_pin_obj(args[ARG_reset].u_obj);
    self->dc = mp_hal_get_pin_obj(args[ARG_dc].u_obj);

    if (args[ARG_cs].u_obj != MP_OBJ_NULL) {
        self->cs = mp_hal_get_pin_obj(args[ARG_cs].u_obj);
    }
    if (args[ARG_backlight].u_obj != MP_OBJ_NULL) {
        self->backlight = mp_hal_get_pin_obj(args[ARG_backlight].u_obj);
    }

    return MP_OBJ_FROM_PTR(self);
}


STATIC uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);
}


STATIC mp_obj_t st7789_color565(mp_obj_t r, mp_obj_t g, mp_obj_t b) {
    return MP_OBJ_NEW_SMALL_INT(color565(
        (uint8_t)mp_obj_get_int(r),
        (uint8_t)mp_obj_get_int(g),
        (uint8_t)mp_obj_get_int(b)
    ));
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(st7789_color565_obj, st7789_color565);


STATIC void map_bitarray_to_rgb565(uint8_t const *bitarray, uint8_t *buffer, int length, int width,
                                  uint16_t color, uint16_t bg_color) {
    int row_pos = 0;
    for (int i = 0; i < length; i++) {
        uint8_t byte = bitarray[i];
        for (int bi = 7; bi >= 0; bi--) {
            uint8_t b = byte & (1 << bi);
            uint16_t cur_color = b ? color : bg_color;
            *buffer = (cur_color & 0xff00) >> 8;
            buffer ++;
            *buffer = cur_color & 0xff;
            buffer ++;

            row_pos ++;
            if (row_pos >= width) {
                row_pos = 0;
                break;
            }
        }
    }
}


STATIC mp_obj_t st7789_map_bitarray_to_rgb565(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_bitarray, ARG_buffer, ARG_width, ARG_color, ARG_bg_color };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_bitarray, MP_ARG_OBJ | MP_ARG_REQUIRED, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_buffer, MP_ARG_OBJ | MP_ARG_REQUIRED, {.u_obj = MP_OBJ_NULL} },
        { MP_QSTR_width, MP_ARG_INT | MP_ARG_REQUIRED, {.u_int = -1} },
        { MP_QSTR_color, MP_ARG_INT, {.u_int = WHITE} },
        { MP_QSTR_bg_color, MP_ARG_INT, {.u_int = BLACK } },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    mp_buffer_info_t bitarray_info;
    mp_buffer_info_t buffer_info;
    mp_get_buffer_raise(args[ARG_bitarray].u_obj, &bitarray_info, MP_BUFFER_READ);
    mp_get_buffer_raise(args[ARG_buffer].u_obj, &buffer_info, MP_BUFFER_WRITE);
    mp_int_t width = args[ARG_width].u_int;
    mp_int_t color = args[ARG_color].u_int;
    mp_int_t bg_color = args[ARG_bg_color].u_int;

    map_bitarray_to_rgb565(bitarray_info.buf, buffer_info.buf, bitarray_info.len, width, color, bg_color);

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_KW(st7789_map_bitarray_to_rgb565_obj, 3, st7789_map_bitarray_to_rgb565);


STATIC const mp_map_elem_t st7789_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_st7789) },
    { MP_ROM_QSTR(MP_QSTR_color565), (mp_obj_t)&st7789_color565_obj },
    { MP_ROM_QSTR(MP_QSTR_map_bitarray_to_rgb565), (mp_obj_t)&st7789_map_bitarray_to_rgb565_obj },
    { MP_ROM_QSTR(MP_QSTR_ST7789), (mp_obj_t)&st7789_ST7789_type },
    { MP_ROM_QSTR(MP_QSTR_BLACK), MP_ROM_INT(BLACK) },
    { MP_ROM_QSTR(MP_QSTR_BLUE), MP_ROM_INT(BLUE) },
    { MP_ROM_QSTR(MP_QSTR_RED), MP_ROM_INT(RED) },
    { MP_ROM_QSTR(MP_QSTR_GREEN), MP_ROM_INT(GREEN) },
    { MP_ROM_QSTR(MP_QSTR_CYAN), MP_ROM_INT(CYAN) },
    { MP_ROM_QSTR(MP_QSTR_MAGENTA), MP_ROM_INT(MAGENTA) },
    { MP_ROM_QSTR(MP_QSTR_YELLOW), MP_ROM_INT(YELLOW) },
    { MP_ROM_QSTR(MP_QSTR_WHITE), MP_ROM_INT(WHITE) },
};

STATIC MP_DEFINE_CONST_DICT (mp_module_st7789_globals, st7789_module_globals_table );

const mp_obj_module_t mp_module_st7789 = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&mp_module_st7789_globals,
};

MP_REGISTER_MODULE(MP_QSTR_st7789, mp_module_st7789, MODULE_ST7789_ENABLED);
