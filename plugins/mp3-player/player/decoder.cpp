#include "decoder.h"
#include <cmath>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <stdexcept>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
static void check(HRESULT hr) {
  if (FAILED(hr))
    throw hr;
}
Clip decode_file(const std::wstring &path, const std::atomic<bool> &stopping) {
  // Called only on the decoder thread, whose COM/MF lifetime encloses this
  // call.
  ComPtr<IMFSourceReader> reader;
  check(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader));
  check(reader->SetStreamSelection(
      static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE));
  check(reader->SetStreamSelection(
      static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE));
  ComPtr<IMFMediaType> requested, actual;
  check(MFCreateMediaType(&requested));
  check(requested->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
  check(requested->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float));
  // Fixed decoded format bounds a ten-minute clip to ~220 MiB of PCM.
  check(requested->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000));
  check(requested->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2));
  check(reader->SetCurrentMediaType(
      static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr,
      requested.Get()));
  check(reader->GetCurrentMediaType(
      static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &actual));
  UINT32 channels = 0, rate = 0, bits = 0;
  check(actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels));
  check(actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate));
  check(actual->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits));
  if ((channels != 1 && channels != 2) || rate < 8000 || rate > 96000 ||
      bits != 32)
    throw E_INVALIDARG;
  Clip clip;
  clip.rate = rate;
  const size_t max_samples = static_cast<size_t>(rate) * 600 * 2;
  while (!stopping.load()) {
    DWORD flags = 0;
    ComPtr<IMFSample> sample;
    check(reader->ReadSample(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr,
        &flags, nullptr, &sample));
    if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
      throw E_INVALIDARG;
    if (sample) {
      ComPtr<IMFMediaBuffer> buffer;
      check(sample->ConvertToContiguousBuffer(&buffer));
      BYTE *bytes = nullptr;
      DWORD length = 0;
      check(buffer->Lock(&bytes, nullptr, &length));
      // Unlock even when bounds checking/allocation throws. No COM lock reaches
      // audio.
      try {
        if (length % (sizeof(float) * channels))
          throw E_INVALIDARG;
        size_t frames = length / (sizeof(float) * channels);
        if (clip.pcm.size() + frames * 2 > max_samples)
          throw HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        const float *input = reinterpret_cast<const float *>(bytes);
        for (size_t i = 0; i < frames; i++)
          for (unsigned c = 0; c < 2; c++) {
            float value = input[i * channels + (channels == 1 ? 0 : c)];
            if (!std::isfinite(value))
              throw E_INVALIDARG;
            clip.pcm.push_back(value);
          }
      } catch (...) {
        buffer->Unlock();
        throw;
      }
      check(buffer->Unlock());
    }
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
      break;
  }
  if (stopping.load() || clip.pcm.empty())
    throw E_ABORT;
  return clip;
}
