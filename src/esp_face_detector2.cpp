#include "esp_face_detector.h"
#include "freertos/idf_additions.h"
#include "human_face_detect.hpp"
#include <memory>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace mp_dl::detector {

// Object
struct MP_FaceDetector {
    mp_obj_base_t base;
    std::shared_ptr<HumanFaceDetect> detector = nullptr;
    int img_width;
    int img_height;
    bool return_features;
    std::queue<std::vector<dl::image::detect_result_t>> results_queue;
    std::mutex mtx;
    std::condition_variable cv;
    bool detection_in_progress = false;
};

// Constructor
static mp_obj_t face_detector_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    enum { ARG_img_width, ARG_img_height, ARG_return_features };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_width, MP_ARG_INT, {.u_int = 320} },
        { MP_QSTR_height, MP_ARG_INT, {.u_int = 240} },
        { MP_QSTR_features, MP_ARG_BOOL, {.u_bool = false} },
    };

    mp_arg_val_t parsed_args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed_args);

    MP_FaceDetector *self = mp_obj_malloc_with_finaliser(MP_FaceDetector, &mp_face_detector_type);
    self->detector = std::make_shared<HumanFaceDetect>();

    self->img_width = parsed_args[ARG_img_width].u_int;
    self->img_height = parsed_args[ARG_img_height].u_int;
    self->return_features = parsed_args[ARG_return_features].u_bool;

    return MP_OBJ_FROM_PTR(self);
}

// Destructor
static mp_obj_t face_detector_del(mp_obj_t self_in) {
    MP_FaceDetector *self = static_cast<MP_FaceDetector *>(MP_OBJ_TO_PTR(self_in));
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
    img.width = self->img_width;
    img.height = self->img_height;
    img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888;

    if (bufinfo.len != img.width * img.height * 3) {
        mp_raise_ValueError("Frame buffer size does not match the image size");
    } else {
        img.data = (uint8_t *)bufinfo.buf;
    }

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

        if (self->return_features) {
            mp_obj_t features[10];
            for (int i = 0; i < 10; ++i) {
                features[i] = mp_obj_new_int(res.keypoint[i]);
            }
            mp_obj_list_append(list, mp_obj_new_tuple(10, features));
        }
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_2_CXX(face_detector_detect_obj, face_detector_detect);

// Detect in thread method
static void detect_in_thread(MP_FaceDetector *self, dl::image::img_t img) {
    auto detect_results = self->detector->run(img);
    {
        std::lock_guard<std::mutex> lock(self->mtx);
        self->results_queue.push(detect_results);
        self->detection_in_progress = false;
    }
    self->cv.notify_one();
}

static mp_obj_t face_detector_detect_async(mp_obj_t self_in, mp_obj_t framebuffer_obj) {
    MP_FaceDetector *self = static_cast<MP_FaceDetector *>(MP_OBJ_TO_PTR(self_in));

    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(framebuffer_obj, &bufinfo, MP_BUFFER_READ);

    dl::image::img_t img;
    img.width = self->img_width;
    img.height = self->img_height;
    img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB888;

    if (bufinfo.len != img.width * img.height * 3) {
        mp_raise_ValueError("Frame buffer size does not match the image size");
    } else {
        img.data = (uint8_t *)bufinfo.buf;
    }

    {
        std::lock_guard<std::mutex> lock(self->mtx);
        if (self->detection_in_progress) {
            mp_raise_ValueError("Detection already in progress");
        }
        self->detection_in_progress = true;
    }

    std::thread(detect_in_thread, self, img).detach();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2_CXX(face_detector_detect_async_obj, face_detector_detect_async);

// Get status method
static mp_obj_t face_detector_get_status(mp_obj_t self_in) {
    MP_FaceDetector *self = static_cast<MP_FaceDetector *>(MP_OBJ_TO_PTR(self_in));
    std::lock_guard<std::mutex> lock(self->mtx);
    return mp_obj_new_bool(self->detection_in_progress);
}
static MP_DEFINE_CONST_FUN_OBJ_1_CXX(face_detector_get_status_obj, face_detector_get_status);

// Get results method
static mp_obj_t face_detector_get_results(mp_obj_t self_in) {
    MP_FaceDetector *self = static_cast<MP_FaceDetector *>(MP_OBJ_TO_PTR(self_in));
    std::unique_lock<std::mutex> lock(self->mtx);
    self->cv.wait(lock, [self] { return !self->detection_in_progress || !self->results_queue.empty(); });

    if (self->results_queue.empty()) {
        return mp_const_none;
    }

    auto detect_results = self->results_queue.front();
    self->results_queue.pop();

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

        if (self->return_features) {
            mp_obj_t features[10];
            for (int i = 0; i < 10; ++i) {
                features[i] = mp_obj_new_int(res.keypoint[i]);
            }
            mp_obj_list_append(list, mp_obj_new_tuple(10, features));
        }
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_1_CXX(face_detector_get_results_obj, face_detector_get_results);

// Local dict
static const mp_rom_map_elem_t face_detector_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_detect), MP_ROM_PTR(&face_detector_detect_obj) },
    { MP_ROM_QSTR(MP_QSTR_detect_async), MP_ROM_PTR(&face_detector_detect_async_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_status), MP_ROM_PTR(&face_detector_get_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_results), MP_ROM_PTR(&face_detector_get_results_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&face_detector_del_obj) },
};
static MP_DEFINE_CONST_DICT(face_detector_locals_dict, face_detector_locals_dict_table);

// Print
static void print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind)
{
    mp_printf(print, "Face detector object");
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