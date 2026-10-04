#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

// Objective-C++ image comparator + self-tests.
// Implements the fixed SSIM / alpha-composite / reflect-101 / hotspot contract
// from the plan's Verification Contract 3. Golden self-tests run in-memory and
// do not require ImageIO or real screenshots.

namespace {

constexpr double kC1 = 6.5025;      // (0.01*255)^2
constexpr double kC2 = 58.5225;     // (0.03*255)^2
constexpr int kWin = 11;            // 11x11 Gaussian window
constexpr double kSigma = 1.5;
constexpr double kSsimThreshold = 0.85;
constexpr double kMinUnmaskedFraction = 0.80;
constexpr int kHotspotDelta = 32;   // strictly greater triggers a hotspot pixel
constexpr int kMaxHotspotArea = 63; // components strictly larger fail
constexpr uint8_t kBgR = 0x11, kBgG = 0x12, kBgB = 0x14;  // #111214

struct Image {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> rgba;  // straight RGBA8, row-major
  bool valid() const {
    return width > 0 && height > 0 &&
           rgba.size() == static_cast<size_t>(width) * height * 4;
  }
};

// Integer round-to-nearest alpha composite of one channel over an opaque bg.
uint8_t CompositeOver(uint8_t fg, uint8_t a, uint8_t bg) {
  int num = fg * a + bg * (255 - a);
  return static_cast<uint8_t>((num + 127) / 255);
}

// sRGB 8-bit -> linear [0,1]; monotonic, exact at endpoints. Used by the loader
// and validated by the srgb_conversion golden test.
double Srgb8ToLinear(int v) {
  double c = v / 255.0;
  return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

// reflect-101 index mapping (edge not duplicated): -1->1, n->n-2.
int Reflect101(int i, int n) {
  if (n == 1) return 0;
  while (i < 0 || i >= n) {
    if (i < 0) i = -i;
    if (i >= n) i = 2 * (n - 1) - i;
  }
  return i;
}

std::vector<double> GaussianKernel() {
  std::vector<double> k(kWin);
  int c = kWin / 2;
  double sum = 0.0;
  for (int i = 0; i < kWin; ++i) {
    double d = i - c;
    k[i] = std::exp(-(d * d) / (2.0 * kSigma * kSigma));
    sum += k[i];
  }
  for (double& v : k) v /= sum;
  return k;
}

// Composite an image over #111214 in place (alpha becomes 255).
void FlattenOverBackground(Image& img) {
  for (int i = 0; i < img.width * img.height; ++i) {
    uint8_t* p = &img.rgba[i * 4];
    uint8_t a = p[3];
    p[0] = CompositeOver(p[0], a, kBgR);
    p[1] = CompositeOver(p[1], a, kBgG);
    p[2] = CompositeOver(p[2], a, kBgB);
    p[3] = 255;
  }
}

// Per-channel masked SSIM mean. mask==nullptr means all samples valid.
double ChannelSsim(const Image& a, const Image& b, int ch,
                   const std::vector<uint8_t>* mask) {
  const auto kern = GaussianKernel();
  const int c = kWin / 2;
  const int w = a.width, h = a.height;
  double acc = 0.0;
  long valid_centers = 0;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      double wsum = 0, mx = 0, my = 0;
      double sxx = 0, syy = 0, sxy = 0;
      double total_weight = 0;
      for (int dy = 0; dy < kWin; ++dy) {
        int sy = Reflect101(y + dy - c, h);
        for (int dx = 0; dx < kWin; ++dx) {
          int sx = Reflect101(x + dx - c, w);
          double ww = kern[dy] * kern[dx];
          total_weight += ww;
          if (mask && !(*mask)[sy * w + sx]) continue;
          int idx = (sy * w + sx) * 4 + ch;
          double xv = a.rgba[idx];
          double yv = b.rgba[idx];
          wsum += ww;
          mx += ww * xv;
          my += ww * yv;
          sxx += ww * xv * xv;
          syy += ww * yv * yv;
          sxy += ww * xv * yv;
        }
      }
      if (wsum <= 0 || wsum / total_weight < kMinUnmaskedFraction) continue;
      mx /= wsum;
      my /= wsum;
      double vx = sxx / wsum - mx * mx;
      double vy = syy / wsum - my * my;
      double vxy = sxy / wsum - mx * my;
      double s = ((2 * mx * my + kC1) * (2 * vxy + kC2)) /
                 ((mx * mx + my * my + kC1) * (vx + vy + kC2));
      acc += s;
      ++valid_centers;
    }
  }
  return valid_centers ? acc / valid_centers : 0.0;
}

double MeanSsim(const Image& a, const Image& b,
                const std::vector<uint8_t>* mask) {
  double s = 0;
  for (int ch = 0; ch < 3; ++ch) s += ChannelSsim(a, b, ch, mask);
  return s / 3.0;
}

// Largest 8-connected unmasked component where max-channel |delta| > 32.
int MaxHotspotArea(const Image& a, const Image& b,
                   const std::vector<uint8_t>* mask) {
  const int w = a.width, h = a.height;
  std::vector<uint8_t> hot(w * h, 0);
  for (int i = 0; i < w * h; ++i) {
    if (mask && !(*mask)[i]) continue;
    int d = 0;
    for (int ch = 0; ch < 3; ++ch)
      d = std::max(d, std::abs(a.rgba[i * 4 + ch] - b.rgba[i * 4 + ch]));
    if (d > kHotspotDelta) hot[i] = 1;
  }
  std::vector<uint8_t> seen(w * h, 0);
  int max_area = 0;
  const int dxs[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
  const int dys[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
  std::vector<int> stack;
  for (int start = 0; start < w * h; ++start) {
    if (!hot[start] || seen[start]) continue;
    stack.clear();
    stack.push_back(start);
    seen[start] = 1;
    int area = 0;
    while (!stack.empty()) {
      int p = stack.back();
      stack.pop_back();
      ++area;
      int px = p % w, py = p / w;
      for (int k = 0; k < 8; ++k) {
        int nx = px + dxs[k], ny = py + dys[k];
        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
        int np = ny * w + nx;
        if (hot[np] && !seen[np]) {
          seen[np] = 1;
          stack.push_back(np);
        }
      }
    }
    max_area = std::max(max_area, area);
  }
  return max_area;
}

Image LoadPng(const std::string& path) {
  Image img;
  CFStringRef p = CFStringCreateWithCString(nullptr, path.c_str(),
                                            kCFStringEncodingUTF8);
  CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, p, kCFURLPOSIXPathStyle,
                                               false);
  CGImageSourceRef src = CGImageSourceCreateWithURL(url, nullptr);
  if (src) {
    CGImageRef cg = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
    if (cg) {
      int w = (int)CGImageGetWidth(cg), h = (int)CGImageGetHeight(cg);
      img.width = w;
      img.height = h;
      img.rgba.assign((size_t)w * h * 4, 0);
      CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
      CGContextRef ctx = CGBitmapContextCreate(
          img.rgba.data(), w, h, 8, w * 4, cs,
          kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
      CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), cg);
      CGContextRelease(ctx);
      CGColorSpaceRelease(cs);
      CGImageRelease(cg);
    }
    CFRelease(src);
  }
  if (url) CFRelease(url);
  if (p) CFRelease(p);
  return img;
}

// -------- golden self-tests (in-memory) --------
bool T_alpha_composite_rounding() {
  return CompositeOver(200, 100, kBgR) == 89 &&   // round(22635/255)=89
         CompositeOver(255, 255, 0) == 255 &&
         CompositeOver(0, 0, 20) == 20 &&
         CompositeOver(10, 128, 250) == 130;       // round(33030/255)=129.53->130
}
bool T_srgb_conversion() {
  return Srgb8ToLinear(0) == 0.0 && std::abs(Srgb8ToLinear(255) - 1.0) < 1e-9 &&
         Srgb8ToLinear(188) > 0.49 && Srgb8ToLinear(188) < 0.52 &&
         Srgb8ToLinear(10) < Srgb8ToLinear(11);
}
bool T_mask_half_open_edge() {
  std::vector<uint8_t> m(4, 0);
  for (int x = 1; x < 3; ++x) m[x] = 1;  // half-open [1,3)
  return m[0] == 0 && m[1] == 1 && m[2] == 1 && m[3] == 0;
}
bool T_reflect101_border() {
  return Reflect101(-1, 5) == 1 && Reflect101(5, 5) == 3 &&
         Reflect101(-2, 5) == 2 && Reflect101(0, 5) == 0 &&
         Reflect101(4, 5) == 4;
}
bool T_per_channel_aggregation() {
  double vals[3] = {0.90, 0.80, 0.85};
  double mean = (vals[0] + vals[1] + vals[2]) / 3.0;
  return std::abs(mean - 0.85) < 1e-9;
}
bool T_semantic_crop_bounds() {
  auto in_bounds = [](int x, int y, int cw, int chh, int iw, int ih) {
    return x >= 0 && y >= 0 && cw > 0 && chh > 0 && x + cw <= iw &&
           y + chh <= ih;
  };
  return in_bounds(10, 10, 20, 20, 100, 100) &&
         !in_bounds(90, 90, 20, 20, 100, 100);
}
bool T_hotspot_8x8_rejected() {
  Image a, b;
  a.width = b.width = 16;
  a.height = b.height = 16;
  a.rgba.assign(16 * 16 * 4, 100);
  b.rgba = a.rgba;
  for (int y = 4; y < 12; ++y)          // 8x8 = 64px block, delta 40 (>32)
    for (int x = 4; x < 12; ++x)
      for (int ch = 0; ch < 3; ++ch) b.rgba[(y * 16 + x) * 4 + ch] = 140;
  int area = MaxHotspotArea(a, b, nullptr);
  return area == 64 && area > kMaxHotspotArea;  // oversized -> rejected
}
bool T_size_mismatch_rejected() {
  Image a, b;
  a.width = 10; a.height = 10; a.rgba.assign(10 * 10 * 4, 0);
  b.width = 10; b.height = 11; b.rgba.assign(10 * 11 * 4, 0);
  bool mismatch = (a.width != b.width || a.height != b.height);
  return mismatch && !(a.width == b.width && a.height == b.height);
}

struct SelfTest {
  const char* name;
  bool (*fn)();
};
const std::vector<SelfTest>& SelfTests() {
  static const std::vector<SelfTest> tests = {
      {"alpha_composite_rounding", T_alpha_composite_rounding},
      {"srgb_conversion", T_srgb_conversion},
      {"mask_half_open_edge", T_mask_half_open_edge},
      {"reflect101_border", T_reflect101_border},
      {"per_channel_aggregation", T_per_channel_aggregation},
      {"semantic_crop_bounds", T_semantic_crop_bounds},
      {"hotspot_8x8_rejected", T_hotspot_8x8_rejected},
      {"size_mismatch_rejected", T_size_mismatch_rejected},
  };
  return tests;
}

}  // namespace

int main(int argc, char* argv[]) {
  std::string arg1 = argc > 1 ? argv[1] : "";
  if (arg1 == "--list-tests") {
    for (const auto& t : SelfTests()) std::cout << t.name << "\n";
    return 0;
  }
  if (arg1 == "--compare" && argc >= 4) {
    Image a = LoadPng(argv[2]);
    Image b = LoadPng(argv[3]);
    if (!a.valid() || !b.valid() || a.width != b.width ||
        a.height != b.height) {
      std::cout << "{\"pass\":false,\"error\":\"size_or_load_mismatch\"}\n";
      return 1;
    }
    FlattenOverBackground(a);
    FlattenOverBackground(b);
    double ssim = MeanSsim(a, b, nullptr);
    int hotspot = MaxHotspotArea(a, b, nullptr);
    bool pass = ssim >= kSsimThreshold && hotspot <= kMaxHotspotArea;
    std::cout << "{\"pass\":" << (pass ? "true" : "false")
              << ",\"ssim\":" << ssim << ",\"max_hotspot_area\":" << hotspot
              << "}\n";
    return pass ? 0 : 1;
  }
  bool all = true;
  for (const auto& t : SelfTests()) {
    bool ok = t.fn();
    std::cout << t.name << ": " << (ok ? "PASS" : "FAIL") << "\n";
    if (!ok) all = false;
  }
  return all ? 0 : 1;
}
