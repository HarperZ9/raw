#include "raw/image.hpp"
#include <fstream>
#include <algorithm>
namespace raw {
static unsigned char to8(float v){
    v = std::clamp(v, 0.0f, 1.0f); return (unsigned char)(v*255.0f + 0.5f); }
void writePPM(const Buffer<Vec3>& img, const std::string& path){
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << img.w << " " << img.h << "\n255\n";
    for (int y=0;y<img.h;++y) for (int x=0;x<img.w;++x){
        const Vec3& c = img.at(x,y);
        unsigned char rgb[3] = {to8(c.x), to8(c.y), to8(c.z)};
        f.write((char*)rgb, 3); }
}
void writePGM(const Buffer<float>& img, const std::string& path){
    std::ofstream f(path, std::ios::binary);
    f << "P5\n" << img.w << " " << img.h << "\n255\n";
    for (int y=0;y<img.h;++y) for (int x=0;x<img.w;++x){
        unsigned char v = to8(img.at(x,y)); f.write((char*)&v, 1); }
}
}
