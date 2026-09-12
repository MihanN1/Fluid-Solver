#pragma once
#include "Config.hpp"

#include <string>
#include <vector>

struct RigidBody {
    int object = 0;
    bool free = false;
    bool everFree = false;
    bool prescribed = false;

    float mass = 0.0f;
    float inertia[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    float invInertia[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    bool pinX = false, pinY = false, pinZ = false;
    bool pinRotX = false, pinRotY = false, pinRot = false;

    double x = 0.0, y = 0.0, z = 0.0;
    double qw = 1.0, qx = 0.0, qy = 0.0, qz = 0.0;
    float vx = 0.0f, vy = 0.0f, vz = 0.0f;
    float omegaX = 0.0f, omegaY = 0.0f, omega = 0.0f;
    float cx = 0.0f, cy = 0.0f, cz = 0.0f;
    float radius = 0.0f;
    float volume = 0.0f;

    float addedMass = 0.0f;
    float addedInertia[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};

    float forceX = 0.0f, forceY = 0.0f, forceZ = 0.0f;
    float torqueX = 0.0f, torqueY = 0.0f, torque = 0.0f;

    std::vector<BodyKeyframe> keys;
    float baseVx = 0.0f, baseVy = 0.0f, baseVz = 0.0f;
    float baseOmegaX = 0.0f, baseOmegaY = 0.0f, baseOmega = 0.0f;

    bool freeAt(double when) const;
    void sampleVelocity(double when);
    void step(double when, float dt);
    void integrate(float dt);
    void advancePose(float dt);
    void applyPins();

    void orientation(double m[9]) const;
    void normaliseOrientation();
};

struct BodyGeometry {
    float cx = 0.0f, cy = 0.0f, cz = 0.0f;
    float radius = 0.0f;
    float volume = 0.0f;
    float inertia[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
};

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
                           int& contactsReported);

void buildRigidBodies(const std::vector<BodyMotion>& motions,
                      const std::vector<BodyGeometry>& geometry,
                      std::vector<RigidBody>& out,
                      float fluidDensity,
                      std::vector<std::string>& notes);
