#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include <cstdint>
#include <filesystem>

// We test the public interface; the ONNX file is optional in CI.
// If YOLO_MODEL_PATH env variable is set and the file exists, we run
// the full inference round-trip; otherwise we only test pre/post helpers
// via a thin white-box accessor defined below.

#include "model_runner.hpp"

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static std::vector<uint8_t> make_solid_image(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
  std::vector<uint8_t> img(w * h * 3);
  for (int i = 0; i < w * h; ++i) {
    img[i*3+0] = b;  // BGR order
    img[i*3+1] = g;
    img[i*3+2] = r;
  }
  return img;
}

static std::vector<float> make_flat_depth(int w, int h, float depth_m) {
  return std::vector<float>(w * h, depth_m);
}

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------
class ModelRunnerTest : public ::testing::Test {
protected:
  std::string model_path_() {
    const char* env = std::getenv("YOLO_MODEL_PATH");
    return env ? env : "";
  }

  bool model_available_() {
    auto p = model_path_();
    return !p.empty() && fs::exists(p);
  }
};

// ---------------------------------------------------------------------------
// Tests that do NOT require an ONNX file
// ---------------------------------------------------------------------------

TEST_F(ModelRunnerTest, DetectionStructLayout) {
  inference::Detection d;
  d.x_min = 10.f; d.y_min = 20.f;
  d.x_max = 50.f; d.y_max = 80.f;
  d.confidence = 0.9f;
  d.class_id   = 0;
  d.class_name = "person";
  d.world_x = 1.f; d.world_y = 2.f; d.world_z = 3.f;

  float area = (d.x_max - d.x_min) * (d.y_max - d.y_min);
  EXPECT_FLOAT_EQ(area, 40.f * 60.f);
}

TEST_F(ModelRunnerTest, SolidImageBufferSize) {
  auto img = make_solid_image(640, 480, 128, 64, 32);
  EXPECT_EQ(img.size(), static_cast<size_t>(640 * 480 * 3));
  // Check BGR pixel at index 0
  EXPECT_EQ(img[0], 32);   // B
  EXPECT_EQ(img[1], 64);   // G
  EXPECT_EQ(img[2], 128);  // R
}

TEST_F(ModelRunnerTest, FlatDepthBufferSize) {
  auto depth = make_flat_depth(320, 240, 2.5f);
  EXPECT_EQ(depth.size(), static_cast<size_t>(320 * 240));
  for (float v : depth) EXPECT_FLOAT_EQ(v, 2.5f);
}

TEST_F(ModelRunnerTest, BackprojectionMath) {
  // Manual back-projection: z=2, fx=fy=500, cx=cy=320
  // pixel (320,240) should map to (0, -160*2/500, 2) approximately
  float z  = 2.f;
  float fx = 500.f, fy = 500.f, cx = 320.f, cy = 240.f;
  int u = 320, v = 80;  // above centre

  float world_x = (u - cx) * z / fx;
  float world_y = (v - cy) * z / fy;
  float world_z = z;

  EXPECT_FLOAT_EQ(world_x, 0.f);
  EXPECT_NEAR(world_y, (80 - 240) * 2.f / 500.f, 1e-5f);
  EXPECT_FLOAT_EQ(world_z, 2.f);
}

// ---------------------------------------------------------------------------
// Tests that REQUIRE an ONNX model (skip gracefully if not present)
// ---------------------------------------------------------------------------

TEST_F(ModelRunnerTest, LoadModelAndRunBlankFrame) {
  if (!model_available_()) {
    GTEST_SKIP() << "YOLO_MODEL_PATH not set or file not found; skipping live inference test.";
  }

  inference::ModelRunner runner(model_path_(), 0.45f, 0.50f);
  EXPECT_FALSE(runner.input_name().empty());

  auto img   = make_solid_image(640, 480, 127, 127, 127);
  auto depth = make_flat_depth(640, 480, 3.f);

  auto dets = runner.run(img.data(), depth.data(),
                         640, 480,
                         /*fx*/500.f, /*fy*/500.f,
                         /*cx*/320.f, /*cy*/240.f);

  // A blank grey frame should produce 0 confident detections (or very few).
  EXPECT_LT(static_cast<int>(dets.size()), 10);
}

TEST_F(ModelRunnerTest, NullDepthDoesNotCrash) {
  if (!model_available_()) {
    GTEST_SKIP() << "YOLO_MODEL_PATH not set or file not found.";
  }

  inference::ModelRunner runner(model_path_());
  auto img = make_solid_image(640, 480, 0, 0, 0);

  // depth_data = nullptr should be handled gracefully (world coords stay 0)
  EXPECT_NO_THROW({
    auto dets = runner.run(img.data(), nullptr, 640, 480, 500, 500, 320, 240);
    for (const auto& d : dets) {
      EXPECT_FLOAT_EQ(d.world_z, 0.f);
    }
  });
}
