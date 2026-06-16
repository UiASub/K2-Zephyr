#ifndef QUAT_MATH_H
#define QUAT_MATH_H

#include <math.h>

/*
 * Inline quaternion math, header-only.
 *
 * Convention:
 *   - Scalar-last storage (x, y, z, w) to match VN-100 register layout.
 *   - Hamilton product, right-handed coordinate system.
 *   - Unit quaternions represent rotation from world to body frame
 *     (same convention as VN-100 Attitude Quaternion register 9).
 *   - Euler conversions use the aerospace 3-2-1 (ZYX intrinsic)
 *     sequence: yaw about Z, then pitch about new Y, then roll about
 *     new X. Matches what VN-100 reg 8 / reg 239 output.
 */

typedef struct {
    float x;
    float y;
    float z;
    float w;
} quat_t;

static inline quat_t quat_identity(void)
{
    quat_t q = { 0.0f, 0.0f, 0.0f, 1.0f };
    return q;
}

/* Hamilton product: r = a * b */
static inline quat_t quat_mul(quat_t a, quat_t b)
{
    quat_t r;
    r.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    r.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    r.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    r.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    return r;
}

/* Conjugate; for unit quaternions this is also the inverse. */
static inline quat_t quat_conj(quat_t q)
{
    quat_t r = { -q.x, -q.y, -q.z, q.w };
    return r;
}

static inline quat_t quat_normalize(quat_t q)
{
    float n2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (n2 < 1e-12f) {
        return quat_identity();
    }
    float inv = 1.0f / sqrtf(n2);
    q.x *= inv; q.y *= inv; q.z *= inv; q.w *= inv;
    return q;
}

/*
 * Build a quaternion from yaw / pitch / roll in degrees, using the
 * 3-2-1 intrinsic sequence (yaw → pitch → roll). Output is the
 * rotation from world to body frame.
 */
static inline quat_t quat_from_euler_deg(float yaw_deg, float pitch_deg, float roll_deg)
{
    const float DEG2RAD_HALF = 0.0087266463f;  /* π/360 */

    float hy = yaw_deg   * DEG2RAD_HALF;
    float hp = pitch_deg * DEG2RAD_HALF;
    float hr = roll_deg  * DEG2RAD_HALF;

    float cy = cosf(hy), sy = sinf(hy);
    float cp = cosf(hp), sp = sinf(hp);
    float cr = cosf(hr), sr = sinf(hr);

    quat_t q;
    q.w = cy * cp * cr + sy * sp * sr;
    q.x = cy * cp * sr - sy * sp * cr;
    q.y = cy * sp * cr + sy * cp * sr;
    q.z = sy * cp * cr - cy * sp * sr;
    return q;
}

/*
 * Extract yaw / pitch / roll (3-2-1 ZYX, degrees) from a unit
 * quaternion. Pitch is clamped at ±90° to keep asinf domain-safe.
 *
 * Note: this is only used for telemetry. The PID error path skips
 * the Euler extraction entirely.
 */
static inline void quat_to_euler_deg(quat_t q, float *yaw_deg, float *pitch_deg, float *roll_deg)
{
    const float RAD2DEG = 57.2957795f;

    float sinp = 2.0f * (q.w * q.y - q.z * q.x);
    if (sinp >  1.0f) sinp =  1.0f;
    if (sinp < -1.0f) sinp = -1.0f;
    *pitch_deg = asinf(sinp) * RAD2DEG;

    *roll_deg = atan2f(2.0f * (q.w * q.x + q.y * q.z),
                       1.0f - 2.0f * (q.x * q.x + q.y * q.y)) * RAD2DEG;

    *yaw_deg = atan2f(2.0f * (q.w * q.z + q.x * q.y),
                      1.0f - 2.0f * (q.y * q.y + q.z * q.z)) * RAD2DEG;
}

/*
 * Convert a (small or large) rotation quaternion to its axis-angle
 * representation as a 3-vector in degrees. Picks the shortest
 * rotation by flipping sign when w < 0. No singularity at 180°.
 *
 * Used to express attitude error in body-frame axis components so
 * each PID can act on its own axis.
 */
static inline void quat_to_rotvec_deg(quat_t q,
                                      float *vx_deg, float *vy_deg, float *vz_deg)
{
    const float RAD2DEG = 57.2957795f;

    if (q.w < 0.0f) {
        q.x = -q.x; q.y = -q.y; q.z = -q.z; q.w = -q.w;
    }

    float sin_half_sq = q.x * q.x + q.y * q.y + q.z * q.z;
    if (sin_half_sq < 1e-12f) {
        /* Tiny rotation — first-order: vec ≈ 2 * (qx, qy, qz) in radians */
        *vx_deg = 2.0f * q.x * RAD2DEG;
        *vy_deg = 2.0f * q.y * RAD2DEG;
        *vz_deg = 2.0f * q.z * RAD2DEG;
        return;
    }

    float sin_half = sqrtf(sin_half_sq);
    float angle = 2.0f * atan2f(sin_half, q.w);  /* radians, in [0, π] */
    float scale = (angle / sin_half) * RAD2DEG;
    *vx_deg = q.x * scale;
    *vy_deg = q.y * scale;
    *vz_deg = q.z * scale;
}

/*
 * Rotate a 3-vector by a unit quaternion: v_out = q * v * q*
 * (active rotation, taking a vector expressed in one frame to the
 *  same vector expressed in the rotated frame).
 */
static inline void quat_rotate_vec(quat_t q, float vx, float vy, float vz,
                                   float *ox, float *oy, float *oz)
{
    /* t = 2 * cross(q.xyz, v) */
    float tx = 2.0f * (q.y * vz - q.z * vy);
    float ty = 2.0f * (q.z * vx - q.x * vz);
    float tz = 2.0f * (q.x * vy - q.y * vx);

    /* v' = v + q.w * t + cross(q.xyz, t) */
    *ox = vx + q.w * tx + (q.y * tz - q.z * ty);
    *oy = vy + q.w * ty + (q.z * tx - q.x * tz);
    *oz = vz + q.w * tz + (q.x * ty - q.y * tx);
}

#endif /* QUAT_MATH_H */
