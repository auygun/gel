#ifndef BASE_VECMATH_IMPL_H
#define BASE_VECMATH_IMPL_H

// This file is included at the bottom of vecmath.h.
// It contains helper macros, helper functions, and all method implementations.

//
// Helper macros.
//

#define _M_SET_ROW(row, k0, k1, k2, k3) \
  k[row][0] = k0;                       \
  k[row][1] = k1;                       \
  k[row][2] = k2;                       \
  k[row][3] = k3

#define _M_SET_ROW3x3(row, k0, k1, k2) \
  k[row][0] = k0;                      \
  k[row][1] = k1;                      \
  k[row][2] = k2

#define _M_SET_ROW_D(mat, row, k0, k1, k2, k3) \
  mat.k[row][0] = k0;                          \
  mat.k[row][1] = k1;                          \
  mat.k[row][2] = k2;                          \
  mat.k[row][3] = k3

#define _M_DET2x2(V, r0, r1, k0, k1) \
  Determinant2x2(V[r0][k0], V[r1][k0], V[r0][k1], V[r1][k1])

#define _M_DET3x3(V, r0, r1, r2, k0, k1, k2)                                  \
  ((V[r0][k0] * Determinant2x2(V[r1][k1], V[r2][k1], V[r1][k2], V[r2][k2])) - \
   (V[r1][k0] * Determinant2x2(V[r0][k1], V[r2][k1], V[r0][k2], V[r2][k2])) + \
   (V[r2][k0] * Determinant2x2(V[r0][k1], V[r1][k1], V[r0][k2], V[r1][k2])))

namespace base {

//
// Helper templates.
//

template <typename T>
T Sel(T cmp, T ge, T lt) {
  return (cmp < T(-0.0)) ? lt : ge;
}

template <typename T>
T Sqr(T v) {
  return v * v;
}

template <typename T>
T Length3(T a, T b, T c) {
  return (T)std::sqrt(Sqr(a) + Sqr(b) + Sqr(c));
}

template <typename T>
void RotateElements(T& e0, T& e1, T c, T s) {
  T tmp = e0 * c + e1 * s;
  e1 = (-e0 * s) + e1 * c;
  e0 = tmp;
}

template <typename T>
T Determinant2x2(T a, T b, T c, T d) {
  return ((a * d) - (b * c));
}

template <typename T>
T Determinant3x3(T a, T b, T c, T d, T e, T f, T g, T h, T i) {
  return ((a * Determinant2x2(e, f, h, i)) - (b * Determinant2x2(d, f, g, i)) +
          (c * Determinant2x2(d, e, g, h)));
}

template <typename T>
T Determinant4x4(T a,
                 T b,
                 T c,
                 T d,
                 T e,
                 T f,
                 T g,
                 T h,
                 T i,
                 T j,
                 T k,
                 T l,
                 T m,
                 T n,
                 T o,
                 T p) {
  return ((a * Determinant3x3(f, g, h, j, k, l, n, o, p)) -
          (b * Determinant3x3(e, g, h, i, k, l, m, o, p)) +
          (c * Determinant3x3(e, f, h, i, j, l, m, n, p)) -
          (d * Determinant3x3(e, f, g, i, j, k, m, n, o)));
}

// Get angle from 2D vector.
template <typename T>
T AngleFromVector(T x, T z) {
  T absx = std::abs(x);
  T absz = std::abs(z);

  if (absx > absz) {
    T v = std::atan(absz / absx) * ((T(1.0) / T(kPi)) * T(0.5));
    if (x > T(0)) {
      if (z < T(0))
        return -v;
      return v;
    } else {
      if (z < T(0))
        return v + T(0.5);
      return -v + T(0.5);
    }
  } else {
    T v = std::atan(absx / absz) * ((T(1.0) / T(kPi)) * T(0.5));
    if (z > T(0)) {
      if (x < T(0))
        return v + T(0.25);
      return -v + T(0.25);
    } else {
      if (x < T(0))
        return -v + T(0.75);
      return v + T(0.75);
    }
  }
}

template <typename T>
void CreateAngleZYXFromMatrix(Vector3<T>& v,
                              T e00,
                              T e01,
                              T e02,
                              T e10,
                              T e11,
                              T e12,
                              T e20,
                              T e21,
                              T e22) {
  T sb = -e20;
  T q1 = std::abs(e00) + std::abs(e10);
  T q2 = std::abs(e21) + std::abs(e22);
  if ((q1 < T(0.00001)) || (q2 < T(0.00001))) {
    if (sb <= T(0.0))
      v[1] = T(0.25);
    else
      v[1] = -T(0.25);
    v[2] = -AngleFromVector(e11, -e01);
    v[0] = 0;
  } else {
    T tmp = std::sqrt(std::abs(T(1.0) - Sqr(sb)));
    v[1] = -AngleFromVector(tmp, sb);
    v[2] = -AngleFromVector(e00, e10);
    v[0] = AngleFromVector(e22, -e21);
  }
}

//
// Vector2 implementation
//

template <typename T>
Vector2<T>::Vector2() {}

template <typename T>
Vector2<T>::Vector2(T v) {
  k[0] = k[1] = v;
}

template <typename T>
Vector2<T>::Vector2(T x, T y) {
  k[0] = x;
  k[1] = y;
}

template <typename T>
Vector2<T>& Vector2<T>::operator=(T s) {
  k[0] = k[1] = s;
  return *this;
}

template <typename T>
T& Vector2<T>::operator[](int i) {
  return k[i];
}

template <typename T>
const T& Vector2<T>::operator[](int i) const {
  return k[i];
}

template <typename T>
bool Vector2<T>::operator==(const Vector2& other) const {
  return k[0] == other.k[0] && k[1] == other.k[1];
}

template <typename T>
bool Vector2<T>::operator==(T v) const {
  return k[0] == v && k[1] == v;
}

template <typename T>
bool Vector2<T>::operator!=(const Vector2& other) const {
  return k[0] != other.k[0] || k[1] != other.k[1];
}

template <typename T>
bool Vector2<T>::operator!=(T v) const {
  return k[0] != v || k[1] != v;
}

template <typename T>
bool Vector2<T>::AlmostEqual(const Vector2& other, T epsilon) const {
  if (std::abs(k[0] - other.k[0]) > epsilon)
    return false;
  if (std::abs(k[1] - other.k[1]) > epsilon)
    return false;
  return true;
}

template <typename T>
Vector2<T> Vector2<T>::operator+(const Vector2& other) const {
  return Vector2(k[0] + other.k[0], k[1] + other.k[1]);
}

template <typename T>
Vector2<T> Vector2<T>::operator-(const Vector2& other) const {
  return Vector2(k[0] - other.k[0], k[1] - other.k[1]);
}

template <typename T>
Vector2<T> Vector2<T>::operator-() const {
  return Vector2(-k[0], -k[1]);
}

template <typename T>
Vector2<T> Vector2<T>::operator*(const Vector2& other) const {
  return Vector2(k[0] * other.k[0], k[1] * other.k[1]);
}

template <typename T>
Vector2<T> Vector2<T>::operator*(T scalar) const {
  return Vector2(k[0] * scalar, k[1] * scalar);
}

template <typename T>
Vector2<T> Vector2<T>::operator/(const Vector2& other) const {
  return Vector2(k[0] / other.k[0], k[1] / other.k[1]);
}

template <typename T>
Vector2<T> Vector2<T>::operator/(T scalar) const {
  return Vector2(k[0] / scalar, k[1] / scalar);
}

template <typename T>
void Vector2<T>::operator+=(const Vector2& other) {
  k[0] += other.k[0];
  k[1] += other.k[1];
}

template <typename T>
void Vector2<T>::operator-=(const Vector2& other) {
  k[0] -= other.k[0];
  k[1] -= other.k[1];
}

template <typename T>
void Vector2<T>::operator*=(const Vector2& other) {
  k[0] *= other.k[0];
  k[1] *= other.k[1];
}

template <typename T>
void Vector2<T>::operator*=(T v) {
  k[0] *= v;
  k[1] *= v;
}

template <typename T>
void Vector2<T>::operator/=(const Vector2& other) {
  k[0] /= other.k[0];
  k[1] /= other.k[1];
}

template <typename T>
void Vector2<T>::operator/=(T v) {
  k[0] /= v;
  k[1] /= v;
}

template <typename T>
T Vector2<T>::DotProduct(const Vector2& v) {
  return k[0] * v.k[0] + k[1] * v.k[1];
}

template <typename T>
T Vector2<T>::CrossProduct(const Vector2& v) {
  return k[0] * v.k[1] - k[1] * v.k[0];
}

template <typename T>
Vector2<T> Vector2<T>::Project(const Vector2& v) const {
  T vv = v.DotProduct(v);
  if (vv == T(0.0))
    return Vector2(0);
  T s = DotProduct(v) / vv;
  return v * s;
}

template <typename T>
Vector2<T> Vector2<T>::Reflect(const Vector2& n) const {
  return (*this) + (Project(n) * T(-2));
}

template <typename T>
T Vector2<T>::Length() const {
  return std::sqrt(Sqr(k[0]) + Sqr(k[1]));
}

template <typename T>
T Vector2<T>::LengthSqr() const {
  return Sqr(k[0]) + Sqr(k[1]);
}

template <typename T>
T Vector2<T>::Distance(const Vector2& other) const {
  return std::sqrt(Sqr(k[0] - other.k[0]) + Sqr(k[1] - other.k[1]));
}

template <typename T>
T Vector2<T>::DistanceSqr(const Vector2& other) const {
  return Sqr(k[0] - other.k[0]) + Sqr(k[1] - other.k[1]);
}

template <typename T>
Vector2<T>& Vector2<T>::Normalize() {
  T len = Length();
  k[0] /= len;
  k[1] /= len;
  return *this;
}

template <typename T>
Vector2<T>& Vector2<T>::SafeNormalize() {
  T len_sqr = LengthSqr();
  len_sqr = Sel(-len_sqr, T(1), len_sqr);  // Protect against zero-length vector.
  T len = std::sqrt(len_sqr);
  k[0] /= len;
  k[1] /= len;
  return *this;
}

template <typename T>
Vector2<T>& Vector2<T>::SetLength(T len) {
  T len_sqr = LengthSqr();
  len_sqr = Sel(-len_sqr, T(1), len_sqr);  // Protect against zero-length vector.
  T s = len / std::sqrt(len_sqr);
  k[0] *= s;
  k[1] *= s;
  return *this;
}

template <typename T>
Vector2<T>& Vector2<T>::SetMaxLength(T max_len) {
  if (LengthSqr() > Sqr(max_len))
    SetLength(max_len);
  return *this;
}

template <typename T>
const T* Vector2<T>::GetData() const {
  return &k[0];
}

template <typename T>
std::string Vector2<T>::ToString() {
  using namespace std::string_literals;
  return "("s + std::to_string(k[0]) + ", "s + std::to_string(k[1]) + ")"s;
}

//
// Vector3 implementation
//

template <typename T>
Vector3<T>::Vector3() {}

template <typename T>
Vector3<T>::Vector3(T v) {
  k[0] = k[1] = k[2] = v;
}

template <typename T>
Vector3<T>::Vector3(T x, T y, T z) {
  k[0] = x;
  k[1] = y;
  k[2] = z;
}

template <typename T>
Vector3<T>& Vector3<T>::operator=(T s) {
  k[0] = k[1] = k[2] = s;
  return *this;
}

template <typename T>
T& Vector3<T>::operator[](int i) {
  return k[i];
}

template <typename T>
const T& Vector3<T>::operator[](int i) const {
  return k[i];
}

template <typename T>
bool Vector3<T>::operator==(const Vector3& other) const {
  return k[0] == other.k[0] && k[1] == other.k[1] && k[2] == other.k[2];
}

template <typename T>
bool Vector3<T>::operator==(T v) const {
  return k[0] == v && k[1] == v && k[2] == v;
}

template <typename T>
bool Vector3<T>::operator!=(const Vector3& other) const {
  return k[0] != other.k[0] || k[1] != other.k[1] || k[2] != other.k[2];
}

template <typename T>
bool Vector3<T>::operator!=(T v) const {
  return k[0] != v || k[1] != v || k[2] != v;
}

template <typename T>
bool Vector3<T>::AlmostEqual(const Vector3& other, T epsilon) const {
  if (std::abs(k[0] - other.k[0]) > epsilon)
    return false;
  if (std::abs(k[1] - other.k[1]) > epsilon)
    return false;
  if (std::abs(k[2] - other.k[2]) > epsilon)
    return false;
  return true;
}

template <typename T>
Vector3<T> Vector3<T>::operator+(const Vector3& other) const {
  return Vector3(k[0] + other.k[0], k[1] + other.k[1], k[2] + other.k[2]);
}

template <typename T>
Vector3<T> Vector3<T>::operator-(const Vector3& other) const {
  return Vector3(k[0] - other.k[0], k[1] - other.k[1], k[2] - other.k[2]);
}

template <typename T>
Vector3<T> Vector3<T>::operator-() const {
  return Vector3(-k[0], -k[1], -k[2]);
}

template <typename T>
Vector3<T> Vector3<T>::operator*(const Vector3& other) const {
  return Vector3(k[0] * other.k[0], k[1] * other.k[1], k[2] * other.k[2]);
}

template <typename T>
Vector3<T> Vector3<T>::operator*(T scalar) const {
  return Vector3(k[0] * scalar, k[1] * scalar, k[2] * scalar);
}

template <typename T>
Vector3<T> Vector3<T>::operator*(const Matrix4<T>& mat) {
  Vector3 r;
  for (int i = 0; i < 3; i++)
    r.k[i] = mat.k[0][i] * k[0] + mat.k[1][i] * k[1] + mat.k[2][i] * k[2] +
             mat.k[3][i];
  return r;
}

template <typename T>
Vector3<T> Vector3<T>::operator/(const Vector3& other) const {
  return Vector3(k[0] / other.k[0], k[1] / other.k[1], k[2] / other.k[2]);
}

template <typename T>
Vector3<T> Vector3<T>::operator/(T scalar) const {
  return Vector3(k[0] / scalar, k[1] / scalar, k[2] / scalar);
}

template <typename T>
void Vector3<T>::operator+=(const Vector3& other) {
  k[0] += other.k[0];
  k[1] += other.k[1];
  k[2] += other.k[2];
}

template <typename T>
void Vector3<T>::operator-=(const Vector3& other) {
  k[0] -= other.k[0];
  k[1] -= other.k[1];
  k[2] -= other.k[2];
}

template <typename T>
void Vector3<T>::operator*=(const Vector3& other) {
  k[0] *= other.k[0];
  k[1] *= other.k[1];
  k[2] *= other.k[2];
}

template <typename T>
void Vector3<T>::operator*=(T v) {
  k[0] *= v;
  k[1] *= v;
  k[2] *= v;
}

template <typename T>
void Vector3<T>::operator*=(const Matrix4<T>& mat) {
  Vector3 r;
  for (int i = 0; i < 3; i++)
    r.k[i] = mat.k[0][i] * k[0] + mat.k[1][i] * k[1] + mat.k[2][i] * k[2] +
             mat.k[3][i];
  k[0] = r.k[0];
  k[1] = r.k[1];
  k[2] = r.k[2];
}

template <typename T>
void Vector3<T>::operator/=(const Vector3& other) {
  k[0] /= other.k[0];
  k[1] /= other.k[1];
  k[2] /= other.k[2];
}

template <typename T>
void Vector3<T>::operator/=(T v) {
  k[0] /= v;
  k[1] /= v;
  k[2] /= v;
}

template <typename T>
T Vector3<T>::DotProduct(const Vector3& other) const {
  return k[0] * other.k[0] + k[1] * other.k[1] + k[2] * other.k[2];
}

template <typename T>
Vector3<T> Vector3<T>::CrossProduct(const Vector3& other) const {
  return Vector3(k[1] * other.k[2] - k[2] * other.k[1],
                 -k[0] * other.k[2] + k[2] * other.k[0],
                 k[0] * other.k[1] - k[1] * other.k[0]);
}

template <typename T>
Vector3<T> Vector3<T>::Project(const Vector3& v) const {
  T vv = v.DotProduct(v);
  if (vv == T(0.0))
    return Vector3(0);
  T s = DotProduct(v) / vv;
  return v * s;
}

template <typename T>
Vector3<T> Vector3<T>::ProjectPlane(const Vector3& n) const {
  T nn = n.DotProduct(n);
  if (nn == T(0.0))
    return Vector3(0);
  T s = DotProduct(n) / nn;
  return *this - (n * s);
}

template <typename T>
Vector3<T> Vector3<T>::Reflect(const Vector3& n) const {
  return (*this) + (Project(n) * T(-2));
}

template <typename T>
T Vector3<T>::Length() const {
  return std::sqrt(Sqr(k[0]) + Sqr(k[1]) + Sqr(k[2]));
}

template <typename T>
T Vector3<T>::LengthSqr() const {
  return Sqr(k[0]) + Sqr(k[1]) + Sqr(k[2]);
}

template <typename T>
T Vector3<T>::Distance(const Vector3& other) const {
  return std::sqrt(Sqr(k[0] - other.k[0]) + Sqr(k[1] - other.k[1]) +
                   Sqr(k[2] - other.k[2]));
}

template <typename T>
T Vector3<T>::DistanceSqr(const Vector3& other) const {
  return Sqr(k[0] - other.k[0]) + Sqr(k[1] - other.k[1]) +
         Sqr(k[2] - other.k[2]);
}

template <typename T>
Vector3<T>& Vector3<T>::Normalize() {
  T len = Length();
  k[0] /= len;
  k[1] /= len;
  k[2] /= len;
  return *this;
}

template <typename T>
Vector3<T>& Vector3<T>::SafeNormalize() {
  T len_sqr = LengthSqr();
  len_sqr = Sel(-len_sqr, T(1), len_sqr);  // Protect against zero-length vector.
  T len = std::sqrt(len_sqr);
  k[0] /= len;
  k[1] /= len;
  k[2] /= len;
  return *this;
}

template <typename T>
Vector3<T>& Vector3<T>::SetLength(T len) {
  T len_sqr = LengthSqr();
  len_sqr = Sel(-len_sqr, T(1), len_sqr);  // Protect against zero-length vector.
  T s = len / std::sqrt(len_sqr);
  k[0] *= s;
  k[1] *= s;
  k[2] *= s;
  return *this;
}

template <typename T>
Vector3<T>& Vector3<T>::SetMaxLength(T max_len) {
  if (LengthSqr() > Sqr(max_len))
    SetLength(max_len);
  return *this;
}

template <typename T>
const T* Vector3<T>::GetData() const {
  return &k[0];
}

template <typename T>
std::string Vector3<T>::ToString() {
  using namespace std::string_literals;
  return "("s + std::to_string(k[0]) + ", "s + std::to_string(k[1]) + ", "s +
         std::to_string(k[2]) + ")"s;
}

//
// Vector4 implementation
//

template <typename T>
Vector4<T>::Vector4() {}

template <typename T>
Vector4<T>::Vector4(T v) {
  k[0] = k[1] = k[2] = k[3] = v;
}

template <typename T>
Vector4<T>::Vector4(T x, T y, T z, T w) {
  k[0] = x;
  k[1] = y;
  k[2] = z;
  k[3] = w;
}

template <typename T>
Vector4<T>& Vector4<T>::operator=(T s) {
  k[0] = k[1] = k[2] = k[3] = s;
  return *this;
}

template <typename T>
T& Vector4<T>::operator[](int i) {
  return k[i];
}

template <typename T>
const T& Vector4<T>::operator[](int i) const {
  return k[i];
}

template <typename T>
bool Vector4<T>::operator==(const Vector4& other) const {
  return k[0] == other.k[0] && k[1] == other.k[1] && k[2] == other.k[2] &&
         k[3] == other.k[3];
}

template <typename T>
bool Vector4<T>::operator==(T v) const {
  return k[0] == v && k[1] == v && k[2] == v && k[3] == v;
}

template <typename T>
bool Vector4<T>::operator!=(const Vector4& other) const {
  return k[0] != other.k[0] || k[1] != other.k[1] || k[2] != other.k[2] ||
         k[3] != other.k[3];
}

template <typename T>
bool Vector4<T>::operator!=(T v) const {
  return k[0] != v || k[1] != v || k[2] != v || k[3] != v;
}

template <typename T>
bool Vector4<T>::AlmostEqual(const Vector4& other, T epsilon) const {
  if (std::abs(k[0] - other.k[0]) > epsilon)
    return false;
  if (std::abs(k[1] - other.k[1]) > epsilon)
    return false;
  if (std::abs(k[2] - other.k[2]) > epsilon)
    return false;
  if (std::abs(k[3] - other.k[3]) > epsilon)
    return false;
  return true;
}

template <typename T>
Vector4<T> Vector4<T>::operator+(const Vector4& other) const {
  return Vector4(k[0] + other.k[0], k[1] + other.k[1], k[2] + other.k[2],
                 k[3] + other.k[3]);
}

template <typename T>
Vector4<T> Vector4<T>::operator-(const Vector4& other) const {
  return Vector4(k[0] - other.k[0], k[1] - other.k[1], k[2] - other.k[2],
                 k[3] - other.k[3]);
}

template <typename T>
Vector4<T> Vector4<T>::operator-() const {
  return Vector4(-k[0], -k[1], -k[2], -k[3]);
}

template <typename T>
Vector4<T> Vector4<T>::operator*(const Vector4& other) const {
  return Vector4(k[0] * other.k[0], k[1] * other.k[1], k[2] * other.k[2],
                 k[3] * other.k[3]);
}

template <typename T>
Vector4<T> Vector4<T>::operator*(T scalar) const {
  return Vector4(k[0] * scalar, k[1] * scalar, k[2] * scalar, k[3] * scalar);
}

template <typename T>
Vector4<T> Vector4<T>::operator*(const Matrix4<T>& mat) {
  Vector4 r;
  for (int i = 0; i < 3; i++)
    r.k[i] = mat.k[0][i] * k[0] + mat.k[1][i] * k[1] + mat.k[2][i] * k[2] +
             mat.k[3][i] * k[3];
  return r;
}

template <typename T>
Vector4<T> Vector4<T>::operator/(const Vector4& other) const {
  return Vector4(k[0] / other.k[0], k[1] / other.k[1], k[2] / other.k[2],
                 k[3] / other.k[3]);
}

template <typename T>
Vector4<T> Vector4<T>::operator/(T scalar) const {
  return Vector4(k[0] / scalar, k[1] / scalar, k[2] / scalar, k[3] / scalar);
}

template <typename T>
void Vector4<T>::operator+=(const Vector4& other) {
  k[0] += other.k[0];
  k[1] += other.k[1];
  k[2] += other.k[2];
  k[3] += other.k[3];
}

template <typename T>
void Vector4<T>::operator-=(const Vector4& other) {
  k[0] -= other.k[0];
  k[1] -= other.k[1];
  k[2] -= other.k[2];
  k[3] -= other.k[3];
}

template <typename T>
void Vector4<T>::operator*=(const Vector4& other) {
  k[0] *= other.k[0];
  k[1] *= other.k[1];
  k[2] *= other.k[2];
  k[3] *= other.k[3];
}

template <typename T>
void Vector4<T>::operator*=(T v) {
  k[0] *= v;
  k[1] *= v;
  k[2] *= v;
  k[3] *= v;
}

template <typename T>
void Vector4<T>::operator*=(const Matrix4<T>& mat) {
  Vector4 r;
  for (int i = 0; i < 3; i++)
    r.k[i] = mat.k[0][i] * k[0] + mat.k[1][i] * k[1] + mat.k[2][i] * k[2] +
             mat.k[3][i] * k[3];
  k[0] = r.k[0];
  k[1] = r.k[1];
  k[2] = r.k[2];
  k[3] = r.k[3];
}

template <typename T>
void Vector4<T>::operator/=(const Vector4& other) {
  k[0] /= other.k[0];
  k[1] /= other.k[1];
  k[2] /= other.k[2];
  k[3] /= other.k[3];
}

template <typename T>
void Vector4<T>::operator/=(T v) {
  k[0] /= v;
  k[1] /= v;
  k[2] /= v;
  k[3] /= v;
}

template <typename T>
T Vector4<T>::DotProduct(const Vector4& other) const {
  return k[0] * other.k[0] + k[1] * other.k[1] + k[2] * other.k[2] +
         k[3] * other.k[3];
}

template <typename T>
T Vector4<T>::Length() const {
  return std::sqrt(Sqr(k[0]) + Sqr(k[1]) + Sqr(k[2]) + Sqr(k[3]));
}

template <typename T>
T Vector4<T>::LengthSqr() const {
  return Sqr(k[0]) + Sqr(k[1]) + Sqr(k[2]) + Sqr(k[3]);
}

template <typename T>
T Vector4<T>::Distance(const Vector4& other) const {
  return std::sqrt(Sqr(k[0] - other.k[0]) + Sqr(k[1] - other.k[1]) +
                   Sqr(k[2] - other.k[2]) + Sqr(k[3] - other.k[3]));
}

template <typename T>
T Vector4<T>::DistanceSqr(const Vector4& other) const {
  return Sqr(k[0] - other.k[0]) + Sqr(k[1] - other.k[1]) +
         Sqr(k[2] - other.k[2]) + Sqr(k[3] - other.k[3]);
}

template <typename T>
Vector4<T>& Vector4<T>::Normalize() {
  T len = Length();
  k[0] /= len;
  k[1] /= len;
  k[2] /= len;
  k[3] /= len;
  return *this;
}

template <typename T>
Vector4<T>& Vector4<T>::SafeNormalize() {
  T len_sqr = LengthSqr();
  len_sqr = Sel(-len_sqr, T(1), len_sqr);  // Protect against zero-length vector.
  T len = std::sqrt(len_sqr);
  k[0] /= len;
  k[1] /= len;
  k[2] /= len;
  k[3] /= len;
  return *this;
}

template <typename T>
Vector4<T>& Vector4<T>::SetLength(T len) {
  T len_sqr = LengthSqr();
  len_sqr = Sel(-len_sqr, T(1), len_sqr);  // Protect against zero-length vector.
  T s = len / std::sqrt(len_sqr);
  k[0] *= s;
  k[1] *= s;
  k[2] *= s;
  k[3] *= s;
  return *this;
}

template <typename T>
Vector4<T>& Vector4<T>::SetMaxLength(T max_len) {
  if (LengthSqr() > Sqr(max_len))
    SetLength(max_len);
  return *this;
}

template <typename T>
const T* Vector4<T>::GetData() const {
  return &k[0];
}

template <typename T>
std::string Vector4<T>::ToString() {
  using namespace std::string_literals;
  return "("s + std::to_string(k[0]) + ", "s + std::to_string(k[1]) + ", "s +
         std::to_string(k[2]) + ", "s + std::to_string(k[3]) + ")"s;
}

//
// Matrix4 implementation
//

template <typename T>
Matrix4<T>::Matrix4() {}

template <typename T>
Matrix4<T>::Matrix4(T s)
    : k{
          {s, 0, 0, 0},
          {0, s, 0, 0},
          {0, 0, s, 0},
          {0, 0, 0, s},
      } {}

template <typename T>
Matrix4<T>::Matrix4(T v00,
                    T v01,
                    T v02,
                    T v03,
                    T v10,
                    T v11,
                    T v12,
                    T v13,
                    T v20,
                    T v21,
                    T v22,
                    T v23,
                    T v30,
                    T v31,
                    T v32,
                    T v33)
    : k{
          {v00, v01, v02, v03},
          {v10, v11, v12, v13},
          {v20, v21, v22, v23},
          {v30, v31, v32, v33},
      } {}

template <typename T>
Matrix4<T>& Matrix4<T>::operator=(T s) {
  _M_SET_ROW(0, s, 0, 0, 0);
  _M_SET_ROW(1, 0, s, 0, 0);
  _M_SET_ROW(2, 0, 0, s, 0);
  _M_SET_ROW(3, 0, 0, 0, s);
  return *this;
}

template <typename T>
void Matrix4<T>::Unit() {
  _M_SET_ROW(0, 1, 0, 0, 0);
  _M_SET_ROW(1, 0, 1, 0, 0);
  _M_SET_ROW(2, 0, 0, 1, 0);
  _M_SET_ROW(3, 0, 0, 0, 1);
}

template <typename T>
void Matrix4<T>::Unit3x3() {
  k[0][0] = T(1);
  k[1][0] = T(0);
  k[2][0] = T(0);
  k[0][1] = T(0);
  k[1][1] = T(1);
  k[2][1] = T(0);
  k[0][2] = T(0);
  k[1][2] = T(0);
  k[2][2] = T(1);
}

template <typename T>
void Matrix4<T>::UnitNot3x3() {
  k[0][3] = T(0);
  k[1][3] = T(0);
  k[2][3] = T(0);
  k[3][3] = T(1);
  k[3][2] = T(0);
  k[3][1] = T(0);
  k[3][0] = T(0);
}

template <typename T>
void Matrix4<T>::Transpose() {
  std::swap(k[0][1], k[1][0]);
  std::swap(k[0][2], k[2][0]);
  std::swap(k[0][3], k[3][0]);
  std::swap(k[1][2], k[2][1]);
  std::swap(k[1][3], k[3][1]);
  std::swap(k[2][3], k[3][2]);
}

template <typename T>
void Matrix4<T>::Transpose(Matrix4& dst) const {
  _M_SET_ROW_D(dst, 0, (k[0][0]), (k[1][0]), (k[2][0]), (k[3][0]));
  _M_SET_ROW_D(dst, 1, (k[0][1]), (k[1][1]), (k[2][1]), (k[3][1]));
  _M_SET_ROW_D(dst, 2, (k[0][2]), (k[1][2]), (k[2][2]), (k[3][2]));
  _M_SET_ROW_D(dst, 3, (k[0][3]), (k[1][3]), (k[2][3]), (k[3][3]));
}

template <typename T>
void Matrix4<T>::Transpose3x3() {
  std::swap(k[0][1], k[1][0]);
  std::swap(k[0][2], k[2][0]);
  std::swap(k[1][2], k[2][1]);
}

template <typename T>
void Matrix4<T>::Transpose3x3(Matrix4& dst) const {
  dst.k[0][0] = k[0][0];
  dst.k[1][1] = k[1][1];
  dst.k[2][2] = k[2][2];
  dst.k[0][1] = k[1][0];
  dst.k[0][2] = k[2][0];
  dst.k[1][2] = k[2][1];
  dst.k[1][0] = k[0][1];
  dst.k[2][0] = k[0][2];
  dst.k[2][1] = k[1][2];
}

template <typename T>
bool Matrix4<T>::Inverse() {
  T d = Determinant4x4((k[0][0]), (k[0][1]), (k[0][2]), (k[0][3]), (k[1][0]),
                       (k[1][1]), (k[1][2]), (k[1][3]), (k[2][0]), (k[2][1]),
                       (k[2][2]), (k[2][3]), (k[3][0]), (k[3][1]), (k[3][2]),
                       (k[3][3]));
  if (d == T(0.0)) {
    Unit();
    return false;
  }

  T d_inv = T(1.0) / d;
  T k00 = d_inv * _M_DET3x3(k, 1, 2, 3, 1, 2, 3);
  T k01 = -d_inv * _M_DET3x3(k, 0, 2, 3, 1, 2, 3);
  T k02 = d_inv * _M_DET3x3(k, 0, 1, 3, 1, 2, 3);
  T k03 = -d_inv * _M_DET3x3(k, 0, 1, 2, 1, 2, 3);
  T k10 = -d_inv * _M_DET3x3(k, 1, 2, 3, 0, 2, 3);
  T k11 = d_inv * _M_DET3x3(k, 0, 2, 3, 0, 2, 3);
  T k12 = -d_inv * _M_DET3x3(k, 0, 1, 3, 0, 2, 3);
  T k13 = d_inv * _M_DET3x3(k, 0, 1, 2, 0, 2, 3);
  T k20 = d_inv * _M_DET3x3(k, 1, 2, 3, 0, 1, 3);
  T k21 = -d_inv * _M_DET3x3(k, 0, 2, 3, 0, 1, 3);
  T k22 = d_inv * _M_DET3x3(k, 0, 1, 3, 0, 1, 3);
  T k23 = -d_inv * _M_DET3x3(k, 0, 1, 2, 0, 1, 3);
  T k30 = -d_inv * _M_DET3x3(k, 1, 2, 3, 0, 1, 2);
  T k31 = d_inv * _M_DET3x3(k, 0, 2, 3, 0, 1, 2);
  T k32 = -d_inv * _M_DET3x3(k, 0, 1, 3, 0, 1, 2);
  T k33 = d_inv * _M_DET3x3(k, 0, 1, 2, 0, 1, 2);

  _M_SET_ROW(0, k00, k01, k02, k03);
  _M_SET_ROW(1, k10, k11, k12, k13);
  _M_SET_ROW(2, k20, k21, k22, k23);
  _M_SET_ROW(3, k30, k31, k32, k33);
  return true;
}

template <typename T>
bool Matrix4<T>::Inverse(Matrix4& dst) const {
  T d = Determinant4x4((k[0][0]), (k[0][1]), (k[0][2]), (k[0][3]), (k[1][0]),
                       (k[1][1]), (k[1][2]), (k[1][3]), (k[2][0]), (k[2][1]),
                       (k[2][2]), (k[2][3]), (k[3][0]), (k[3][1]), (k[3][2]),
                       (k[3][3]));
  if (d == T(0.0)) {
    dst.Unit();
    return false;
  }

  T d_inv = T(1.0) / d;
  dst.k[0][0] = d_inv * _M_DET3x3(k, 1, 2, 3, 1, 2, 3);
  dst.k[0][1] = -d_inv * _M_DET3x3(k, 0, 2, 3, 1, 2, 3);
  dst.k[0][2] = d_inv * _M_DET3x3(k, 0, 1, 3, 1, 2, 3);
  dst.k[0][3] = -d_inv * _M_DET3x3(k, 0, 1, 2, 1, 2, 3);
  dst.k[1][0] = -d_inv * _M_DET3x3(k, 1, 2, 3, 0, 2, 3);
  dst.k[1][1] = d_inv * _M_DET3x3(k, 0, 2, 3, 0, 2, 3);
  dst.k[1][2] = -d_inv * _M_DET3x3(k, 0, 1, 3, 0, 2, 3);
  dst.k[1][3] = d_inv * _M_DET3x3(k, 0, 1, 2, 0, 2, 3);
  dst.k[2][0] = d_inv * _M_DET3x3(k, 1, 2, 3, 0, 1, 3);
  dst.k[2][1] = -d_inv * _M_DET3x3(k, 0, 2, 3, 0, 1, 3);
  dst.k[2][2] = d_inv * _M_DET3x3(k, 0, 1, 3, 0, 1, 3);
  dst.k[2][3] = -d_inv * _M_DET3x3(k, 0, 1, 2, 0, 1, 3);
  dst.k[3][0] = -d_inv * _M_DET3x3(k, 1, 2, 3, 0, 1, 2);
  dst.k[3][1] = d_inv * _M_DET3x3(k, 0, 2, 3, 0, 1, 2);
  dst.k[3][2] = -d_inv * _M_DET3x3(k, 0, 1, 3, 0, 1, 2);
  dst.k[3][3] = d_inv * _M_DET3x3(k, 0, 1, 2, 0, 1, 2);
  return true;
}

template <typename T>
bool Matrix4<T>::Inverse3x3() {
  T d = Determinant3x3((k[0][0]), (k[0][1]), (k[0][2]), (k[1][0]), (k[1][1]),
                       (k[1][2]), (k[2][0]), (k[2][1]), (k[2][2]));
  if (d == T(0.0)) {
    Unit3x3();
    return false;
  }

  T d_inv = T(1.0) / d;
  T k00 = d_inv * _M_DET2x2(k, 1, 2, 1, 2);
  T k01 = -d_inv * _M_DET2x2(k, 0, 2, 1, 2);
  T k02 = d_inv * _M_DET2x2(k, 0, 1, 1, 2);
  T k10 = -d_inv * _M_DET2x2(k, 1, 2, 0, 2);
  T k11 = d_inv * _M_DET2x2(k, 0, 2, 0, 2);
  T k12 = -d_inv * _M_DET2x2(k, 0, 1, 0, 2);
  T k20 = d_inv * _M_DET2x2(k, 1, 2, 0, 1);
  T k21 = -d_inv * _M_DET2x2(k, 0, 2, 0, 1);
  T k22 = d_inv * _M_DET2x2(k, 0, 1, 0, 1);

  k[0][0] = k00;
  k[0][1] = k01;
  k[0][2] = k02;
  k[1][0] = k10;
  k[1][1] = k11;
  k[1][2] = k12;
  k[2][0] = k20;
  k[2][1] = k21;
  k[2][2] = k22;
  return true;
}

template <typename T>
bool Matrix4<T>::Inverse3x3(Matrix4& dst) const {
  T d = Determinant3x3((k[0][0]), (k[0][1]), (k[0][2]), (k[1][0]), (k[1][1]),
                       (k[1][2]), (k[2][0]), (k[2][1]), (k[2][2]));
  if (d == T(0.0)) {
    dst.Unit3x3();
    return false;
  }

  T d_inv = T(1.0) / d;
  dst.k[0][0] = d_inv * _M_DET2x2(k, 1, 2, 1, 2);
  dst.k[0][1] = -d_inv * _M_DET2x2(k, 0, 2, 1, 2);
  dst.k[0][2] = d_inv * _M_DET2x2(k, 0, 1, 1, 2);
  dst.k[1][0] = -d_inv * _M_DET2x2(k, 1, 2, 0, 2);
  dst.k[1][1] = d_inv * _M_DET2x2(k, 0, 2, 0, 2);
  dst.k[1][2] = -d_inv * _M_DET2x2(k, 0, 1, 0, 2);
  dst.k[2][0] = d_inv * _M_DET2x2(k, 1, 2, 0, 1);
  dst.k[2][1] = -d_inv * _M_DET2x2(k, 0, 2, 0, 1);
  dst.k[2][2] = d_inv * _M_DET2x2(k, 0, 1, 0, 1);
  return true;
}

template <typename T>
void Matrix4<T>::InverseOrthogonal() {
  T k00 = k[0][0];
  T k11 = k[1][1];
  T k22 = k[2][2];
  T k10 = k[1][0];
  T k20 = k[2][0];
  T k21 = k[2][1];
  T k01 = k[0][1];
  T k02 = k[0][2];
  T k12 = k[1][2];
  T k30 = k[3][0];
  T k31 = k[3][1];
  T k32 = k[3][2];

  // (T*R)^-1 = (R^-1)*(T^-1) = transpose(R)*(-T)
  k[0][0] = k00;
  k[1][1] = k11;
  k[2][2] = k22;
  k[0][1] = k10;
  k[0][2] = k20;
  k[1][2] = k21;
  k[1][0] = k01;
  k[2][0] = k02;
  k[2][1] = k12;
  k[3][0] = -(k30 * k00 + k31 * k01 + k32 * k02);
  k[3][1] = -(k30 * k10 + k31 * k11 + k32 * k12);
  k[3][2] = -(k30 * k20 + k31 * k21 + k32 * k22);
  k[3][3] = 1;
  k[0][3] = 0;
  k[1][3] = 0;
  k[2][3] = 0;
}

template <typename T>
void Matrix4<T>::InverseOrthogonal(Matrix4& dst) const {
  dst.k[0][0] = k[0][0];
  dst.k[1][1] = k[1][1];
  dst.k[2][2] = k[2][2];
  dst.k[0][1] = k[1][0];
  dst.k[0][2] = k[2][0];
  dst.k[1][2] = k[2][1];
  dst.k[1][0] = k[0][1];
  dst.k[2][0] = k[0][2];
  dst.k[2][1] = k[1][2];
  dst.k[3][0] = -(k[3][0] * k[0][0] + k[3][1] * k[0][1] + k[3][2] * k[0][2]);
  dst.k[3][1] = -(k[3][0] * k[1][0] + k[3][1] * k[1][1] + k[3][2] * k[1][2]);
  dst.k[3][2] = -(k[3][0] * k[2][0] + k[3][1] * k[2][1] + k[3][2] * k[2][2]);
  dst.k[3][3] = 1;
  dst.k[0][3] = 0;
  dst.k[1][3] = 0;
  dst.k[2][3] = 0;
}

template <typename T>
void Matrix4<T>::Multiply(T s) {
  for (int row = 0; row < 4; row++)
    for (int col = 0; col < 4; col++)
      k[row][col] *= s;
}

template <typename T>
void Matrix4<T>::Multiply(T s, Matrix4& dst) {
  for (int row = 0; row < 4; row++)
    for (int col = 0; col < 4; col++)
      dst.k[row][col] *= s;
}

template <typename T>
void Matrix4<T>::Multiply3x3(T s) {
  for (int row = 0; row < 3; row++)
    for (int col = 0; col < 3; col++)
      k[row][col] *= s;
}

template <typename T>
void Matrix4<T>::Multiply3x3(T s, Matrix4& dst) {
  for (int row = 0; row < 3; row++)
    for (int col = 0; col < 3; col++)
      dst.k[row][col] *= s;
}

template <typename T>
void Matrix4<T>::Multiply(const Matrix4& m, Matrix4& dst) const {
  for (int row = 0; row < 4; row++)
    for (int col = 0; col < 4; col++)
      dst.k[row][col] = (k[row][0] * m.k[0][col]) +
                        (k[row][1] * m.k[1][col]) +
                        (k[row][2] * m.k[2][col]) + (k[row][3] * m.k[3][col]);
}

template <typename T>
void Matrix4<T>::Multiply3x3(const Matrix4& m, Matrix4& dst) const {
  for (int row = 0; row < 3; row++)
    for (int col = 0; col < 3; col++)
      dst.k[row][col] = (k[row][0] * m.k[0][col]) +
                        (k[row][1] * m.k[1][col]) + (k[row][2] * m.k[2][col]);
}

template <typename T>
void Matrix4<T>::Normalize3x3() {
  T len = std::max(std::max(Length3(k[0][0], k[1][0], k[2][0]),
                            Length3(k[0][1], k[1][1], k[2][1])),
                   Length3(k[0][2], k[1][2], k[2][2]));

  if (len != 0) {
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++)
        k[i][j] = k[i][j] / len;
  }
}

template <typename T>
void Matrix4<T>::NormalizeRows3x3() {
  for (int i = 0; i < 3; i++) {
    T len_sqr = Sqr(k[i][0]) + Sqr(k[i][1]) + Sqr(k[i][2]);
    if (len_sqr != 0) {
      T len = std::sqrt(len_sqr);
      k[i][0] = k[i][0] / len;
      k[i][1] = k[i][1] / len;
      k[i][2] = k[i][2] / len;
    } else {
      k[i][0] = k[i][1] = k[i][2] = 0;
      k[i][i] = 1;
    }
  }
}

template <typename T>
void Matrix4<T>::Create(const Quaternion<T>& rotation,
                        const Vector3<T>& translation) {
  rotation.CreateMatrix3x3(*this);
  k[0][3] = T(0);
  k[1][3] = T(0);
  k[2][3] = T(0);
  k[3][3] = T(1);
  Row(3) = translation;
}

template <typename T>
void Matrix4<T>::CreateLookAt(const Vector3<T>& from,
                              const Vector3<T>& to,
                              const Vector3<T>& up) {
  Row(2) = to - from;
  Row(1) = up;
  RecreateMatrix<2, 1>();
  UnitNot3x3();
  Row(3) = from;
}

template <typename T>
void Matrix4<T>::CreateOrthographicProjection(T left,
                                              T right,
                                              T bottom,
                                              T top) {
  T rml = right - left;
  T rpl = right + left;
  T tmb = top - bottom;
  T tpb = top + bottom;
  _M_SET_ROW(0, T(2) / rml, 0, 0, 0);
  _M_SET_ROW(1, 0, T(2) / tmb, 0, 0);
  _M_SET_ROW(2, 0, 0, T(-1), 0);
  _M_SET_ROW(3, -rpl / rml, -tpb / tmb, 0, 1);
}

template <typename T>
void Matrix4<T>::CreatePerspectiveProjection(T fov,
                                             T fov_aspect,
                                             T width,
                                             T height,
                                             T near_plane,
                                             T far_plane) {
  T scale =
      T(2.0) /
      std::sqrt(Sqr(T(1.0) / std::cos((T(kPi) * fov) / T(360.0))) - T(1.0));
  T y_scale = scale * fov_aspect / (width / height);
  T x_scale = y_scale / (width / height);
  _M_SET_ROW(0, x_scale / T(2.0), 0, 0, 0);
  _M_SET_ROW(1, 0, (-y_scale) / T(2.0), 0, 0);
  _M_SET_ROW(2, 0, 0, far_plane / (far_plane - near_plane), 1);
  _M_SET_ROW(3, 0, 0, -near_plane * far_plane / (far_plane - near_plane), 0);
}

template <typename T>
void Matrix4<T>::CreateTranslation(const Vector3<T>& t) {
  _M_SET_ROW(0, 1, 0, 0, 0);
  _M_SET_ROW(1, 0, 1, 0, 0);
  _M_SET_ROW(2, 0, 0, 1, 0);
  _M_SET_ROW(3, t[0], t[1], t[2], 1);
}

template <typename T>
void Matrix4<T>::CreateAxisRotation(const Vector3<T>& rotation_axis, T angle) {
  // rotation_axis must be normalized.
  // Angle is in turns, i.e. degrees / 360 (0..1 range).
  T x = rotation_axis[0];
  T y = rotation_axis[1];
  T z = rotation_axis[2];

  T a = Sqr(x);
  T b = Sqr(y);
  T c = Sqr(z);
  T cs = std::cos(angle * T(2.0) * T(k2Pi));
  T sn = std::sin(angle * T(2.0) * T(k2Pi));
  T z_sn = z * sn;
  T x_sn = x * sn;
  T y_sn = y * sn;
  T xz_cs = x * z * (T(1.0) - cs);
  T xy_cs = x * y * (T(1.0) - cs);
  T yz_cs = y * z * (T(1.0) - cs);

  k[0][0] = a + cs * (T(1.0) - a);
  k[0][1] = xy_cs - z_sn;
  k[0][2] = y_sn + xz_cs;
  k[1][0] = z_sn + xy_cs;
  k[1][1] = b + cs * (T(1.0) - b);
  k[1][2] = yz_cs - x_sn;
  k[2][0] = xz_cs - y_sn;
  k[2][1] = x_sn + yz_cs;
  k[2][2] = c + cs * (T(1.0) - c);

  UnitNot3x3();
}

template <typename T>
void Matrix4<T>::CreateFromAngles(const Vector3<T>& angles,
                                  int angle_priority) {
  // Rotation priorities:
  // 0: [mat] = [z]*[y]*[x]
  // 1: [mat] = [z]*[x]*[y]
  // 2: [mat] = [y]*[z]*[x]
  // 3: [mat] = [y]*[x]*[z]
  // 4: [mat] = [x]*[z]*[y]
  // 5: [mat] = [x]*[y]*[z]
  Unit();
  switch (angle_priority) {
    case 5:
      M_x_RotZ(angles[2]);
      M_x_RotY(angles[1]);
      M_x_RotX(angles[0]);
      break;
    case 3:
      M_x_RotZ(angles[2]);
      M_x_RotX(angles[0]);
      M_x_RotY(angles[1]);
      break;
    case 4:
      M_x_RotY(angles[1]);
      M_x_RotZ(angles[2]);
      M_x_RotX(angles[0]);
      break;
    case 1:
      M_x_RotY(angles[1]);
      M_x_RotX(angles[0]);
      M_x_RotZ(angles[2]);
      break;
    case 2:
      M_x_RotX(angles[0]);
      M_x_RotZ(angles[2]);
      M_x_RotY(angles[1]);
      break;
    case 0:
      M_x_RotX(angles[0]);
      M_x_RotY(angles[1]);
      M_x_RotZ(angles[2]);
      break;
    default:
      NOTREACHED();
  }
}

template <typename T>
void Matrix4<T>::CreateXRotation(T v) {
  T s = (T)std::sin(v * T(k2Pi));
  T c = (T)std::cos(v * T(k2Pi));
  _M_SET_ROW(0, 1, 0, 0, 0);
  _M_SET_ROW(1, 0, c, s, 0);
  _M_SET_ROW(2, 0, -s, c, 0);
  _M_SET_ROW(3, 0, 0, 0, 1);
}

template <typename T>
void Matrix4<T>::CreateYRotation(T v) {
  T s = (T)std::sin(v * T(k2Pi));
  T c = (T)std::cos(v * T(k2Pi));
  _M_SET_ROW(0, c, 0, -s, 0);
  _M_SET_ROW(1, 0, 1, 0, 0);
  _M_SET_ROW(2, s, 0, c, 0);
  _M_SET_ROW(3, 0, 0, 0, 1);
}

template <typename T>
void Matrix4<T>::CreateZRotation(T v) {
  T s = (T)std::sin(v * T(k2Pi));
  T c = (T)std::cos(v * T(k2Pi));
  _M_SET_ROW(0, c, s, 0, 0);
  _M_SET_ROW(1, -s, c, 0, 0);
  _M_SET_ROW(2, 0, 0, 1, 0);
  _M_SET_ROW(3, 0, 0, 0, 1);
}

template <typename T>
void Matrix4<T>::CreateXRotation3x3(T v) {
  T s = (T)std::sin(v * T(k2Pi));
  T c = (T)std::cos(v * T(k2Pi));
  _M_SET_ROW3x3(0, 1, 0, 0);
  _M_SET_ROW3x3(1, 0, c, s);
  _M_SET_ROW3x3(2, 0, -s, c);
}

template <typename T>
void Matrix4<T>::CreateYRotation3x3(T v) {
  T s = (T)std::sin(v * T(k2Pi));
  T c = (T)std::cos(v * T(k2Pi));
  _M_SET_ROW3x3(0, c, 0, -s);
  _M_SET_ROW3x3(1, 0, 1, 0);
  _M_SET_ROW3x3(2, s, 0, c);
}

template <typename T>
void Matrix4<T>::CreateZRotation3x3(T v) {
  T s = (T)std::sin(v * T(k2Pi));
  T c = (T)std::cos(v * T(k2Pi));
  _M_SET_ROW3x3(0, c, s, 0);
  _M_SET_ROW3x3(1, -s, c, 0);
  _M_SET_ROW3x3(2, 0, 0, 1);
}

template <typename T>
void Matrix4<T>::M_x_RotX(T v) {
  T v2pi = v * T(k2Pi);
  T sn = (T)std::sin(v2pi);
  T cs = (T)std::cos(v2pi);
  RotateElements(k[0][1], k[0][2], cs, sn);
  RotateElements(k[1][1], k[1][2], cs, sn);
  RotateElements(k[2][1], k[2][2], cs, sn);
}

template <typename T>
void Matrix4<T>::M_x_RotY(T v) {
  T v2pi = v * T(k2Pi);
  T sn = (T)-std::sin(v2pi);
  T cs = (T)std::cos(v2pi);
  RotateElements(k[0][0], k[0][2], cs, sn);
  RotateElements(k[1][0], k[1][2], cs, sn);
  RotateElements(k[2][0], k[2][2], cs, sn);
}

template <typename T>
void Matrix4<T>::M_x_RotZ(T v) {
  T v2pi = v * T(k2Pi);
  T sn = (T)std::sin(v2pi);
  T cs = (T)std::cos(v2pi);
  RotateElements(k[0][0], k[0][1], cs, sn);
  RotateElements(k[1][0], k[1][1], cs, sn);
  RotateElements(k[2][0], k[2][1], cs, sn);
}

template <typename T>
void Matrix4<T>::RotX_x_M(T v) {
  T v2pi = v * T(k2Pi);
  T sn = (T)std::sin(v2pi);
  T cs = (T)std::cos(v2pi);
  RotateElements(k[1][0], k[2][0], cs, sn);
  RotateElements(k[1][1], k[2][1], cs, sn);
  RotateElements(k[1][2], k[2][2], cs, sn);
}

template <typename T>
void Matrix4<T>::RotY_x_M(T v) {
  T v2pi = v * T(k2Pi);
  T sn = (T)-std::sin(v2pi);
  T cs = (T)std::cos(v2pi);
  RotateElements(k[0][0], k[2][0], cs, sn);
  RotateElements(k[0][1], k[2][1], cs, sn);
  RotateElements(k[0][2], k[2][2], cs, sn);
}

template <typename T>
void Matrix4<T>::RotZ_x_M(T v) {
  T v2pi = v * T(k2Pi);
  T sn = (T)std::sin(v2pi);
  T cs = (T)std::cos(v2pi);
  RotateElements(k[0][0], k[1][0], cs, sn);
  RotateElements(k[0][1], k[1][1], cs, sn);
  RotateElements(k[0][2], k[1][2], cs, sn);
}

template <typename T>
template <int priority0, int priority1>
void Matrix4<T>::RecreateMatrix() {
  // Normalize the priority0 row, and use it together with the priority1 row
  // to create an orthogonal matrix. The third row is ignored.
  DCHECK(priority0 >= 0 && priority1 <= 2);
  int missing = 3 - (priority0 + priority1);
  constexpr int type = priority0 - priority1;
  if constexpr (type == -1 || type == 2) {
    Row(priority0).Normalize();
    Row(missing) = -(Row(priority1).CrossProduct(Row(priority0))).Normalize();
    Row(priority1) = Row(missing).CrossProduct(Row(priority0));
  } else {
    Row(priority0).Normalize();
    Row(missing) = (Row(priority1).CrossProduct(Row(priority0))).Normalize();
    Row(priority1) = Row(priority0).CrossProduct(Row(missing));
  }
}

template <typename T>
Vector3<T> Matrix4<T>::GetAngles(int angle_priority) const {
  Vector3<T> angles;
  switch (angle_priority) {
    case 5: {
      CreateAngleZYXFromMatrix(angles, k[0][0], k[0][1], k[0][2], k[1][1],
                               k[1][1], k[1][2], k[2][2], k[2][1], k[2][2]);
      break;
    }
    case 3: {
      Vector3<T> a;
      CreateAngleZYXFromMatrix(a, k[1][1], k[1][0], k[1][2], k[0][1], k[0][0],
                               k[0][2], k[2][1], k[2][0], k[2][2]);
      angles[0] = -a[1];
      angles[1] = -a[0];
      angles[2] = -a[2];
      break;
    }
    case 4: {
      Vector3<T> a;
      CreateAngleZYXFromMatrix(a, k[0][0], k[0][2], k[0][1], k[2][0], k[2][2],
                               k[2][1], k[1][0], k[1][2], k[1][1]);
      angles[0] = -a[0];
      angles[1] = -a[2];
      angles[2] = -a[1];
      break;
    }
    case 1: {
      Vector3<T> a;
      CreateAngleZYXFromMatrix(a, k[0][0], k[0][1], k[0][2], k[1][0], k[1][1],
                               k[1][2], k[2][0], k[2][1], k[2][2]);
      angles[0] = a[1];
      angles[1] = a[2];
      angles[2] = a[0];
      break;
    }
    case 2: {
      Vector3<T> a;
      CreateAngleZYXFromMatrix(a, k[1][1], k[1][2], k[1][0], k[2][1], k[2][2],
                               k[2][0], k[0][1], k[0][2], k[0][0]);
      angles[2] = a[1];
      angles[1] = a[0];
      angles[0] = a[2];
      break;
    }
    case 0: {
      Vector3<T> a;
      CreateAngleZYXFromMatrix(a, k[2][2], k[2][1], k[2][0], k[1][2], k[1][1],
                               k[1][0], k[0][2], k[0][1], k[0][0]);
      angles[1] = -a[1];
      angles[2] = -a[0];
      angles[0] = -a[2];
      break;
    }
    default:
      NOTREACHED();
  }
  return -angles;
}

template <typename T>
void Matrix4<T>::Lerp(const Matrix4& other, float t, Matrix4& dst) const {
  Quaternion<T> q1, q2, qr;
  q1.Create(*this);
  q2.Create(other);
  q1.Lerp(q2, t, qr);
  qr.CreateMatrix(dst);
  dst.Row(3) = base::Lerp(Row(3), other.Row(3), t);
}

template <typename T>
Vector3<T>& Matrix4<T>::Row(int row) {
  return *((Vector3<T>*)&k[row][0]);
}

template <typename T>
const Vector3<T>& Matrix4<T>::Row(int row) const {
  return *((const Vector3<T>*)&k[row][0]);
}

template <typename T>
const T* Matrix4<T>::GetData() const {
  return &k[0][0];
}

template <typename T>
std::string Matrix4<T>::ToString() {
  using namespace std::string_literals;
  std::string str = "(";
  for (int row = 0; row < 4; row++)
    str += "("s + std::to_string(k[row][0]) + ", "s +
           std::to_string(k[row][1]) + ", "s + std::to_string(k[row][2]) +
           ", "s + std::to_string(k[row][3]) + "), "s;
  str.pop_back();
  str.pop_back();
  return str + ")";
}

//
// Quaternion implementation
//

template <typename T>
Quaternion<T>::Quaternion() {}

template <typename T>
Quaternion<T>::Quaternion(const Vector3<T>& v, T w) {
  Create(v, w);
}

template <typename T>
Quaternion<T>::Quaternion(T x, T y, T z, T w) {
  k[0] = x;
  k[1] = y;
  k[2] = z;
  k[3] = w;
}

template <typename T>
void Quaternion<T>::Create(const Vector3<T>& v, T angle) {
  T x = v[0];
  T y = v[1];
  T z = v[2];
  T len_sqr = Sqr(x) + Sqr(y) + Sqr(z);

  if (std::abs(len_sqr - T(1.0)) > 0) {
    T len = std::sqrt(len_sqr);
    x = x / len;
    y = y / len;
    z = z / len;
  }

  T a = angle * (T(1.0) / T(2.0) * T(kPi) * T(2.0));
  k[3] = std::cos(a);
  T sn = std::sin(a);
  k[0] = x * sn;
  k[1] = y * sn;
  k[2] = z * sn;
}

template <typename T>
void Quaternion<T>::Create(const Vector3<T>& v) {
  Quaternion roll(Vector3<T>(0, 0, 1), v[2]);
  Quaternion pitch(Vector3<T>(1, 0, 0), v[0]);
  Quaternion yaw(Vector3<T>(0, 1, 0), v[1]);

  *this = roll;
  Multiply(pitch);
  Multiply(yaw);
}

template <typename T>
void Quaternion<T>::Create(const Matrix4<T>& mat) {
  T trace = T(0);
  for (int i = 0; i < 3; i++)
    trace += mat.k[i][i];

  if (trace > T(0)) {
    T s = std::sqrt(trace + T(1.0));
    k[3] = s * T(0.5);
    s = T(0.5) / s;

    for (int i = 0; i < 3; i++) {
      int j = (i == 2) ? 0 : (i + 1);
      int m = (j == 2) ? 0 : (j + 1);
      k[i] = (mat.k[m][j] - mat.k[j][m]) * s;
    }
  } else {
    int i = 0;
    if (mat.k[1][1] > mat.k[0][0])
      i = 1;
    if (mat.k[2][2] > mat.k[i][i])
      i = 2;
    int j = (i == 2) ? 0 : (i + 1);
    int m = (j == 2) ? 0 : (j + 1);

    T s = std::sqrt((mat.k[i][i] - (mat.k[j][j] + mat.k[m][m])) + T(1.0));
    k[i] = s * T(0.5);
    s = T(0.5) / s;
    k[3] = (mat.k[m][j] - mat.k[j][m]) * s;
    k[j] = (mat.k[j][i] + mat.k[i][j]) * s;
    k[m] = (mat.k[m][i] + mat.k[i][m]) * s;
  }
}

template <typename T>
void Quaternion<T>::Create(T x, T y, T z, T w) {
  k[0] = x;
  k[1] = y;
  k[2] = z;
  k[3] = w;
}

template <typename T>
bool Quaternion<T>::operator==(const Quaternion& q) const {
  return ((k[0] == q.k[0]) && (k[1] == q.k[1]) && (k[2] == q.k[2]) &&
          (k[3] == q.k[3]));
}

template <typename T>
bool Quaternion<T>::operator!=(const Quaternion& q) const {
  return !((k[0] == q.k[0]) && (k[1] == q.k[1]) && (k[2] == q.k[2]) &&
           (k[3] == q.k[3]));
}

template <typename T>
void Quaternion<T>::Unit() {
  k[0] = T(0.0);
  k[1] = T(0.0);
  k[2] = T(0.0);
  k[3] = T(1.0);
}

template <typename T>
void Quaternion<T>::Normalize() {
  T k0 = k[0];
  T k1 = k[1];
  T k2 = k[2];
  T k3 = k[3];
  T len_sqr = Sqr(k0) + Sqr(k1) + Sqr(k2) + Sqr(k3);
  if (len_sqr != T(0)) {
    T len = std::sqrt(len_sqr);
    k[0] = k0 / len;
    k[1] = k1 / len;
    k[2] = k2 / len;
    k[3] = k3 / len;
  }
}

template <typename T>
void Quaternion<T>::Inverse() {
  k[0] = -k[0];
  k[1] = -k[1];
  k[2] = -k[2];
}

template <typename T>
T Quaternion<T>::DotProd(const Quaternion& q) const {
  T k0 = k[0];
  T k1 = k[1];
  T k2 = k[2];
  T k3 = k[3];
  T qk0 = q.k[0];
  T qk1 = q.k[1];
  T qk2 = q.k[2];
  T qk3 = q.k[3];
  return k0 * qk0 + k1 * qk1 + k2 * qk2 + k3 * qk3;
}

template <typename T>
void Quaternion<T>::Lerp(const Quaternion& other,
                         T t,
                         Quaternion& dst) const {
  T a0 = k[0];
  T a1 = k[1];
  T a2 = k[2];
  T a3 = k[3];

  T dot =
      (a0 * other.k[0] + a1 * other.k[1] + a2 * other.k[2] + a3 * other.k[3]);
  T u = Sel(dot, T(1.0) - t, t - T(1.0));

  T d0 = a0 * u + other.k[0] * t;
  T d1 = a1 * u + other.k[1] * t;
  T d2 = a2 * u + other.k[2] * t;
  T d3 = a3 * u + other.k[3] * t;
  T len_sqr = Sqr(d0) + Sqr(d1) + Sqr(d2) + Sqr(d3);
  if (len_sqr != T(0)) {
    T len = std::sqrt(len_sqr);
    dst.k[0] = d0 / len;
    dst.k[1] = d1 / len;
    dst.k[2] = d2 / len;
    dst.k[3] = d3 / len;
  }
}

template <typename T>
void Quaternion<T>::Multiply(const Quaternion& other, Quaternion& dst) const {
  T a0 = k[0];
  T a1 = k[1];
  T a2 = k[2];
  T a3 = k[3];
  T b0 = other.k[0];
  T b1 = other.k[1];
  T b2 = other.k[2];
  T b3 = other.k[3];
  dst.k[0] = a3 * b0 + a0 * b3 + a1 * b2 - a2 * b1;
  dst.k[1] = a3 * b1 + a1 * b3 + a2 * b0 - a0 * b2;
  dst.k[2] = a3 * b2 + a2 * b3 + a0 * b1 - a1 * b0;
  dst.k[3] = a3 * b3 - a0 * b0 - a1 * b1 - a2 * b2;
}

template <typename T>
void Quaternion<T>::Multiply(const Quaternion& other) {
  T a0 = k[0];
  T a1 = k[1];
  T a2 = k[2];
  T a3 = k[3];
  T b0 = other.k[0];
  T b1 = other.k[1];
  T b2 = other.k[2];
  T b3 = other.k[3];
  k[0] = a3 * b0 + a0 * b3 + a1 * b2 - a2 * b1;
  k[1] = a3 * b1 + a1 * b3 + a2 * b0 - a0 * b2;
  k[2] = a3 * b2 + a2 * b3 + a0 * b1 - a1 * b0;
  k[3] = a3 * b3 - a0 * b0 - a1 * b1 - a2 * b2;
}

template <typename T>
void Quaternion<T>::operator*=(const Quaternion& other) {
  Multiply(other);
}

template <typename T>
Quaternion<T> Quaternion<T>::operator*(const Quaternion& other) const {
  Quaternion q;
  Multiply(other, q);
  return q;
}

template <typename T>
T Quaternion<T>::CreateAxisAngle(Vector3<T>& v) const {
  DCHECK(((k[3] >= -T(1.0)) || (k[3] <= T(1.0)))
         << "Quaternion needs normalizing.");

  T LenSqr = Sqr(k[0]) + Sqr(k[1]) + Sqr(k[2]);
  if (LenSqr > T(0.000001)) {
    T len = std::sqrt(LenSqr);
    v[0] = k[0] / len;
    v[1] = k[1] / len;
    v[2] = k[2] / len;
    return T(2.0) * std::acos(k[3]) / T(k2Pi);  // Angle
  } else {
    // Angle is 0 (mod 2*pi), so any axis will do.
    v[0] = T(1.0);
    v[1] = T(0.0);
    v[2] = T(0.0);
    return 0;  // Angle
  }
}

template <typename T>
void Quaternion<T>::CreateMatrix3x3(Matrix4<T>& mat) const {
  T xs, ys, zs, wx, wy, wz, xx, xy, xz, yy, yz, zz;
  T s = T(2.0);

  xs = k[0] * s;
  ys = k[1] * s;
  zs = k[2] * s;
  wx = k[3] * xs;
  wy = k[3] * ys;
  wz = k[3] * zs;
  xx = k[0] * xs;
  xy = k[0] * ys;
  xz = k[0] * zs;
  yy = k[1] * ys;
  yz = k[1] * zs;
  zz = k[2] * zs;

  mat.k[0][0] = (T(1.0) - (yy + zz));
  mat.k[0][1] = (xy - wz);
  mat.k[0][2] = (xz + wy);
  mat.k[1][0] = (xy + wz);
  mat.k[1][1] = (T(1.0) - (xx + zz));
  mat.k[1][2] = (yz - wx);
  mat.k[2][0] = (xz - wy);
  mat.k[2][1] = (yz + wx);
  mat.k[2][2] = (T(1.0) - (xx + yy));
}

template <typename T>
void Quaternion<T>::CreateMatrix(Matrix4<T>& mat) const {
  CreateMatrix3x3(mat);
  mat.UnitNot3x3();
}

template <typename T>
std::string Quaternion<T>::ToString() {
  using namespace std::string_literals;
  return "("s + std::to_string(k[0]) + ", "s + std::to_string(k[1]) +
         std::to_string(k[2]) + ", "s + std::to_string(k[3]) + ")"s;
}

}  // namespace base

#endif  // BASE_VECMATH_IMPL_H
