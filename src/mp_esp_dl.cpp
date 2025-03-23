#include "mp_esp_dl.hpp"

namespace mp_esp_dl {
    void initialize_img(dl::image::img_t &img, int width, int height) {
        img.width = width;
        img.height = height;
        img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888;
        img.data = nullptr;
    }
} // namespace mp_esp_dl