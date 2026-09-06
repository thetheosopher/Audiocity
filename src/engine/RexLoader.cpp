#include "RexLoader.h"
#include "ImportCancellation.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

#include "REX.h"

#if JUCE_WINDOWS
#include <Windows.h>
#endif

namespace audiocity::engine::rex
{
namespace
{
struct RuntimeState
{
    std::mutex mutex;
    bool attemptedInit = false;
    bool available = false;
};

RuntimeState& runtimeState()
{
    static RuntimeState state;
    return state;
}

#if JUCE_WINDOWS
juce::File getThisModuleDirectory()
{
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&getThisModuleDirectory),
            &module) == 0)
    {
        return {};
    }

    wchar_t modulePath[MAX_PATH]{};
    const auto length = GetModuleFileNameW(module, modulePath, static_cast<DWORD>(std::size(modulePath)));
    if (length == 0 || length >= static_cast<DWORD>(std::size(modulePath)))
        return {};

    return juce::File(juce::String(modulePath)).getParentDirectory();
}
#endif

bool ensureInitializedLocked() noexcept
{
    auto& state = runtimeState();
    if (state.attemptedInit)
        return state.available;

    state.attemptedInit = true;

#if JUCE_WINDOWS
    auto moduleDir = getThisModuleDirectory();
    if (moduleDir == juce::File{})
        moduleDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();

    if (moduleDir == juce::File{})
    {
        state.available = false;
        return false;
    }

    state.available = (REX::REXInitializeDLL_DirPath(moduleDir.getFullPathName().toWideCharPointer())
        == REX::kREXError_NoError);
#else
    state.available = false;
#endif

    return state.available;
}

struct RexHandleScope final
{
    REX::REXHandle handle = nullptr;

    ~RexHandleScope()
    {
        if (handle != nullptr)
            REX::REXDelete(&handle);
    }
};
}

bool isRuntimeAvailable() noexcept
{
    auto& state = runtimeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    return ensureInitializedLocked();
}

bool decodeFile(const juce::File& file, DecodedLoop& out) noexcept
{
    if (!file.existsAsFile() || isImportCancellationRequested())
        return false;

    auto& state = runtimeState();
    std::unique_lock<std::mutex> lock(state.mutex, std::defer_lock);
    while (!lock.try_lock())
    {
        if (isImportCancellationRequested())
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!ensureInitializedLocked())
        return false;

    auto fileInput = file.createInputStream();
    if (fileInput == nullptr)
        return false;

    const auto fileSize = fileInput->getTotalLength();
    if (fileSize <= 0 || fileSize > static_cast<juce::int64>((std::numeric_limits<REX::REX_int32_t>::max)()))
        return false;

    juce::MemoryBlock fileData(static_cast<std::size_t>(fileSize), false);
    auto* fileBytes = static_cast<char*>(fileData.getData());
    constexpr int kFileReadChunkBytes = 1024 * 1024;
    for (juce::int64 position = 0; position < fileSize;)
    {
        if (isImportCancellationRequested())
            return false;

        const auto bytesThisChunk = static_cast<int>(juce::jmin<juce::int64>(kFileReadChunkBytes, fileSize - position));
        if (fileInput->read(fileBytes + position, bytesThisChunk) != bytesThisChunk)
            return false;
        position += bytesThisChunk;
    }

    RexHandleScope rexHandle;
    auto result = REX::REXCreate(
        &rexHandle.handle,
        static_cast<const char*>(fileData.getData()),
        static_cast<REX::REX_int32_t>(fileData.getSize()),
        nullptr,
        nullptr);

    if (result != REX::kREXError_NoError || rexHandle.handle == nullptr)
        return false;

    REX::REXInfo info{};
    result = REX::REXGetInfo(rexHandle.handle, static_cast<REX::REX_int32_t>(sizeof(REX::REXInfo)), &info);
    if (result != REX::kREXError_NoError)
        return false;

    if (info.fChannels < 1 || info.fChannels > 2 || info.fSliceCount <= 0)
        return false;

    const auto outputSampleRate = juce::jmax(11025, static_cast<int>(info.fSampleRate));
    result = REX::REXSetOutputSampleRate(rexHandle.handle, outputSampleRate);
    if (result != REX::kREXError_NoError)
        return false;

    std::vector<REX::REXSliceInfo> slices(static_cast<std::size_t>(info.fSliceCount));
    int64_t totalFrames = 0;
    for (int sliceIndex = 0; sliceIndex < info.fSliceCount; ++sliceIndex)
    {
        if (isImportCancellationRequested())
            return false;

        auto& sliceInfo = slices[static_cast<std::size_t>(sliceIndex)];
        result = REX::REXGetSliceInfo(
            rexHandle.handle,
            static_cast<REX::REX_int32_t>(sliceIndex),
            static_cast<REX::REX_int32_t>(sizeof(REX::REXSliceInfo)),
            &sliceInfo);

        if (result != REX::kREXError_NoError || sliceInfo.fSampleLength <= 0)
            return false;

        totalFrames += static_cast<int64_t>(sliceInfo.fSampleLength);
    }

    if (totalFrames <= 0 || totalFrames > static_cast<int64_t>((std::numeric_limits<int>::max)()))
        return false;

    out.audio.setSize(info.fChannels, static_cast<int>(totalFrames), false, true, true);
    out.audio.clear();
    out.slices.clear();
    out.slices.reserve(static_cast<std::size_t>(info.fSliceCount));

    int frameOffset = 0;
    for (int sliceIndex = 0; sliceIndex < info.fSliceCount; ++sliceIndex)
    {
        if (isImportCancellationRequested())
            return false;

        const auto frameCount = slices[static_cast<std::size_t>(sliceIndex)].fSampleLength;
        std::vector<float> left(static_cast<std::size_t>(frameCount), 0.0f);
        std::vector<float> right;
        if (info.fChannels == 2)
            right.assign(static_cast<std::size_t>(frameCount), 0.0f);

        float* renderBuffers[2] =
        {
            left.data(),
            info.fChannels == 2 ? right.data() : nullptr
        };

        result = REX::REXRenderSlice(
            rexHandle.handle,
            static_cast<REX::REX_int32_t>(sliceIndex),
            static_cast<REX::REX_int32_t>(frameCount),
            renderBuffers);

        if (result != REX::kREXError_NoError)
            return false;

        if (isImportCancellationRequested())
            return false;

        out.audio.copyFrom(0, frameOffset, left.data(), frameCount);
        if (info.fChannels == 2)
            out.audio.copyFrom(1, frameOffset, right.data(), frameCount);

        DecodedSlice decodedSlice;
        decodedSlice.startSample = frameOffset;
        decodedSlice.audio.setSize(info.fChannels, frameCount, false, true, true);
        decodedSlice.audio.copyFrom(0, 0, left.data(), frameCount);
        if (info.fChannels == 2)
            decodedSlice.audio.copyFrom(1, 0, right.data(), frameCount);
        out.slices.push_back(std::move(decodedSlice));

        frameOffset += frameCount;
    }

    out.sampleRateHz = static_cast<double>(outputSampleRate);
    return true;
}
}
