#include "raw/image.hpp"
#include "check.hpp"
#include <cstdio>
#include <fstream>
using namespace raw;
int main() {
    Buffer<Vec3> img; img.resize(2,1);
    img.at(0,0) = {1,0,0}; img.at(1,0) = {0,1,0};
    writePPM(img, "test_out.ppm");
    std::ifstream f("test_out.ppm", std::ios::binary);
    std::string magic; int w,h,mx; f >> magic >> w >> h >> mx;
    CHECK(magic == "P6"); CHECK(w == 2); CHECK(h == 1); CHECK(mx == 255);
    f.get(); // single whitespace after maxval
    unsigned char px[6]; f.read((char*)px, 6);
    CHECK(px[0]==255 && px[1]==0 && px[2]==0); // red
    CHECK(px[3]==0 && px[4]==255 && px[5]==0); // green
    std::remove("test_out.ppm");
    return raw_test_summary();
}
