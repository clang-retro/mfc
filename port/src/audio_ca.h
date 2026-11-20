// audio_ca.h - Core Audio through audio queues, kept apart from the
// Windows headers so that the system's and the layer's types never meet.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_AUDIO_CA_H
#define _WINPORT_AUDIO_CA_H

#include <stddef.h>
#include <stdint.h>

namespace winport {
namespace audio {
namespace ca {

struct Stream;
// Whether there is a default device that way.
bool Available(bool capture);
Stream* Open(bool capture, int rate, int channels);
bool Write(Stream* s, const int16_t* frames, size_t n);
size_t Read(Stream* s, int16_t* frames, size_t n);
size_t Delay(Stream* s);
void Pause(Stream* s, bool on);
void Drop(Stream* s);
void Close(Stream* s);

}  // namespace ca
}  // namespace audio
}  // namespace winport

#endif
