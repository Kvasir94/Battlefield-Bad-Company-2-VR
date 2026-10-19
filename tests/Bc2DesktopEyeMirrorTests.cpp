#include "Bc2DesktopEyeMirror.h"
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace {
using Color = std::array<unsigned char, 4>;
constexpr Color Waiting{6, 9, 13, 255}, EyeColor{41, 117, 203, 255};
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Hr(HRESULT result, const char* operation) {
    if (FAILED(result)) throw std::runtime_error(std::string(operation) + " failed: " + std::to_string(result));
}
struct CaptureFolder {
    std::filesystem::path root;
    CaptureFolder() {
        const auto temp = std::filesystem::temp_directory_path();
        for (unsigned attempt = 0; attempt != 100; ++attempt) {
            root = temp / ("fvr-desktop-mirror-test-" + std::to_string(GetCurrentProcessId()) + "-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(attempt));
            if (std::filesystem::create_directory(root)) return;
        }
        throw std::runtime_error("Cannot create unique capture directory");
    }
    ~CaptureFolder() {
        // root is the unique directory successfully created by this fixture.
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};
struct Device {
    HWND window = nullptr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> chain;
    Device(unsigned width = 64, unsigned height = 48) {
        window = CreateWindowExW(0, L"STATIC", L"Desktop mirror test", WS_OVERLAPPEDWINDOW,
            0, 0, 128, 128, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Require(window != nullptr, "CreateWindowExW failed");
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width = width; desc.BufferDesc.Height = height;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 1; desc.OutputWindow = window; desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        const auto result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &desc,
            &chain, &device, nullptr, &context);
        if (FAILED(result)) { DestroyWindow(window); window = nullptr; Hr(result, "D3D11CreateDeviceAndSwapChain"); }
    }
    ~Device() { chain.Reset(); context.Reset(); device.Reset(); if (window) DestroyWindow(window); }
    ComPtr<ID3D11Texture2D> Back() {
        ComPtr<ID3D11Texture2D> back;
        Hr(chain->GetBuffer(0, IID_PPV_ARGS(&back)), "GetBuffer"); return back;
    }
    ComPtr<ID3D11Texture2D> Eye(unsigned width, unsigned height) {
        std::vector<Color> pixels(std::size_t(width) * height, EyeColor);
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        D3D11_SUBRESOURCE_DATA data{pixels.data(), width * 4, 0};
        ComPtr<ID3D11Texture2D> eye;
        Hr(device->CreateTexture2D(&desc, &data, &eye), "Create eye texture"); return eye;
    }
    void NativeColor() {
        auto back = Back(); ComPtr<ID3D11RenderTargetView> view;
        Hr(device->CreateRenderTargetView(back.Get(), nullptr, &view), "Create native RTV");
        constexpr float red[]{1, 0, 0, 1}; context->ClearRenderTargetView(view.Get(), red);
    }
    void CheckPixels(Color expected) {
        auto back = Back(); D3D11_TEXTURE2D_DESC desc{}; back->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = desc.MiscFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> read;
        Hr(device->CreateTexture2D(&desc, nullptr, &read), "Create readback");
        context->CopyResource(read.Get(), back.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        // Blocking Map also completes all earlier capture copies on this context.
        Hr(context->Map(read.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map backbuffer readback");
        bool matches = true;
        for (unsigned y = 0; y != desc.Height; ++y) {
            const auto* row = static_cast<const unsigned char*>(mapped.pData) + std::size_t(y) * mapped.RowPitch;
            for (unsigned x = 0; x != desc.Width; ++x)
                for (unsigned channel = 0; channel != 4; ++channel)
                    matches = matches && row[x * 4 + channel] == expected[channel];
        }
        context->Unmap(read.Get(), 0);
        Require(matches, "GPU backbuffer pixels differ from expected mirror/waiting color");
    }
};
void Run() {
    CaptureFolder folder;
    Device first;
    fvr::bc2::DesktopEyeMirror mirror;
    mirror.Configure(true, true, folder.root);
    unsigned frame = 0;
    auto present = [&](Device& target, std::int64_t time, Color expected) {
        target.NativeColor();
        mirror.OnPresent(target.chain.Get(), target.context.Get(), time, ++frame);
        target.CheckPixels(expected);
    };
    constexpr std::int64_t start = 1000000000ll;
    present(first, start, Waiting);
    auto stats = mirror.Statistics();
    Require(stats.stale == 1 && stats.waitingAttempts == 1 && stats.waitingClears == 1 &&
        stats.waitingFailures == 0 && stats.waitingLastHresult == S_OK, "No-eye waiting diagnostics incorrect");
    auto eye = first.Eye(64, 48);
    mirror.OnLeftEye(first.context.Get(), eye.Get(), start, frame);
    present(first, start + 1, EyeColor);
    Require(mirror.Statistics().mirrored == 1, "Fresh eye was not counted as mirrored");
    present(first, start + 300000001ll, Waiting);
    Require(mirror.Statistics().stale == 2, "Expired eye was not counted as stale");
    auto wrongSize = first.Eye(32, 24);
    mirror.OnLeftEye(first.context.Get(), wrongSize.Get(), start + 400000000ll, frame);
    present(first, start + 400000001ll, Waiting);
    Require(mirror.Statistics().mismatched == 1, "Wrong-size eye was not counted as mismatched");

    // All four slots still contain 64x48 captures. No retained backbuffer/RTV
    // may prevent resize, and those slots must decode as 16x12, not 20x10.
    Hr(first.chain->ResizeBuffers(1, 80, 40, DXGI_FORMAT_R8G8B8A8_UNORM, 0), "ResizeBuffers with pending capture slots");
    auto resized = first.Eye(80, 40);
    mirror.OnLeftEye(first.context.Get(), resized.Get(), start + 500000000ll, frame);
    for (unsigned i = 0; i != 8; ++i) present(first, start + 500000001ll + i, EyeColor);
    stats = mirror.Statistics();
    Require(stats.captured == 8 && stats.captureBusy == 0, "Old/new resize captures were not decoded successfully");

    Device second;
    auto secondEye = second.Eye(64, 48);
    // Reject a source/context mismatch, then recover with the matching device.
    mirror.OnLeftEye(first.context.Get(), secondEye.Get(), start + 600000000ll, frame);
    present(first, start + 600000001ll, Waiting);
    mirror.OnLeftEye(second.context.Get(), secondEye.Get(), start + 700000000ll, frame);
    present(second, start + 700000001ll, EyeColor);
    for (unsigned i = 0; i != 8; ++i) present(second, start + 700000002ll + i, EyeColor);
    stats = mirror.Statistics();
    Require(stats.waitingFailures == 0 && stats.waitingAttempts == stats.waitingClears,
        "Waiting clear failed during integration test");
    Require(stats.mirrored == 18, "Fresh resized/device-rebound mirror recovery incorrect");
    mirror.Finish();

    std::ifstream rows(folder.root / "desktop.jsonl");
    Require(bool(rows), "Capture manifest missing");
    std::string line; unsigned oldSize = 0, newSize = 0, captures = 0;
    while (std::getline(rows, line)) {
        const bool old = line.find("\"width\":16,\"height\":12") != std::string::npos;
        const bool resizedSize = line.find("\"width\":20,\"height\":10") != std::string::npos;
        Require(old || resizedSize, "Capture manifest has unexpected dimensions");
        const auto field = line.find("\"present\":");
        Require(field != std::string::npos, "Capture manifest missing present index");
        const auto index = std::stoul(line.substr(field + 10));
        char name[32]; std::snprintf(name, sizeof name, "d-%05u.rgb", static_cast<unsigned>(index));
        const auto file = folder.root / "desktop" / name;
        Require(std::filesystem::is_regular_file(file), "Captured RGB file missing");
        Require(std::filesystem::file_size(file) == (old ? 16u * 12 * 3 : 20u * 10 * 3),
            "Captured RGB payload does not match its original staging dimensions");
        if (index < 4) { Require(old, "Pre-resize capture used resized dimensions"); ++oldSize; }
        if (index >= 4 && index < 8) { Require(resizedSize, "Resized capture used old dimensions"); ++newSize; }
        ++captures;
    }
    Require(oldSize == 4 && newSize == 4 && captures == stats.captured, "Resize capture evidence incomplete");
    Require(mirror.Statistics().captureDropped == 0, "Capture writer dropped test frames");
}
}
int main() {
    try { Run(); std::cout << "Desktop eye mirror WARP integration passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
