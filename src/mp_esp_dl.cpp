#include "mp_esp_dl.hpp"

namespace mp_esp_dl {
    void initialize_img(dl::image::img_t &img, int width, int height) {
        img.width = width;
        img.height = height;
        img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888;
        img.data = nullptr;
    }

    template <typename T>
    T *get_and_validate_framebuffer(mp_obj_t self_in, mp_obj_t framebuffer_obj) {
        // Cast self_in to the correct type
        T *self = static_cast<T *>(MP_OBJ_TO_PTR(self_in));

        // Validate the framebuffer
        mp_buffer_info_t bufinfo;
        mp_get_buffer_raise(framebuffer_obj, &bufinfo, MP_BUFFER_READ);

        if (bufinfo.len != self->img.width * self->img.height * 3) {
            mp_raise_ValueError("Frame buffer size does not match the image size with an RGB888 pixel format");
        }

        // Assign the buffer data to the image
        self->img.data = (uint8_t *)bufinfo.buf;

        return self;
    }
} // namespace mp_esp_dl