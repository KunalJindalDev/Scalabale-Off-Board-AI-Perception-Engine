#include "model_runner.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <numeric>
#include <stdexcept>

namespace inference {

// ---------------------------------------------------------------------------
// COCO 80-class names (YOLOv8 default ordering)
// ---------------------------------------------------------------------------
const std::vector<std::string> ModelRunner::kClassNames = {
  "person","bicycle","car","motorbike","aeroplane","bus","train","truck",
  "boat","traffic light","fire hydrant","stop sign","parking meter","bench",
  "bird","cat","dog","horse","sheep","cow","elephant","bear","zebra","giraffe",
  "backpack","umbrella","handbag","tie","suitcase","frisbee","skis",
  "snowboard","sports ball","kite","baseball bat","baseball glove",
  "skateboard","surfboard","tennis racket","bottle","wine glass","cup",
  "fork","knife","spoon","bowl","banana","apple","sandwich","orange",
  "broccoli","carrot","hot dog","pizza","donut","cake","chair","sofa",
  "pottedplant","bed","diningtable","toilet","tvmonitor","laptop","mouse",
  "remote","keyboard","cell phone","microwave","oven","toaster","sink",
  "refrigerator","book","clock","vase","scissors","teddy bear","hair drier",
  "toothbrush"
};

// ---------------------------------------------------------------------------
// Constructor – loads ONNX model
// ---------------------------------------------------------------------------
ModelRunner::ModelRunner(const std::string& model_path,
                         float conf_thresh,
                         float iou_thresh)
  : env_(ORT_LOGGING_LEVEL_WARNING, "InferenceServer"),
    session_(env_, model_path.c_str(), Ort::SessionOptions{}),
    conf_thresh_(conf_thresh),
    iou_thresh_(iou_thresh)
{
  // Verify the model has at least one input/output
  const size_t n_inputs  = session_.GetInputCount();
  const size_t n_outputs = session_.GetOutputCount();
  if (n_inputs == 0 || n_outputs == 0) {
    throw std::runtime_error("ONNX model has no inputs or outputs: " + model_path);
  }
}

// ---------------------------------------------------------------------------
// input_name
// ---------------------------------------------------------------------------
std::string ModelRunner::input_name() const {
  return session_.GetInputNameAllocated(0, allocator_).get();
}

// ---------------------------------------------------------------------------
// Pre-processing: BGR8 → float32 NCHW [1,3,640,640], normalised to [0,1]
// ---------------------------------------------------------------------------
std::vector<float> ModelRunner::preprocess(const uint8_t* rgb_data,
                                            int w, int h) {
  // Nearest-neighbour resize to 640×640
  std::vector<float> input(3 * kInputH * kInputW);

  for (int row = 0; row < kInputH; ++row) {
    for (int col = 0; col < kInputW; ++col) {
      int src_row = static_cast<int>(row * h / kInputH);
      int src_col = static_cast<int>(col * w / kInputW);
      const uint8_t* px = rgb_data + (src_row * w + src_col) * 3;
      // BGR → RGB channel split into CHW planes
      input[0 * kInputH * kInputW + row * kInputW + col] = px[2] / 255.f; // R
      input[1 * kInputH * kInputW + row * kInputW + col] = px[1] / 255.f; // G
      input[2 * kInputH * kInputW + row * kInputW + col] = px[0] / 255.f; // B
    }
  }
  return input;
}

// ---------------------------------------------------------------------------
// NMS
// ---------------------------------------------------------------------------
std::vector<int> ModelRunner::nms(const std::vector<Detection>& dets) const {
  // Sort by confidence descending
  std::vector<int> idx(dets.size());
  std::iota(idx.begin(), idx.end(), 0);
  std::sort(idx.begin(), idx.end(), [&](int a, int b){
    return dets[a].confidence > dets[b].confidence;
  });

  std::vector<bool> suppressed(dets.size(), false);
  std::vector<int>  keep;

  for (int i : idx) {
    if (suppressed[i]) continue;
    keep.push_back(i);
    for (int j : idx) {
      if (suppressed[j] || j == i) continue;
      // Compute IOU between dets[i] and dets[j]
      float ix1 = std::max(dets[i].x_min, dets[j].x_min);
      float iy1 = std::max(dets[i].y_min, dets[j].y_min);
      float ix2 = std::min(dets[i].x_max, dets[j].x_max);
      float iy2 = std::min(dets[i].y_max, dets[j].y_max);
      float inter = std::max(0.f, ix2-ix1) * std::max(0.f, iy2-iy1);
      float area_i = (dets[i].x_max-dets[i].x_min)*(dets[i].y_max-dets[i].y_min);
      float area_j = (dets[j].x_max-dets[j].x_min)*(dets[j].y_max-dets[j].y_min);
      float iou = inter / (area_i + area_j - inter + 1e-6f);
      if (iou > iou_thresh_) suppressed[j] = true;
    }
  }
  return keep;
}

// ---------------------------------------------------------------------------
// Post-processing: parse YOLOv8 output tensor [1, 84, 8400]
// YOLOv8 output layout: each column is one anchor; rows 0-3 = cx,cy,w,h;
// rows 4-83 = per-class scores.
// ---------------------------------------------------------------------------
std::vector<Detection> ModelRunner::postprocess(const float* raw,
                                                 int64_t /*num_det*/,
                                                 int orig_w, int orig_h) {
  constexpr int kAnchors   = 8400;
  constexpr int kNumClass  = 80;

  std::vector<Detection> candidates;

  float scale_x = static_cast<float>(orig_w) / kInputW;
  float scale_y = static_cast<float>(orig_h) / kInputH;

  for (int a = 0; a < kAnchors; ++a) {
    // raw[row * kAnchors + a]
    float cx = raw[0 * kAnchors + a];
    float cy = raw[1 * kAnchors + a];
    float bw = raw[2 * kAnchors + a];
    float bh = raw[3 * kAnchors + a];

    // Find best class
    int   best_cls = 0;
    float best_scr = raw[4 * kAnchors + a];
    for (int c = 1; c < kNumClass; ++c) {
      float s = raw[(4 + c) * kAnchors + a];
      if (s > best_scr) { best_scr = s; best_cls = c; }
    }

    if (best_scr < conf_thresh_) continue;

    Detection d;
    d.x_min      = (cx - bw / 2.f) * scale_x;
    d.y_min      = (cy - bh / 2.f) * scale_y;
    d.x_max      = (cx + bw / 2.f) * scale_x;
    d.y_max      = (cy + bh / 2.f) * scale_y;
    d.confidence = best_scr;
    d.class_id   = best_cls;
    d.class_name = (best_cls < static_cast<int>(kClassNames.size()))
                     ? kClassNames[best_cls] : "unknown";
    candidates.push_back(d);
  }

  auto keep = nms(candidates);
  std::vector<Detection> out;
  out.reserve(keep.size());
  for (int i : keep) out.push_back(candidates[i]);
  return out;
}

// ---------------------------------------------------------------------------
// Back-project bounding-box centre into 3-D
// ---------------------------------------------------------------------------
void ModelRunner::backproject(Detection& det,
                               const float* depth_data,
                               int width, int height,
                               float fx, float fy,
                               float cx, float cy) const {
  if (!depth_data) return;

  int u = static_cast<int>((det.x_min + det.x_max) / 2.f);
  int v = static_cast<int>((det.y_min + det.y_max) / 2.f);
  u = std::clamp(u, 0, width  - 1);
  v = std::clamp(v, 0, height - 1);

  float z = depth_data[v * width + u];
  if (z <= 0.f || !std::isfinite(z)) return;

  det.world_z = z;
  det.world_x = (u - cx) * z / fx;
  det.world_y = (v - cy) * z / fy;
}

// ---------------------------------------------------------------------------
// run() – full pipeline
// ---------------------------------------------------------------------------
std::vector<Detection> ModelRunner::run(const uint8_t* rgb_data,
                                         const float*   depth_data,
                                         int width, int height,
                                         float fx, float fy,
                                         float cx_in, float cy_in) {
  // 1. Pre-process
  auto input_tensor_data = preprocess(rgb_data, width, height);

  // 2. Build Ort tensors
  std::array<int64_t,4> shape{1, 3, kInputH, kInputW};
  auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

  Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
      memory_info,
      input_tensor_data.data(),
      input_tensor_data.size(),
      shape.data(), shape.size());

  // 3. Run session
  auto input_name_ptr  = session_.GetInputNameAllocated(0, allocator_);
  auto output_name_ptr = session_.GetOutputNameAllocated(0, allocator_);

  const char* input_names[]  = { input_name_ptr.get() };
  const char* output_names[] = { output_name_ptr.get() };

  auto output_tensors = session_.Run(
      Ort::RunOptions{nullptr},
      input_names,  &input_tensor, 1,
      output_names, 1);

  // 4. Post-process
  const float* raw_out = output_tensors[0].GetTensorData<float>();
  auto shape_out = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();
  // shape_out: [1, 84, 8400]
  int64_t num_anchors = (shape_out.size() >= 3) ? shape_out[2] : 8400;

  auto detections = postprocess(raw_out, num_anchors, width, height);

  // 5. Back-project depths
  for (auto& d : detections) {
    backproject(d, depth_data, width, height, fx, fy, cx_in, cy_in);
  }

  return detections;
}

} // namespace inference
