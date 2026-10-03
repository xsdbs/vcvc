#pragma once
#include <algorithm>
#include <cmath>

namespace SDK
{
    constexpr float Pi = 3.14159265358979323846f;
    struct Vector
    {
        float x{},y{},z{};
        Vector operator+(const Vector& other) const { return {x+other.x,y+other.y,z+other.z}; }
        Vector operator-(const Vector& other) const { return {x-other.x,y-other.y,z-other.z}; }
        Vector operator*(float scale) const { return {x*scale,y*scale,z*scale}; }
        float Dot(const Vector& other) const { return x*other.x+y*other.y+z*other.z; }
        float LengthSquared() const { return Dot(*this); }
        float Length() const { return std::sqrt(LengthSquared()); }
        Vector Normalized() const { const float length = Length(); return length > 0 ? *this*(1.f/length) : Vector{}; }
    };
    struct Matrix3x4 { float values[3][4]; };
    struct ViewMatrix { float values[4][4]; };
    inline Vector Transform(const Vector& point, const Matrix3x4& matrix)
    {
        return {
            point.x*matrix.values[0][0]+point.y*matrix.values[0][1]+point.z*matrix.values[0][2]+matrix.values[0][3],
            point.x*matrix.values[1][0]+point.y*matrix.values[1][1]+point.z*matrix.values[1][2]+matrix.values[1][3],
            point.x*matrix.values[2][0]+point.y*matrix.values[2][1]+point.z*matrix.values[2][2]+matrix.values[2][3]};
    }
    inline Vector AngleTo(const Vector& from, const Vector& to)
    {
        const Vector delta = to-from;
        return {-std::atan2(delta.z,std::hypot(delta.x,delta.y))*180.f/Pi,
            std::atan2(delta.y,delta.x)*180.f/Pi,0};
    }
    inline Vector NormalizeAngles(Vector angles)
    {
        angles.x = std::clamp(std::remainder(angles.x,360.f),-89.f,89.f);
        angles.y = std::remainder(angles.y,360.f);
        angles.z = 0;
        return angles;
    }
    inline void AngleVectors(const Vector& angles, Vector& forward, Vector& right, Vector& up)
    {
        const float pitch = angles.x*Pi/180.f, yaw = angles.y*Pi/180.f;
        const float sp = std::sin(pitch), cp = std::cos(pitch), sy = std::sin(yaw), cy = std::cos(yaw);
        forward = {cp*cy,cp*sy,-sp};
        right = {sy,-cy,0};
        up = {sp*cy,sp*sy,cp};
    }
    inline void FixMovement(float oldYaw, float newYaw, float& forward, float& side)
    {
        const float delta = (newYaw-oldYaw)*Pi/180.f;
        const float oldForward = forward, oldSide = side;
        forward = std::cos(delta)*oldForward-std::sin(delta)*oldSide;
        side = std::sin(delta)*oldForward+std::cos(delta)*oldSide;
    }
    inline bool Project(const Vector& point, const ViewMatrix& matrix, int width, int height, Vector& screen)
    {
        const float w = matrix.values[3][0]*point.x+matrix.values[3][1]*point.y+matrix.values[3][2]*point.z+matrix.values[3][3];
        if (w <= .001f) return false;
        const float x = matrix.values[0][0]*point.x+matrix.values[0][1]*point.y+matrix.values[0][2]*point.z+matrix.values[0][3];
        const float y = matrix.values[1][0]*point.x+matrix.values[1][1]*point.y+matrix.values[1][2]*point.z+matrix.values[1][3];
        screen = {width*.5f*(1+x/w),height*.5f*(1-y/w),0};
        return true;
    }
    // Distance between finite segments; handles degenerate capsule axes and parallel rays.
    inline float SegmentDistanceSquared(const Vector& p1,const Vector& q1,const Vector& p2,const Vector& q2)
    {
        const Vector d1 = q1-p1, d2 = q2-p2, r = p1-p2;
        const float a = d1.Dot(d1), e = d2.Dot(d2), f = d2.Dot(r);
        float s = 0,t = 0;
        if (a <= 1e-8f && e <= 1e-8f) return r.LengthSquared();
        if (a <= 1e-8f) t = std::clamp(f/e,0.f,1.f);
        else {
            const float c = d1.Dot(r);
            if (e <= 1e-8f) s = std::clamp(-c/a,0.f,1.f);
            else {
                const float b = d1.Dot(d2), denominator = a*e-b*b;
                if (denominator > 1e-8f) s = std::clamp((b*f-c*e)/denominator,0.f,1.f);
                t = (b*s+f)/e;
                if (t < 0) { t = 0; s = std::clamp(-c/a,0.f,1.f); }
                else if (t > 1) { t = 1; s = std::clamp((b-c)/a,0.f,1.f); }
            }
        }
        return ((p1+d1*s)-(p2+d2*t)).LengthSquared();
    }
}
