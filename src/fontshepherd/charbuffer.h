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

#ifndef _FS_CHARBUFFER_DEFINED
#define _FS_CHARBUFFER_DEFINED
#include <sstream>

#include <boost/iostreams/device/array.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filter/zlib.hpp>

typedef boost::iostreams::basic_array_source<char> BoostSourceD;
typedef boost::iostreams::stream<BoostSourceD> BoostIn;
typedef boost::iostreams::filtering_stream<BoostSourceD> ZBoostIn;

typedef boost::asio::streambuf BoostTargetD;
typedef boost::iostreams::stream<BoostTargetD> BoostOut;
typedef boost::iostreams::filtering_stream<BoostTargetD> ZBoostOut;

BoostIn& operator>> (BoostIn& is, uint8_t &ch);

BoostIn& operator>> (BoostIn& is, int8_t &ch);

BoostIn& operator>> (BoostIn& is, uint16_t &val);

BoostIn& operator>> (BoostIn& is, int16_t &val);

BoostIn& operator>> (BoostIn& is, uint32_t &val);

BoostOut& operator<< (BoostOut& os, uint8_t &ch);

BoostOut& operator<< (BoostOut& os, int8_t &ch);

BoostOut& operator<< (BoostOut& os, uint16_t &ch);

BoostOut& operator<< (BoostOut& os, int16_t &ch);

BoostOut& operator<< (BoostOut& os, uint32_t &ch);
#endif
