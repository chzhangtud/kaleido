#pragma once

#include "common.h"

class TimeSeriesPlot {
public:
    TimeSeriesPlot(size_t size)
        : maxSize(size), values(size, 0.0f), offset(0) {
    }

    void addValue(float v) {
        values[offset] = v;
        offset = (offset + 1) % maxSize;
    }

    const float* data() const {
        return values.data();
    }

    size_t size() const {
        return maxSize;
    }

    int currentOffset() const {
        return offset;
    }

private:
    size_t maxSize;
    std::vector<float> values;
    int offset;
};

