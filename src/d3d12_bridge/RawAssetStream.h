#pragma once
// RawAssetStream.h: Zero-Dependency Multi-Threaded NVMe Async Asset Prefetcher for RAW (THE PLAYGROUND).
// Streams Blender .glb/.obj meshlets (RawAssetBridge) and .png/.jpg/.tga textures (RawTextureBridge)
// on a background worker thread with zero main-thread hitching. Original work, MIT.
#include "RawAssetBridge.h"
#include "RawTextureBridge.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace raw::bridge
{

class RawAssetStream {
public:
    ~RawAssetStream() { Shutdown(); }

    void Start();
    void Shutdown();
    void EnqueueMesh(const std::string& path);
    void EnqueueTexture(const std::string& path);

    // Non-blocking drain on the render thread: moves completed assets ready for GPU upload.
    void DrainReady(std::vector<RawMeshAsset>* outMeshes, std::vector<RawTextureAsset>* outTextures);
    uint32_t CompletedCount() const { return m_completed.load(std::memory_order_relaxed); }

private:
    void WorkerLoop();

    std::mutex                 m_mu;
    std::condition_variable    m_cv;
    bool                       m_running = false;
    std::thread                m_worker;
    std::deque<std::string>    m_meshQueue;
    std::deque<std::string>    m_texQueue;
    std::vector<RawMeshAsset>  m_readyMeshes;
    std::vector<RawTextureAsset> m_readyTextures;
    std::atomic<uint32_t>      m_completed{0};
};

} // namespace raw::bridge
