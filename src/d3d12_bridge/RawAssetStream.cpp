// RawAssetStream.cpp: Background NVMe async mesh/texture prefetch worker for RAW.
// Original work, MIT.
#include "RawAssetStream.h"

namespace raw::bridge
{

void RawAssetStream::Start()
{
    std::lock_guard<std::mutex> lk(m_mu);
    if (m_running) return;
    m_running = true;
    m_worker = std::thread(&RawAssetStream::WorkerLoop, this);
}

void RawAssetStream::Shutdown()
{
    {
        std::lock_guard<std::mutex> lk(m_mu);
        if (!m_running) return;
        m_running = false;
    }
    m_cv.notify_all();
    if (m_worker.joinable()) m_worker.join();
}

void RawAssetStream::EnqueueMesh(const std::string& path)
{
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_meshQueue.push_back(path);
    }
    m_cv.notify_one();
}

void RawAssetStream::EnqueueTexture(const std::string& path)
{
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_texQueue.push_back(path);
    }
    m_cv.notify_one();
}

void RawAssetStream::DrainReady(std::vector<RawMeshAsset>* outMeshes, std::vector<RawTextureAsset>* outTextures)
{
    std::lock_guard<std::mutex> lk(m_mu);
    if (outMeshes && !m_readyMeshes.empty()) {
        outMeshes->insert(outMeshes->end(), std::make_move_iterator(m_readyMeshes.begin()),
                          std::make_move_iterator(m_readyMeshes.end()));
        m_readyMeshes.clear();
    }
    if (outTextures && !m_readyTextures.empty()) {
        outTextures->insert(outTextures->end(), std::make_move_iterator(m_readyTextures.begin()),
                            std::make_move_iterator(m_readyTextures.end()));
        m_readyTextures.clear();
    }
}

void RawAssetStream::WorkerLoop()
{
    while (true) {
        std::string path;
        bool isMesh = false;
        {
            std::unique_lock<std::mutex> lk(m_mu);
            m_cv.wait(lk, [&] { return !m_running || !m_meshQueue.empty() || !m_texQueue.empty(); });
            if (!m_running) break;
            if (!m_meshQueue.empty()) { path = std::move(m_meshQueue.front()); m_meshQueue.pop_front(); isMesh = true; }
            else if (!m_texQueue.empty()) { path = std::move(m_texQueue.front()); m_texQueue.pop_front(); isMesh = false; }
        }
        if (path.empty()) continue;
        if (isMesh) {
            RawMeshAsset m;
            if (LoadModernMeshAsset(path, &m)) {
                std::lock_guard<std::mutex> lk(m_mu);
                m_readyMeshes.push_back(std::move(m));
                m_completed.fetch_add(1u, std::memory_order_relaxed);
            }
        } else {
            RawTextureAsset t;
            if (LoadModernTextureAsset(path, &t)) {
                std::lock_guard<std::mutex> lk(m_mu);
                m_readyTextures.push_back(std::move(t));
                m_completed.fetch_add(1u, std::memory_order_relaxed);
            }
        }
    }
}

} // namespace raw::bridge
