/* Copyright (C) 2022 by Alexey Kryukov
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE. */

#ifndef _FONSHEPHERD_COLOR_H
#define _FONSHEPHERD_COLOR_H

struct rgba_color {
    unsigned char red=0, green=0, blue=0, alpha=255;
    friend bool operator==(const rgba_color &lhs, const rgba_color &rhs) {
	return (
	    lhs.red == rhs.red && lhs.green == rhs.green &&
	    lhs.blue == rhs.blue && lhs.alpha == rhs.alpha);
    }
    friend bool operator!=(const rgba_color &lhs, const rgba_color &rhs) {
	return !(lhs==rhs);
    }
};

struct ColorStop {
    bool isVariable;
    double stopOffset;
    uint16_t paletteIndex;
    double alpha;
    uint32_t varIndexBase;
};

struct ColorLine {
    bool isVariable;
    uint8_t extend;
    std::vector<ColorStop> colorStops;
};

struct gradient_stop {
    rgba_color color;
    uint16_t color_idx = 0xFFFF;
    double offset;
};

enum class GradientExtend {
    EXTEND_PAD = 0, EXTEND_REPEAT = 1, EXTEND_REFLECT = 2
};

enum class GradientType {
    NONE, LINEAR, RADIAL, SWEEP
};

enum class GradientUnits {
    userSpaceOnUse, objectBoundingBox
};

#ifndef _FS_STRUCT_DBOUNDS_DEFINED
#define _FS_STRUCT_DBOUNDS_DEFINED
typedef struct dbounds {
    double minx, maxx;
    double miny, maxy;
} DBounds;
#endif


class CpalTable;
struct Gradient {
    GradientType type = GradientType::NONE;
    GradientExtend sm = GradientExtend::EXTEND_PAD;
    GradientUnits units = GradientUnits::objectBoundingBox;
    std::array<double, 6> transform = { 1, 0, 0, 1, 0, 0 };
    std::map<std::string, double> props;
    std::vector<struct gradient_stop> stops;
    // Need this for QGradient, which needs "logical" object coordinates
    // for nearly all significant parameters
    DBounds bbox = { 0, 0, 0, 0 };

    Gradient () {};
    Gradient (ColorLine *cline, CpalTable *cpal, uint16_t palidx);
    void transformProps (const std::array<double, 6> &trans);
    void convertBoundingBox (DBounds &bb);
};

#endif
