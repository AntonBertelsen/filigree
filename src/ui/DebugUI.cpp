#include "ui/DebugUI.hpp"
#include "core/Engine.hpp"
#include "core/VulkanContext.hpp"
#include "renderer/VulkanRenderer.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <vk_mem_alloc.h>
#include <algorithm>
#include <iostream>

DebugUI::DebugUI() {
    std::fill(std::begin(frameTimeHistory), std::end(frameTimeHistory), 0.0f);

    // Initialize ImGui Context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Set custom premium dark style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.5f;

    // Use nice slate colors for premium look
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                   = ImVec4(0.90f, 0.92f, 0.96f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.08f, 0.09f, 0.12f, 0.90f);
    colors[ImGuiCol_Border]                 = ImVec4(0.20f, 0.22f, 0.27f, 1.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.15f, 0.16f, 0.21f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.24f, 0.27f, 0.35f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.29f, 0.34f, 0.44f, 1.00f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.08f, 0.09f, 0.12f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
    colors[ImGuiCol_Button]                 = ImVec4(0.20f, 0.26f, 0.38f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.28f, 0.36f, 0.52f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.35f, 0.45f, 0.65f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.18f, 0.22f, 0.30f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.25f, 0.31f, 0.42f, 1.00f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.30f, 0.37f, 0.50f, 1.00f);
}

DebugUI::~DebugUI() {
    ImGui::DestroyContext();
}

void DebugUI::update(Engine& engine, float deltaTime) {
    // Start ImGui frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Update frametime history plot data
    frameTimeHistory[frameTimeHistoryIdx] = deltaTime * 1000.0f;
    frameTimeHistoryIdx = (frameTimeHistoryIdx + 1) % HIST_SIZE;

    // Run benchmark timer and accumulator if running
    runBenchmarkCycle(engine, deltaTime);

    // Render panels
    drawPerformanceOverlay(engine);
    drawControlPanel(engine);
    drawBenchmarkPanel(engine, deltaTime);
    drawGpuTimingPanel(engine);

    // Render ImGui draw lists
    ImGui::Render();
}

bool DebugUI::wantCaptureMouse() const {
    return ImGui::GetIO().WantCaptureMouse;
}

bool DebugUI::wantCaptureKeyboard() const {
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool DebugUI::shouldSuspendCamera() const {
    return benchmarkRunning;
}

void DebugUI::drawPerformanceOverlay(Engine& engine) {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 240), ImGuiCond_FirstUseEver);
    ImGui::Begin("Performance Statistics", nullptr, ImGuiWindowFlags_NoScrollbar);

    float fps = ImGui::GetIO().Framerate;
    float avgFrameTime = 1000.0f / fps;
    ImGui::Text("FPS: %.1f (%.2f ms)", fps, avgFrameTime);

    // Plot frametime history
    ImGui::PlotHistogram("##Frametime", frameTimeHistory, HIST_SIZE, 0, nullptr, 0.0f, 33.3f, ImVec2(-1, 60));

    ImGui::Separator();

    uint32_t currentFrame = engine.context->getCurrentFrameIndex();
    uint32_t totalDrawCount = 0;
    uint32_t totalSoftwareDrawCount = 0;
    bool isNanite = (engine.geometryPipeline == Engine::GeometryPipeline::NANITE);

    if (isNanite) {
        if (engine.gpuScene.drawCountBuffer[currentFrame] != VK_NULL_HANDLE) {
            void* mappedData = nullptr;
            vmaMapMemory(engine.context->getAllocator(), engine.gpuScene.drawCountAllocation[currentFrame], &mappedData);
            totalDrawCount = *static_cast<uint32_t*>(mappedData);
            vmaUnmapMemory(engine.context->getAllocator(), engine.gpuScene.drawCountAllocation[currentFrame]);
        }
        if (engine.gpuScene.softwareDrawCountBuffer[currentFrame] != VK_NULL_HANDLE) {
            void* mappedData = nullptr;
            vmaMapMemory(engine.context->getAllocator(), engine.gpuScene.softwareDrawCountAllocation[currentFrame], &mappedData);
            totalSoftwareDrawCount = *static_cast<uint32_t*>(mappedData);
            vmaUnmapMemory(engine.context->getAllocator(), engine.gpuScene.softwareDrawCountAllocation[currentFrame]);
        }
    }

    ImGui::Text("Geometry Pipeline: %s", isNanite ? "NANITE" : "TRADITIONAL");
    ImGui::Text("Total Clusters in Scene: %u", engine.gpuScene.totalCullTasks);
    if (isNanite) {
        ImGui::Text("Visible HW Clusters: %u", totalDrawCount);
        ImGui::Text("Visible SW Clusters: %u", totalSoftwareDrawCount);
        ImGui::Text("Culled Clusters: %u", engine.gpuScene.totalCullTasks - (totalDrawCount + totalSoftwareDrawCount));
    }

    ImGui::End();
}

void DebugUI::drawControlPanel(Engine& engine) {
    ImGui::SetNextWindowPos(ImVec2(10, 260), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 480), ImGuiCond_FirstUseEver);
    ImGui::Begin("Configuration & Controls");

    // Asset Selection
    int modelIdx = static_cast<int>(engine.activeModelIndex);
    const char* modelNames[] = { "Bunny", "Dragon", "Lucy", "Scattered Forest (120 instances)" };
    if (ImGui::Combo("Active Asset", &modelIdx, modelNames, 4)) {
        engine.activeModelIndex = static_cast<uint32_t>(modelIdx);
        engine.updateSceneInstances();
    }

    // Shading Path selection
    int path = static_cast<int>(engine.shadingPath);
    const char* paths[] = { "FORWARD", "DEFERRED (VisBuffer)" };
    if (ImGui::Combo("Shading Path", &path, paths, 2)) {
        engine.shadingPath = static_cast<Engine::ShadingPath>(path);
    }

    int geomPipe = static_cast<int>(engine.geometryPipeline);
    const char* geomPipes[] = { "TRADITIONAL (Standard)", "NANITE (Meshlets)" };
    if (ImGui::Combo("Geometry Pipeline", &geomPipe, geomPipes, 2)) {
        engine.geometryPipeline = static_cast<Engine::GeometryPipeline>(geomPipe);
    }

    if (engine.geometryPipeline == Engine::GeometryPipeline::NANITE) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "Nanite Rasterization Path:");

        int mode = static_cast<int>(engine.rasterizerMode);
        const char* modes[] = { "PURE_HARDWARE", "PURE_SOFTWARE", "HYBRID" };
        if (ImGui::Combo("Rasterizer Mode", &mode, modes, 3)) {
            engine.rasterizerMode = static_cast<Engine::RasterizerMode>(mode);
        }

        int hwPath = static_cast<int>(engine.hwPathMode);
        const char* hwModes[] = { "PURE_UAV (No Depth Buffer)", "DEPTH_TESTED (Early-Z)" };
        if (ImGui::Combo("Hardware Path Mode", &hwPath, hwModes, 2)) {
            engine.hwPathMode = static_cast<Engine::HardwarePathMode>(hwPath);
        }

        int sync = static_cast<int>(engine.syncMode);
        const char* syncModes[] = { "SEQUENTIAL (Sync Barrier)", "PARALLEL (Overlap / Concurrent)" };
        if (ImGui::Combo("Pipeline Sync Mode", &sync, syncModes, 2)) {
            engine.syncMode = static_cast<Engine::SyncMode>(sync);
        }

        int vbMode = static_cast<int>(engine.visBufferMode);
        const char* vbModes[] = { "SINGLE_PASS_64BIT (Hardware UAV)", "TWO_PASS_32BIT (M1 Fallback / Early-Z)" };
        if (!engine.context->isShaderInt64AtomicsSupported()) {
            ImGui::BeginDisabled();
            ImGui::Combo("VisBuffer Storage Mode", &vbMode, vbModes, 2);
            ImGui::EndDisabled();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "64-bit atomics unsupported on this GPU.");
        } else {
            if (ImGui::Combo("VisBuffer Storage Mode", &vbMode, vbModes, 2)) {
                engine.visBufferMode = static_cast<Engine::VisBufferMode>(vbMode);
                engine.recreateSwapChain();
            }
        }

        if (engine.rasterizerMode == Engine::RasterizerMode::HYBRID) {
            ImGui::SliderFloat("Software Size Threshold (px)", &engine.sizeThreshold, 1.0f, 256.0f, "%.1f");
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "Occlusion Culling & LOD:");

        ImGui::Checkbox("LOD Selection System", &engine.lodEnabled);
        if (engine.lodEnabled) {
            ImGui::SliderFloat("LOD Error Threshold (px)", &engine.lodThreshold, 0.1f, 20.0f, "%.1f");
        }

        ImGui::Checkbox("HZB Occlusion Culling", &engine.hzbCullingEnabled);
        ImGui::Checkbox("Freeze Culling Frustum", &engine.freezeCulling);

        if (!engine.context->isDrawIndirectCountSupported()) {
            ImGui::Checkbox("Fallback DrawCount Optimization", &engine.enableDrawCountOptimization);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Limits fallback indirect draw count via GPU-to-CPU readback to avoid driver overhead from empty commands on macOS.");
            }
        }
    }


    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Display:");
    bool vsync = engine.context->isVsyncEnabled();
    if (ImGui::Checkbox("VSync", &vsync)) {
        engine.context->setVsyncEnabled(vsync);
        engine.recreateSwapChain(); // apply new present mode immediately
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(uncap for benchmarking)");

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1.0f), "Debug Visualizers:");
    ImGui::Checkbox("Draw Bounding Spheres", &engine.drawBoundingSpheres);
    ImGui::Checkbox("HZB Mip Visualizer", &engine.debugVisualiseHzb);
    if (engine.debugVisualiseHzb) {
        int mip = static_cast<int>(engine.debugHzbMipLevel);
        if (ImGui::SliderInt("Debug HZB Mip Level", &mip, 0, 10)) {
            engine.debugHzbMipLevel = static_cast<uint32_t>(mip);
        }
    }

    int dbgMode = static_cast<int>(engine.visBufferDebugMode);
    const char* dbgModes[] = { "SHADED", "NEUTRAL", "TRIANGLE_ID", "BARYCENTRICS", "MESHLET_ID" };
    if (ImGui::Combo("VisBuffer Debug Mode", &dbgMode, dbgModes, 5)) {
        engine.visBufferDebugMode = static_cast<uint32_t>(dbgMode);
    }

    ImGui::End();
}

void DebugUI::drawBenchmarkPanel(Engine& engine, float deltaTime) {
    ImGui::SetNextWindowPos(ImVec2(400, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 360), ImGuiCond_FirstUseEver);
    ImGui::Begin("Performance Benchmarking");

    if (benchmarkRunning) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "BENCHMARK RUNNING: %.1fs / 5.0s", benchmarkTimer);
        ImGui::ProgressBar(benchmarkTimer / 5.0f);
    } else {
        if (ImGui::Button("Run 5-Second Benchmark Test")) {
            benchmarkRunning = true;
            benchmarkTimer = 0.0f;
            benchmarkFrameTimes.clear();
            benchmarkFrameTimes.reserve(1000);
        }
    }

    ImGui::Separator();
    ImGui::Text("Saved Benchmark Results:");

    if (ImGui::BeginTable("BenchmarkResultsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Render Mode / Setup", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Avg FPS", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Min FPS", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("1% Low", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();

        for (const auto& res : pastBenchmarkResults) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(res.modeDescription.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.1f", res.avgFps);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.1f", res.minFps);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.1f", res.p99Fps);
        }
        ImGui::EndTable();
    }

    if (!pastBenchmarkResults.empty() && ImGui::Button("Clear Results")) {
        pastBenchmarkResults.clear();
    }

    ImGui::End();
}

void DebugUI::drawGpuTimingPanel(Engine& engine) {
    const GpuTimestampPool& pool = engine.renderer->getTimestampPool();
    uint32_t frameIdx = engine.context->getCurrentFrameIndex();

    ImGui::SetNextWindowPos(ImVec2(10, 430), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 280), ImGuiCond_FirstUseEver);
    ImGui::Begin("GPU Pass Timing");

    if (!pool.isSupported()) {
        ImGui::TextColored(ImVec4(1,0.4f,0.4f,1), "Timestamps not supported on this GPU.");
        ImGui::End();
        return;
    }

    // Pair: (label, begin_ts, end_ts, color)
    struct PassInfo { const char* label; GpuTimestamp begin; GpuTimestamp end; ImVec4 color; };
    PassInfo passes[] = {
        { "Cull",         TS_FRAME_START,  TS_CULL_END,       {0.4f, 0.8f, 0.4f, 1.0f} },
        { "SW Rasterizer",TS_CULL_END,     TS_SW_RASTER_END,  {1.0f, 0.4f, 0.2f, 1.0f} },
        { "HW Rasterizer",TS_SW_RASTER_END,TS_HW_RASTER_END,  {0.3f, 0.6f, 1.0f, 1.0f} },
        { "Resolve",      TS_HW_RASTER_END,TS_RESOLVE_END,    {0.8f, 0.5f, 0.2f, 1.0f} },
        { "HZB",          TS_RESOLVE_END,  TS_HZB_END,        {0.7f, 0.3f, 0.8f, 1.0f} },
        { "Debug Overlay",TS_HZB_END,      TS_DEBUG_END,       {0.5f, 0.5f, 0.5f, 1.0f} },
    };

    float totalGpuMs = pool.elapsedMs(TS_FRAME_START, TS_FRAME_END, frameIdx);

    ImGui::Text("Total GPU frame: %.3f ms  (%.1f fps budget)",
                totalGpuMs, totalGpuMs > 0.0f ? 1000.0f / totalGpuMs : 0.0f);
    ImGui::Separator();

    // Stacked bar chart
    float barWidth  = ImGui::GetContentRegionAvail().x;
    float barHeight = 20.0f;
    ImVec2 barStart = ImGui::GetCursorScreenPos();
    ImDrawList* dl  = ImGui::GetWindowDrawList();

    float cursor = 0.0f;
    for (auto& p : passes) {
        float ms = pool.elapsedMs(p.begin, p.end, frameIdx);
        float frac = (totalGpuMs > 0.0f) ? ms / totalGpuMs : 0.0f;
        float w = frac * barWidth;
        ImU32 col = ImGui::ColorConvertFloat4ToU32(p.color);
        dl->AddRectFilled(
            ImVec2(barStart.x + cursor, barStart.y),
            ImVec2(barStart.x + cursor + w, barStart.y + barHeight),
            col
        );
        cursor += w;
    }
    // Advance cursor past the bar
    ImGui::Dummy(ImVec2(barWidth, barHeight));

    ImGui::Separator();

    // Detailed table
    if (ImGui::BeginTable("GpuPassTable", 3,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("Pass",      ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("ms",        ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("%",         ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableHeadersRow();

        for (auto& p : passes) {
            float ms   = pool.elapsedMs(p.begin, p.end, frameIdx);
            float pct  = (totalGpuMs > 0.0f) ? 100.0f * ms / totalGpuMs : 0.0f;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(p.color, "%s", p.label);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", ms);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.1f%%", pct);
        }

        // Total row
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(ImVec4(1,1,1,1), "TOTAL");
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.3f", totalGpuMs);
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("100%%");

        ImGui::EndTable();
    }

    ImGui::End();
}

void DebugUI::runBenchmarkCycle(Engine& engine, float deltaTime) {
    if (!benchmarkRunning) return;

    benchmarkTimer += deltaTime;
    benchmarkFrameTimes.push_back(deltaTime * 1000.0f); // in ms

    if (benchmarkTimer >= 5.0f) {
        benchmarkRunning = false;
        benchmarkTimer = 0.0f;

        if (benchmarkFrameTimes.empty()) return;

        std::vector<float> sortedTimes = benchmarkFrameTimes;
        std::sort(sortedTimes.begin(), sortedTimes.end());

        float sum = 0.0f;
        for (float t : sortedTimes) {
            sum += t;
        }
        float avgMs = sum / sortedTimes.size();
        float avgFps = 1000.0f / avgMs;

        float maxMs = sortedTimes.back();
        float minFps = 1000.0f / maxMs;

        float minMs = sortedTimes.front();
        float maxFps = 1000.0f / minMs;

        // 99th percentile frame time for 1% Low FPS
        size_t p99Index = static_cast<size_t>(sortedTimes.size() * 0.99f);
        p99Index = std::min(p99Index, sortedTimes.size() - 1);
        float p99Ms = sortedTimes[p99Index];
        float p99Fps = 1000.0f / p99Ms;

        std::string modeDesc;
        if (engine.geometryPipeline == Engine::GeometryPipeline::TRADITIONAL) {
            modeDesc = "Traditional";
        } else {
            std::vector<std::string> rasterModes = { "Pure HW", "Pure SW", "Hybrid" };
            std::vector<std::string> hwModes = { "Pure UAV", "Depth Tested" };
            std::vector<std::string> syncModes = { "Seq", "Par" };
            std::vector<std::string> vbModes = { "64b", "32b" };
            modeDesc = std::string("Nanite: ") + rasterModes[static_cast<int>(engine.rasterizerMode)] + 
                       " (" + hwModes[static_cast<int>(engine.hwPathMode)] + ", " + 
                       syncModes[static_cast<int>(engine.syncMode)] + ", " +
                       vbModes[static_cast<int>(engine.visBufferMode)] + ")";
        }

        BenchmarkResults res{};
        res.avgFps = avgFps;
        res.minFps = minFps;
        res.maxFps = maxFps;
        res.p99Fps = p99Fps;
        res.modeDescription = modeDesc;

        pastBenchmarkResults.push_back(res);
        benchmarkFrameTimes.clear();

        std::cout << "[Benchmark Complete] Mode: " << modeDesc 
                  << " | Avg FPS: " << avgFps 
                  << " | Min FPS: " << minFps 
                  << " | 1% Low FPS: " << p99Fps << std::endl;
    }
}
