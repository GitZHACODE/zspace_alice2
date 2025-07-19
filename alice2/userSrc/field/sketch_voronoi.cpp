#define __MAIN__
#ifdef __MAIN__

#include <alice2.h>
#include <sketches/SketchRegistry.h>
#include <computeGeom/scalarField.h>
#include <vector>
#include <cmath>
#include <numeric>
#include <iostream>

using namespace alice2;

#define M_PI 22 / 7

static int NUM_SEED_POINTS = 5;
static Vec3 southDirection = Vec3(0, -1, 0);
static Vec3 viewDirection = Vec3(1, 1, 0);

static float buildingRotation = 0 * DEG_TO_RAD;
static float carveSpacing = 15;
static float offsetCarves = 0.0;

static int fieldRES = 128;

static bool flipCarves = true;
static bool clearBlends = true;
static int blendCounter = 0;

Vec3 coreCenter = Vec3(0, 0, 0);
Vec3 gridSpacing = Vec3(10, 10, 0);
int gridRings = 2;

struct Graph
{
    std::vector<std::pair<Vec3, Vec3>> segments;
    Vec3 color;
};

struct Slabs
{
    ScalarField2D facadeField, coreField, baseField, voronoiField, intersectField, fluteField, resultField;
    std::vector<ScalarField2D> blendSDFs;
    ContourData contours;
    std::vector<Vec3> sites;
    Vec3 min, max;
    int res;
    std::vector<Graph> graphs;
    std::vector<Vec3> sampledPoints;
    std::vector<Vec3> sampledPoints_normal;

    Vec3 FacadeCol = Vec3(0, 0, 0);  // Magenta
    Vec3 CoreCol = Vec3(0.35f, 0.35f, 0.0f);    // Green
    Vec3 VoronoiCol = Vec3(0.8f, 0.8f, 0.8f); // Blue

    void initialize(Vec3 _min, Vec3 _max, int _res)
    {
        min = _min;
        max = _max;
        res = _res;
        facadeField = ScalarField2D(min, max, res, res);
        coreField = ScalarField2D(min, max, res, res);
        baseField = ScalarField2D(min, max, res, res);
        voronoiField = ScalarField2D(min, max, res, res);
        intersectField = ScalarField2D(min, max, res, res);
        resultField = ScalarField2D(min, max, res, res);
        fluteField = ScalarField2D(min, max, res, res);
        makeBaseField();

        // generateSites();
        generateSitesVonNeumann(coreCenter, gridSpacing.x, gridSpacing.y, gridRings);

        updateAll();
    }

    void makeBaseField()
    {
        facadeField.apply_scalar_ellipse(Vec3(0, 0, 0), 15.0f, 22.0f, buildingRotation);
        // facadeField.apply_scalar_rect(Vec3(0, 0, 0), Vec3(15.0f, 24.0f, 0), buildingRotation);
        // coreField.apply_scalar_rect(Vec3(0, 0, 0), Vec3(5.0f, 5.0f, 0), buildingRotation);

        ScalarField2D temp = ScalarField2D(min, max, res, res);

        temp.apply_scalar_ellipse(Vec3(-2, 2, 0), 4.0f, 6.0f, buildingRotation);
        facadeField.boolean_subtract(temp);
        temp.clear_field();
        temp.apply_scalar_ellipse(Vec3(2, 0, 0), 4.0f, 6.0f, buildingRotation);
        facadeField.boolean_subtract(temp);
        temp.clear_field();
        temp.apply_scalar_ellipse(Vec3(0, -2, 0), 4.0f, 6.0f, buildingRotation);
        facadeField.boolean_subtract(temp);

        temp.clear_field();
        coreField.apply_scalar_rect(Vec3(-12, 0, 0), Vec3(5.0f, 5.0f, 0), buildingRotation);
        temp.apply_scalar_rect(Vec3(12, 0, 0), Vec3(5.0f, 5.0f, 0), buildingRotation);
        coreField.boolean_union(temp);

        baseField = facadeField;
        //baseField.boolean_subtract(coreField);
        // baseField.normalise();
    }

    void generateSites()
    {
        // sites.clear();
        // for (int attempts = 0; sites.size() < NUM_SEED_POINTS && attempts < 10000; ++attempts)
        // {
        //     int x = rand() % res;
        //     int y = rand() % res;
        //     if (baseField.get_values()[y * res + x] < 0.0f)
        //     {
        //         Vec3 p = baseField.cellPosition(x, y);
        //         bool tooClose = false;
        //         for (auto &s : sites)
        //             if ((s - p).length() < 10.0f)
        //             {
        //                 tooClose = true;
        //                 break;
        //             }
        //         if (!tooClose)
        //             sites.push_back(p);
        //     }
        // }

        ///////--- grid

        sites.clear();
        int gridSpacing = 10;

        for (float x = min.x; x <= max.x; x += gridSpacing)
        {
            for (float y = min.y; y <= max.y; y += gridSpacing)
            {
                Vec3 p(x, y, 0);
                if (baseField.sample_nearest(p) < 0.0f)
                {
                    sites.push_back(p);
                }
            }
        }
    }

    void generateSitesVonNeumann(const Vec3 &center, float spacingX, float spacingY, int numRings)
    {
        sites.clear();

        // add center point
        sites.push_back(center);

        // for each ring
        for (int r = 1; r <= numRings; ++r)
        {
            for (int dx = -r; dx <= r; ++dx)
            {
                int dy = r - std::abs(dx);
                for (int sign : {-1, 1})
                {
                    int gridY = dy * sign;
                    Vec3 p(center.x + dx * spacingX, center.y + gridY * spacingY, 0);
                    //if (baseField.sample_nearest(p) < 0.0f)
                    //{

                        bool chkRepeat = false;
                        for(auto s : sites)
                        {
                            if(s == p) 
                            {
                                chkRepeat = true;
                                break;
                            }
                        }


                        if(!chkRepeat) sites.push_back(p);
                    //}
                }
            }
        }
    }

    void updateAll()
    {
        computeVoronoi();
        updateIntersect();
        updateResultField();
        computeContours();
    }
    void computeVoronoi()
    {
        voronoiField.apply_scalar_voronoi(sites);

        // voronoiField.apply_scalar_manhattan_voronoi(sites);
        //  voronoiField.normalise();
    }
    void updateIntersect()
    {
        intersectField = voronoiField;
        intersectField.boolean_inverseintersect(baseField);
    }
    void updateResultField() { resultField = intersectField; }

    void carveSDF(float spacingDistance = 3.0f, float offset = 0, bool flip = false)
    {
        sampledPoints.clear();
        sampledPoints_normal.clear();

        std::vector<float> vals_flute = fluteField.get_values();
        int numVals = vals_flute.size();
        vals_flute.assign(numVals, 100000);

        ScalarField2D temp = ScalarField2D(min, max, res, res);

        for (auto &g : graphs)
        {
            if (g.color != FacadeCol)
                continue;

            std::vector<Vec3> polyline;
            if (!g.segments.empty())
                polyline.push_back(g.segments[0].first);
            for (auto &seg : g.segments)
                polyline.push_back(seg.second);

            float totalLength = 0.0f;
            for (size_t i = 1; i < polyline.size(); ++i)
                totalLength += (polyline[i] - polyline[i - 1]).length();

            int numPoints = std::max(1, int(totalLength / spacingDistance));
            float step = totalLength / (numPoints + 1);
            float accumulated = 0.0f;
            size_t segIdx = 1;

            for (int p = 1; p <= numPoints; ++p)
            {
                float target = p * step;
                while (segIdx < polyline.size() && accumulated + (polyline[segIdx] - polyline[segIdx - 1]).length() < target)
                {
                    accumulated += (polyline[segIdx] - polyline[segIdx - 1]).length();
                    segIdx++;
                }
                if (segIdx >= polyline.size())
                    break;

                float segmentLen = (polyline[segIdx] - polyline[segIdx - 1]).length();
                float localT = (target - accumulated) / segmentLen;
                Vec3 pos = polyline[segIdx - 1] * (1.0f - localT) + polyline[segIdx] * localT;

                Vec3 dir = (polyline[segIdx] - polyline[segIdx - 1]).normalized();
                Vec3 normal = facadeField.gradientAt(pos);
                normal.normalize();

                float alignment = southDirection.dot(normal);
                if (alignment < 0)
                    continue;

                float sizeMajor = spacingDistance * 0.5f + std::abs(alignment) * spacingDistance * 0.5f;
                float sizeMinor = sizeMajor * 0.5f;

                sampledPoints.push_back(pos);
                sampledPoints_normal.push_back(normal);

                temp.clear_field();
                temp.apply_scalar_ellipse(pos + normal * offset, (flip) ? sizeMinor : sizeMajor, (flip) ? sizeMajor : sizeMinor, normal.angleBetween(Vec3(1, 0, 0)) * DEG_TO_RAD);

                auto vals_temp = temp.get_values();
                for (int i = 0; i < vals_flute.size(); i++)
                    vals_flute[i] = std::min(vals_flute[i], vals_temp[i]);
            }
        }

        fluteField.set_values(vals_flute);
        resultField = facadeField;

        // flip intersect
        // std::vector<float> vals_temp = resultField.get_values();
        // for(auto & v : vals_temp) v *= -1;

        // resultField.set_values(vals_temp);

        ///-----
        resultField.boolean_subtract(fluteField);
    }

    void computeContours(float level = 0.5f)
    {
        contours = resultField.get_contours(level);
        computeContourColors();

        buildGraphsFromContours();
    }

    void computeContourColors()
    {
        contours.colors.clear();
        for (auto &seg : contours.line_segments)
        {
            Vec3 mid = (seg.first + seg.second) * 0.5f;
            float facadeDist = std::abs(facadeField.sample_nearest(mid));
            float coreDist = std::abs(coreField.sample_nearest(mid));
            float voronoiDist = std::abs(voronoiField.sample_nearest(mid));

            if (facadeDist < coreDist && facadeDist < voronoiDist)
                contours.colors.push_back(FacadeCol); // red
            else if (coreDist < facadeDist && coreDist < voronoiDist)
                contours.colors.push_back(CoreCol); // green
            else
                contours.colors.push_back(VoronoiCol); // blue
        }
    }

    void optimizeSites()
    {
        voronoiField.apply_scalar_voronoi(sites);
        std::vector<Vec3> new_sites(sites.size(), Vec3(0, 0, 0));
        std::vector<int> counts(sites.size(), 0);
        for (int y = 0; y < res; ++y)
            for (int x = 0; x < res; ++x)
            {
                Vec3 p = voronoiField.cellPosition(x, y);
                if (baseField.get_values()[y * res + x] >= 0.0f)
                    continue;
                int c = findClosestSite(p);
                new_sites[c] += p;
                counts[c]++;
            }
        for (size_t i = 0; i < sites.size(); ++i)
            if (counts[i] > 0)
                sites[i] = new_sites[i] * (1.0f / counts[i]);
        updateAll();
    }

    int findClosestSite(const Vec3 &p)
    {
        float minD = std::numeric_limits<float>::max();
        int closest = -1;
        for (size_t i = 0; i < sites.size(); ++i)
        {
            float d = (p - sites[i]).length();
            if (d < minD)
            {
                minD = d;
                closest = int(i);
            }
        }
        return closest;
    }

    void buildGraphsFromContours()
    {
        graphs.clear();
        std::vector<bool> used(contours.line_segments.size(), false);

        for (size_t i = 0; i < contours.line_segments.size(); ++i)
        {
            if (used[i])
                continue;
            Graph g;
            g.color = contours.colors[i];
            std::vector<std::pair<Vec3, Vec3>> sequence = {contours.line_segments[i]};
            used[i] = true;

            bool extended = true;
            while (extended)
            {
                extended = false;
                for (size_t j = 0; j < contours.line_segments.size(); ++j)
                {
                    if (!used[j] && contours.colors[j] == g.color)
                    {
                        auto &seg = contours.line_segments[j];
                        if ((seg.first - sequence.back().second).length() < 0.01f)
                        {
                            sequence.push_back(seg);
                            used[j] = true;
                            extended = true;
                            break;
                        }
                        else if ((seg.second - sequence.back().second).length() < 0.01f)
                        {
                            sequence.push_back({seg.second, seg.first});
                            used[j] = true;
                            extended = true;
                            break;
                        }
                        else if ((seg.second - sequence.front().first).length() < 0.01f)
                        {
                            sequence.insert(sequence.begin(), {seg.first, seg.second});
                            used[j] = true;
                            extended = true;
                            break;
                        }
                        else if ((seg.first - sequence.front().first).length() < 0.01f)
                        {
                            sequence.insert(sequence.begin(), {seg.second, seg.first});
                            used[j] = true;
                            extended = true;
                            break;
                        }
                    }
                }
            }

            // ensure all segments are contiguous
            for (size_t k = 1; k < sequence.size(); ++k)
            {
                if ((sequence[k - 1].second - sequence[k].first).length() > 0.01f)
                {
                    sequence[k] = {sequence[k - 1].second, sequence[k].second};
                }
            }

            g.segments = sequence;
            graphs.push_back(g);
        }

        int counter = 0;
        for(auto&g : graphs)
        {
            if(g.color == FacadeCol) counter++;
        }

        std::cout<<"\n numGraphs Facade : " << counter;
    }

    void saveToBlenSDFStack(bool clear = false)
    {
        if (clear)
            blendSDFs.clear();

        blendSDFs.push_back(resultField);
    }
};

class VoronoiFullSketch : public ISketch
{
public:
    void setup() override
    {
        scene().setBackgroundColor(Vec3(1.0f, 1.0f, 1.0f));
        scene().setShowAxes(false);
        scene().setShowGrid(false);
        slabs.initialize(Vec3(-25.0f, -25.0f, 0.0f), Vec3(25.0f, 25.0f, 0.0f), fieldRES);
    }
    void update(float deltaTime) override {}
    void draw(Renderer &renderer, Camera &camera) override
    {
        // draw south direction line in yellow
        renderer.drawLine(Vec3(0, 0, 0), southDirection * 1.0f, Vec3(1, 1, 0), 3.0f);
        // draw view direction line in cyan
        renderer.drawLine(Vec3(0, 0, 0), viewDirection * 1.0f, Vec3(0, 1, 1), 3.0f);

        for (int y = 0; y < slabs.res; ++y)
            for (int x = 0; x < slabs.res; ++x)
            {
                float d = slabs.resultField.get_values()[y * slabs.res + x];
                Vec3 p = slabs.resultField.cellPosition(x, y);
                if (d > 0.2f)
                    renderer.drawPoint(p, Vec3(1, 1, 1), 2.0f);
                else if (d < -0.2f)
                    renderer.drawPoint(p, Vec3(0.5, 0.5, 0.5), 2.0f);
                else
                    renderer.drawPoint(p, Vec3(0, 0, 0), 2.0f);
            }

        ContourData cd = slabs.resultField.get_contours(0.0f);
        for (auto &seg : cd.line_segments)
        {
            Vec3 p0 = seg.first + Vec3(0.0f, 0.0f, 0);
            Vec3 p1 = seg.second + Vec3(0.0f, 0.0f, 0);
            renderer.drawLine(p0, p1, Vec3(1, 0, 0.75), 2.0f);
        }

        for (auto &g : slabs.graphs)
        {
            for (int i = 0; i < g.segments.size(); i++)
            {
                if (i < g.segments.size() - 1)
                    renderer.drawLine(g.segments[i].first, g.segments[i + 1].first, g.color, 2.0f);
                else
                    renderer.drawLine(g.segments[i].first, g.segments[i].second, g.color, 2.0f);
            }
        }

        for (auto &s : slabs.sites)
            renderer.drawPoint(s, Vec3(1, 0, 0), 8.0f);

        for (int i = 0; i < slabs.sampledPoints.size(); i++)
        {
            renderer.drawPoint(slabs.sampledPoints[i] + slabs.sampledPoints_normal[i] * offsetCarves, Vec3(0, 0.5, 1), 10.0f);
            renderer.drawLine(slabs.sampledPoints[i], slabs.sampledPoints[i] + slabs.sampledPoints_normal[i] * 2.0f, Vec3(0, 0.5, 1), 2.0f);
        }

        ////---------------  UI
        
        renderer.setColor(Vec3(0, 0, 0));
        renderer.drawString("e ->  Export JSON: ", 10, 100);
        renderer.drawString("r ->  Reset ", 10, 130);
        renderer.drawString("b ->  Flip Carves ", 10, 160);

        renderer.drawString("Seeds: " + std::to_string(NUM_SEED_POINTS), 10, 200);
        renderer.drawString("s ->  V Seeds + ", 10, 230);
        renderer.drawString("S ->  V Seeds - ", 10, 260);

        renderer.drawString("Carve Spacing: " + std::to_string(carveSpacing), 10, 300);
        renderer.drawString("k ->  C Spacing + ", 10, 330);
        renderer.drawString("K ->  C Spacing - ", 10, 360);

        renderer.drawString("Carve Center Offset: " + std::to_string(offsetCarves), 10, 400);
        renderer.drawString("m ->  C Offset + ", 10, 430);
        renderer.drawString("M ->  C Offset - ", 10, 460);

        renderer.drawString("Blend Counter: " + std::to_string(blendCounter), 10, 500);
    }
    bool onKeyPress(unsigned char key, int x, int y) override
    {
        if (key == 'r' || key == 'R')
        {
            slabs.makeBaseField();
            // slabs.generateSites();
            slabs.generateSitesVonNeumann(coreCenter, gridSpacing.x, gridSpacing.y, gridRings);

            slabs.updateAll();
            return true;
        }
        if (key == 'o' || key == 'O')
        {
            slabs.optimizeSites();
            return true;
        }
        if (key == 'c' || key == 'C')
        {
            slabs.carveSDF(carveSpacing, offsetCarves, flipCarves);
            return true;
        }

        if (key == 'd' || key == 'D')
        {
            clearBlends = !clearBlends;
            return true;
        }

        if (key == 'a' || key == 'A')
        {
            slabs.saveToBlenSDFStack(clearBlends);
            if (clearBlends)
            {
                blendCounter = 1;
                clearBlends = !clearBlends;
            }
            else
                blendCounter++;
            return true;
        }

        if (key == 's')
            NUM_SEED_POINTS++;
        if (key == 'S')
            NUM_SEED_POINTS--;

        if(key == 'q')
        {
            for (int i = 0; i < 10; i++)
                slabs.optimizeSites();

            for (float i = 1.0f; i < 10.0f; i++)
                {
                    for(float j = 0.0f; j < 3.0f; j+=0.2)
                    {
                    carveSpacing = i; offsetCarves = j;
                    slabs.carveSDF(carveSpacing, offsetCarves, flipCarves);
                    Application::getInstance()->takeScreenshot();
                    }
                }

            return true;
        }
        if (key == 'm')
        {
            offsetCarves += 0.1;
        }

        if (key == 'M')
        {
            offsetCarves -= 0.1;
        }

        if (key == 'k')
            carveSpacing += 1;

        if (key == 'K')
            carveSpacing -= 1;

        if (key == 'q' || key == 'Q')
            exportResultFieldToJson("data.json", slabs);

        if (key == 'b' || key == 'B')
            flipCarves = !flipCarves;

        if (key == 'w')  gridRings++;           
        if(key == 'W')  gridRings--;

                return false;
    }
    std::string getName() const override { return "Voronoi Slabs"; }
    std::string getDescription() const override { return "Contours colored by closest field (facade/core/voronoi)."; }
    std::string getAuthor() const override { return "Alice2 User"; }

private:
    Slabs slabs;

    void exportResultFieldToJson(const std::string &filename, Slabs &_slab)
    {
        std::ofstream file(filename);
        file << "{\n  \"resolution\": [" << _slab.res << ", " << _slab.res << "],\n";
        file << "{\n  \"bounds_min\": [" << _slab.min.x << ", " << _slab.min.y << ", " << _slab.min.z << "],\n";
        file << "{\n  \"bounds_max\": [" << _slab.max.x << ", " << _slab.max.y << ", " << _slab.max.z << "],\n";
        file << "  \"values\": [";
        const auto &values = _slab.resultField.get_values();
        for (size_t i = 0; i < values.size(); ++i)
        {
            file << values[i];
            if (i != values.size() - 1)
                file << ", ";
        }
        file << "]\n}";
        file.close();

        printf("\n JSON exported");
    }
};

// ALICE2_REGISTER_SKETCH_AUTO(VoronoiFullSketch)

#endif // __MAIN__
