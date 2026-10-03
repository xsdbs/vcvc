#pragma once
#include <Windows.h>
#include <array>
#include <algorithm>

namespace Performance
{
    inline double Milliseconds()
    {
        static const double frequency = [] { LARGE_INTEGER value{}; QueryPerformanceFrequency(&value); return static_cast<double>(value.QuadPart); }();
        LARGE_INTEGER value{}; QueryPerformanceCounter(&value);
        return value.QuadPart*1000./frequency;
    }
    enum Phase { Scan, Records, Prediction, Burst, Count };
    struct Sample { double last = 0,mean = 0,peak = 0; unsigned count = 0; };
    inline std::array<Sample,Count> samples{};
    struct Measure
    {
        Phase phase; double started = Milliseconds();
        explicit Measure(Phase value) : phase(value) {}
        ~Measure() {
            auto& sample = samples[phase]; sample.last = Milliseconds()-started;
            sample.mean = sample.count ? sample.mean*.95+sample.last*.05 : sample.last;
            sample.peak = std::max(sample.peak,sample.last); ++sample.count;
        }
    };
    struct Budget
    {
        bool enabled; double milliseconds,started = Milliseconds(); int calls = 0,limit;
        Budget(bool active,double time,int maximum) : enabled(active),milliseconds(time),limit(maximum) {}
        bool Available(int reserve = 0,double timeFraction = 1) const
        {
            return !enabled || (calls < limit-reserve && Milliseconds()-started < milliseconds*timeFraction);
        }
        bool Consume() { if (!Available()) return false; ++calls; return true; }
    };
}
