#include <bricks/BrickGenerator.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace alice2::bricks;
void checkBrickPopulation();
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Expected invalid input to be rejected");
}
int main(int argc, char** argv) {
    try {
        checkBrickPopulation();
        constexpr int nx = 21, ny = 7;
        Eigen::MatrixXd v(nx*ny,3);
        Eigen::MatrixXi f((nx-1)*(ny-1)*2,3);
        std::vector<int> start, end;
        int triangle = 0;
        for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) {
            const int id = y*nx+x;
            const double u = x/double(nx-1), w = y/double(ny-1);
            v.row(id) << 4*u, w, 2*u;
            if (x == 0) start.push_back(id);
            if (x == nx-1) end.push_back(id);
            if (x < nx-1 && y < ny-1) {
                f.row(triangle++) << id,id+1,id+nx+1;
                f.row(triangle++) << id,id+nx+1,id+nx;
            }
        }
        BrickGenerator g;
        g.setMesh(v,f);
        g.computeField(FieldMethod::Height);
        require((g.field()-v.col(2)).norm() < 1e-12, "Height must follow Z");
        g.extractContours(0.25);
        require(g.contours().levels.size() == 7, "Height contour count");
        for (int i = 0; i < g.contours().edges.rows(); ++i) {
            const double level = g.contours().levels(g.contours().levelIndex(i));
            for (int j = 0; j < 2; ++j)
                require(std::abs(g.contours().vertices(g.contours().edges(i,j),2)-level) < 1e-10, "Contours must lie on field levels");
        }
        const int fineCount = g.contours().edges.rows();
        g.extractContours(0.5);
        require(g.contours().edges.rows() < fineCount, "Spacing must change contours");
        rejects([&] { g.extractContours(0); });
        rejects([&] { g.computeField(FieldMethod::ExactGeodesic); });
        require(g.field().size() == 0 && g.contours().edges.size() == 0, "Failed method clears stale output");
        g.setBoundaryVertices(start,end);
        g.computeField(FieldMethod::ExactGeodesic);
        double exactError = 0;
        for (int i = 0; i < v.rows(); ++i)
            exactError = std::max(exactError,std::abs(g.field()(i)-std::sqrt(20.0)*v(i,0)/4));
        require(exactError < 1e-8, "Exact solver should recover planar geodesic distances");
        g.extractContours(0.25);
        require(g.contours().edges.rows() > 0, "Exact geodesic contours");
        g.setBoundaryVertices(end,start);
        g.computeField(FieldMethod::ExactGeodesic);
        for (int id : end) require(g.field()(id) == 0, "Seed edits must invalidate exact cache");
        require(g.field()(start.front()) > 4, "Exact distances must follow new sources");
        g.setBoundaryVertices(start,end);
        g.computeField(FieldMethod::ExactBlend);
        for (int id : start) require(g.field()(id) == 0, "Start blend constraint");
        for (int id : end) require(g.field()(id) == g.blendLength(), "End blend constraint");
        require(g.field().minCoeff() >= 0 && g.field().maxCoeff() <= g.blendLength(), "Blend range");
        g.extractContours(g.blendLength()/10);
        require(g.contours().edges.rows() > 0 && g.contours().vertices.allFinite(), "Blend contours");
        rejects([&] { g.setBoundaryVertices({0},{0}); });
        // Reload exercises OBJ colors, byte RGB, slash face tokens and polygon import.
        const auto path = std::filesystem::temp_directory_path()/"alice2_bricks_check.obj";
        {
            std::ofstream obj(path);
            obj << "v 0 0 0 255 0 0\nv 1 0 0 0 0 255\nv 1 1 0 0 0 1\nv 0 1 0 1 0 0\nf 1 2 3 4\n";
        }
        g.loadObj(path.string());
        std::filesystem::remove(path);
        require(g.startVertices().size() == 2 && g.endVertices().size() == 2, "OBJ color seed preservation");
        require(g.triangles().rows() == 2, "Quad triangulation");
        g.computeField(FieldMethod::Height); g.extractContours(0.1);
        require(g.contours().edges.size() == 0, "Flat Z has no contours");
        g.computeField(FieldMethod::ExactBlend);
        require(g.field().allFinite(), "Flat mesh exact blend");
        rejects([&] { g.loadObj("nonexistent_bricks_input.obj"); });
        require(g.vertices().rows() == 4, "Failed reload must preserve mesh");
        {
            std::ofstream obj(path);
            obj << "v 0 0 0 1 0 0\nv 1 0 1\nv 1 1 1 0 0 1\n"
                   "v 0 0 0\nv 1 1 1 0 0 1\nv 0 1 0 1 0 0\nf 1 2 3\nf 4 5 6\n";
        }
        g.loadObj(path.string());
        std::filesystem::remove(path);
        require(g.vertices().rows() == 4 && g.triangles().rows() == 2, "Corner copies must weld");
        require(g.startVertices().size() == 2 && g.endVertices().size() == 1, "Welding must combine color marks");
        g.computeField(FieldMethod::ExactBlend);
        require(g.field().allFinite(), "Welded exact blend");
        Eigen::MatrixXd badV = v; badV.row(0) = badV.row(1);
        rejects([&] { g.setMesh(badV,f); });
        Eigen::MatrixXd unused(v.rows()+1,3); unused.topRows(v.rows())=v; unused.bottomRows(1).setZero();
        rejects([&] { g.setMesh(unused,f); });
        if (argc > 1) {
            g.loadObj(argv[1]);
            for (auto method : {FieldMethod::Height, FieldMethod::ExactGeodesic, FieldMethod::ExactBlend}) {
                g.computeField(method);
                g.extractContours((g.field().maxCoeff()-g.field().minCoeff())/20);
                g.generateBricks(BrickOptions{});
                std::cout << "Input method " << static_cast<int>(method) << ": " << g.vertices().rows()
                          << " vertices, " << g.triangles().rows() << " triangles, red " << g.startVertices().size()
                          << ", blue " << g.endVertices().size() << ", " << g.contours().edges.rows()
                          << " contour edges, " << g.bricks().size() << " bricks\n";
            }
        }
        std::cout << "Brick fields checks passed; planar exact max error: " << exactError << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
