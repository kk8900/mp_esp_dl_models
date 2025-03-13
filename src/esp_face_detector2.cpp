#include "esp_face_detector.h"
#include "freertos/idf_additions.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "human_face_detect.hpp"
#include <memory>

namespace mp_dl::detector {

// Object
struct MP_FaceDetector {
    mp_obj_base_t base;
    std::shared_ptr<HumanFaceDetect> detector = nullptr;
    int img_width;
    int img_height;
    bool return_features;
    QueueHandle_t results_queue;
    QueueHandle_t image_queue;
    TaskHandle_t detect_task_handle;
    bool detection_in_progress;
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
    self->detect_task_handle = nullptr;
    self->detection_in_progress = false;

    // Initialize queues
    self->results_queue = xQueueCreate(1, sizeof(std::list<dl::detect::result_t>));
    self->image_queue = xQueueCreate(1, sizeof(dl::image::img_t));
    if (self->results_queue == NULL || self->image_queue == NULL) {
        mp_raise_msg(&mp_type_RuntimeError, "Failed to create queues");
    }

    return MP_OBJ_FROM_PTR(self);
}

// Destructor
static mp_obj_t face_detector_del(mp_obj_t self_in) {
    MP_FaceDetector *self = static_cast<MP_FaceDetector *>(MP_OBJ_TO_PTR(self_in));
    self->detector = nullptr;
    if (self->results_queue) {
        vQueueDelete(self->results_queue);
    }
    if (self->image_queue) {
        vQueueDelete(self->image_queue);
    }
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

// Detect in task function
void detect_task(void *pvParameters) {
    MP_FaceDetector *self = static_cast<MP_FaceDetector *>(pvParameters);
    dl::image::img_t img;

    while (self->detection_in_progress) {
        if (xQueueReceive(self->image_queue, &img, portMAX_DELAY) == pdPASS) {
            auto &detect_results = self->detector->run(img);
            xQueueSend(self->results_queue, &detect_results, portMAX_DELAY);
            self->detection_in_progress = false;
        }
    }
}

// Detect in thread method
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

    if (self->detection_in_progress) {
        mp_raise_ValueError("Detection already in progress");
    }

    self->detection_in_progress = true;
    xQueueSend(self->image_queue, &img, portMAX_DELAY);

    if (self->detect_task_handle == nullptr) {
        xTaskCreate(detect_task, "detect_task", 4096, self, 5, &self->detect_task_handle);
    }

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2_CXX(face_detector_detect_async_obj, face_detector_detect_async);

// Get status method
static mp_obj_t face_detector_get_status(mp_obj_t self_in) {
    MP_FaceDetector *self = static_cast<MP_FaceDetector *>(MP_OBJ_TO_PTR(self_in));
    return mp_obj_new_bool(self->detection_in_progress);
}
static MP_DEFINE_CONST_FUN_OBJ_1_CXX(face_detector_get_status_obj, face_detector_get_status);

// Get results method
static mp_obj_t face_detector_get_results(mp_obj_t self_in) {
    MP_FaceDetector *self = static_cast<MP_FaceDetector *>(MP_OBJ_TO_PTR(self_in));

    std::list<dl::detect::result_t> detect_results;
    if (xQueueReceive(self->results_queue, &detect_results, portMAX_DELAY) != pdTRUE) {
        return mp_const_none;
    }

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