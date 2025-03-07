#include "human_face_detect.hpp"
// #include "bsp/esp-bsp.h"
#include "py/obj.h"
#include "py/runtime.h"
#include "py/builtin.h"

typedef struct _mp_face_detector_obj_t {
    mp_obj_base_t base;
    HumanFaceDetect *detector;
} mp_face_detector_obj_t;

const mp_obj_type_t face_detector_type;

extern "C" void face_detector_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    mp_face_detector_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "FaceDetect()");
}

extern "C" mp_obj_t face_detector_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_camera_obj_t *self = mp_obj_malloc_with_finaliser(mp_camera_obj_t, &face_detector_type);
    self->base.type = &face_detector_type;
    self->detector = new HumanFaceDetect();
    return MP_OBJ_FROM_PTR(self);
}

extern "C" void face_detector_deinit(mp_obj_t self_in) {
    mp_face_detector_obj_t *self = MP_OBJ_TO_PTR(self_in);
    delete self->detector;
}
static MP_DEFINE_CONST_FUN_OBJ_1(face_detector_deinit_obj, face_detector_deinit);

extern "C" mp_obj_t face_detector_detect(mp_obj_t self_in, mp_obj_t framebuffer_obj, mp_obj_t width, mp_obj_t height) {
    mp_face_detector_obj_t *self = MP_OBJ_TO_PTR(self_in);

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(framebuffer_obj, &bufinfo, MP_BUFFER_READ);

    dl::image::img_t img;
    img.data = (uint8_t *)bufinfo.buf;
    img.width = mp_obj_get_int(width);
    img.height = mp_obj_get_int(height);
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
static MP_DEFINE_CONST_FUN_OBJ_4(face_detector_detect_obj, face_detector_detect);

static const mp_rom_map_elem_t face_detector_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_detect), MP_ROM_PTR(&face_detector_detect_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&face_detector_deinit_obj) },
};

static MP_DEFINE_CONST_DICT(face_detector_locals_dict, face_detector_locals_dict_table);

static const mp_rom_map_elem_t face_detector_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_FaceDetector) },
    { MP_ROM_QSTR(MP_QSTR_FaceDetector),    MP_ROM_PTR(&face_detector_type) },
};
static MP_DEFINE_CONST_DICT(face_detector_module_globals, face_detector_module_globals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    face_detector_type,
    MP_QSTR_FaceDetector,
    MP_TYPE_FLAG_NONE,
    make_new, face_detector_make_new,
    print, face_detector_print,
    locals_dict, &face_detector_locals_dict
);

const mp_obj_module_t face_detector_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&face_detector_module_globals,
};
MP_REGISTER_MODULE(MP_QSTR_FaceDetector, face_detector_module);

// extern "C" void app_main(void) {
//     // Register the FaceDetect type in MicroPython
//     mp_store_global(MP_QSTR_FaceDetect, MP_OBJ_FROM_PTR(&face_detector_type));
// }
