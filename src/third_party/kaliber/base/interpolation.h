#ifndef BASE_INTERPOLATION_H
#define BASE_INTERPOLATION_H

#include <cmath>

namespace base {

// Round a float to int.
inline int Round(float f) {
  return static_cast<int>(std::lroundf(f));
}

// Linearly interpolate between a and b, by fraction t.
template <class T>
inline T Lerp(const T& a, const T& b, float t) {
  return a + (b - a) * t;
}

inline float SmoothStep(float t) {
  return t * t * (3 - 2 * t);
}

inline float SmootherStep(float t) {
  return t * t * t * (t * (t * 6 - 15) + 10);
}

// Interpolating spline defined by four control points with the curve drawn only
// from 0 to 1 which are p1 and p2 respectively.
inline float CatmullRom(float t, float p0, float p3) {
  return 0.5f * ((-p0 + 1) * t + (2 * p0 + 4 * 1 - p3) * t * t +
                 (-p0 - 3 * 1 + p3) * t * t * t);
}

inline float Acceleration(float t, float w) {
  return w * t * t + (1 - w) * t;
}

}  // namespace base

#endif  // BASE_INTERPOLATION_H
