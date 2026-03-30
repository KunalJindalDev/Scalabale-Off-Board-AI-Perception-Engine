#pragma once

#include <string>
#include <vector>
#include <memory>
// ONNX Runtime header — works with both the tarball layout
// (onnxruntime/core/session/...) and the flat layout (/usr/local/include/...)
#if __has_include(<onnxruntime/core/session/onnxruntime_cxx_api.h>)
#  include <onnxruntime/core/session/onnxruntime_cxx_api.h>
#else
#  include <onnxruntime_cxx_api.h>
#endif

namespace inference {

struct Detection {
  float x_min, y_min, x_max, y_max;
  float confidence;
  int   class_id;
  std::string class_name;
  // 3-D position back-projected from depth image (robot-frame, metres)
  float world_x{0.f}, world_y{0.f}, world_z{0.f};
};

// Wraps an ONNX Runtime session for YOLOv8 object detection.
// Thread-safe: a single ModelRunner may be called from multiple threads
// (Ort::Session::Run is serialised internally by the runtime).
class ModelRunner {
public:
  // model_path  – path to a YOLOv8n/s/m ONNX export
  // conf_thresh – minimum confidence to keep a detection
  // iou_thresh  – IOU threshold for non-maximum suppression
  explicit ModelRunner(const std::string& model_path,
                       float conf_thresh = 0.45f,
                       float iou_thresh  = 0.50f);

  ~ModelRunner() = default;

  // Run inference on a pre-decoded RGB image.
  // rgb_data  – contiguous BGR8 buffer, width*height*3 bytes
  // depth_data – contiguous float32 buffer, width*height values (metres)
  //              may be nullptr; 3-D coords will be zeroed in that case
  // fx,fy,cx,cy – camera intrinsics (used for depth → 3-D)
  std::vector<Detection> run(const uint8_t* rgb_data,
                             const float*   depth_data,
                             int width, int height,
                             float fx, float fy,
                             float cx, float cy);

  // Returns the name of the first input tensor (for diagnostics).
  std::string input_name() const;

private:
  // Pre-processing: resize + normalize → float32 NCHW tensor
  std::vector<float> preprocess(const uint8_t* rgb_data, int w, int h);

  // Post-processing: parse raw YOLOv8 output, apply conf filter + NMS
  std::vector<Detection> postprocess(const float* raw_output,
                                     int64_t num_detections,
                                     int orig_w, int orig_h);

  // Non-maximum suppression
  std::vector<int> nms(const std::vector<Detection>& dets) const;

  // Back-project the centre of a bounding box into 3-D using depth
  void backproject(Detection& det,
                   const float* depth_data,
                   int width, int height,
                   float fx, float fy,
                   float cx, float cy) const;

  // COCO class names (80 classes; YOLOv8 default)
  static const std::vector<std::string> kClassNames;

  Ort::Env          env_;
  Ort::Session      session_;
  Ort::AllocatorWithDefaultOptions allocator_;

  float conf_thresh_;
  float iou_thresh_;

  // YOLOv8 standard input size
  static constexpr int kInputW = 640;
  static constexpr int kInputH = 640;
};

} // namespace inference
