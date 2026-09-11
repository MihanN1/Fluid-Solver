#include "RigidBody.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr float kDegToRad = 3.14159265358979f / 180.0f;

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

constexpr float kPi = 3.14159265358979f;

float easeInOf(InterpKind kind, float t) {
    switch (kind) {
    case InterpKind::Sine:
        return 1.0f - std::cos(t * kPi * 0.5f);
    case InterpKind::Quad:
        return t * t;
    case InterpKind::Cubic:
        return t * t * t;
    case InterpKind::Quart:
        return t * t * t * t;
    case InterpKind::Quint:
        return t * t * t * t * t;
    case InterpKind::Expo:
        return t <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f));
    case InterpKind::Circ:
        return 1.0f - std::sqrt(std::max(0.0f, 1.0f - t * t));
    case InterpKind::Back: {
        const float c = 1.70158f;
        return t * t * ((c + 1.0f) * t - c);
    }
    case InterpKind::Elastic: {
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;
        const float p = 0.3f;
        return -std::pow(2.0f, 10.0f * (t - 1.0f)) *
               std::sin((t - 1.0f - p * 0.25f) * 2.0f * kPi / p);
    }
    default:
        return t;
    }
}

float bounceOut(float t) {
    const float n = 7.5625f;
    const float d = 2.75f;
    if (t < 1.0f / d)
        return n * t * t;
    if (t < 2.0f / d) {
        t -= 1.5f / d;
        return n * t * t + 0.75f;
    }
    if (t < 2.5f / d) {
        t -= 2.25f / d;
        return n * t * t + 0.9375f;
    }
    t -= 2.625f / d;
    return n * t * t + 0.984375f;
}

EaseKind resolveEase(InterpKind kind, EaseKind ease) {
    if (ease != EaseKind::Auto)
        return ease;
    switch (kind) {
    case InterpKind::Back:
    case InterpKind::Bounce:
    case InterpKind::Elastic:
        return EaseKind::In;
    default:
        return EaseKind::Out;
    }
}

float shape(InterpKind kind, EaseKind ease, float t) {
    t = std::min(1.0f, std::max(0.0f, t));
    if (kind == InterpKind::Constant)
        return 0.0f;
    if (kind == InterpKind::Linear || kind == InterpKind::Bezier)
        return t;

    const EaseKind side = resolveEase(kind, ease);
    if (kind == InterpKind::Bounce) {
        switch (side) {
        case EaseKind::In:
            return 1.0f - bounceOut(1.0f - t);
        case EaseKind::InOut:
            return t < 0.5f ? 0.5f * (1.0f - bounceOut(1.0f - 2.0f * t))
                            : 0.5f * (1.0f + bounceOut(2.0f * t - 1.0f));
        default:
            return bounceOut(t);
        }
    }

    switch (side) {
    case EaseKind::In:
        return easeInOf(kind, t);
    case EaseKind::InOut:
        return t < 0.5f ? 0.5f * easeInOf(kind, 2.0f * t)
                        : 1.0f - 0.5f * easeInOf(kind, 2.0f - 2.0f * t);
    default:
        return 1.0f - easeInOf(kind, 1.0f - t);
    }
}

void applyTensor(const double tensor[9], const double vector[3],
                 double out[3]) {
    for (int row = 0; row < 3; ++row)
        out[row] = tensor[row * 3] * vector[0] +
                   tensor[row * 3 + 1] * vector[1] +
                   tensor[row * 3 + 2] * vector[2];
}

void rotateTensor(const double rotation[9], const double tensor[9],
                  double out[9]) {
    double turned[9];
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            turned[row * 3 + col] = rotation[row * 3] * tensor[col] +
                                    rotation[row * 3 + 1] * tensor[3 + col] +
                                    rotation[row * 3 + 2] * tensor[6 + col];
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            out[row * 3 + col] = turned[row * 3] * rotation[col * 3] +
                                 turned[row * 3 + 1] * rotation[col * 3 + 1] +
                                 turned[row * 3 + 2] * rotation[col * 3 + 2];
}

bool invertTensor(const double tensor[9], double out[9]) {
    const double first = tensor[4] * tensor[8] - tensor[5] * tensor[7];
    const double second = tensor[5] * tensor[6] - tensor[3] * tensor[8];
    const double third = tensor[3] * tensor[7] - tensor[4] * tensor[6];
    const double det =
        tensor[0] * first + tensor[1] * second + tensor[2] * third;
    const double size =
        std::fabs(tensor[0]) + std::fabs(tensor[4]) + std::fabs(tensor[8]);
    if (!(std::fabs(det) > 1e-12 * size * size * size))
        return false;

    const double scale = 1.0 / det;
    out[0] = first * scale;
    out[1] = (tensor[2] * tensor[7] - tensor[1] * tensor[8]) * scale;
    out[2] = (tensor[1] * tensor[5] - tensor[2] * tensor[4]) * scale;
    out[3] = second * scale;
    out[4] = (tensor[0] * tensor[8] - tensor[2] * tensor[6]) * scale;
    out[5] = (tensor[2] * tensor[3] - tensor[0] * tensor[5]) * scale;
    out[6] = third * scale;
    out[7] = (tensor[1] * tensor[6] - tensor[0] * tensor[7]) * scale;
    out[8] = (tensor[0] * tensor[4] - tensor[1] * tensor[3]) * scale;
    return true;
}
}

bool RigidBody::freeAt(double when) const {
    if (keys.empty())
        return free;
    const float t = static_cast<float>(when);
    if (t <= keys.front().time)
        return keys.front().free;
    std::size_t upper = 0;
    while (upper + 1 < keys.size() && keys[upper + 1].time <= t)
        ++upper;
    return keys[upper].free;
}

void RigidBody::sampleVelocity(double when) {
    if (keys.empty()) {
        vx = baseVx;
        vy = baseVy;
        vz = baseVz;
        omegaX = baseOmegaX;
        omegaY = baseOmegaY;
        omega = baseOmega;
        return;
    }

    const float t = static_cast<float>(when);
    if (t <= keys.front().time) {
        vx = keys.front().vx;
        vy = keys.front().vy;
        vz = keys.front().vz;
        omegaX = keys.front().omegaX * kDegToRad;
        omegaY = keys.front().omegaY * kDegToRad;
        omega = keys.front().omega * kDegToRad;
        return;
    }
    if (t >= keys.back().time) {
        vx = keys.back().vx;
        vy = keys.back().vy;
        vz = keys.back().vz;
        omegaX = keys.back().omegaX * kDegToRad;
        omegaY = keys.back().omegaY * kDegToRad;
        omega = keys.back().omega * kDegToRad;
        return;
    }

    std::size_t upper = 1;
    while (upper + 1 < keys.size() && keys[upper].time < t)
        ++upper;
    const std::size_t lower = upper - 1;
    const BodyKeyframe& a = keys[lower];
    const BodyKeyframe& b = keys[upper];
    const float span = b.time - a.time;
    const float raw = span > 0.0f ? (t - a.time) / span : 0.0f;

    if (a.interp == InterpKind::Bezier) {
        const auto tangent = [&](std::size_t index, float BodyKeyframe::*field) {
            const float here = keys[index].*field;
            if (index == 0 || index + 1 >= keys.size()) {
                const std::size_t other = index == 0 ? 1 : index - 1;
                const float gap = keys[other].time - keys[index].time;
                return gap != 0.0f ? (keys[other].*field - here) / gap : 0.0f;
            }
            const float before = keys[index - 1].*field;
            const float after = keys[index + 1].*field;
            if ((here - before) * (after - here) <= 0.0f)
                return 0.0f;
            const float gap = keys[index + 1].time - keys[index - 1].time;
            return gap != 0.0f ? (after - before) / gap : 0.0f;
        };
        const auto hermite = [&](float BodyKeyframe::*field) {
            const float p0 = a.*field;
            const float p1 = b.*field;
            const float m0 = tangent(lower, field) * span;
            const float m1 = tangent(upper, field) * span;
            const float s = raw;
            const float s2 = s * s;
            const float s3 = s2 * s;
            return (2.0f * s3 - 3.0f * s2 + 1.0f) * p0 +
                   (s3 - 2.0f * s2 + s) * m0 +
                   (-2.0f * s3 + 3.0f * s2) * p1 +
                   (s3 - s2) * m1;
        };
        vx = hermite(&BodyKeyframe::vx);
        vy = hermite(&BodyKeyframe::vy);
        vz = hermite(&BodyKeyframe::vz);
        omegaX = hermite(&BodyKeyframe::omegaX) * kDegToRad;
        omegaY = hermite(&BodyKeyframe::omegaY) * kDegToRad;
        omega = hermite(&BodyKeyframe::omega) * kDegToRad;
        return;
    }

    const float w = shape(a.interp, a.ease, raw);
    vx = lerp(a.vx, b.vx, w);
    vy = lerp(a.vy, b.vy, w);
    vz = lerp(a.vz, b.vz, w);
    omegaX = lerp(a.omegaX, b.omegaX, w) * kDegToRad;
    omegaY = lerp(a.omegaY, b.omegaY, w) * kDegToRad;
    omega = lerp(a.omega, b.omega, w) * kDegToRad;
}

void RigidBody::integrate(float dt) {
    if (!free)
        return;

    const float translational = mass + addedMass;
    double rotational[9];
    for (int term = 0; term < 9; ++term)
        rotational[term] = static_cast<double>(inertia[term]) +
                           static_cast<double>(addedInertia[term]);

    if (translational > 0.0f) {
        vx += dt * forceX / translational;
        vy += dt * forceY / translational;
        vz += dt * forceZ / translational;
    }

    double rotation[9];
    orientation(rotation);
    double world[9];
    rotateTensor(rotation, rotational, world);
    double inverse[9];
    if (invertTensor(world, inverse)) {
        const double spin[3] = {omegaX, omegaY, omega};
        double momentum[3];
        applyTensor(world, spin, momentum);
        momentum[0] += dt * (static_cast<double>(torqueX) -
                             (spin[1] * momentum[2] - spin[2] * momentum[1]));
        momentum[1] += dt * (static_cast<double>(torqueY) -
                             (spin[2] * momentum[0] - spin[0] * momentum[2]));
        momentum[2] += dt * (static_cast<double>(torque) -
                             (spin[0] * momentum[1] - spin[1] * momentum[0]));
        double turn[3];
        applyTensor(inverse, momentum, turn);
        omegaX = static_cast<float>(turn[0]);
        omegaY = static_cast<float>(turn[1]);
        omega = static_cast<float>(turn[2]);
    }

    applyPins();
}

void RigidBody::applyPins() {
    if (pinX)
        vx = 0.0f;
    if (pinY)
        vy = 0.0f;
    if (pinZ)
        vz = 0.0f;
    if (pinRotX)
        omegaX = 0.0f;
    if (pinRotY)
        omegaY = 0.0f;
    if (pinRot)
        omega = 0.0f;
}

void RigidBody::orientation(double m[9]) const {
    const double xx = qx * qx;
    const double yy = qy * qy;
    const double zz = qz * qz;
    const double xy = qx * qy;
    const double xz = qx * qz;
    const double yz = qy * qz;
    const double wx = qw * qx;
    const double wy = qw * qy;
    const double wz = qw * qz;

    m[0] = 1.0 - 2.0 * (yy + zz);
    m[1] = 2.0 * (xy - wz);
    m[2] = 2.0 * (xz + wy);
    m[3] = 2.0 * (xy + wz);
    m[4] = 1.0 - 2.0 * (xx + zz);
    m[5] = 2.0 * (yz - wx);
    m[6] = 2.0 * (xz - wy);
    m[7] = 2.0 * (yz + wx);
    m[8] = 1.0 - 2.0 * (xx + yy);
}

void RigidBody::normaliseOrientation() {
    const double norm = std::sqrt(qw * qw + qx * qx + qy * qy + qz * qz);
    if (!(norm > 0.0)) {
        qw = 1.0;
        qx = 0.0;
        qy = 0.0;
        qz = 0.0;
        return;
    }
    qw /= norm;
    qx /= norm;
    qy /= norm;
    qz /= norm;
}

void RigidBody::advancePose(float dt) {
    x += static_cast<double>(vx) * dt;
    y += static_cast<double>(vy) * dt;
    z += static_cast<double>(vz) * dt;

    const double spinX = omegaX;
    const double spinY = omegaY;
    const double spinZ = omega;
    const double half = 0.5 * dt;
    const double turnW = -(spinX * qx + spinY * qy + spinZ * qz);
    const double turnX = spinX * qw + spinY * qz - spinZ * qy;
    const double turnY = spinY * qw + spinZ * qx - spinX * qz;
    const double turnZ = spinZ * qw + spinX * qy - spinY * qx;
    qw += half * turnW;
    qx += half * turnX;
    qy += half * turnY;
    qz += half * turnZ;
    normaliseOrientation();
}

void RigidBody::step(double when, float dt) {
    const bool letGo = freeAt(when);
    if (letGo) {
        integrate(dt);
    } else {
        sampleVelocity(when);
        applyPins();
    }
    free = letGo;
}

void buildRigidBodies(const std::vector<BodyMotion>& motions,
                      const std::vector<BodyGeometry>& geometry,
                      std::vector<RigidBody>& out,
                      float fluidDensity,
                      std::vector<std::string>& notes) {
    const int objectCount = static_cast<int>(geometry.size()) - 1;
    out.assign(geometry.size(), RigidBody());
    for (int id = 0; id <= objectCount; ++id) {
        out[id].object = id;
        out[id].cx = geometry[id].cx;
        out[id].cy = geometry[id].cy;
        out[id].cz = geometry[id].cz;
        out[id].radius = geometry[id].radius;
        out[id].volume = geometry[id].volume;
    }

    for (const BodyMotion& motion : motions) {
        if (motion.object < 1 || motion.object > objectCount) {
            notes.push_back(
                "bodyMotion moves object " + std::to_string(motion.object) +
                ", but this geometry has " + std::to_string(objectCount) +
                (objectCount == 1 ? " object" : " objects") +
                ". That part of the line does nothing.");
            continue;
        }

        RigidBody& body = out[motion.object];
        body.free = motion.free;
        body.prescribed = true;
        body.everFree = motion.free;
        for (const BodyKeyframe& frame : motion.keys)
            if (frame.free)
                body.everFree = true;
        body.pinX = motion.pinX;
        body.pinY = motion.pinY;
        body.pinZ = motion.pinZ;
        body.pinRotX = motion.pinRotX;
        body.pinRotY = motion.pinRotY;
        body.pinRot = motion.pinRot;
        body.baseVx = motion.vx;
        body.baseVy = motion.vy;
        body.baseVz = motion.vz;
        body.baseOmegaX = motion.omegaX * kDegToRad;
        body.baseOmegaY = motion.omegaY * kDegToRad;
        body.baseOmega = motion.omega * kDegToRad;
        body.vx = motion.vx;
        body.vy = motion.vy;
        body.vz = motion.vz;
        body.omegaX = motion.omegaX * kDegToRad;
        body.omegaY = motion.omegaY * kDegToRad;
        body.omega = motion.omega * kDegToRad;
        body.keys = motion.keys;
        body.mass = motion.mass;
        if (motion.density > 0.0f)
            body.mass = motion.density * body.volume;
        const float density =
            motion.density > 0.0f
                ? motion.density
                : (body.volume > 0.0f ? body.mass / body.volume : 0.0f);
        for (int term = 0; term < 9; ++term)
            body.inertia[term] =
                density * geometry[motion.object].inertia[term];
        if (motion.inertiaX > 0.0f)
            body.inertia[0] = motion.inertiaX;
        if (motion.inertiaY > 0.0f)
            body.inertia[4] = motion.inertiaY;
        if (motion.inertia > 0.0f)
            body.inertia[8] = motion.inertia;
        body.applyPins();
    }

    for (RigidBody& body : out) {
        if (!body.everFree)
            continue;
        const float disc = 0.5f * body.mass * body.radius * body.radius;
        if (body.inertia[0] <= 0.0f)
            body.inertia[0] = disc;
        if (body.inertia[4] <= 0.0f)
            body.inertia[4] = disc;
        if (body.inertia[8] <= 0.0f)
            body.inertia[8] = disc;

        double tensor[9];
        for (int term = 0; term < 9; ++term)
            tensor[term] = body.inertia[term];
        double inverse[9];
        if (invertTensor(tensor, inverse)) {
            for (int term = 0; term < 9; ++term)
                body.invInertia[term] = static_cast<float>(inverse[term]);
        } else {
            notes.push_back(
                "object " + std::to_string(body.object) +
                " came out with an inertia that cannot be turned about, so it "
                "is held at the spin it was let go with. Give it inertia=, "
                "inertiaX= and inertiaY= and it will turn.");
        }

        body.addedMass = fluidDensity * body.volume;
        const float added =
            0.125f * fluidDensity * body.volume * body.radius * body.radius;
        body.addedInertia[0] = added;
        body.addedInertia[4] = added;
        body.addedInertia[8] = added;
    }
}

void resolveBodyCollisions(std::vector<RigidBody>& bodies,
                           const std::vector<int>& owner,
                           const std::vector<int>& contested,
                           int nx,
                           int ny,
                           int nz,
                           float Lx,
                           float Ly,
                           float Lz,
                           float restitution,
                           float stepDt,
                           int& contactsReported) {
    if (owner.empty())
        return;

    const float bounce = -restitution;
    for (RigidBody& body : bodies) {
        if (!body.free)
            continue;
        const float centreX = body.cx + static_cast<float>(body.x);
        const float centreY = body.cy + static_cast<float>(body.y);
        const float centreZ = body.cz + static_cast<float>(body.z);
        const float reach = body.radius;
        const float nextX = centreX + body.vx * stepDt;
        const float nextY = centreY + body.vy * stepDt;
        const float nextZ = centreZ + body.vz * stepDt;

        if (nextX - reach < 0.0f && body.vx < 0.0f)
            body.vx *= bounce;
        if (nextX + reach > Lx && body.vx > 0.0f)
            body.vx *= bounce;
        if (nextY - reach < 0.0f && body.vy < 0.0f)
            body.vy *= bounce;
        if (nextY + reach > Ly && body.vy > 0.0f)
            body.vy *= bounce;
        if (nextZ - reach < 0.0f && body.vz < 0.0f)
            body.vz *= bounce;
        if (nextZ + reach > Lz && body.vz > 0.0f)
            body.vz *= bounce;
    }

    if (bodies.size() < 3)
        return;

    const std::size_t count = bodies.size();
    std::vector<uint8_t> touching(count * count, 0);
    for (int k = 0; k < nz; ++k) {
    for (int j = 0; j < ny; ++j) {
        const int row = (k * ny + j) * nx;
        for (int i = 0; i < nx; ++i) {
            const int mine = owner[row + i];
            if (mine == 0)
                continue;
            const int neighbours[3] = {
                i + 1 < nx ? owner[row + i + 1] : 0,
                j + 1 < ny ? owner[row + nx + i] : 0,
                k + 1 < nz ? owner[row + nx * ny + i] : 0};
            for (int other : neighbours) {
                if (other == 0 || other == mine)
                    continue;
                if (static_cast<std::size_t>(mine) >= count ||
                    static_cast<std::size_t>(other) >= count)
                    continue;
                touching[mine * count + other] = 1;
                touching[other * count + mine] = 1;
            }
        }
    }
    }
    for (int id : contested) {
        const int mine = owner[id];
        if (mine <= 0 || static_cast<std::size_t>(mine) >= count)
            continue;
        for (std::size_t other = 1; other < count; ++other)
            if (static_cast<int>(other) != mine)
                touching[mine * count + other] = 1;
    }

    for (std::size_t a = 1; a < count; ++a) {
        for (std::size_t b = a + 1; b < count; ++b) {
            if (!touching[a * count + b])
                continue;
            RigidBody& first = bodies[a];
            RigidBody& second = bodies[b];
            if (!first.free && !second.free)
                continue;

            const float ax = first.cx + static_cast<float>(first.x);
            const float ay = first.cy + static_cast<float>(first.y);
            const float az = first.cz + static_cast<float>(first.z);
            const float bx = second.cx + static_cast<float>(second.x);
            const float by = second.cy + static_cast<float>(second.y);
            const float bz = second.cz + static_cast<float>(second.z);
            float normalX = bx - ax;
            float normalY = by - ay;
            float normalZ = bz - az;
            const float length =
                std::hypot(std::hypot(normalX, normalY), normalZ);
            if (!(length > 1e-12f))
                continue;
            normalX /= length;
            normalY /= length;
            normalZ /= length;

            const float closing = (second.vx - first.vx) * normalX +
                                  (second.vy - first.vy) * normalY +
                                  (second.vz - first.vz) * normalZ;
            if (closing >= 0.0f)
                continue;

            const float massA =
                first.free ? first.mass + first.addedMass : 0.0f;
            const float massB =
                second.free ? second.mass + second.addedMass : 0.0f;
            const float invA = massA > 0.0f ? 1.0f / massA : 0.0f;
            const float invB = massB > 0.0f ? 1.0f / massB : 0.0f;
            if (!(invA + invB > 0.0f))
                continue;

            const float impulse =
                -(1.0f + restitution) * closing / (invA + invB);
            if (first.free) {
                first.vx -= impulse * invA * normalX;
                first.vy -= impulse * invA * normalY;
                first.vz -= impulse * invA * normalZ;
                first.applyPins();
            }
            if (second.free) {
                second.vx += impulse * invB * normalX;
                second.vy += impulse * invB * normalY;
                second.vz += impulse * invB * normalZ;
                second.applyPins();
            }

            if (contactsReported < 3) {
                ++contactsReported;
                std::cout << "  bodies " << a << " and " << b
                          << " met at " << -closing << " m/s and bounced at "
                          << restitution << " of it\n";
            }
        }
    }
}
