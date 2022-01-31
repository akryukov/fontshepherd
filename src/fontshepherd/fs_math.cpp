/* Copyright (C) 2000-2012 by George Williams
 * Copyright (C) 2022 by Alexey Kryukov
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

#include <cmath>
#include <algorithm>
#include <iterator>
#include "fs_math.h"

static const double RE_NearZero	= .00000001;
/* GWW: 52 bits => divide by 2^51 */
static const double RE_Factor	= 1024.0*1024.0*1024.0*1024.0*1024.0*2.0;

void FontShepherd::math::matMultiply (const double m1[6], const double m2[6], double to[6]) {
    double trans[6];

    trans[0] = m1[0]*m2[0] + m1[1]*m2[2];
    trans[1] = m1[0]*m2[1] + m1[1]*m2[3];
    trans[2] = m1[2]*m2[0] + m1[3]*m2[2];
    trans[3] = m1[2]*m2[1] + m1[3]*m2[3];
    trans[4] = m1[4]*m2[0] + m1[5]*m2[2] + m2[4];
    trans[5] = m1[4]*m2[1] + m1[5]*m2[3] + m2[5];

    std::copy (std::begin (trans), std::end (trans), to);
}

bool FontShepherd::math::realNear (double a, double b) {
    double d;

    if (a==0)
	return (b>-1e-8 && b<1e-8);
    if (b==0)
	return (a>-1e-8 && a<1e-8);

    d = a/(1024*1024.);
    if (d<0) d = -d;
	return( b>a-d && b<a+d );
}

bool FontShepherd::math::realApprox (double a, double b) {
    if (a==0) {
	if (b<.0001 && b>-.0001)
	    return true;
    } else if (b==0) {
	if (a<.0001 && a>-.0001)
	    return true;
    } else {
	a /= b;
	if (a>=.95 && a<=1.05)
	    return true;
    }
    return false;
}

bool FontShepherd::math::realWithin (double a, double b, double fudge) {
    return (b>=a-fudge && b<=a+fudge);
}

bool FontShepherd::math::realRatio (double a, double b, double fudge) {
    if (b==0)
	return realWithin (a, b, fudge);

    return realWithin (a/b, 1.0, fudge);
}

double FontShepherd::math::round (double f, float prec) {
    return (double) (floor (f*(1.0f/prec) + 0.5)/(1.0f/prec));
}

bool FontShepherd::math::within16RoundingErrors (double v1, double v2) {
    double temp=v1*v2;
    double re;

    /* GWW: Ok, if the two values are on different sides of 0 there
     * is no way they can be within a rounding error of each other */
    if (temp<0)
	return false;
    else if (temp==0) {
	if (v1==0)
	    return (v2<RE_NearZero && v2>-RE_NearZero);
	else
	    return (v1<RE_NearZero && v1>-RE_NearZero);
    } else if (v1>0) {
	/* Rounding error from the biggest absolute value */
	if (v1>v2) {
	    re = v1/ (RE_Factor/16);
	    return (v1-v2 < re);
	} else {
	    re = v2/ (RE_Factor/16);
	    return (v2-v1 < re);
	}
    } else {
	if (v1<v2) {
	    re = v1/ (RE_Factor/16);	/* This will be a negative number */
	    return (v1-v2 > re);
	} else {
	    re = v2/ (RE_Factor/16);
	    return (v2-v1 > re);
	}
    }
}

