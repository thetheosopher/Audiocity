#include "RexLoader.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <mutex>
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
    if (!file.existsAsFile())
        return false;

    auto& state = runtimeState();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!ensureInitializedLocked())
        return false;

    juce::MemoryBlock fileData;
    if (!file.loadFileAsData(fileData))
        return false;

    if (fileData.getSize() == 0 || fileData.getSize() > static_cast<size_t>((std::numeric_limits<REX::REX_int32_t>::max)()))
        return false;

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

    int frameOffset = 0;
    for (int sliceIndex = 0; sliceIndex < info.fSliceCount; ++sliceIndex)
    {
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

        out.audio.copyFrom(0, frameOffset, left.data(), frameCount);
        if (info.fChannels == 2)
            out.audio.copyFrom(1, frameOffset, right.data(), frameCount);

        frameOffset += frameCount;
    }

    out.sampleRateHz = static_cast<double>(outputSampleRate);
    return true;
}
}
