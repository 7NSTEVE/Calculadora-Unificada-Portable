#pragma once

#include "document_model.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace DocStudio {

struct AudioBookmark {
    std::string labelUtf8;
    std::uint64_t timestampMs = 0;
};

struct TranscriptSegment {
    std::uint64_t startMs = 0;
    std::uint64_t endMs = 0;
    double rmsDb = -60.0;
    bool isSilence = true;
    std::string textUtf8;
};

struct MicCaptureReport {
    bool hardwareUsed = false;
    bool fallbackUsed = false;
    bool unplugRecovered = false;
    std::uint64_t capturedMs = 0;
    std::string deviceNameUtf8;
    std::string statusNoteUtf8;
};

// Pure C++17 16-bit PCM WAV codec + real OS mic capture (Win32 waveIn / Linux ALSA-PulseAudio)
// + synchronized Ink Replay timeline, bookmarks, clamped offset, offline segmentation/transcript,
// and synced HTML5 tutorial export.
class AudioTimelineSession {
public:
    static bool EncodeWavPcm16(
        const std::vector<std::int16_t>& samples,
        std::uint32_t sampleRateHz,
        std::vector<std::uint8_t>& outWavBytes);

    static bool DecodeWavPcm16(
        const std::uint8_t* wavBytes,
        std::size_t size,
        std::vector<std::int16_t>& outSamples,
        std::uint32_t& outSampleRateHz);

    void StartRecording(std::uint32_t sampleRateHz = 16000);
    void AppendSamples(const std::int16_t* samples, std::size_t count);
    void AppendSyntheticTone(double freqHz, std::uint64_t durationMs, std::int16_t amplitude = 6000);
    void StopRecording();

    // Real OS microphone capture (Win32 waveIn / Linux arecord/parec) for durationMs.
    // Gracefully falls back to quiet PCM if no physical capture device is attached or
    // simulateUnplug is true (preserving partial audio safely without crashing).
    MicCaptureReport CaptureFromMicrophone(
        std::uint64_t durationMs = 400,
        std::uint32_t sampleRateHz = 16000,
        bool simulateUnplug = false);

    bool LoadFromWavBytes(const std::uint8_t* wavBytes, std::size_t size);
    bool LoadFromWavFile(const std::string& pathUtf8, std::string* outErr = nullptr);
    bool ExportWavBytes(std::vector<std::uint8_t>& outWavBytes) const;
    bool ExportWavFile(const std::string& pathUtf8, std::string* outErr = nullptr) const;

    bool IsRecording() const noexcept { return recording_; }
    bool IsPlaying() const noexcept { return playing_; }
    void SetPlaying(bool play) noexcept { playing_ = play; }

    std::uint32_t SampleRateHz() const noexcept { return sampleRateHz_; }
    std::uint64_t DurationMs() const noexcept;
    std::uint64_t CursorMs() const noexcept { return cursorMs_; }
    void SeekMs(std::uint64_t ms) noexcept;
    std::uint64_t AdvancePlaybackMs(std::uint64_t deltaMs) noexcept;

    // Offset & Bookmarks/Chapters (clamped safely to audio duration)
    std::int64_t OffsetMs() const noexcept { return offsetMs_; }
    std::int64_t SetOffsetMsClamped(std::int64_t requestedOffsetMs) noexcept;
    void AddBookmark(const std::string& labelUtf8, std::uint64_t timestampMs);
    const std::vector<AudioBookmark>& Bookmarks() const noexcept { return bookmarks_; }

    // Deterministic offline voice-activity segmentation & searchable transcript (zero cloud)
    std::vector<TranscriptSegment> TranscribeOffline() const;

    // Self-contained synced Voice + Ink + Casio KeyLog HTML5 tutorial export
    bool ExportSyncedTutorialHtml5(
        const DocumentModel& doc,
        const std::string& outHtmlPathUtf8,
        std::string* outErr = nullptr) const;

    // Normalized [0..1] amplitude peaks per bin for waveform UI / PDF card rendering
    std::vector<float> ComputeEnvelopeBins(std::size_t numBins) const;

    const std::vector<std::int16_t>& Samples() const noexcept { return samples_; }

private:
    bool recording_ = false;
    bool playing_ = false;
    std::uint32_t sampleRateHz_ = 16000;
    std::uint64_t cursorMs_ = 0;
    std::int64_t offsetMs_ = 0;
    std::vector<std::int16_t> samples_;
    std::vector<AudioBookmark> bookmarks_;
};

} // namespace DocStudio
