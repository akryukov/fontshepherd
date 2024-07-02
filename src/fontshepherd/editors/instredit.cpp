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

#include <cstdio>
#include <sstream>
#include <iostream>
#include <assert.h>
#include <stdint.h>
#include <limits>
#include <exception>
#include <unicode/uchar.h>

#include "sfnt.h"
#include "splineglyph.h"
#include "editors/instredit.h" // also includes tables.h
#include "tables/instr.h" // also includes tables.h

#include "fs_notify.h"

InstrTableEdit::InstrTableEdit (std::shared_ptr<FontTable> tptr, sFont* font, QWidget *parent) :
    TableEdit (parent, Qt::Window), m_font (font), m_table (tptr) {

    setAttribute (Qt::WA_DeleteOnClose);
    setWindowTitle (QString ("%1 - %2").arg
	(QString::fromStdString (m_table->stringName ())).arg (m_font->fontname));

    std::shared_ptr<InstrTable> ftbl = std::dynamic_pointer_cast<InstrTable> (m_table);
    uint8_t *tbldata = reinterpret_cast<uint8_t *> (ftbl->getData ());
    uint32_t tblsize = ftbl->length ();

    QWidget *window = new QWidget (this);
    QGridLayout *grid = new QGridLayout ();

    m_instrEdit = new InstrEdit (tbldata, tblsize, this);
    grid->addWidget (m_instrEdit, 0, 0, 1, 2);

    m_okButton = new QPushButton (QWidget::tr ("OK"));
    connect (m_okButton, &QPushButton::clicked, this, &InstrTableEdit::save);
    m_cancelButton = new QPushButton (QWidget::tr ("&Cancel"));
    connect (m_cancelButton, &QPushButton::clicked, this, &InstrTableEdit::close);

    grid->addWidget (m_okButton, 2, 0, 1, 1);
    grid->addWidget (m_cancelButton, 2, 1, 1, 1);

    window->setLayout (grid);
    setCentralWidget (window);
    adjustSize ();

    m_valid = true;
}

bool InstrTableEdit::checkUpdate (bool) {
    return true;
}

bool InstrTableEdit::isModified () {
    return m_table->modified ();
}

bool InstrTableEdit::isValid () {
    return m_valid;
}

std::shared_ptr<FontTable> InstrTableEdit::table () {
    return m_table;
}

void InstrTableEdit::closeEvent (QCloseEvent *event) {
    // If we are going to delete the font, ignore changes in table edits
    if (!isModified () || checkUpdate (true))
        m_table->clearEditor ();
    else
        event->ignore ();
}

void InstrTableEdit::save () {
    if (m_instrEdit->changed ()) {
	std::shared_ptr<InstrTable> ftbl = std::dynamic_pointer_cast<InstrTable> (m_table);
	ftbl->setData (m_instrEdit->data ());
	emit (update (m_table));
    }
    close ();
}

std::map<std::string, uint8_t> InstrEdit::ByInstr = {};

const std::map<uint8_t, instr_def> InstrEdit::InstrSet = {
    { 0x00, { "SVTCA", 0x00, 0x01, 0, 0,
    	  "Set freedom & projection Vectors To Coordinate Axis[a]\n 0=>both to y axis\n 1=>both to x axis" }},
    { 0x02, { "SPVTCA", 0x02, 0x03, 0, 0,
    	  "Set Projection Vector To Coordinate Axis[a]\n 0=>y axis\n 1=>x axis" }},
    { 0x04, { "SFVTCA", 0x04, 0x05, 0, 0,
    	  "Set Freedom Vector To Coordinate Axis[a]\n 0=>y axis\n 1=>x axis" }},
    { 0x06, { "SPVTL", 0x06, 0x07, 2, 0,
    	  "Set Projection Vector To Line[a]\n 0 => parallel to line\n 1=>orthogonal to line\nPops two points used to establish the line\nSets the projection vector" }},
    { 0x08, { "SFVTL", 0x08, 0x09, 2, 0,
    	  "Set Fredom Vector To Line[a]\n 0 => parallel to line\n 1=>orthogonal to line\nPops two points used to establish the line\nSets the freedom vector" }},
    { 0x0a, { "SPVFS", 0x0a, 0x0a, 2, 0,
    	  "Set Projection Vector From Stack\npops 2 2.14 values (x,y) from stack\nmust be a unit vector" }},
    { 0x0b, { "SFVFS", 0x0b, 0x0b, 2, 0,
    	  "Set Freedom Vector From Stack\npops 2 2.14 values (x,y) from stack\nmust be a unit vector" }},
    { 0x0c, { "GPV", 0x0c, 0x0c, 0, 2,
    	  "Get Projection Vector\nDecomposes projection vector, pushes its\ntwo coordinates onto stack as 2.14" }},
    { 0x0d, { "GFV", 0x0d, 0x0d, 0, 2,
    	  "Get Freedom Vector\nDecomposes freedom vector, pushes its\ntwo coordinates onto stack as 2.14" }},
    { 0x0e, { "SFVTPV", 0x0e, 0x0e, 0, 0,
    	  "Set Freedom Vector To Projection Vector" }},
    { 0x0f, { "ISECT", 0x0f, 0x0f, 5, 0,
    	  "moves point to InterSECTion of two lines\nPops start,end start,end points of two lines\nand a point to move. Point is moved to\nintersection" }},
    { 0x10, { "SRP0", 0x10, 0x10, 1, 0,
    	  "Set Reference Point 0\nPops a point which becomes the new rp0" }},
    { 0x11, { "SRP1", 0x11, 0x11, 1, 0,
    	  "Set Reference Point 1\nPops a point which becomes the new rp1" }},
    { 0x12, { "SRP2", 0x12, 0x12, 1, 0,
    	  "Set Reference Point 2\nPops a point which becomes the new rp2" }},
    { 0x13, { "SZP0", 0x13, 0x13, 1, 0,
    	  "Set Zone Pointer 0\nPops the zone number into zp0" }},
    { 0x14, { "SZP1", 0x14, 0x14, 1, 0,
    	  "Set Zone Pointer 1\nPops the zone number into zp1" }},
    { 0x15, { "SZP2", 0x15, 0x15, 1, 0,
    	  "Set Zone Pointer 2\nPops the zone number into zp2" }},
    { 0x16, { "SZPS", 0x16, 0x16, 1, 0,
    	  "Set Zone PointerS\nPops the zone number into zp0, zp1 and zp2" }},
    { 0x17, { "SLOOP", 0x17, 0x17, 1, 0,
    	  "Set LOOP variable\nPops the new value for the loop counter\nDefaults to 1 after each use" }},
    { 0x18, { "RTG", 0x18, 0x18, 0, 0,
    	  "Round To Grid\nSets the round state" }},
    { 0x19, { "RTHG", 0x19, 0x19, 0, 0,
    	  "Round To Half Grid\nSets the round state (round to closest .5 not int)" }},
    { 0x1a, { "SMD", 0x1a, 0x1a, 1, 0,
    	  "Set Minimum Distance\nPops a 26.6 value from stack to be new minimum distance" }},
    { 0x1b, { "ELSE", 0x1b, 0x1b, 0, 0,
    	  "ELSE clause\nStart of Else clause of preceding IF" }},
    { 0x1c, { "JMPR", 0x1c, 0x1c, 1, 0,
    	  "JuMP Relative\nPops offset (in bytes) to move the instruction pointer" }},
    { 0x1d, { "SCVTCI", 0x1d, 0x1d, 1, 0,
    	  "Sets Control Value Table Cut-In\nPops 26.6 from stack, sets cvt cutin" }},
    { 0x1e, { "SSWCI", 0x1e, 0x1e, 1, 0,
    	  "Set Single Width Cut-In\nPops value for single width cut-in value (26.6)" }},
    { 0x1f, { "SSW", 0x1f, 0x1f, 1, 0,
    	  "Set Single Width\nPops value for single width value (FUnit)" }},
    { 0x20, { "DUP", 0x20, 0x20, 1, 2,
    	  "DUPlicate top stack element\nPushes the top stack element again" }},
    { 0x21, { "POP", 0x21, 0x21, 1, 0,
    	  "POP top stack element" }},
    { 0x22, { "CLEAR", 0x22, 0x22, 0, 0,
    	  "CLEAR\nPops all elements on stack" }},
    { 0x23, { "SWAP", 0x23, 0x23, 2, 2,
    	  "SWAP top two elements on stack" }},
    { 0x24, { "DEPTH", 0x24, 0x24, 0, 1,
    	  "DEPTH of stack\nPushes the number of elements on the stack" }},
    { 0x25, { "CINDEX", 0x25, 0x25, 1, 1,
    	  "Copy INDEXed element to stack\nPops an index & copies stack\nelement[index] to top of stack" }},
    { 0x26, { "MINDEX", 0x26, 0x26, 1, 0,
    	  "Move INDEXed element to stack\nPops an index & moves stack\nelement[index] to top of stack\n(removing it from where it was)" }},
    { 0x27, { "ALIGNPTS", 0x27, 0x27, 2, 0,
    	  "ALIGN PoinTS\nAligns (&pops) the two points which are on the stack\nby moving along freedom vector to the average of their\npositions on projection vector" }},
    { 0x29, { "UTP", 0x29, 0x29, 1, 0,
    	  "UnTouch Point\nPops a point number and marks it untouched" }},
    { 0x2a, { "LOOPCALL", 0x2a, 0x2a, 2, 0,
    	  "LOOP and CALL function\nPops a function number & count\nCalls function count times" }},
    { 0x2b, { "CALL", 0x2b, 0x2b, 1, 0,
    	  "CALL function\nPops a value, calls the function represented by it" }},
    { 0x2c, { "FDEF", 0x2c, 0x2c, 1, 0,
    	  "Function DEFinition\nPops a value (n) and starts the nth\nfunction definition" }},
    { 0x2d, { "ENDF", 0x2d, 0x2d, 0, 0,
    	  "END Function definition" }},
    { 0x2e, { "MDAP", 0x2e, 0x2f, 1, 0,
    	  "Move Direct Absolute Point[a]\n 0=>do not round\n 1=>round\nPops a point number, touches that point\nand perhaps rounds it to the grid along\nthe projection vector. Sets rp0&rp1 to the point" }},
    { 0x30, { "IUP", 0x30, 0x31, 0, 0,
    	  "Interpolate Untouched Points[a]\n 0=> interpolate in y direction\n 1=> x direction" }},
    { 0x32, { "SHP", 0x32, 0x33, -1, 0,
    	  "SHift Point using reference point[a]\n 0=>uses rp2 in zp1\n 1=>uses rp1 in zp0\nPops as many points as specified by the loop count\nShifts each by the amount the reference\npoint was shifted" }},
    { 0x34, { "SHC", 0x34, 0x35, 1, 0,
    	  "SHift Contour using reference point[a]\n 0=>uses rp2 in zp1\n 1=>uses rp1 in zp0\nPops number of contour to be shifted\nShifts the entire contour by the amount\nreference point was shifted" }},
    { 0x36, { "SHZ", 0x36, 0x37, 1, 0,
    	  "SHift Zone using reference point[a]\n 0=>uses rp2 in zp1\n 1=>uses rp1 in zp0\nPops the zone to be shifted\nShifts all points in zone by the amount\nthe reference point was shifted" }},
    { 0x38, { "SHPIX", 0x38, 0x38, -1, 0,
    	  "SHift point by a PIXel amount\nPops an amount (26.6) and as many points\nas the loop counter specifies\neach point is shifted along the FREEDOM vector" }},
    { 0x39, { "IP", 0x39, 0x39, -1, 0,
    	  "Interpolate Point\nPops as many points as specified in loop counter\nInterpolates each point to preserve original status\nwith respect to RP1 and RP2" }},
    { 0x3a, { "MSIRP", 0x3a, 0x3b, 2, 0,
    	  "Move Stack Indirect Relative Point[a]\n 0=>do not set rp0\n 1=>set rp0 to point\nPops a 26.6 distance and a point\nMoves point so it is distance from rp0" }},
    { 0x3c, { "ALIGNRP", 0x3c, 0x3c, -1, 0,
    	  "ALIGN to Reference Point\nPops as many points as specified in loop counter\nAligns points with RP0 by moving each\nalong freedom vector until distance to\nRP0 on projection vector is 0" }},
    { 0x3d, { "RTDG", 0x3d, 0x3d, 0, 0,
    	  "Round To Double Grid\nSets the round state (round to closest .5/int)" }},
    { 0x3e, { "MIAP", 0x3e, 0x3f, 2, 0,
    	  "Move Indirect Absolute Point[a]\n 0=>do not round, don't use cvt cutin\n 1=>round\nPops a point number & a cvt entry,\ntouches the point and moves it to the coord\nspecified in the cvt (along the projection vector).\nSets rp0&rp1 to the point" }},
    { 0x40, { "NPUSHB", 0x40, 0x40, 0, -1,
    	  "N PUSH Bytes\nReads an (unsigned) count byte from the\ninstruction stream, then reads and pushes\nthat many unsigned bytes" }},
    { 0x41, { "NPUSHW", 0x41, 0x41, 0, -1,
    	  "N PUSH Words\nReads an (unsigned) count byte from the\ninstruction stream, then reads and pushes\nthat many signed 2byte words" }},
    { 0x42, { "WS", 0x42, 0x42, 2, 0,
    	  "Write Store\nPops a value and an index and writes the value to storage[index]" }},
    { 0x43, { "RS", 0x43, 0x43, 1, 1,
    	  "Read Store\nPops an index into store array\nPushes value at that index" }},
    { 0x44, { "WCVTP", 0x44, 0x44, 2, 0,
    	  "Write Control Value Table in Pixel units\nPops a number(26.6) and a\nCVT index and writes the number to cvt[index]" }},
    { 0x45, { "RCVT", 0x45, 0x45, 1, 1,
    	  "Read Control Value Table entry\nPops an index to the CVT and\npushes it in 26.6 format" }},
    { 0x46, { "GC", 0x46, 0x47, 1, 1,
    	  "Get Coordinate[a] projected onto projection vector\n 0=>use current pos\n 1=>use original pos\nPops one point, pushes the coordinate of\nthe point along projection vector" }},
    { 0x48, { "SCFS", 0x48, 0x48, 2, 0,
    	  "Sets Coordinate From Stack using projection & freedom vectors\nPops a coordinate 26.6 and a point\nMoves point to given coordinate" }},
    { 0x49, { "MD", 0x49, 0x4a, 2, 1,
    	  "Measure Distance[a]\n 0=>distance with current positions\n 1=>distance with original positions\nPops two point numbers, pushes distance between them" }},
    { 0x4b, { "MPPEM", 0x4b, 0x4b, 0, 1,
    	  "Measure Pixels Per EM\nPushs the pixels per em (for current rasterization)" }},
    { 0x4c, { "MPS", 0x4c, 0x4c, 0, 1,
    	  "Measure Point Size\nPushes the current point size" }},
    { 0x4d, { "FLIPON", 0x4d, 0x4d, 0, 0,
    	  "set the auto FLIP boolean to ON" }},
    { 0x4e, { "FLIPOFF", 0x4e, 0x4e, 0, 0,
    	  "set the auto FLIP boolean to OFF" }},
    { 0x4f, { "DEBUG", 0x4f, 0x4f, 1, 0,
    	  "DEBUG call\nPops a value and executes a debugging interpreter\n(if available)" }},
    { 0x50, { "LT", 0x50, 0x50, 2, 1,
    	  "Less Than\nPops two values, pushes (0/1) if bottom el < top" }},
    { 0x51, { "LTEQ", 0x51, 0x51, 2, 1,
    	  "Less Than or EQual\nPops two values, pushes (0/1) if bottom el <= top" }},
    { 0x52, { "GT", 0x52, 0x52, 2, 1,
    	  "Greater Than\nPops two values, pushes (0/1) if bottom el > top" }},
    { 0x53, { "GTEQ", 0x53, 0x53, 2, 1,
    	  "Greater Than or EQual\nPops two values, pushes (0/1) if bottom el >= top" }},
    { 0x54, { "EQ", 0x54, 0x54, 2, 1,
    	  "EQual\nPops two values, tests for equality, pushes result(0/1)" }},
    { 0x55, { "NEQ", 0x55, 0x55, 2, 1,
    	  "Not EQual\nPops two values, tests for inequality, pushes result(0/1)" }},
    { 0x56, { "ODD", 0x56, 0x56, 1, 1,
    	  "ODD\nPops one value, rounds it and tests if it is odd(0/1)" }},
    { 0x57, { "EVEN", 0x57, 0x57, 1, 1,
    	  "EVEN\nPops one value, rounds it and tests if it is even(0/1)" }},
    { 0x58, { "IF", 0x58, 0x58, 1, 0,
    	  "IF test\nPops an integer,\nif 0 (false) next instruction is ELSE or EIF\nif non-0 execution continues normally\n(unless there's an ELSE)" }},
    { 0x59, { "EIF", 0x59, 0x59, 0, 0,
    	  "End IF\nEnds and IF or IF-ELSE sequence" }},
    { 0x5a, { "AND", 0x5a, 0x5a, 2, 1,
    	  "logical AND\nPops two values, ands them, pushes result" }},
    { 0x5b, { "OR", 0x5b, 0x5b, 2, 1,
    	  "logical OR\nPops two values, ors them, pushes result" }},
    { 0x5c, { "NOT", 0x5c, 0x5c, 1, 1,
    	  "logical NOT\nPops a number, if 0 pushes 1, else pushes 0" }},
    { 0x5d, { "DELTAP1", 0x5d, 0x5d, 1, 0,
    	  "DELTA exception P1\nPops a value n & then n exception specifications & points\nmoves each point at a given size by the amount" }},
    { 0x5e, { "SDB", 0x5e, 0x5e, 1, 0,
    	  "Set Delta Base\nPops value sets delta base" }},
    { 0x5f, { "SDS", 0x5f, 0x5f, 1, 0,
    	  "Set Delta Shift\nPops a new value for delta shift" }},
    { 0x60, { "ADD", 0x60, 0x60, 2, 1,
    	  "ADD\nPops two 26.6 fixed numbers from stack\nadds them, pushes result" }},
    { 0x61, { "SUB", 0x61, 0x61, 2, 1,
    	  "SUBtract\nPops two 26.6 fixed numbers from stack\nsubtracts them, pushes result" }},
    { 0x62, { "DIV", 0x62, 0x62, 2, 1,
    	  "DIVide\nPops two 26.6 numbers, divides them, pushes result" }},
    { 0x63, { "MUL", 0x63, 0x63, 2, 1,
    	  "MULtiply\nPops two 26.6 numbers, multiplies them, pushes result" }},
    { 0x64, { "ABS", 0x64, 0x64, 1, 1,
    	  "ABSolute Value\nReplaces top of stack with its abs" }},
    { 0x65, { "NEG", 0x65, 0x65, 1, 1,
    	  "NEGate\nNegates the top of the stack" }},
    { 0x66, { "FLOOR", 0x66, 0x66, 1, 1,
    	  "FLOOR\nPops a value, rounds to lowest int, pushes result" }},
    { 0x67, { "CEILING", 0x67, 0x67, 1, 1,
    	  "CEILING\nPops one 26.6 value, rounds upward to an int\npushes result" }},
    { 0x68, { "ROUND", 0x68, 0x6b, 1, 1,
    	  "ROUND value[ab]\n ab=0 => grey distance\n ab=1 => black distance\n ab=2 => white distance\nRounds a coordinate (26.6) at top of stack\nand compensates for engine effects" }},
    { 0x6c, { "NROUND", 0x6c, 0x6f, 1, 1,
    	  "No ROUNDing of value[ab]\n ab=0 => grey distance\n ab=1 => black distance\n ab=2 => white distance\nPops a coordinate (26.6), changes it (without\nrounding) to compensate for engine effects\npushes it back" }},
    { 0x70, { "WCVTF", 0x70, 0x70, 2, 0,
    	  "Write Control Value Table in Funits\nPops a number(Funits) and a\nCVT index and writes the number to cvt[index]" }},
    { 0x71, { "DELTAP2", 0x71, 0x71, 1, 0,
    	  "DELTA exception P2\nPops a value n & then n exception specifications & points\nmoves each point at a given size by the amount" }},
    { 0x72, { "DELTAP3", 0x72, 0x72, 1, 0,
    	  "DELTA exception P3\nPops a value n & then n exception specifications & points\nmoves each point at a given size by the amount" }},
    { 0x73, { "DELTAC1", 0x73, 0x73, 1, 0,
    	  "DELTA exception C1\nPops a value n & then n exception specifications & cvt entries\nchanges each cvt entry at a given size by the pixel amount" }},
    { 0x74, { "DELTAC2", 0x74, 0x74, 1, 0,
    	  "DELTA exception C2\nPops a value n & then n exception specifications & cvt entries\nchanges each cvt entry at a given size by the pixel amount" }},
    { 0x75, { "DELTAC3", 0x75, 0x75, 1, 0,
    	  "DELTA exception C3\nPops a value n & then n exception specifications & cvt entries\nchanges each cvt entry at a given size by the pixel amount" }},
    { 0x76, { "SROUND", 0x76, 0x76, 1, 0,
    	  "Super ROUND\nToo complicated. Look it up" }},
    { 0x77, { "S45ROUND", 0x77, 0x77, 1, 0,
    	  "Super 45\260 ROUND\nToo complicated. Look it up" }},
    { 0x78, { "JROT", 0x78, 0x78, 2, 0,
    	  "Jump Relative On True\nPops a boolean and an offset\nChanges instruction pointer by offset bytes\nif boolean is true" }},
    { 0x79, { "JROF", 0x79, 0x79, 2, 0,
    	  "Jump Relative On False\nPops a boolean and an offset\nChanges instruction pointer by offset bytes\nif boolean is false" }},
    { 0x7a, { "ROFF", 0x7a, 0x7a, 0, 0,
    	  "Round OFF\nSets round state so that no rounding occurs\nbut engine compensation does" }},
    { 0x7c, { "RUTG", 0x7c, 0x7c, 0, 0,
    	  "Round Up To Grid\nSets the round state" }},
    { 0x7d, { "RDTG", 0x7d, 0x7d, 0, 0,
    	  "Round Down To Grid\n\nSets round state to the obvious" }},
    { 0x7e, { "SANGW", 0x7e, 0x7e, 1, 0,
    	  "Set ANGle Weight\nPops an int, and sets the angle\nweight state variable to it\nObsolete" }},
    { 0x7f, { "AA", 0x7f, 0x7f, 1, 0,
    	  "Adjust Angle\nObsolete instruction\nPops one value" }},
    { 0x80, { "FLIPPT", 0x80, 0x80, -1, 0,
    	  "FLIP PoinT\nPops as many points as specified in loop counter\nFlips whether each point is on/off curve" }},
    { 0x81, { "FLIPRGON", 0x81, 0x81, 2, 0,
    	  "FLIP RanGe ON\nPops two point numbers\nsets all points between to be on curve points" }},
    { 0x82, { "FLIPRGOFF", 0x82, 0x82, 2, 0,
    	  "FLIP RanGe OFF\nPops two point numbers\nsets all points between to be off curve points" }},
    { 0x85, { "SCANCTRL", 0x85, 0x85, 1, 0,
    	  "SCAN conversion ConTRoL\nPops a number which sets the\ndropout control mode" }},
    { 0x86, { "SDPVTL", 0x86, 0x87, 2, 0,
    	  "Set Dual Projection Vector To Line[a]\n 0 => parallel to line\n 1=>orthogonal to line\nPops two points used to establish the line\nSets a second projection vector based on original\npositions of points" }},
    { 0x88, { "GETINFO", 0x88, 0x88, 1, 1,
    	  "GET INFOrmation\nPops information type, pushes result" }},
    { 0x89, { "IDEF", 0x89, 0x89, 1, 0,
    	  "Instruction DEFinition\nPops a value which becomes the opcode\nand begins definition of new instruction" }},
    { 0x8a, { "ROLL", 0x8a, 0x8a, 3, 3,
    	  "ROLL the top three stack elements" }},
    { 0x8b, { "MAX", 0x8b, 0x8b, 2, 1,
    	  "MAXimum of top two stack entries\nPops two values, pushes the maximum back" }},
    { 0x8c, { "MIN", 0x8c, 0x8c, 2, 1,
    	  "Minimum of top two stack entries\nPops two values, pushes the minimum back" }},
    { 0x8d, { "SCANTYPE", 0x8d, 0x8d, 1, 0,
    	  "SCANTYPE\nPops number which sets which scan\nconversion rules to use" }},
    { 0x8e, { "INSTCTRL", 0x8e, 0x8e, 2, 0,
    	  "INSTRuction execution ConTRoL\nPops a selector and value\nSets a state variable" }},
    { 0xb0, { "PUSHB", 0xb0, 0xb7, 0, -2,
    	  "PUSH Byte[abc]\n abc is the number-1 of bytes to push\nReads abc+1 unsigned bytes from\nthe instruction stream and pushes them" }},
    { 0xb8, { "PUSHW", 0xb8, 0xbf, 0, -2,
    	  "PUSH Word[abc]\n abc is the number-1 of words to push\nReads abc+1 signed words from\nthe instruction stream and pushes them" }},
    { 0xc0, { "MDRP", 0xc0, 0xdf, 1, 0,
    	  "Move Direct Relative Point[abcde]\n a=0=>don't set rp0\n a=1=>set rp0 to p\n b=0=>do not keep distance more than minimum\n b=1=>keep distance at least minimum\n c=0 do not round\n c=1 round\n de=0 => grey distance\n de=1 => black distance\n de=2 => white distance\nPops a point moves it so that it maintains\nits original distance to the rp0. Sets\nrp1 to rp0, rp2 to point, sometimes rp0 to point" }},
    { 0xe0, { "MIRP", 0xe0, 0xff, 2, 0,
    	  "Move Indirect Relative Point[abcde]\n a=0=>don't set rp0\n a=1=>set rp0 to p\n b=0=>do not keep distance more than minimum\n b=1=>keep distance at least minimum\n c=0 do not round nor use cvt cutin\n c=1 round & use cvt cutin\n de=0 => grey distance\n de=1 => black distance\n de=2 => white distance\nPops a cvt index and a point moves it so that it\nis cvt[index] from rp0. Sets\nrp1 to rp0, rp2 to point, sometimes rp0 to point" }},
};

const std::map<std::string, uint8_t> InstrEdit::ByArg = {
    { "x-axis", 1 },
    { "orthog", 1 },
    { "rnd", 1 },
    { "x", 1 },
    { "rp1", 1 },
    { "rp0", 1 },
    { "rnd", 1 },
    { "orig", 1 },
    { "black", 1 },
    { "white", 2 },
    { "min", 8 },
};


instr_data InstrEdit::invalidCode (uint8_t code) {
    std::stringstream ss;
    ss << "Invalid code: 0x" << std::hex << static_cast<unsigned int> (code);

    instr_data ret;
    ret.isInstr = false;
    ret.code = code;
    ret.base = code;
    ret.nPushes = 0;
    ret.repr = ss.str ();
    ret.toolTip = ss.str ();
    return ret;
}

int InstrEdit::byInstr (std::string instr) {
    if (InstrEdit::ByInstr.count (instr)) {
	return InstrEdit::ByInstr.at (instr);
    } else {
	for (auto &pair : InstrSet) {
	    if (pair.second.name == instr) {
		InstrEdit::ByInstr[instr] = pair.second.rangeStart;
		return pair.second.rangeStart;
	    }
	}
    }
    return -1;
}

void InstrEdit::checkCodeArgs (instr_data &d, std::string &name) {
    std::vector<std::string> args;
    args.reserve (5);
    std::stringstream ss;
    ss << name;
    switch (d.base) {
      case 0x00: // SVTCA
      case 0x02: // SPVTCA
      case 0x04: // SFVTCA
	if (d.code & 1)
	    args.push_back ("x-axis");
	else
	    args.push_back ("y-axis");
	break;
      case 0x06: // SPVTL
      case 0x08: // SFVTL
	if (d.code & 1)
	    args.push_back ("orthog");
	else
	    args.push_back ("parallel");
	break;
      case 0x2e: // MDAP
	if (d.code & 1)
	    args.push_back ("rnd");
	else
	    args.push_back ("no-rnd");
	break;
      case 0x30: // IUP
	if (d.code & 1)
	    args.push_back ("x");
	else
	    args.push_back ("y");
	break;
      case 0x32: // SHP
      case 0x34: // SHC
      case 0x36: // SHZ
	if (d.code & 1)
	    args.push_back ("rp1");
	else
	    args.push_back ("rp2");
	break;
      case 0x3a: // MSIRP
	if (d.code & 1)
	    args.push_back ("rp0");
	break;
      case 0x3e: // MIAP
	if (d.code & 1)
	    args.push_back ("rnd");
	else
	    args.push_back ("no-rnd");
	break;
      case 0x46: // GC
	if (d.code & 1)
	    args.push_back ("orig");
	else
	    args.push_back ("cur");
	break;
      case 0x49: // MD
	if (d.code & 1)
	    args.push_back ("orig");
	else
	    args.push_back ("grid");
	break;
      case 0x68: // ROUND
      case 0x6c: // NROUND
	if (d.code & 1)
	    args.push_back ("black");
	else if (d.code & 2)
	    args.push_back ("white");
	else
	    args.push_back ("gray");
	break;
      case 0x86: // SDPVTL
	if (d.code & 1)
	    args.push_back ("orthog");
	else
	    args.push_back ("parallel");
	break;
      case 0xb0: // PUSHB
      case 0xb8: // PUSHW
	d.nPushes = d.code - d.base + 1;
	args.push_back (std::to_string (d.nPushes));
	break;
      case 0xc0: // MDRP
      case 0xe0: // MIRP
	if (d.code & 16)
	    args.push_back ("rp0");
	if (d.code & 8)
	    args.push_back ("min");
	if (d.code & 4)
	    args.push_back ("rnd");
	if (d.code & 1)
	    args.push_back ("black");
	else if (d.code & 2)
	    args.push_back ("white");
	else
	    args.push_back ("gray");
	break;
      default:
	;
    }
    if (args.size ()) {
	ss << '[';
	for (size_t i=0; i<(args.size () - 1); i++)
	    ss << args[i] << ", ";
	ss << args[args.size () - 1] << ']';
    }
    d.repr = ss.str ();
}

instr_data InstrEdit::byCode (uint8_t code) {
    instr_def def;
    instr_data ret;
    if (InstrSet.count (code)) {
	def = InstrSet.at (code);
    } else if (code >= 0x8f && code <= 0xaf) {
	return invalidCode (code);
    // MDRP, too wide range of possible values
    } else if (code > 0xc0 && code <= 0xdf) {
	def = InstrSet.at (0xc0);
    // MIRP, too wide range of possible values
    } else if (code > 0xe0 /*&& code <= 0xff*/) {
	def = InstrSet.at (0xe0);
    } else {
	uint8_t test = code-1;
	while (!InstrSet.count (test)) test--;
	if (code >= InstrSet.at (test).rangeStart && code <= InstrSet.at (test).rangeEnd)
	    def = InstrSet.at (test);
	else
	    return invalidCode (code);
    }
    ret.isInstr = true;
    ret.code = code;
    ret.base = def.rangeStart;
    ret.nPushes = 0;
    ret.toolTip = def.toolTip;
    checkCodeArgs (ret, def.name);

    return ret;
}

InstrEdit::InstrEdit (uint8_t *data, uint16_t len, QWidget *parent) :
    QWidget (parent), m_changed (false) {

    decode (data, len);
    QStackedWidget *stack = qobject_cast<QStackedWidget *> (parent);

    QGridLayout *grid = new QGridLayout ();
    m_stack = new QStackedLayout ();
    m_edit = new QTextEdit ();
    m_instrTab = new QTableWidget ();
    m_instrTab->setColumnCount (2);
    grid->setContentsMargins (0, 0, 0, 0);
    // Setting setSizeAdjustPolicy may result into a very strange effect,
    // when docked and there isn't enough instructions to fill all available
    // space in the vertical direction with table lines. So check if our
    // parent widget is a QStackWidget (otherwise we are in a separate window).
    if (!stack)
	m_instrTab->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    m_stack->addWidget (m_edit);
    m_stack->addWidget (m_instrTab);
    m_stack->setCurrentWidget (m_instrTab);

    fillTable ();
    grid->addLayout (m_stack, 0, 0, 1, 2);

    m_discardButton = new QPushButton (QWidget::tr ("Discard"));
    connect (m_discardButton, &QPushButton::clicked, this, &InstrEdit::discard);
    grid->addWidget (m_discardButton, 2, 0, 1, 1);
    m_discardButton->setVisible (false);

    m_editButton = new QPushButton (QWidget::tr ("Edit"));
    connect (m_editButton, &QPushButton::clicked, this, &InstrEdit::edit);
    grid->addWidget (m_editButton, 2, 1, 1, 1);

    this->setLayout (grid);
}

void InstrEdit::decode (uint8_t *data, uint16_t len) {
    size_t pos = 0;
    m_instrs.reserve (len);

    while (pos < len) {
	uint8_t ch = data[pos];
	pos++;
	m_instrs.push_back (byCode (ch));
	instr_data &cur = m_instrs.back ();
	// NPUSHB, NPUSHW
	if ((cur.base == 0x40 || cur.base == 0x41) && pos < len) {
	    cur.nPushes = data[pos];
	    m_instrs.emplace_back ();
	    instr_data &num = m_instrs.back ();
	    num.isInstr = false;
	    num.code = data[pos];
	    num.base = cur.base;
	    num.repr = "  " + std::to_string (num.code);
	    num.toolTip = "A count, specifying how many bytes/shorts\nshould be pushed to the stack";
	    pos++;
	}
	// NPUSHB, PUSHB
	if (cur.base == 0x40 || cur.base == 0xb0) {
	    for (size_t i=0; i<cur.nPushes && pos < len; i++) {
		m_instrs.emplace_back ();
		instr_data &num = m_instrs.back ();
		num.isInstr = false;
		num.code = data[pos];
		num.base = cur.base;
		num.repr = "  " + std::to_string (num.code);
		num.toolTip = "An unsigned byte to be pushed to the stack";
		pos++;
	    }
	// NPUSHW, PUSHW
	} else if (cur.base == 0x41 || cur.base == 0xb8) {
	    for (size_t i=0; i<cur.nPushes && pos < static_cast<uint16_t> (len + 1); i++) {
		m_instrs.emplace_back ();
		instr_data &num = m_instrs.back ();
		num.isInstr = false;
		num.code = FontTable::getushort (reinterpret_cast<char *> (data), pos);
		num.base = cur.base;
		num.repr = "  " + std::to_string (num.code);
		num.toolTip = "A short to be pushed to the stack";
		pos += 2;
	    }
	}
    }
}

void InstrEdit::fillTable () {
    int cnt = m_instrs.size ();
    m_instrTab->horizontalHeader ()->setStretchLastSection (true);
    m_instrTab->setSelectionBehavior (QAbstractItemView::SelectRows);
    m_instrTab->setSelectionMode (QAbstractItemView::SingleSelection);

    if (cnt == 0) {
	m_instrTab->setRowCount (1);
	auto dummy_item1 = new QTableWidgetItem ("");
	dummy_item1->setFlags (dummy_item1->flags () & ~Qt::ItemIsEditable);
	m_instrTab->setItem (0, 1, dummy_item1);
	auto dummy_item2 = new QTableWidgetItem ("<no instrs>");
	dummy_item2->setFlags (dummy_item2->flags () & ~Qt::ItemIsEditable);
	m_instrTab->setItem (0, 1, dummy_item2);
    } else {
	m_instrTab->setRowCount (cnt);
	for (int i=0; i<cnt; i++) {
	    auto &data = m_instrs[i];
	    instr_data *pd = (i > 0) ? &m_instrs[i-1] : nullptr;
	    // NPUSHW (except the first value) and PUSHW
	    int fw = (!data.isInstr  && ((data.base == 0x41 && pd && pd->isInstr) || data.base == 0xb8)) ? 4 : 2;
	    QString hexvalue = QString("%1").arg (static_cast<uint16_t> (data.code), fw, 16, QLatin1Char ('0'));
	    auto hex_item = new QTableWidgetItem (hexvalue);
	    hex_item->setFlags (hex_item->flags () & ~Qt::ItemIsEditable);
	    m_instrTab->setItem (i, 0, hex_item);
	    auto repr_item = new QTableWidgetItem (QString::fromStdString (data.repr));
	    repr_item->setFlags (repr_item->flags () & ~Qt::ItemIsEditable);
	    repr_item->setData (Qt::ToolTipRole, QString::fromStdString (data.toolTip));
	    m_instrTab->setItem (i, 1, repr_item);
	}
    }
    m_instrTab->selectRow (0);

    QFontMetrics fm = m_instrTab->fontMetrics ();
    int w0 = fm.boundingRect ("~0000~").width ();
    int w1 = fm.boundingRect ("~MIRP[rp0, min, rnd, black]~").width ();
    m_instrTab->setColumnWidth (0, w0);
    m_instrTab->setColumnWidth (1, w1);
    m_instrTab->horizontalHeader ()->hide ();
}

bool InstrEdit::changed () {
    return m_changed;
}

std::vector<uint8_t> InstrEdit::data () {
    std::vector<uint8_t> ret;
    ret.reserve (m_instrs.size ()*2);
    instr_data *pd = nullptr;
    for (auto &instr : m_instrs) {
	bool is_word = (!instr.isInstr  && ((instr.base == 0x41 && pd && pd->isInstr) || instr.base == 0xb8));
        if (is_word) {
	    ret.push_back (instr.code>>8);
	    ret.push_back (instr.code&0xff);
        } else {
	    ret.push_back (instr.code);
	}
	pd = &instr;
    }
    return ret;
}

void InstrEdit::fillEdit () {
    int cnt = m_instrs.size ();
    std::stringstream ss;

    for (int i=0; i<cnt; i++) {
        auto &data = m_instrs[i];
	ss << data.repr << std::endl;
    }
    m_edit->setPlainText (QString::fromStdString (ss.str ()));
}

static void set_cursor (QTextEdit *edit, int startPos, int len) {
    QTextCursor c = edit->textCursor ();
    c.setPosition (startPos);
    c.setPosition (startPos+len, QTextCursor::KeepAnchor);
    edit->setTextCursor (c);
}

void InstrEdit::edit () {
    QWidget *cur = m_stack->currentWidget ();
    QTableWidget *testw = qobject_cast<QTableWidget *> (cur);
    if (testw) {
	m_stack->setCurrentWidget (m_edit);
	fillEdit ();
	m_editButton->setText ("Compile");
	m_discardButton->setVisible (true);
    } else {
	int sel_start, sel_len;
	std::vector<instr_data> new_instrs;
	std::string edited = m_edit->toPlainText ().toStdString ();
	int ret = parse (edited, new_instrs, sel_start, sel_len);
	switch (ret) {
	  case TTFinstrs::Parse_OK:
	    m_edit->clear ();
	    m_stack->setCurrentWidget (m_instrTab);
	    m_editButton->setText ("Edit");
	    m_instrs = new_instrs;
	    fillTable ();
	    m_discardButton->setVisible (false);
	    m_changed = true;
	    emit (instrChanged ());
	    break;
	  case TTFinstrs::Parse_WrongInstr:
	    set_cursor (m_edit, sel_start, sel_len);
	    FontShepherd::postError (tr ("TTF Instructions compile error"),
		tr ("Parsing failed (unknown instruction)"), this);
	    break;
	  case TTFinstrs::Parse_NeedsNumber:
	    set_cursor (m_edit, sel_start, sel_len);
	    FontShepherd::postError (tr ("TTF Instructions compile error"),
		tr ("Parsing failed (got command, number expected)"), this);
	    break;
	  case TTFinstrs::Parse_NeedsInstr:
	    set_cursor (m_edit, sel_start, sel_len);
	    FontShepherd::postError (tr ("TTF Instructions compile error"),
		tr ("Parsing failed (got number, command expected)"), this);
	    break;
	  case TTFinstrs::Parse_NeedsBracket:
	    set_cursor (m_edit, sel_start, sel_len);
	    FontShepherd::postError (tr ("TTF Instructions compile error"),
		tr ("Parsing failed (a closing bracket needed)"), this);
	    break;
	  case TTFinstrs::Parse_TooLarge:
	    set_cursor (m_edit, sel_start, sel_len);
	    FontShepherd::postError (tr ("TTF Instructions compile error"),
		tr ("The number is too large (should be between 1 and 8)"), this);
	    break;
	  case TTFinstrs::Parse_TooLargeByte:
	    set_cursor (m_edit, sel_start, sel_len);
	    FontShepherd::postError (tr ("TTF Instructions compile error"),
		tr ("The number is too large (should be between 1 and 255)"), this);
	    break;
	  case TTFinstrs::Parse_TooLargeWord:
	    set_cursor (m_edit, sel_start, sel_len);
	    FontShepherd::postError (tr ("TTF Instructions compile error"),
		tr ("The number is too large (should be between -32,767 and 32,767)"), this);
	    break;
	  case TTFinstrs::Parse_Unexpected:
	    set_cursor (m_edit, sel_start, sel_len);
	    FontShepherd::postError (tr ("TTF Instructions compile error"),
		tr ("Unexpected character"), this);
	    break;
	  default:
	    set_cursor (m_edit, sel_start, sel_len);
	    FontShepherd::postError (tr ("TTF Instructions compile error"),
		tr ("Parsing failed (unknown error)"), this);
	}
    }
}

void InstrEdit::discard () {
    m_edit->clear ();
    m_stack->setCurrentWidget (m_instrTab);
    m_editButton->setText ("Edit");
    m_discardButton->setVisible (false);
}

static void skip_space (std::string &edited, size_t &pos) {
    uint8_t code = edited[pos];
    while (std::isspace (code)) {
        pos++;
        code = edited[pos];
    }
}

int InstrEdit::getInstrArgs (std::vector<std::string> &args, std::string &edited, size_t &pos, int &start, int &len) {
    skip_space (edited, pos);
    char left = edited[pos];
    if (left != '[' && left != '(')
	return TTFinstrs::Parse_OK;
    char right = (left == '[') ? ']' : ')';
    char sep = ',';
    pos+=1;
    skip_space (edited, pos);
    size_t rpos = edited.find (right, pos);
    if (rpos == std::string::npos) {
	start = pos;
	len = 1;
	return TTFinstrs::Parse_NeedsBracket;
    }
    while (pos < rpos) {
        int lim = 0;
	skip_space (edited, pos);
        do {
	    lim++;
        } while (pos+lim < rpos && edited[pos+lim] != sep);
	int len = lim;
	while (std::isspace (edited[pos+len])) len--;
	args.emplace_back (edited.substr (pos, len));
	pos = pos+lim+1;
    }
    // Go to next char after the closing bracked
    //pos++;
    return TTFinstrs::Parse_OK;
}

int InstrEdit::checkInstrArgs (instr_data &d, std::vector<std::string> &args) {
    for (auto &arg : args) {
	if (ByArg.count (arg)) {
	    uint8_t flag = ByArg.at (arg);
	    // MDRP, MIRP
	    if (d.base == 0xc0 || d.base == 0xe0) {
		if (arg == "rp0") flag = 16;
		else if (arg == "rnd") flag = 4;
	    }
	    d.code |= flag;
	}
    }
    return TTFinstrs::Parse_OK;
}

int InstrEdit::parse (std::string &edited, std::vector<instr_data> &instr_lst, int &sel_start, int &sel_len) {
    size_t pos = 0;
    size_t len = edited.length ();
    int nums_needed = 0;
    while (pos < len) {
	skip_space (edited, pos);
	uint8_t code = edited[pos];
	if (std::isalpha (code)) {
	    int len = 0;
	    do {
		len++;
	    } while (std::isalpha (edited[pos+len]) || std::isdigit (edited[pos+len]));
	    std::string instr = edited.substr (pos, len);
	    if (nums_needed) {
		sel_start = pos;
		sel_len = len;
		return TTFinstrs::Parse_NeedsNumber;
	    }
	    int instr_code = byInstr (instr);
	    if (instr_code < 0) {
		sel_start = pos;
		sel_len = len;
		return TTFinstrs::Parse_WrongInstr;
	    }
	    instr_lst.emplace_back ();
	    auto &d = instr_lst.back ();
	    auto &def = InstrSet.at (instr_code);
	    d.isInstr = true;
	    d.base = d.code = instr_code;
	    d.toolTip = def.toolTip;
	    d.nPushes = 0;

	    pos += len;
	    std::vector<std::string> args;
	    int args_ok = getInstrArgs (args, edited, pos, sel_start, sel_len);
	    if (args_ok != TTFinstrs::Parse_OK)
		return args_ok;
	    // PUSHB, PUSHW
	    if (instr_code == 0xb0 || instr_code == 0xb8) {
		if (!std::all_of (args[0].begin (), args[0].end (), ::isdigit)) {
		    return TTFinstrs::Parse_NeedsNumber;
		} else {
		    size_t add = std::atoi (args[0].c_str ());
		    if (add < 1 || add > 8)
			return TTFinstrs::Parse_TooLarge;
		    d.code = d.base + add - 1;
		    d.nPushes = nums_needed = add;
		}
	    // NPUSHB, NPUSHW
	    } else if (instr_code == 0x40 || instr_code == 0x41) {
		nums_needed = 1;
	    } else {
		checkInstrArgs (d, args);
	    }
	    checkCodeArgs (d, instr);

	} else if (std::isdigit (code) || code == '-') {
	    int len = 0;
	    do {
		len++;
	    } while (std::isdigit (edited[pos+len]));
	    sel_start = pos;
	    sel_len = len;
	    if (!nums_needed)
		return TTFinstrs::Parse_NeedsInstr;
	    nums_needed--;
	    std::string str_code = edited.substr (pos, len);
	    int code = std::atoi (str_code.c_str ());
	    auto &last_d = instr_lst.back ();
	    std::string tooltip;

	    pos +=len;
	    // NPUSHB, NPUSHW
	    if (last_d.isInstr && (last_d.base == 0x40 || last_d.base == 0x41) && last_d.nPushes == 0) {
		if (code < 0 || code > 256)
		    return TTFinstrs::Parse_TooLargeByte;
		last_d.nPushes = code;
		nums_needed = code;
		tooltip = "A count, specifying how many bytes/shorts\nshould be pushed to the stack";
	    // NPUSHB, PUSHB
	    } else if (last_d.base == 0x40 || last_d.base == 0xb0) {
		if (code < 0 || code > 255)
		    return TTFinstrs::Parse_TooLargeByte;
		tooltip = "An unsigned byte to be pushed to the stack";
	    // NPUSHW, PUSHW
	    } else if (last_d.base == 0x41 || last_d.base == 0xb8) {
		/*if (code < -32,767 || code > 32,767)
		    return TTFinstrs::Parse_TooLargeWord;*/
		tooltip = "A short to be pushed to the stack";
	    }
	    instr_lst.emplace_back ();
	    auto &d = instr_lst.back ();
	    d.isInstr = false;
	    d.base = last_d.base;
	    d.code = code;
	    d.toolTip = tooltip;
	    d.nPushes = 0;
	    d.repr = "  " + std::to_string (code);
	} else {
	    sel_start = pos;
	    sel_len = 1;
	    return TTFinstrs::Parse_Unexpected;
	}
    }
    return TTFinstrs::Parse_OK;
}

static int32_t to_f26dot6 (double num) {
    return (std::lround (num*64));
}

static double from_f26dot6 (int val) {
    return (static_cast<double> (val)/64);
}

static BasePoint getUnit (IPoint *start, IPoint *end, bool orthog) {
    BasePoint unit;
    unit.x = (end->x - start->x);
    unit.y = (end->y - start->y);
    double length = std::sqrt (std::pow (unit.x, 2) + std::pow (unit.y, 2));
    unit.x /= length;
    unit.y /= length;
    if (orthog) {
	std::swap (unit.x, unit.y);
	unit.x = -unit.x;
    }
    return unit;
}

bool GraphicsState::getPoint (uint32_t num, int zp_num, IPoint &pt) {
    BasePoint *base;
    int zone = zp[zp_num];
    if (errorCode == TTFinstrs::Parse_WrongPointNumber)
	return false;

    if (zone == 1) {
        g->getTTFPoint (num, 0, base);
	if (!base) {
	    errorCode = TTFinstrs::Parse_WrongPointNumber;
	    return false;
	} else {
	    pt.x = base->x * 64; pt.y = base->y * 64;
	}
    } else {
	if (num < twilightPts.size ()) {
	    pt = twilightPts[num];
	} else {
	    errorCode = TTFinstrs::Parse_WrongTwilightPointNumber;
	    return false;
	}
    }
    return true;
}

bool GraphicsState::setZonePointer (instr_props &props, int idx, int val) {
    if (val < 0 || val > 1) {
	errorCode = TTFinstrs::Parse_WrongZone;
	return false;
    }

    if (idx < 0 || idx > 2) {
	for (size_t i=0; i<3; i++) zp[i] = val;
    } else {
	zp[idx] = val;
    }
    if (val == 0)
        props.z0used = true;
    return true;
}

int16_t GraphicsState::readCvt (int idx) {
    if (idx < 0 || idx >= static_cast<int> (cvt.size ())) {
	errorCode = TTFinstrs::Parse_WrongCvtIndex;
	return 0xFFFF;
    }
    return cvt[idx];
}

bool GraphicsState::writeCvt (int idx, int16_t val) {
    if (idx < 0 || idx >= static_cast<int> (cvt.size ())) {
	errorCode = TTFinstrs::Parse_WrongCvtIndex;
	return false;
    }
    cvt[idx] = val;
    return true;
}

int32_t GraphicsState::readStorage (size_t idx) {
    if (idx >= storage.size ()) {
	errorCode = TTFinstrs::Parse_WrongStorageIndex;
	return 0xFFFF;
    }
    return storage[idx];
}

void GraphicsState::writeStorage (size_t idx, int32_t val) {
    if (idx >= storage.size ()) {
	storage.resize (idx+1);
    }
    storage[idx] = val;
}

bool GraphicsState::pop (int32_t &val) {
    if (istack.empty ()) {
	errorCode = TTFinstrs::Parse_StackExceeded;
	return false;
    }
    val = istack.back ();
    istack.pop_back ();
    return true;
}

bool GraphicsState::pop2 (int32_t &val1, int32_t &val2) {
    if (istack.size () < 2) {
	errorCode = TTFinstrs::Parse_StackExceeded;
	return false;
    }
    val1 = istack.back ();
    istack.pop_back ();
    val2 = istack.back ();
    istack.pop_back ();
    return true;
}

int InstrEdit::skipBranch (std::vector<uint8_t> &bytecode, uint32_t &pos, bool func, int indent) {
    size_t len = bytecode.size ();
    int level = indent;
    while (pos<len) {
	uint8_t code = bytecode[pos++];
	instr_data d = byCode (code);
#undef _FS_DEBUG_BYTECODE_INTERPRETER
#ifdef _FS_DEBUG_BYTECODE_INTERPRETER
	for (int i=0; i<indent; i++) std::cerr << "  ";
	std::cerr << d.repr << " (skipped) pos=" << pos << " from " << len << std::endl;
#endif
	switch (d.base) {
	  // NPUSHB, NPUSHW
	  case 0x40:
	  case 0x41:
	    d.nPushes = bytecode[pos++];
	  // PUSHB, PUSHW
	  /* fall through */
	  case 0xb0:
	  case 0xb8:
	    for (size_t i=0; i<d.nPushes && pos < len; i++) {
		if (d.base == 0x40 || d.base == 0xb0) {
		    pos++;
		} else {
		    pos+=2;
		}
	    }
	    break;
	  case 0x58: //IF
	    level++;
	    break;
	  case 0x59: //EIF
	  case 0x1b: //ELSE
	    if (level == indent && !func)
		return 0;
	    if (d.base == 0x59)
		level--;
	    break;
	  case 0x2d: //ENDF
	    if (func)
		return 0;
	  default:
	    ;
	}
    }
    return 0;
}

// A very basic bytecode interpreter, which does essentially nothing
// except attempting to walk through TTF instructions properly maintaining
// stack depth and other parameters. It is currently used to calculate
// some values needed for the 'maxp' table
int InstrEdit::quickExecute (std::vector<uint8_t> &bytecode, GraphicsState &state, instr_props &props, int level) {
    size_t len = bytecode.size ();
    std::vector<int32_t> &istack = state.istack;
    uint32_t pos = 0, startpos;

    while (pos<len) {
	uint8_t code = bytecode[pos++];
	instr_data d = byCode (code);
	int top, top2, ppdiff, res;
	BasePoint unit;
	IPoint ipt1, ipt2;
#ifdef _FS_DEBUG_BYTECODE_INTERPRETER
	for (int i=0; i<level; i++) std::cerr << "  ";
	std::cerr << d.repr << " pos=" << pos << " from " << len << " ; stack size was " << istack.size () << "; stack top: ";
	for (int i=istack.size ()-1; i>=0 && i>((int) istack.size ()-6); i--)
	    std::cerr << istack[i] << ' ';
	std::cerr << std::endl;
#endif
	switch (d.base) {
	  case 0x10: // SRP0
	    state.pop (top);
	    state.rp[0] = top;
	    break;
	  case 0x11: // SRP1
	    state.pop (top);
	    state.rp[1] = top;
	    break;
	  case 0x12: // SRP2
	    state.pop (top);
	    state.rp[2] = top;
	    break;
	  case 0x13: // SZP0
	    state.pop (top);
	    state.setZonePointer (props, 0, top);
	    break;
	  case 0x14: // SZP1
	    state.pop (top);
	    state.setZonePointer (props, 1, top);
	    break;
	  case 0x15: // SZP2
	    state.pop (top);
	    state.setZonePointer (props, 2, top);
	    break;
	  case 0x16: // SZPS
	    state.pop (top);
	    state.setZonePointer (props, -1, top);
	    break;
	  // NPUSHB, NPUSHW
	  case 0x40:
	  case 0x41:
	    d.nPushes = bytecode[pos++];
	  // PUSHB, PUSHW
	  /* fall through */
	  case 0xb0:
	  case 0xb8:
	    for (size_t i=0; i<d.nPushes && pos < len; i++) {
		if (d.base == 0x40 || d.base == 0xb0) {
		    istack.push_back (bytecode[pos++]);
		} else {
		    int16_t w = ((bytecode[pos]<<8)|bytecode[pos+1]);
		    istack.push_back (w);
		    pos+=2;
		}
	    }
	    if (istack.size () > props.maxStackDepth)
		props.maxStackDepth = istack.size ();
	    break;
	  case 0x17: //SLOOP
	    state.pop (top);
	    state.nloop = top;
	    break;
	  case 0x4d: //FLIPON
	    state.flip = true;
	    break;
	  case 0x4e: //FLIPOFF
	    state.flip = false;
	    break;
	  case 0x22: //CLEAR
	    istack.clear ();
	    break;
	  case 0x38: //SHPIX
	    // pixel amount, currently ignored
	    state.pop (top);
	  /* fall through */
	  case 0x32: //SHP
	  case 0x39: //IP
	  case 0x80: //FLIPPT
	  case 0x3c: //ALIGNRP
	    for (size_t i=0; i<state.nloop; i++) {
		if (state.pop (top)) {
		    if (top == props.rBearingPointNum)
			props.rBearingTouched = true;
		} else {
		    break;
		}
	    }
	    state.nloop = 1;
	    break;
	  case 0x42: //WS
	    if (state.pop2 (top, top2))
		state.writeStorage (top2, top);
	    break;
	  case 0x43: //RS
	    if (state.pop (top))
		istack.push_back (state.readStorage (top));
	    break;
	  case 0x5D: //DELTAP1
	  case 0x71: //DELTAP2
	  case 0x72: //DELTAP3
	  case 0x73: //DELTAC1
	  case 0x74: //DELTAC2
	  case 0x75: //DELTAC3
	    if (state.pop (top) && static_cast<int> (istack.size ()) >= top*2) {
		for (int i=0; i<top; i++) {
		    istack.pop_back ();
		    istack.pop_back ();
		}
	    }
	    break;
	  case 0x20: //DUP
	    if (state.pop (top)) {
		istack.push_back (top);
		istack.push_back (top);
	    }
	    break;
	  case 0x23: //SWAP
	    if (state.pop2 (top, top2)) {
		istack.push_back (top);
		istack.push_back (top2);
	    }
	    break;
	  case 0x24: //DEPTH
	    istack.push_back (istack.size ());
	    break;
	  case 0x8a: //ROLL
	    if (istack.size () >= 3) {
		top = istack[istack.size () - 3];
		istack.erase (istack.end () - 3);
		istack.push_back (top);
	    } else {
		state.errorCode = TTFinstrs::Parse_StackExceeded;
	    }
	    break;
	  case 0x25: //CINDEX
	  case 0x26: //MINDEX
	    if (state.pop (top) && static_cast<int> (istack.size ()) >= top) {
		top2 = istack[istack.size () - top];
		if (d.base == 0x26)
		    istack.erase (istack.end () - top);
		istack.push_back (top2);
	    }
	    break;
	  case 0x3e: //MIAP
	    if (state.pop2 (top, top2)) {
		top = state.readCvt (top);
		if (state.zp[0] == 0) {
		    if (static_cast<size_t> (top2) >= state.twilightPts.size ()) {
			state.twilightPts.resize (top2 + 1);
		    }
		    state.twilightPts[top2].x = std::lround (top * state.projVector.x);
		    state.twilightPts[top2].y = std::lround (top * state.projVector.y);
		} else {
		    if (top2 == props.rBearingPointNum)
			props.rBearingTouched = true;
		}
		state.rp[0] = state.rp[1] = top2;
	    }
	    break;
	  case 0xe0: //MIRP
	  case 0x3a: //MSIRP
	    if (state.pop2 (top, top2)) {
		if (d.base == 0xe0) {
		    int16_t cvt_val = state.readCvt (top);
		    top = state.flip ? std::abs (cvt_val) : cvt_val;
		}
		if (state.zp[1] == 0) {
		    if (state.getPoint (state.rp[0], 0, ipt1)) {
			if (static_cast<size_t> (top2) >= state.twilightPts.size ()) {
			    state.twilightPts.resize (top2+1);
			}
			state.twilightPts[top2].x = ipt1.x + std::lround (top * state.projVector.x);
			state.twilightPts[top2].y = ipt2.y + std::lround (top * state.projVector.y);
		    }
		} else {
		    if (top2 == props.rBearingPointNum)
			props.rBearingTouched = true;
		}
		state.rp[1] = state.rp[0];
		state.rp[2] = top2;
		if (d.code & 16)
		    state.rp[0] = top2;
	    }
	    break;
	  case 0x2e: //MDAP
	    if (state.pop (top)) {
		if (state.zp[0] == 0) {
		    if (static_cast<size_t> (top) >= state.twilightPts.size ()) {
			state.twilightPts.resize (top+1);
		    }
		} else {
		    if (top == props.rBearingPointNum)
			props.rBearingTouched = true;
		}
		state.rp[0] = state.rp[1] = top;
	    }
	    break;
	  case 0xc0: //MDRP
	    if (state.pop (top)) {
		if (state.zp[1] == 0) {
		    if (static_cast<size_t> (top) >= state.twilightPts.size ()) {
			state.twilightPts.resize (top+1);
		    }
		} else {
		    if (top == props.rBearingPointNum)
			props.rBearingTouched = true;
		}
		if (d.code & 16)
		    state.rp[0] = top;
	    }
	    break;
	  case 0x2a: //LOOPCALL
	  case 0x2b: //CALL
	    if (state.pop (top)) {
		top2 = 1;
		if (d.base == 0x2a)
		    state.pop (top2);
		if (static_cast<size_t> (top) < props.fdefs.size ()) {
		    for (int i=0; i<top2 && !state.errorCode; i++) {
			quickExecute (props.fdefs[top], state, props, level+1);
		    }
		} else {
		    state.errorCode = TTFinstrs::Parse_WrongFunctionNumber;
		}
	    }
	    break;
	  case 0x89: //IDEF
	  case 0x2c: //FDEF
	    if (state.pop (top)) {
		// Dont't include the FDEF/IDEF operator itself
		startpos = pos;
		skipBranch (bytecode, pos, true, level);
		if (d.base == 0x89)
		    props.numIdefs++;
		else if (d.base == 0x2c) {
		    if (props.fdefs.size () < static_cast<size_t> (top+1))
			props.fdefs.resize (top+1);
		    std::copy (
			bytecode.begin ()+startpos, bytecode.begin ()+pos,
			std::back_inserter (props.fdefs[top])
		    );
		}
	    }
	    break;
	  // this may never be reached in the process of parsing fpgm itself, but only
	  // when called recursively on a previously saved function
	  case 0x2d: //ENDF
	    return 0;
	  case 0x50: //LT
	  case 0x51: //LTEQ
	  case 0x52: //GT
	  case 0x53: //GTEQ
	  case 0x54: //EQ
	  case 0x55: //NEQ
	    if (state.pop2 (top2, top)) {
		switch (d.base) {
		  case 0x50: //LT
		    istack.push_back (top < top2);
		    break;
		  case 0x51: //LTEQ
		    istack.push_back (top <= top2);
		    break;
		  case 0x52: //GT
		    istack.push_back (top > top2);
		    break;
		  case 0x53: //GTEQ
		    istack.push_back (top >= top2);
		    break;
		  case 0x54: //EQ
		    istack.push_back (top == top2);
		    break;
		  case 0x55: //NEQ
		    istack.push_back (top != top2);
		}
	    }
	    break;
	  case 0x58: //IF
	    if (state.pop (top)) {
		if (!top) {
		    skipBranch (bytecode, pos, false , level);
		}
	    }
	    break;
	  case 0x1b: //ELSE
	    // if we have reached this, then the previous branch has been executed
	    skipBranch (bytecode, pos, false, level);
	    break;
	  case 0x59: //EIF
	    // do nothing
	    break;
	  case 0x5A: //AND
	    if (state.pop2 (top2, top))
		istack.push_back (static_cast<bool> (top & top2));
	    break;
	  case 0x5B: //OR
	    if (state.pop2 (top2, top))
		istack.push_back (static_cast<bool> (top | top2));
	    break;
	  case 0x5C: //NOT
	    if (state.pop (top))
		istack.push_back (!static_cast<bool> (top));
	    break;
	  case 0x1C: //JMPR
	    if (state.pop (top))
		pos += (top-1);
	    break;
	  case 0x79: //JROF
	    if (state.pop2 (top, top2)) {
		if (!top) pos += (top2-1);
	    }
	    break;
	  case 0x78: //JROT
	    if (state.pop2 (top, top2)) {
		if (top) pos += (top2-1);
	    }
	    break;
	  case 0x0b: //SFVFS
	  case 0x0a: //SPVFS
	    if (state.pop2 (top, top2)) {
		if (d.code == 0xb0) {
		    state.freeVector.x = from_f26dot6 (top2);
		    state.freeVector.y = from_f26dot6 (top);
		} else {
		    state.projVector.x = from_f26dot6 (top2);
		    state.projVector.y = from_f26dot6 (top);
		}
	    }
	    break;
	  case 0x04: //SFVTCA
	    if (d.code & 1) {
		state.freeVector.x = 1;
		state.freeVector.y = 0;
	    } else {
		state.freeVector.x = 0;
		state.freeVector.y = 1;
	    }
	    break;
	  case 0x02: //SPVTCA
	    if (d.code&1) {
		state.projVector.x = 1;
		state.projVector.y = 0;
	    } else {
		state.projVector.x = 0;
		state.projVector.y = 1;
	    }
	    break;
	  case 0x08: //SFVTL
	  case 0x06: //SPVTL
	    if (state.pop2 (top, top2)) {
		if (state.getPoint (top, 2, ipt1) && state.getPoint (top2, 1, ipt2)) {
		    unit = getUnit (&ipt1, &ipt2, d.code & 1);
		    if (d.base == 0x08) {
			state.freeVector = unit;
		    } else {
			state.projVector = unit;
		    }
		}
	    }
	    break;
	  case 0x0c: //GPV
	    istack.push_back (to_f26dot6 (state.projVector.x));
	    istack.push_back (to_f26dot6 (state.projVector.y));
	    break;
	  case 0x0d: //GFV
	    istack.push_back (to_f26dot6 (state.freeVector.x));
	    istack.push_back (to_f26dot6 (state.freeVector.y));
	    break;
	  case 0x0e: //SFVTP
	    state.freeVector = state.projVector;
	    break;
	  case 0x00: //SVTCA
	    if (d.code & 1) {
		state.freeVector.x = state.projVector.x = 1;
		state.freeVector.y = state.projVector.y = 0;
	    } else {
		state.freeVector.x = state.projVector.x = 0;
		state.freeVector.y = state.projVector.y = 1;
	    }
	    break;
	  case 0x46: //GC
	    // currently we don't have any gridfitted outlines, so always use the original position
	    if (state.pop (top) && state.getPoint (top, 2, ipt1)) {
		top = std::lround ( (ipt1.x * state.projVector.x + ipt1.y * state.projVector.y) *
				    (static_cast<double> (state.size) / state.upm));
		istack.push_back  (top);
	    }
	    break;
	  case 0x48: //SCFS
	    if (state.pop2 (top, top2) && state.zp[2] == 0) {
		if (static_cast<size_t> (top2) >= state.twilightPts.size ()) {
		    state.twilightPts.resize (top2 + 1);
		}
		state.twilightPts[top2].x = std::lround (top * state.projVector.x);
		state.twilightPts[top2].y = std::lround (top * state.projVector.y);
	    }
	    break;
	  case 0x49: //MD
	    if (state.pop2 (top, top2))
		istack.push_back (64);
	    break;
	  case 0x4b: //MPPEM
	    istack.push_back (state.size);
	    break;
	  case 0x66: //FLOOR
	    if (state.pop (top))
		istack.push_back (to_f26dot6 (std::floor (from_f26dot6 (top))));
	    break;
	  case 0x67: //CEILING
	    if (state.pop (top))
		istack.push_back (to_f26dot6 (std::ceil (from_f26dot6 (top))));
	    break;
	  case 0x68: //ROUND
	    // round somewhow, ignoring the round state...
	    if (state.pop (top))
		istack.push_back (to_f26dot6 (std::lround (from_f26dot6 (top))));
	    break;
	  case 0x60: //ADD
	  case 0x61: //SUB
	  case 0x62: //DIV
	  case 0x63: //MUL
	  case 0x8b: //MAX
	  case 0x8c: //MIN
	    if (state.pop2 (top, top2)) {
		switch (d.base) {
		  case 0x60: //ADD
		    res = top + top2;
		    break;
		  case 0x61: //SUB
		    res = top2 - top;
		    break;
		  case 0x62: //DIV
		    res = (top2*64)/top;
		    break;
		  case 0x63: //MUL
		    res = (top*top2)/64;
		    break;
		  case 0x8b: //MAX
		    res = std::max (top, top2);
		    break;
		  case 0x8c: //MIN
		    res = std::min (top, top2);
		}
		istack.push_back (res);
	    }
	    break;
	  case 0x64: //ABS
	    if (state.pop (top))
		istack.push_back (std::abs (top));
	    break;
	  case 0x56: //ODD
	    if (state.pop (top)) {
		top = std::lround (from_f26dot6 (top));
		istack.push_back (top%2);
	    }
	    break;
	  case 0x57: //EVEN
	    if (state.pop (top)) {
		top = std::lround (from_f26dot6 (top));
		istack.push_back (!(top%2));
	    }
	    break;
	  case 0x45: //RCVT
	    if (state.pop (top))
		istack.push_back (state.readCvt (top));
	    break;
	  case 0x44: //WCVTP
	  case 0x70: //WCVTF
	    if (state.pop2 (top, top2)) {
		if (d.base == 0x70) {
		    top = std::lround (top * (static_cast<double> (state.size)*64 / state.upm));
		}
		state.writeCvt (top2, top);
	    }
	    break;
	  default:
	    ppdiff = InstrSet.at (d.base).nPops - InstrSet.at (d.base).nPushes;
	    if (ppdiff > 0) {
		for (int i=0; i<ppdiff && !state.errorCode; i++)
		    state.pop (top);
	    } else if (ppdiff < 0) {
		for (int i=0; i>ppdiff; i--)
		    istack.push_back (1);
		if (istack.size () > props.maxStackDepth)
		    props.maxStackDepth = istack.size ();
	    }
	}
        if (state.errorCode) {
	    state.errorPos = pos;
	    return 1;
	}
    }
    return 0;
}

void InstrEdit::reportError (GraphicsState &state, uint32_t table, uint16_t gid) {
    QString loc;
    if (table == CHR ('f','p','g','m'))
	loc = tr ("'fpgm' table");
    else if (table == CHR ('p','r','e','p'))
	loc = tr ("'prep' table");
    else
	loc = tr ("glyph %1 program").arg (gid);

    switch (state.errorCode) {
      case TTFinstrs::Parse_OK:
	break;
      case TTFinstrs::Parse_WrongZone:
        FontShepherd::postError (
	    tr ("Error parsing %1 (position %2): wrong zone number specified")
		.arg (loc).arg (state.errorPos)
	);
	break;
      case TTFinstrs::Parse_WrongPointNumber:
        FontShepherd::postError (
	    tr ("Error parsing %1 (position %2): wrong point number specified")
		.arg (loc).arg (state.errorPos)
	);
	break;
      case TTFinstrs::Parse_WrongTwilightPointNumber:
        FontShepherd::postError (
	    tr ("Error parsing %1 (position %2): a point in the twilight zone referenced, but not yet defined")
		.arg (loc).arg (state.errorPos)
	);
	break;
      case TTFinstrs::Parse_WrongFunctionNumber:
        FontShepherd::postError (
	    tr ("Error parsing %1 (position %2): wrong function number specified")
		.arg (loc).arg (state.errorPos)
	);
	break;
      case TTFinstrs::Parse_WrongCvtIndex:
        FontShepherd::postError (
	    tr ("Error parsing %1 (position %2): a CVT index requested exceeds the 'cvt' table size")
		.arg (loc).arg (state.errorPos)
	);
	break;
      case TTFinstrs::Parse_WrongStorageIndex:
        FontShepherd::postError (
	    tr ("Error parsing %1 (position %2): an attempt to read a storage location which has not yet been writted")
		.arg (loc).arg (state.errorPos)
	);
	break;
      case TTFinstrs::Parse_StackExceeded:
        FontShepherd::postError (
	    tr ("Error parsing %1 (position %2): stack capacity exceeded")
		.arg (loc).arg (state.errorPos)
	);
	break;
      case TTFinstrs::Parse_UnexpectedEnd:
        FontShepherd::postError (
	    tr ("Error parsing %1 (position %2): the instruction stream has ended unexpectedly")
		.arg (loc).arg (state.errorPos)
	);
	break;
    }
}
