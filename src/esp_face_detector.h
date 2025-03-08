#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <memory>

#include "human_face_detect.hpp"
#include "esp_face_detector.h"

namespace mp_dl::detector {

// Object
struct MP_FaceDetector {
    mp_obj_base_t base;
    std::shared_ptr<HumanFaceDetect> detector = nullptr;
    // HumanFaceDetect *detector;
};

// Constructor
static mp_obj_t face_detector_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    //mp_arg_check_num(n_args, n_kw, 0, 1, false);
    MP_FaceDetector *self = mp_obj_malloc(MP_FaceDetector, &mp_face_detector_type);
    self->detector = std::make_shared<HumanFaceDetect>();
    // self->detector = new HumanFaceDetect();
    return MP_OBJ_FROM_PTR(self);
}

// Destructor
static mp_obj_t face_detector_del(mp_obj_t self_in) {
    MP_FaceDetector *self = static_cast<MP_FaceDetector *>(MP_OBJ_TO_PTR(self_in));
    // delete self->detector;
    self->detector = nullptr;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1_CXX(face_detector_del_obj, face_detector_del);

// Detect method
static mp_obj_t face_detector_detect(mp_obj_t self_in, mp_obj_t framebuffer_obj) {
    MP_FaceDetector *self = static_cast<MP_FaceDetector *>(MP_OBJ_TO_PTR(self_in));

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(framebuffer_obj, &bufinfo, MP_BUFFER_READ);

    dl::image::img_t img;
    img.data = (uint8_t *)bufinfo.buf;
    img.width = 320;
    img.height = 240;
    img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888;

    auto &detect_results = self->detector->run(img);

    if (detect_results.size() == 0) {
        return mp_const_none;
    }

    mp_obj_t list = mp_obj_new_list(0, NULL);
    for (const auto &res : detect_results) {
        mp_obj_t tuple[4];
        tuple[0] = mp_obj_new_int(res.box[0]);
        tuple[1] = mp_obj_new_int(res.box[1]);
        tuple[2] = mp_obj_new_int(res.box[2]);
        tuple[3] = mp_obj_new_int(res.box[3]);
        mp_obj_list_append(list, mp_obj_new_tuple(4, tuple));
    }

    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_2_CXX(face_detector_detect_obj, face_detector_detect);

// Local dict
static const mp_rom_map_elem_t face_detector_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_detect), MP_ROM_PTR(&face_detector_detect_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&face_detector_del_obj) },
};
static MP_DEFINE_CONST_DICT(face_detector_locals_dict, face_detector_locals_dict_table);

// Print
static void print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    mp_printf(print, "Board");
}

} //namespace

// Type
MP_DEFINE_CONST_OBJ_TYPE(
    mp_face_detector_type,
    MP_QSTR_FaceDetector,
    MP_TYPE_FLAG_NONE,
    make_new, (const void *)mp_dl::detector::face_detector_make_new,
    print, (const void *)mp_dl::detector::print,
    locals_dict, &mp_dl::detector::face_detector_locals_dict
);

// Module definition
extern "C" {
    static const mp_rom_map_elem_t module_globals_table[] = {
        { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_mp_dl) },
        { MP_ROM_QSTR(MP_QSTR_FaceDetector), MP_ROM_PTR(&mp_face_detector_type) },
    };
    static MP_DEFINE_CONST_DICT(module_globals, module_globals_table);

    const mp_obj_module_t face_detector_module = {
        .base = { &mp_type_module },
        .globals = (mp_obj_dict_t *)&module_globals,
    };
    MP_REGISTER_MODULE(MP_QSTR_mp_dl, face_detector_module);
}