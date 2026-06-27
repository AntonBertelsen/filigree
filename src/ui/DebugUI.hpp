#pragma once

#include <vector>
#include <string>
#include "renderer/GpuTimestampPool.hpp"

class Engine;

class DebugUI {
public:
    DebugUI();
    ~DebugUI();

    void update(Engine& engine, float deltaTime);

    bool wantCaptureMouse() const;
    bool wantCaptureKeyboard() const;
    bool shouldSuspendCamera() const;

private:
    void drawPerformanceOverlay(Engine& engine);
    void drawControlPanel(Engine& engine);
    void drawBenchmarkPanel(Engine& engine, float deltaTime);
    void drawGpuTimingPanel(Engine& engine);
    void runBenchmarkCycle(Engine& engine, float deltaTime);

    // Benchmark state
    bool benchmarkRunning = false;
    float benchmarkTimer = 0.0f;
    std::vector<float> benchmarkFrameTimes;

    struct BenchmarkResults {
        float avgFps = 0.0f;
        float minFps = 0.0f;
        float maxFps = 0.0f;
        float p99Fps = 0.0f;
        std::string modeDescription;
    };
    std::vector<BenchmarkResults> pastBenchmarkResults;

    // Frametime history for GUI plot
    static constexpr size_t HIST_SIZE = 120;
    float frameTimeHistory[HIST_SIZE] = { 0.0f };
    size_t frameTimeHistoryIdx = 0;
};
