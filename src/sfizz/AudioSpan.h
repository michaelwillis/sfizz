// Copyright (c) 2019, Paul Ferrand
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:

// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once
#include "AudioBuffer.h"
#include "Buffer.h"
#include "Config.h"
#include "Debug.h"
#include "LeakDetector.h"
#include "SIMDHelpers.h"
#include <array>
#include <initializer_list>
#include <type_traits>

namespace sfz
{
/**
 * @brief Extension of the concept of spans to multiple channels.
 * 
 * A span (and by extension an audiospan) is at its core a structure
 * containing a pointer and a size to a buffer that is owned by another
 * object. A span is thus a view into a buffer that is cheap to copy and
 * pass around, and safe as long as the underlying buffer is allocated.
 * The goal of the class is to reduce interfaces and usage annoyance for 
 * codebases. Obviously, this requires that most functions use AudioSpans.
 * It also protects against overreading a buffer. Users can still indicate
 * that they 
 * @code{.cpp}
 * constexpr int bufferSize { 1024 };
 * void gain(AudioSpan<float, 2> arrayView, float gain)
 * {
 *      for (auto& f: arrayView)
 *          f *= gain;
 * }
 * 
 * int main(char argc, char** argv)
 * {
 *      float leftChannel [bufferSize];
 *      float rightChannel [bufferSize];
 * 
 *      for (int i = 0; i < bufferSize; ++i)
 *      {
 *          leftChannel[i] = 1.0f;
 *          rightChannel[i] = 1.0f;
 *      }
 * 
 *      // Type is inferred
 *      AudioSpan explicitView { { leftChannel, rightChannel }, bufferSize };
 *      // Size will be taken as the minimum size of all the spans given
 *      // for all types that can be automatically cast to absl::Span or std::span
 *      AudioSpan explicitView2 { { leftChannel, rightChannel } };
 * 
 *      gain(explicitView, 0.5f); // the array elements are now equal to 0.5f
 *      gain(explicitView2, 0.5f); // the array elements are now equal to 0.25f
 * 
 *      // You can also build spans implicitely
 *      gain({ { leftChannel, rightChannel }, bufferSize }, 0.5f); // elements equal to 0.125f
 * }
 * @endcode
 * You can build AudioSpans from AudioBuffers directly, the AudioBufferT.cpp file in the test
 * folder show some example.
 *  As with many things templated in C++ the `Type` can be`const` or `volatile`, and `const float` 
 * is not the same type as `float`. You thus cannot build an `AudioSpan<float>` from 
 * a `const float *` buffer for example.
 * 
 * @tparam Type the underlying buffer type
 * @tparam MaxChannels the maximum number of channels. Defaults to sfz::config::numChannels 
 */
template <class Type, unsigned int MaxChannels = sfz::config::numChannels>
class AudioSpan {
public:
    using size_type = size_t;
    AudioSpan()
    {
    }

    /**
     * @brief Construct a new Audio Span object
     * 
     * @param spans an array of MaxChannels pointers to buffers.
     * @param numChannels the number of spans to take in from the array
     * @param offset starting offset for the AudioSpan
     * @param numFrames size of the AudioSpan
     */
    AudioSpan(const std::array<Type*, MaxChannels>& spans, int numChannels, size_type offset, size_type numFrames)
        : numFrames(numFrames)
        , numChannels(numChannels)
    {
        ASSERT(static_cast<unsigned int>(numChannels) <= MaxChannels);
        for (auto i = 0; i < numChannels; ++i)
            this->spans[i] = spans[i] + offset;
    }

    /**
     * @brief Construct a new Audio Span object from initializer lists
     * 
     * @param spans the list of span
     * @param numFrames the size of the audio span
     */
    AudioSpan(std::initializer_list<Type*> spans, size_type numFrames)
        : numFrames(numFrames)
        , numChannels(spans.size())
    {
        ASSERT(spans.size() <= MaxChannels);
        auto newSpan = spans.begin();
        auto thisSpan = this->spans.begin();
        for (; newSpan < spans.end() && thisSpan < this->spans.end(); thisSpan++, newSpan++) {
            // This will not end well...
            ASSERT(*newSpan != nullptr);
            *thisSpan = *newSpan;
        }
    }

    /**
     * @brief Construct a new Audio Span object from a list of absl::Span<Type>
     * 
     * This constructor can be implicitely called for any source that can be cast transparently to
     * an absl::Span<Type>. The size of the AudioSpan is inferred from the size of the smallest 
     * absl::Span<Type>.
     * 
     * @param spans a list of objects compatible with absl::Span<Type>
     */
    AudioSpan(std::initializer_list<absl::Span<Type>> spans)
        : numChannels(spans.size())
    {
        ASSERT(spans.size() <= MaxChannels);
        auto size = absl::Span<Type>::npos;
        auto newSpan = spans.begin();
        auto thisSpan = this->spans.begin();
        for (; newSpan < spans.end() && thisSpan < this->spans.end(); thisSpan++, newSpan++) {
            *thisSpan = newSpan->data();
            size = std::min(size, newSpan->size());
        }
    }

    /**
     * @brief Construct a new Audio Span object from an AudioBuffer with a const Type.
     * 
     * This constructor can be implicitely called for any source that can be cast transparently to
     * an AudioBuffer<const Type>.
     * 
     * @tparam U the underlying type compatible with the template Type of the AudioSpan
     * @tparam N the number of channels in the AudioBuffer
     * @tparam Alignment the alignment block size for the platform
     * @param audioBuffer the source AudioBuffer.
     */
    template <class U, unsigned int N, unsigned int Alignment, typename = std::enable_if<N <= MaxChannels>, typename = std::enable_if_t<std::is_const<U>::value, int>>
    AudioSpan(AudioBuffer<U, N, Alignment>& audioBuffer)
        : numFrames(audioBuffer.getNumFrames())
        , numChannels(audioBuffer.getNumChannels())
    {
        for (int i = 0; i < numChannels; i++) {
            this->spans[i] = audioBuffer.channelReader(i);
        }
    }

    /**
     * @brief Construct a new Audio Span object from an AudioBuffer with a non-const Type.
     * 
     * This constructor can be implicitely called for any source that can be cast transparently to
     * an AudioBuffer<Type>.
     * 
     * @tparam U the underlying type compatible with the template Type of the AudioSpan
     * @tparam N the number of channels in the AudioBuffer
     * @tparam Alignment the alignment block size for the platform
     * @param audioBuffer the source AudioBuffer.
     */
    template <class U, unsigned int N, unsigned int Alignment, typename = std::enable_if<N <= MaxChannels>>
    AudioSpan(AudioBuffer<U, N, Alignment>& audioBuffer)
        : numFrames(audioBuffer.getNumFrames())
        , numChannels(audioBuffer.getNumChannels())
    {
        for (int i = 0; i < numChannels; i++) {
            this->spans[i] = audioBuffer.channelWriter(i);
        }
    }

    /**
     * @brief AudioSpan copy constructor
     * 
     * @param other the other AudioSpan
     */
    template <class U, unsigned int N, typename = std::enable_if<N <= MaxChannels>>
    AudioSpan(const AudioSpan<U, N>& other)
        : numFrames(other.getNumFrames())
        , numChannels(other.getNumChannels())
    {
        for (int i = 0; i < numChannels; i++) {
            this->spans[i] = other.getChannel(i);
        }
    }

    /**
     * @brief Get a raw pointer to a specific channel from the AudioSpan
     * 
     * @param channelIndex the channel
     * @return Type* the raw pointer to the channel
     */
    Type* getChannel(int channelIndex)
    {
        ASSERT(channelIndex < numChannels);
        if (channelIndex < numChannels)
            return spans[channelIndex];

        return {};
    }

    /**
     * @brief Get a Span<Type> corresponding to a specific channel
     * 
     * @param channelIndex the channel
     * @return absl::Span<Type> 
     */
    absl::Span<Type> getSpan(int channelIndex)
    {
        ASSERT(channelIndex < numChannels);
        if (channelIndex < numChannels)
            return { spans[channelIndex], numFrames };

        return {};
    }

    /**
     * @brief Get a Span<const Type> corresponding to a specific channel
     * 
     * @param channelIndex the channel
     * @return absl::Span<const Type> 
     */
    absl::Span<const Type> getConstSpan(int channelIndex)
    {
        ASSERT(channelIndex < numChannels);
        if (channelIndex < numChannels)
            return { spans[channelIndex], numFrames };

        return {};
    }

    /**
     * @brief Get the mean of the squared values of the AudioSpan elements on all channels. 
     * 
     * @return Type
     */
    Type meanSquared() noexcept
    {
        if (numChannels == 0)
            return 0.0;
        Type result = 0.0;
        for (int i = 0; i < numChannels; ++i)
            result += sfz::meanSquared<Type>(getConstSpan(i));
        return result / numChannels;
    }

    /**
     * @brief Fills all the elements of the AudioSpan with the same value
     * 
     * @param value the filling value
     */
    void fill(Type value) noexcept
    {
        static_assert(!std::is_const<Type>::value, "Can't allow mutating operations on const AudioSpans");
        for (int i = 0; i < numChannels; ++i)
            sfz::fill<Type>(getSpan(i), value);
    }

    /**
     * @brief Apply a gain span elementwise to all channels in the AudioSpan.
     * 
     * @param gain the gain to apply
     */
    void applyGain(absl::Span<const Type> gain) noexcept
    {
        static_assert(!std::is_const<Type>::value, "Can't allow mutating operations on const AudioSpans");
        for (int i = 0; i < numChannels; ++i)
            sfz::applyGain<Type>(gain, getSpan(i));
    }

    /**
     * @brief Apply a gain to all channels in the AudioSpan.
     * 
     * @param gain the gain to apply
     */
    void applyGain(Type gain) noexcept
    {
        static_assert(!std::is_const<Type>::value, "Can't allow mutating operations on const AudioSpans");
        for (int i = 0; i < numChannels; ++i)
            sfz::applyGain<Type>(gain, getSpan(i));
    }

    /**
     * @brief Add another AudioSpan with a compatible number of channels to the current
     * AudioSpan.
     * 
     * @param other the other AudioSpan
     */
    template <class U, unsigned int N, typename = std::enable_if<N <= MaxChannels>>
    void add(AudioSpan<U, N>& other)
    {
        static_assert(!std::is_const<Type>::value, "Can't allow mutating operations on const AudioSpans");
        ASSERT(other.getNumChannels() == numChannels);
        if (other.getNumChannels() == numChannels) {
            for (int i = 0; i < numChannels; ++i)
                sfz::add<Type>(other.getConstSpan(i), getSpan(i));
        }
    }

    /**
     * @brief Copy the elements of another AudioSpan with a compatible number of channels 
     * to the current AudioSpan.
     * 
     * @param other the other AudioSpan
     */
    template <class U, unsigned int N, typename = std::enable_if<N <= MaxChannels>>
    void copy(AudioSpan<U, N>& other)
    {
        static_assert(!std::is_const<Type>::value, "Can't allow mutating operations on const AudioSpans");
        ASSERT(other.getNumChannels() == numChannels);
        if (other.getNumChannels() == numChannels) {
            for (int i = 0; i < numChannels; ++i)
                sfz::copy<Type>(other.getConstSpan(i), getSpan(i));
        }
    }

    /**
     * @brief Get the size of this AudioSpan.
     * 
     * @returns size_type the number of frames in the AudioSpan
     */
    size_type getNumFrames()
    {
        return numFrames;
    }

    /**
     * @brief Get the number of channels of this AudioSpan.
     * 
     * @returns size_type the number of channels in the AudioSpan
     */
    int getNumChannels()
    {
        return numChannels;
    }
    
    /**
     * @brief Creates a new AudioSpan but with only the `length` first elements of each channel.
     * 
     * @param length the number of elements to take on each channel
     */
    AudioSpan<Type> first(size_type length)
    {
        ASSERT(length <= numFrames);
        return { spans, numChannels, 0, length };
    }

    /**
     * @brief Creates a new AudioSpan but with only the `length` last elements of each channel.
     * 
     * @param length the number of elements to take on each channel
     */
    AudioSpan<Type> last(size_type length)
    {
        ASSERT(length <= numFrames);
        return { spans, numChannels, numFrames - length, length };
    }

    /**
     * @brief Creates a new AudioSpan starting at an offset `offset` on each channel and
     * taking `length` elements. The new AudioSpan will have `length` elements. This basically
     * removes the first `offset` elements and the last `numFrames - length - offset` elements
     *  from the AudioSpan.
     * 
     * @param length the number of elements to take on each channel
     */
    AudioSpan<Type> subspan(size_type offset, size_type length)
    {
        ASSERT(length + offset <= numFrames);
        return { spans, numChannels, offset, length };
    }

    /**
     * @brief Creates a new AudioSpan starting at an offset `offset` on each channel and
     * taking all the remaining elements. This function basically removes the first `offset`
     * elements from the AudioSpan. The new Audiospan will have size `numFrames - offset`.
     * 
     * @param length the number of elements to take on each channel
     */
    AudioSpan<Type> subspan(size_type offset)
    {
        ASSERT(offset <= numFrames);
        return { spans, numChannels, offset, numFrames - offset };
    }

private:
    std::array<Type*, MaxChannels> spans;
    size_type numFrames { 0 };
    int numChannels { 0 };
};
}