#include "Multigrid.hpp"
#include "TestHarness.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr double PI = 3.14159265358979323846;

double exactPressure(double x, double y, double Lx, double Ly) {
    return std::cos(PI * x / (2.0 * Lx)) * std::cos(PI * y / Ly);
}

double lambdaFor(double Lx, double Ly) {
    const double kx = PI / (2.0 * Lx);
    const double ky = PI / Ly;
    return kx * kx + ky * ky;
}

double solveAndMeasure(int n) {
    const double Lx = 1.0, Ly = 1.0;
    const float dx = static_cast<float>(Lx / n);
    const float dy = static_cast<float>(Ly / n);

    Multigrid multigrid(n, n, 1, dx, dy, dx, 4);
    multigrid.setGeometry(std::vector<uint8_t>(static_cast<size_t>(n) * n, 0));

    std::vector<float> pressure(static_cast<size_t>(n) * n, 0.0f);
    std::vector<float> rhs(static_cast<size_t>(n) * n, 0.0f);
    std::vector<float> exact(static_cast<size_t>(n) * n, 0.0f);

    const double lambda = lambdaFor(Lx, Ly);
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const double x = (i + 0.5) * dx;
            const double y = (j + 0.5) * dy;
            const double value = exactPressure(x, y, Lx, Ly);
            exact[j * n + i] = static_cast<float>(value);
            rhs[j * n + i] = static_cast<float>(-lambda * value);
        }
    }

    multigrid.solve(pressure, rhs, 1.15f, 1.85f, 400, 1e-10f);

    double worst = 0.0;
    for (size_t id = 0; id < pressure.size(); ++id)
        worst = std::max(worst,
                         std::fabs(double(pressure[id]) - double(exact[id])));
    return worst;
}

int checkCoefficientScaling() {
    const int n = 32;
    const float d = 1.0f / n;
    std::vector<uint8_t> solid(static_cast<size_t>(n) * n, 0);
    std::vector<float> rhs(static_cast<size_t>(n) * n, 0.0f);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            rhs[j * n + i] =
                static_cast<float>(std::sin(3.0 * PI * (i + 0.5) / n) *
                                   std::cos(2.0 * PI * (j + 0.5) / n));

    std::vector<float> plain(rhs.size(), 0.0f);
    std::vector<float> scaled(rhs.size(), 0.0f);

    Multigrid a(n, n, 1, d, d, d, 4);
    a.setGeometry(solid);
    a.solve(plain, rhs, 1.15f, 1.85f, 400, 1e-10f);

    Multigrid b(n, n, 1, d, d, d, 4);
    b.setGeometry(solid);
    b.setCoefficients(
        std::vector<float>(static_cast<size_t>(n + 1) * n, 2.0f),
        std::vector<float>(static_cast<size_t>(n) * (n + 1), 2.0f));
    b.solve(scaled, rhs, 1.15f, 1.85f, 400, 1e-10f);

    double worst = 0.0, scale = 0.0;
    for (size_t id = 0; id < plain.size(); ++id) {
        scale = std::max(scale, std::fabs(double(plain[id])));
        worst = std::max(worst,
                         std::fabs(2.0 * double(scaled[id]) - double(plain[id])));
    }
    if (scale <= 0.0)
        return testing::fail("the reference solve produced nothing");

    const double relative = worst / scale;
    testing::report("doubled face weights: relative miss " +
                    std::to_string(relative));
    if (!(relative < 2e-3))
        return testing::fail("setCoefficients does not scale the operator");
    return 0;
}

int checkSingularCase() {
    const int n = 32;
    const float d = 1.0f / n;

    Multigrid multigrid(n, n, 1, d, d, d, 4);
    MultigridBC closed;
    closed.left = closed.right = PressureSideBC::Neumann;
    closed.bottom = closed.top = PressureSideBC::Neumann;
    multigrid.setPressureBC(closed);
    multigrid.setGeometry(std::vector<uint8_t>(static_cast<size_t>(n) * n, 0));

    if (!multigrid.singularPressure())
        return testing::fail("a closed box was not recognised as singular");

    std::vector<float> pressure(static_cast<size_t>(n) * n, 0.0f);
    std::vector<float> rhs(static_cast<size_t>(n) * n, 0.0f);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            rhs[j * n + i] =
                static_cast<float>(std::cos(2.0 * PI * (i + 0.5) / n) *
                                   std::cos(2.0 * PI * (j + 0.5) / n));

    const float residual =
        multigrid.solve(pressure, rhs, 1.15f, 1.85f, 200, 1e-8f);
    double mean = 0.0;
    for (float value : pressure)
        mean += value;
    mean /= static_cast<double>(pressure.size());

    testing::report("closed box: residual " + std::to_string(residual) +
                    ", mean " + std::to_string(mean));
    if (!testing::allFinite(pressure))
        return testing::fail("the closed box solve produced non-numbers");
    if (!(residual < 1e-5f))
        return testing::fail("the closed box solve did not converge");
    if (!(std::fabs(mean) < 1e-4))
        return testing::fail("the constant was not taken out of the answer");
    return 0;
}

int checkJumpCoefficients() {
    const int nx = 96, ny = 64;
    const float dx = 0.6f / nx, dy = 0.4f / ny;
    const size_t cells = static_cast<size_t>(nx) * ny;

    Multigrid multigrid(nx, ny, 1, dx, dy, dx, 8);
    MultigridBC bc;
    bc.left = bc.right = bc.bottom = PressureSideBC::Neumann;
    bc.top = PressureSideBC::Dirichlet;
    multigrid.setPressureBC(bc);
    multigrid.setGeometry(std::vector<uint8_t>(cells, 0));

    std::vector<float> density(cells, 1.225f);
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i)
            if ((j + 0.5f) * dy < 0.3f && (i + 0.5f) * dx < 0.15f)
                density[j * nx + i] = 1000.0f;

    std::vector<float> faceX(static_cast<size_t>(nx + 1) * ny, 1.0f);
    std::vector<float> faceY(static_cast<size_t>(nx) * (ny + 1), 1.0f);
    for (int j = 0; j < ny; ++j) {
        for (int i = 1; i < nx; ++i)
            faceX[j * (nx + 1) + i] =
                0.5f * (1.0f / density[j * nx + i - 1] +
                        1.0f / density[j * nx + i]);
        faceX[j * (nx + 1)] = 1.0f / density[j * nx];
        faceX[j * (nx + 1) + nx] = 1.0f / density[j * nx + nx - 1];
    }
    for (int j = 1; j < ny; ++j)
        for (int i = 0; i < nx; ++i)
            faceY[j * nx + i] =
                0.5f * (1.0f / density[(j - 1) * nx + i] +
                        1.0f / density[j * nx + i]);
    for (int i = 0; i < nx; ++i) {
        faceY[i] = 1.0f / density[i];
        faceY[ny * nx + i] = 1.0f / density[(ny - 1) * nx + i];
    }
    multigrid.setCoefficients(faceX, faceY);

    std::vector<float> pressure(cells, 0.0f);
    std::vector<float> rhs(cells, 0.0f);
    for (size_t id = 0; id < cells; ++id)
        rhs[id] = -9.81f / density[id];

    double norm = 0.0;
    for (float value : rhs)
        norm += static_cast<double>(value) * value;
    const float scale = static_cast<float>(std::sqrt(norm));

    const float residual =
        multigrid.solve(pressure, rhs, 1.15f, 1.85f, 60, 1e-6f, scale);
    testing::report("816:1 jump: residual " + std::to_string(residual) +
                    " after " + std::to_string(multigrid.cyclesUsed()) +
                    " cycles");
    if (!testing::allFinite(pressure))
        return testing::fail("the jump solve produced non-numbers");
    if (!(residual < 1e-4f))
        return testing::fail("the solve across a density jump did not "
                             "converge, which is a two phase run producing "
                             "nothing but noise");
    return 0;
}

}

int main() {
    const double e32 = solveAndMeasure(32);
    const double e64 = solveAndMeasure(64);
    const double e128 = solveAndMeasure(128);

    const double first = std::log2(e32 / e64);
    const double second = std::log2(e64 / e128);

    std::printf("  32:%.3e  64:%.3e  128:%.3e   order %.2f then %.2f\n",
                e32, e64, e128, first, second);

    if (!(e128 < e64 && e64 < e32))
        return testing::fail("refining the grid did not reduce the error");
    if (!(first > 1.8 && second > 1.8))
        return testing::fail("the Poisson solve is not second order any more");

    if (int rc = checkCoefficientScaling())
        return rc;
    if (int rc = checkJumpCoefficients())
        return rc;
    if (int rc = checkSingularCase())
        return rc;

    std::printf("PoissonTests OK\n");
    return 0;
}
