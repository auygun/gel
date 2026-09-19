#ifndef BASE_VECMATH_H
#define BASE_VECMATH_H

#include <cmath>
#include <numbers>
#include <string>
#include <utility>

#include "third_party/kaliber/base/interpolation.h"
#include "third_party/kaliber/base/log.h"

namespace base {

constexpr double kPi = std::numbers::pi;
constexpr double k2Pi = kPi * 2.0;

// Forward declaration.
template <typename T>
class Vector3;
template <typename T>
class Matrix4;
template <typename T>
class Quaternion;

//
// Vector2
//

template <typename T>
class Vector2 {
 public:
  union {
    struct {
      T x;
      T y;
    };
    T k[2];
  };

  Vector2();
  explicit Vector2(T v);
  Vector2(T x, T y);

  Vector2& operator=(T s);

  T& operator[](int i);
  const T& operator[](int i) const;

  bool operator==(const Vector2& other) const;
  bool operator==(T v) const;
  bool operator!=(const Vector2& other) const;
  bool operator!=(T v) const;

  bool AlmostEqual(const Vector2& other, T epsilon) const;

  Vector2 operator+(const Vector2& other) const;
  Vector2 operator-(const Vector2& other) const;
  Vector2 operator-() const;
  Vector2 operator*(const Vector2& other) const;
  Vector2 operator*(T scalar) const;
  Vector2 operator/(const Vector2& other) const;
  Vector2 operator/(T scalar) const;

  void operator+=(const Vector2& other);
  void operator-=(const Vector2& other);
  void operator*=(const Vector2& other);
  void operator*=(T v);
  void operator/=(const Vector2& other);
  void operator/=(T v);

  T DotProduct(const Vector2& v);
  T CrossProduct(const Vector2& v);

  Vector2 Project(const Vector2& v) const;
  Vector2 Reflect(const Vector2& n) const;

  T Length() const;
  T LengthSqr() const;
  T Distance(const Vector2& other) const;
  T DistanceSqr(const Vector2& other) const;

  Vector2& Normalize();
  Vector2& SafeNormalize();
  Vector2& SetLength(T len);
  Vector2& SetMaxLength(T max_len);

  const T* GetData() const;

  std::string ToString();
};

//
// Vector3
//

template <typename T>
class Vector3 {
 public:
  union {
    struct {
      T x;
      T y;
      T z;
    };
    T k[3];
  };

  Vector3();
  explicit Vector3(T v);
  Vector3(T x, T y, T z);

  Vector3& operator=(T s);

  T& operator[](int i);
  const T& operator[](int i) const;

  bool operator==(const Vector3& other) const;
  bool operator==(T v) const;
  bool operator!=(const Vector3& other) const;
  bool operator!=(T v) const;

  bool AlmostEqual(const Vector3& other, T epsilon) const;

  Vector3 operator+(const Vector3& other) const;
  Vector3 operator-(const Vector3& other) const;
  Vector3 operator-() const;
  Vector3 operator*(const Vector3& other) const;
  Vector3 operator*(T scalar) const;
  Vector3 operator*(const Matrix4<T>& mat);
  Vector3 operator/(const Vector3& other) const;
  Vector3 operator/(T scalar) const;

  void operator+=(const Vector3& other);
  void operator-=(const Vector3& other);
  void operator*=(const Vector3& other);
  void operator*=(T v);
  void operator*=(const Matrix4<T>& mat);
  void operator/=(const Vector3& other);
  void operator/=(T v);

  T DotProduct(const Vector3& other) const;
  Vector3 CrossProduct(const Vector3& other) const;

  Vector3 Project(const Vector3& v) const;
  Vector3 ProjectPlane(const Vector3& n) const;
  Vector3 Reflect(const Vector3& n) const;

  T Length() const;
  T LengthSqr() const;
  T Distance(const Vector3& other) const;
  T DistanceSqr(const Vector3& other) const;

  Vector3& Normalize();
  Vector3& SafeNormalize();
  Vector3& SetLength(T len);
  Vector3& SetMaxLength(T max_len);

  const T* GetData() const;

  std::string ToString();
};

//
// Vector4
//

template <typename T>
class Vector4 {
 public:
  union {
    struct {
      T x;
      T y;
      T z;
      T w;
    };
    T k[4];
  };

  Vector4();
  explicit Vector4(T v);
  Vector4(T x, T y, T z, T w);

  Vector4& operator=(T s);

  T& operator[](int i);
  const T& operator[](int i) const;

  bool operator==(const Vector4& other) const;
  bool operator==(T v) const;
  bool operator!=(const Vector4& other) const;
  bool operator!=(T v) const;

  bool AlmostEqual(const Vector4& other, T epsilon) const;

  Vector4 operator+(const Vector4& other) const;
  Vector4 operator-(const Vector4& other) const;
  Vector4 operator-() const;
  Vector4 operator*(const Vector4& other) const;
  Vector4 operator*(T scalar) const;
  Vector4 operator*(const Matrix4<T>& mat);
  Vector4 operator/(const Vector4& other) const;
  Vector4 operator/(T scalar) const;

  void operator+=(const Vector4& other);
  void operator-=(const Vector4& other);
  void operator*=(const Vector4& other);
  void operator*=(T v);
  void operator*=(const Matrix4<T>& mat);
  void operator/=(const Vector4& other);
  void operator/=(T v);

  T DotProduct(const Vector4& other) const;

  T Length() const;
  T LengthSqr() const;
  T Distance(const Vector4& other) const;
  T DistanceSqr(const Vector4& other) const;

  Vector4& Normalize();
  Vector4& SafeNormalize();
  Vector4& SetLength(T len);
  Vector4& SetMaxLength(T max_len);

  const T* GetData() const;

  std::string ToString();
};

//
// Matrix4
//

template <typename T>
class Matrix4 {
 public:
  T k[4][4];  // Row-major layout.

  Matrix4();
  explicit Matrix4(T s);
  Matrix4(T v00,
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
          T v33);

  Matrix4& operator=(T s);

  void Unit();
  void Unit3x3();
  void UnitNot3x3();

  void Transpose();
  void Transpose(Matrix4& dst) const;
  void Transpose3x3();
  void Transpose3x3(Matrix4& dst) const;

  bool Inverse();
  bool Inverse(Matrix4& dst) const;
  bool Inverse3x3();
  bool Inverse3x3(Matrix4& dst) const;
  void InverseOrthogonal();
  void InverseOrthogonal(Matrix4& dst) const;

  // Scale all 4x4 elements.
  void Multiply(T s);
  void Multiply(T s, Matrix4& dst);

  // Scale the 3x3 sub-matrix.
  void Multiply3x3(T s);
  void Multiply3x3(T s, Matrix4& dst);

  // Multiply two 4x4 matrices.
  void Multiply(const Matrix4& m, Matrix4& dst) const;

  // Multiply the 3x3 sub-matrices.
  void Multiply3x3(const Matrix4& m, Matrix4& dst) const;

  void Normalize3x3();
  void NormalizeRows3x3();

  void Create(const Quaternion<T>& rotation, const Vector3<T>& translation);

  void CreateLookAt(const Vector3<T>& from,
                    const Vector3<T>& to,
                    const Vector3<T>& up = {T(0), T(1), T(0)});

  void CreateOrthographicProjection(T left, T right, T bottom, T top);

  void CreatePerspectiveProjection(T fov,
                                   T fov_aspect,
                                   T width,
                                   T height,
                                   T near_plane,
                                   T far_plane);

  void CreateTranslation(const Vector3<T>& t);

  // Create rotation matrix from axis-angle.
  void CreateAxisRotation(const Vector3<T>& rotation_axis, T angle);

  // Create rotation matrix from Euler angles.
  void CreateFromAngles(const Vector3<T>& angles, int angle_priority);

  // Create rotation matrix. Angle v is in turns (0..1).
  void CreateXRotation(T v);
  void CreateYRotation(T v);
  void CreateZRotation(T v);

  // Create 3x3 rotation matrix. Angle v is in turns (0..1).
  void CreateXRotation3x3(T v);
  void CreateYRotation3x3(T v);
  void CreateZRotation3x3(T v);

  // Right-multiply by rotation matrix. Angle v is in turns (0..1).
  void M_x_RotX(T v);
  void M_x_RotY(T v);
  void M_x_RotZ(T v);

  // Left-multiply by rotation matrix. Angle v is in turns (0..1).
  void RotX_x_M(T v);
  void RotY_x_M(T v);
  void RotZ_x_M(T v);

  // Normalize and orthogonalize the 3x3 sub-matrix.
  template <int priority0, int priority1>
  void RecreateMatrix();

  // Extract Euler angles.
  Vector3<T> GetAngles(int angle_priority) const;

  void Lerp(const Matrix4& other, float t, Matrix4& dst) const;

  Vector3<T>& Row(int row);
  const Vector3<T>& Row(int row) const;

  const T* GetData() const;

  std::string ToString();
};

//
// Quaternion
//

template <typename T>
class Quaternion {
 public:
  T k[4];

  Quaternion();
  Quaternion(const Vector3<T>& v, T w);
  Quaternion(T x, T y, T z, T w);

  // Create from axis-angle.
  void Create(const Vector3<T>& v, T angle);

  // Create from Euler angles.
  void Create(const Vector3<T>& v);

  // Create from rotation matrix.
  void Create(const Matrix4<T>& mat);

  void Create(T x, T y, T z, T w);

  bool operator==(const Quaternion& q) const;
  bool operator!=(const Quaternion& q) const;

  void Unit();

  void Normalize();

  void Inverse();

  T DotProd(const Quaternion& q) const;

  void Lerp(const Quaternion& other, T t, Quaternion& dst) const;

  void Multiply(const Quaternion& other, Quaternion& dst) const;
  void Multiply(const Quaternion& other);

  void operator*=(const Quaternion& other);
  Quaternion operator*(const Quaternion& other) const;

  T CreateAxisAngle(Vector3<T>& v) const;

  void CreateMatrix3x3(Matrix4<T>& mat) const;
  void CreateMatrix(Matrix4<T>& mat) const;

  std::string ToString();
};

using Vector2f = Vector2<float>;
using Vector3f = Vector3<float>;
using Vector4f = Vector4<float>;
using Matrix4f = Matrix4<float>;
using Quatf = Quaternion<float>;

}  // namespace base

#include "third_party/kaliber/base/vecmath_impl.h"

#endif  // BASE_VECMATH_H
