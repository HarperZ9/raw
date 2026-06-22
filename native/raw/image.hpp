#pragma once
#include "raw/vec.hpp"
#include <vector>
#include <string>
namespace raw {
template<class T> struct Buffer {
    int w{0}, h{0};
    std::vector<T> px;
    void resize(int W, int H){ w=W; h=H; px.assign((size_t)W*H, T{}); }
    T& at(int x,int y){ return px[(size_t)y*w + x]; }
    const T& at(int x,int y) const { return px[(size_t)y*w + x]; }
};
void writePPM(const Buffer<Vec3>& img, const std::string& path);
void writePGM(const Buffer<float>& img, const std::string& path);
}
