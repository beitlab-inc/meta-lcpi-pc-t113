#include <iostream>
#include <cmath>
#include <vector>
#include <map>
#include <string>
#include <thread>
#include <chrono>
#include "rtaudio/RtAudio.h"

std::map<std::string, double> generatePianoFrequencies() {
    std::map<std::string, double> frequencies;
    std::string notes[] = {"C", "C#", "D", "D#", "E", "F",
                           "F#", "G", "G#", "A", "A#", "B"};
    int index = 0;
    for (int octave = 0; octave <= 8; ++octave) {
        for (int i = 0; i < 12; ++i) {
            if (index >= 88) break;
            int n = index - 48; // A4 = index 48 = 440Hz
            double freq = 440.0 * std::pow(2.0, n / 12.0);
            frequencies[notes[i] + std::to_string(octave)] = freq;
            ++index;
        }
        if (index >= 88) break;
    }
    return frequencies;
}

struct AudioData {
    std::vector<float> buffer;
    size_t position = 0;
};

int audioCallback(void* outputBuffer, void*, unsigned int nBufferFrames,
                  double, RtAudioStreamStatus status, void* userData) {
    AudioData* data = static_cast<AudioData*>(userData);
    float* out = static_cast<float*>(outputBuffer);

    if (status) std::cerr << "Stream underflow detected!" << std::endl;

    for (unsigned int i = 0; i < nBufferFrames; ++i) {
        if (data->position < data->buffer.size()) {
            out[i] = data->buffer[data->position++];
        } else {
            out[i] = 0.0f;
        }
    }

    return (data->position >= data->buffer.size()) ? 1 : 0;
}

std::vector<float> generateSineWave(double freq, double duration, int sampleRate) {
    int totalSamples = static_cast<int>(duration * sampleRate);
    std::vector<float> buffer(totalSamples);
    for (int i = 0; i < totalSamples; ++i) {
        buffer[i] = static_cast<float>(std::sin(2.0 * M_PI * freq * i / sampleRate));
    }
    return buffer;
}

int main() {
    auto frequencies = generatePianoFrequencies();
    std::string note;
    std::cout << "Enter note (e.g. C4, A4, G#3): ";
    std::cin >> note;

    if (frequencies.find(note) == frequencies.end()) {
        std::cerr << "Invalid note!" << std::endl;
        return 1;
    }

    int sampleRate = 44100;
    double duration = 2.0;

    AudioData data;
    data.buffer = generateSineWave(frequencies[note], duration, sampleRate);

    RtAudio dac;
    if (dac.getDeviceCount() < 1) {
        std::cerr << "No audio devices found!\n";
        return 1;
    }

    RtAudio::StreamParameters params;
    params.deviceId = dac.getDefaultOutputDevice();
    params.nChannels = 1;
    params.firstChannel = 0;
    unsigned int bufferFrames = 256;

    try {
        dac.openStream(&params, nullptr, RTAUDIO_FLOAT32, sampleRate, &bufferFrames, &audioCallback, &data);
        dac.startStream();

        while (dac.isStreamRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        dac.closeStream();
    } catch (RtAudioError& e) {
        std::cerr << "RtAudio error: " << e.getMessage() << std::endl;
        return 1;
    }

    return 0;
}
