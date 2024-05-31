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
#include <unicode/uchar.h>

#include "sfnt.h"
#include "editors/instredit.h" // also includes tables.h
#include "tables/instr.h" // also includes tables.h

#include "fs_notify.h"

InstrTableEdit::InstrTableEdit (FontTable* tbl, sFont* font, QWidget *parent) :
    TableEdit (parent, Qt::Window), m_font (font), m_tbl (tbl) {

    setAttribute (Qt::WA_DeleteOnClose);
    setWindowTitle (QString ("%1 - %2").arg
	(QString::fromStdString (m_tbl->stringName ())).arg (m_font->fontname));

    InstrTable *ftbl = dynamic_cast<InstrTable *> (m_tbl);
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
    return m_tbl->modified ();
}

bool InstrTableEdit::isValid () {
    return m_valid;
}

FontTable* InstrTableEdit::table () {
    return m_tbl;
}

void InstrTableEdit::closeEvent (QCloseEvent *event) {
    // If we are going to delete the font, ignore changes in table edits
    if (!isModified () || checkUpdate (true))
        m_tbl->clearEditor ();
    else
        event->ignore ();
}

void InstrTableEdit::save () {
    if (m_instrEdit->changed ()) {
	InstrTable *ftbl = dynamic_cast<InstrTable *> (m_tbl);
	ftbl->setData (m_instrEdit->data ());
	emit (update (m_tbl));
    }
    close ();
}

std::map<std::string, uint8_t> InstrEdit::ByInstr = {};

const std::map<uint8_t, instr_def> InstrEdit::InstrSet = {
    { 0x00, { "SVTCA", 0x00, 0x01,
    	  "Set freedom & projection Vectors To Coordinate Axis[a]\n 0=>both to y axis\n 1=>both to x axis" }},
    { 0x02, { "SPVTCA", 0x02, 0x03,
    	  "Set Projection Vector To Coordinate Axis[a]\n 0=>y axis\n 1=>x axis" }},
    { 0x04, { "SFVTCA", 0x04, 0x05,
    	  "Set Freedom Vector To Coordinate Axis[a]\n 0=>y axis\n 1=>x axis" }},
    { 0x06, { "SPVTL", 0x06, 0x07,
    	  "Set Projection Vector To Line[a]\n 0 => parallel to line\n 1=>orthogonal to line\nPops two points used to establish the line\nSets the projection vector" }},
    { 0x08, { "SFVTL", 0x08, 0x09,
    	  "Set Fredom Vector To Line[a]\n 0 => parallel to line\n 1=>orthogonal to line\nPops two points used to establish the line\nSets the freedom vector" }},
    { 0x0a, { "SPVFS", 0x0a, 0x0a,
    	  "Set Projection Vector From Stack\npops 2 2.14 values (x,y) from stack\nmust be a unit vector" }},
    { 0x0b, { "SFVFS", 0x0b, 0x0b,
    	  "Set Freedom Vector From Stack\npops 2 2.14 values (x,y) from stack\nmust be a unit vector" }},
    { 0x0c, { "GPV", 0x0c, 0x0c,
    	  "Get Projection Vector\nDecomposes projection vector, pushes its\ntwo coordinates onto stack as 2.14" }},
    { 0x0d, { "GFV", 0x0d, 0x0d,
    	  "Get Freedom Vector\nDecomposes freedom vector, pushes its\ntwo coordinates onto stack as 2.14" }},
    { 0x0e, { "SFVTPV", 0x0e, 0x0e,
    	  "Set Freedom Vector To Projection Vector" }},
    { 0x0f, { "ISECT", 0x0f, 0x0f,
    	  "moves point to InterSECTion of two lines\nPops start,end start,end points of two lines\nand a point to move. Point is moved to\nintersection" }},
    { 0x10, { "SRP0", 0x10, 0x10,
    	  "Set Reference Point 0\nPops a point which becomes the new rp0" }},
    { 0x11, { "SRP1", 0x11, 0x11,
    	  "Set Reference Point 1\nPops a point which becomes the new rp1" }},
    { 0x12, { "SRP2", 0x12, 0x12,
    	  "Set Reference Point 2\nPops a point which becomes the new rp2" }},
    { 0x13, { "SZP0", 0x13, 0x13,
    	  "Set Zone Pointer 0\nPops the zone number into zp0" }},
    { 0x14, { "SZP1", 0x14, 0x14,
    	  "Set Zone Pointer 1\nPops the zone number into zp1" }},
    { 0x15, { "SZP2", 0x15, 0x15,
    	  "Set Zone Pointer 2\nPops the zone number into zp2" }},
    { 0x16, { "SZPS", 0x16, 0x16,
    	  "Set Zone PointerS\nPops the zone number into zp0,zp1 and zp2" }},
    { 0x17, { "SLOOP", 0x17, 0x17,
    	  "Set LOOP variable\nPops the new value for the loop counter\nDefaults to 1 after each use" }},
    { 0x18, { "RTG", 0x18, 0x18,
    	  "Round To Grid\nSets the round state" }},
    { 0x19, { "RTHG", 0x19, 0x19,
    	  "Round To Half Grid\nSets the round state (round to closest .5 not int)" }},
    { 0x1a, { "SMD", 0x1a, 0x1a,
    	  "Set Minimum Distance\nPops a 26.6 value from stack to be new minimum distance" }},
    { 0x1b, { "ELSE", 0x1b, 0x1b,
    	  "ELSE clause\nStart of Else clause of preceding IF" }},
    { 0x1c, { "JMPR", 0x1c, 0x1c,
    	  "JuMP Relative\nPops offset (in bytes) to move the instruction pointer" }},
    { 0x1d, { "SCVTCI", 0x1d, 0x1d,
    	  "Sets Control Value Table Cut-In\nPops 26.6 from stack, sets cvt cutin" }},
    { 0x1e, { "SSWCI", 0x1e, 0x1e,
    	  "Set Single Width Cut-In\nPops value for single width cut-in value (26.6)" }},
    { 0x1f, { "SSW", 0x1f, 0x1f,
    	  "Set Single Width\nPops value for single width value (FUnit)" }},
    { 0x20, { "DUP", 0x20, 0x20,
    	  "DUPlicate top stack element\nPushes the top stack element again" }},
    { 0x21, { "POP", 0x21, 0x21,
    	  "POP top stack element" }},
    { 0x22, { "CLEAR", 0x22, 0x22,
    	  "CLEAR\nPops all elements on stack" }},
    { 0x23, { "SWAP", 0x23, 0x23,
    	  "SWAP top two elements on stack" }},
    { 0x24, { "DEPTH", 0x24, 0x24,
    	  "DEPTH of stack\nPushes the number of elements on the stack" }},
    { 0x25, { "CINDEX", 0x25, 0x25,
    	  "Copy INDEXed element to stack\nPops an index & copies stack\nelement[index] to top of stack" }},
    { 0x26, { "MINDEX", 0x26, 0x26,
    	  "Move INDEXed element to stack\nPops an index & moves stack\nelement[index] to top of stack\n(removing it from where it was)" }},
    { 0x27, { "ALIGNPTS", 0x27, 0x27,
    	  "ALIGN PoinTS\nAligns (&pops) the two points which are on the stack\nby moving along freedom vector to the average of their\npositions on projection vector" }},
    { 0x29, { "UTP", 0x29, 0x29,
    	  "UnTouch Point\nPops a point number and marks it untouched" }},
    { 0x2a, { "LOOPCALL", 0x2a, 0x2a,
    	  "LOOP and CALL function\nPops a function number & count\nCalls function count times" }},
    { 0x2b, { "CALL", 0x2b, 0x2b,
    	  "CALL function\nPops a value, calls the function represented by it" }},
    { 0x2c, { "FDEF", 0x2c, 0x2c,
    	  "Function DEFinition\nPops a value (n) and starts the nth\nfunction definition" }},
    { 0x2d, { "ENDF", 0x2d, 0x2d,
    	  "END Function definition" }},
    { 0x2e, { "MDAP", 0x2e, 0x2f,
    	  "Move Direct Absolute Point[a]\n 0=>do not round\n 1=>round\nPops a point number, touches that point\nand perhaps rounds it to the grid along\nthe projection vector. Sets rp0&rp1 to the point" }},
    { 0x30, { "IUP", 0x30, 0x31,
    	  "Interpolate Untouched Points[a]\n 0=> interpolate in y direction\n 1=> x direction" }},
    { 0x32, { "SHP", 0x32, 0x33,
    	  "SHift Point using reference point[a]\n 0=>uses rp2 in zp1\n 1=>uses rp1 in zp0\nPops as many points as specified by the loop count\nShifts each by the amount the reference\npoint was shifted" }},
    { 0x34, { "SHC", 0x34, 0x35,
    	  "SHift Contour using reference point[a]\n 0=>uses rp2 in zp1\n 1=>uses rp1 in zp0\nPops number of contour to be shifted\nShifts the entire contour by the amount\nreference point was shifted" }},
    { 0x36, { "SHZ", 0x36, 0x37,
    	  "SHift Zone using reference point[a]\n 0=>uses rp2 in zp1\n 1=>uses rp1 in zp0\nPops the zone to be shifted\nShifts all points in zone by the amount\nthe reference point was shifted" }},
    { 0x38, { "SHPIX", 0x38, 0x38,
    	  "SHift point by a PIXel amount\nPops an amount (26.6) and as many points\nas the loop counter specifies\neach point is shifted along the FREEDOM vector" }},
    { 0x39, { "IP", 0x39, 0x39,
    	  "Interpolate Point\nPops as many points as specified in loop counter\nInterpolates each point to preserve original status\nwith respect to RP1 and RP2" }},
    { 0x3a, { "MSIRP", 0x3a, 0x3b,
    	  "Move Stack Indirect Relative Point[a]\n 0=>do not set rp0\n 1=>set rp0 to point\nPops a 26.6 distance and a point\nMoves point so it is distance from rp0" }},
    { 0x3c, { "ALIGNRP", 0x3c, 0x3c,
    	  "ALIGN to Reference Point\nPops as many points as specified in loop counter\nAligns points with RP0 by moving each\nalong freedom vector until distance to\nRP0 on projection vector is 0" }},
    { 0x3d, { "RTDG", 0x3d, 0x3d,
    	  "Round To Double Grid\nSets the round state (round to closest .5/int)" }},
    { 0x3e, { "MIAP", 0x3e, 0x3f,
    	  "Move Indirect Absolute Point[a]\n 0=>do not round, don't use cvt cutin\n 1=>round\nPops a point number & a cvt entry,\ntouches the point and moves it to the coord\nspecified in the cvt (along the projection vector).\nSets rp0&rp1 to the point" }},
    { 0x40, { "NPUSHB", 0x40, 0x40,
    	  "N PUSH Bytes\nReads an (unsigned) count byte from the\ninstruction stream, then reads and pushes\nthat many unsigned bytes" }},
    { 0x41, { "NPUSHW", 0x41, 0x41,
    	  "N PUSH Words\nReads an (unsigned) count byte from the\ninstruction stream, then reads and pushes\nthat many signed 2byte words" }},
    { 0x42, { "WS", 0x42, 0x42,
    	  "Write Store\nPops a value and an index and writes the value to storage[index]" }},
    { 0x43, { "RS", 0x43, 0x43,
    	  "Read Store\nPops an index into store array\nPushes value at that index" }},
    { 0x44, { "WCVTP", 0x44, 0x44,
    	  "Write Control Value Table in Pixel units\nPops a number(26.6) and a\nCVT index and writes the number to cvt[index]" }},
    { 0x45, { "RCVT", 0x45, 0x45,
    	  "Read Control Value Table entry\nPops an index to the CVT and\npushes it in 26.6 format" }},
    { 0x46, { "GC", 0x46, 0x47,
    	  "Get Coordinate[a] projected onto projection vector\n 0=>use current pos\n 1=>use original pos\nPops one point, pushes the coordinate of\nthe point along projection vector" }},
    { 0x48, { "SCFS", 0x48, 0x48,
    	  "Sets Coordinate From Stack using projection & freedom vectors\nPops a coordinate 26.6 and a point\nMoves point to given coordinate" }},
    { 0x49, { "MD", 0x49, 0x4a,
    	  "Measure Distance[a]\n 0=>distance with current positions\n 1=>distance with original positions\nPops two point numbers, pushes distance between them" }},
    { 0x4b, { "MPPEM", 0x4b, 0x4b,
    	  "Measure Pixels Per EM\nPushs the pixels per em (for current rasterization)" }},
    { 0x4c, { "MPS", 0x4c, 0x4c,
    	  "Measure Point Size\nPushes the current point size" }},
    { 0x4d, { "FLIPON", 0x4d, 0x4d,
    	  "set the auto FLIP boolean to ON" }},
    { 0x4e, { "FLIPOFF", 0x4e, 0x4e,
    	  "set the auto FLIP boolean to OFF" }},
    { 0x4f, { "DEBUG", 0x4f, 0x4f,
    	  "DEBUG call\nPops a value and executes a debugging interpreter\n(if available)" }},
    { 0x50, { "LT", 0x50, 0x50,
    	  "Less Than\nPops two values, pushes (0/1) if bottom el < top" }},
    { 0x51, { "LTEQ", 0x51, 0x51,
    	  "Less Than or EQual\nPops two values, pushes (0/1) if bottom el <= top" }},
    { 0x52, { "GT", 0x52, 0x52,
    	  "Greater Than\nPops two values, pushes (0/1) if bottom el > top" }},
    { 0x53, { "GTEQ", 0x53, 0x53,
    	  "Greater Than or EQual\nPops two values, pushes (0/1) if bottom el >= top" }},
    { 0x54, { "EQ", 0x54, 0x54,
    	  "EQual\nPops two values, tests for equality, pushes result(0/1)" }},
    { 0x55, { "NEQ", 0x55, 0x55,
    	  "Not EQual\nPops two values, tests for inequality, pushes result(0/1)" }},
    { 0x56, { "ODD", 0x56, 0x56,
    	  "ODD\nPops one value, rounds it and tests if it is odd(0/1)" }},
    { 0x57, { "EVEN", 0x57, 0x57,
    	  "EVEN\nPops one value, rounds it and tests if it is even(0/1)" }},
    { 0x58, { "IF", 0x58, 0x58,
    	  "IF test\nPops an integer,\nif 0 (false) next instruction is ELSE or EIF\nif non-0 execution continues normally\n(unless there's an ELSE)" }},
    { 0x59, { "EIF", 0x59, 0x59,
    	  "End IF\nEnds and IF or IF-ELSE sequence" }},
    { 0x5a, { "AND", 0x5a, 0x5a,
    	  "logical AND\nPops two values, ands them, pushes result" }},
    { 0x5b, { "OR", 0x5b, 0x5b,
    	  "logical OR\nPops two values, ors them, pushes result" }},
    { 0x5c, { "NOT", 0x5c, 0x5c,
    	  "logical NOT\nPops a number, if 0 pushes 1, else pushes 0" }},
    { 0x5d, { "DELTAP1", 0x5d, 0x5d,
    	  "DELTA exception P1\nPops a value n & then n exception specifications & points\nmoves each point at a given size by the amount" }},
    { 0x5e, { "SDB", 0x5e, 0x5e,
    	  "Set Delta Base\nPops value sets delta base" }},
    { 0x5f, { "SDS", 0x5f, 0x5f,
    	  "Set Delta Shift\nPops a new value for delta shift" }},
    { 0x60, { "ADD", 0x60, 0x60,
    	  "ADD\nPops two 26.6 fixed numbers from stack\nadds them, pushes result" }},
    { 0x61, { "SUB", 0x61, 0x61,
    	  "SUBtract\nPops two 26.6 fixed numbers from stack\nsubtracts them, pushes result" }},
    { 0x62, { "DIV", 0x62, 0x62,
    	  "DIVide\nPops two 26.6 numbers, divides them, pushes result" }},
    { 0x63, { "MUL", 0x63, 0x63,
    	  "MULtiply\nPops two 26.6 numbers, multiplies them, pushes result" }},
    { 0x64, { "ABS", 0x64, 0x64,
    	  "ABSolute Value\nReplaces top of stack with its abs" }},
    { 0x65, { "NEG", 0x65, 0x65,
    	  "NEGate\nNegates the top of the stack" }},
    { 0x66, { "FLOOR", 0x66, 0x66,
    	  "FLOOR\nPops a value, rounds to lowest int, pushes result" }},
    { 0x67, { "CEILING", 0x67, 0x67,
    	  "CEILING\nPops one 26.6 value, rounds upward to an int\npushes result" }},
    { 0x68, { "ROUND", 0x68, 0x6b,
    	  "ROUND value[ab]\n ab=0 => grey distance\n ab=1 => black distance\n ab=2 => white distance\nRounds a coordinate (26.6) at top of stack\nand compensates for engine effects" }},
    { 0x6c, { "NROUND", 0x6c, 0x6f,
    	  "No ROUNDing of value[ab]\n ab=0 => grey distance\n ab=1 => black distance\n ab=2 => white distance\nPops a coordinate (26.6), changes it (without\nrounding) to compensate for engine effects\npushes it back" }},
    { 0x70, { "WCVTF", 0x70, 0x70,
    	  "Write Control Value Table in Funits\nPops a number(Funits) and a\nCVT index and writes the number to cvt[index]" }},
    { 0x71, { "DELTAP2", 0x71, 0x71,
    	  "DELTA exception P2\nPops a value n & then n exception specifications & points\nmoves each point at a given size by the amount" }},
    { 0x72, { "DELTAP3", 0x72, 0x72,
    	  "DELTA exception P3\nPops a value n & then n exception specifications & points\nmoves each point at a given size by the amount" }},
    { 0x73, { "DELTAC1", 0x73, 0x73,
    	  "DELTA exception C1\nPops a value n & then n exception specifications & cvt entries\nchanges each cvt entry at a given size by the pixel amount" }},
    { 0x74, { "DELTAC2", 0x74, 0x74,
    	  "DELTA exception C2\nPops a value n & then n exception specifications & cvt entries\nchanges each cvt entry at a given size by the pixel amount" }},
    { 0x75, { "DELTAC3", 0x75, 0x75,
    	  "DELTA exception C3\nPops a value n & then n exception specifications & cvt entries\nchanges each cvt entry at a given size by the pixel amount" }},
    { 0x76, { "SROUND", 0x76, 0x76,
    	  "Super ROUND\nToo complicated. Look it up" }},
    { 0x77, { "S45ROUND", 0x77, 0x77,
    	  "Super 45\260 ROUND\nToo complicated. Look it up" }},
    { 0x78, { "JROT", 0x78, 0x78,
    	  "Jump Relative On True\nPops a boolean and an offset\nChanges instruction pointer by offset bytes\nif boolean is true" }},
    { 0x79, { "JROF", 0x79, 0x79,
    	  "Jump Relative On False\nPops a boolean and an offset\nChanges instruction pointer by offset bytes\nif boolean is false" }},
    { 0x7a, { "ROFF", 0x7a, 0x7a,
    	  "Round OFF\nSets round state so that no rounding occurs\nbut engine compensation does" }},
    { 0x7c, { "RUTG", 0x7c, 0x7c,
    	  "Round Up To Grid\nSets the round state" }},
    { 0x7d, { "RDTG", 0x7d, 0x7d,
    	  "Round Down To Grid\n\nSets round state to the obvious" }},
    { 0x7e, { "SANGW", 0x7e, 0x7e,
    	  "Set ANGle Weight\nPops an int, and sets the angle\nweight state variable to it\nObsolete" }},
    { 0x7f, { "AA", 0x7f, 0x7f,
    	  "Adjust Angle\nObsolete instruction\nPops one value" }},
    { 0x80, { "FLIPPT", 0x80, 0x80,
    	  "FLIP PoinT\nPops as many points as specified in loop counter\nFlips whether each point is on/off curve" }},
    { 0x81, { "FLIPRGON", 0x81, 0x81,
    	  "FLIP RanGe ON\nPops two point numbers\nsets all points between to be on curve points" }},
    { 0x82, { "FLIPRGOFF", 0x82, 0x82,
    	  "FLIP RanGe OFF\nPops two point numbers\nsets all points between to be off curve points" }},
    { 0x85, { "SCANCTRL", 0x85, 0x85,
    	  "SCAN conversion ConTRoL\nPops a number which sets the\ndropout control mode" }},
    { 0x86, { "SDPVTL", 0x86, 0x87,
    	  "Set Dual Projection Vector To Line[a]\n 0 => parallel to line\n 1=>orthogonal to line\nPops two points used to establish the line\nSets a second projection vector based on original\npositions of points" }},
    { 0x88, { "GETINFO", 0x88, 0x88,
    	  "GET INFOrmation\nPops information type, pushes result" }},
    { 0x89, { "IDEF", 0x89, 0x89,
    	  "Instruction DEFinition\nPops a value which becomes the opcode\nand begins definition of new instruction" }},
    { 0x8a, { "ROLL", 0x8a, 0x8a,
    	  "ROLL the top three stack elements" }},
    { 0x8b, { "MAX", 0x8b, 0x8b,
    	  "MAXimum of top two stack entries\nPops two values, pushes the maximum back" }},
    { 0x8c, { "MIN", 0x8c, 0x8c,
    	  "Minimum of top two stack entries\nPops two values, pushes the minimum back" }},
    { 0x8d, { "SCANTYPE", 0x8d, 0x8d,
    	  "SCANTYPE\nPops number which sets which scan\nconversion rules to use" }},
    { 0x8e, { "INSTCTRL", 0x8e, 0x8e,
    	  "INSTRuction execution ConTRoL\nPops a selector and value\nSets a state variable" }},
    { 0xb0, { "PUSHB", 0xb0, 0xb7,
    	  "PUSH Byte[abc]\n abc is the number-1 of bytes to push\nReads abc+1 unsigned bytes from\nthe instruction stream and pushes them" }},
    { 0xb8, { "PUSHW", 0xb8, 0xbf,
    	  "PUSH Word[abc]\n abc is the number-1 of words to push\nReads abc+1 signed words from\nthe instruction stream and pushes them" }},
    { 0xc0, { "MDRP", 0xc0, 0xdf,
    	  "Move Direct Relative Point[abcde]\n a=0=>don't set rp0\n a=1=>set rp0 to p\n b=0=>do not keep distance more than minimum\n b=1=>keep distance at least minimum\n c=0 do not round\n c=1 round\n de=0 => grey distance\n de=1 => black distance\n de=2 => white distance\nPops a point moves it so that it maintains\nits original distance to the rp0. Sets\nrp1 to rp0, rp2 to point, sometimes rp0 to point" }},
    { 0xe0, { "MIRP", 0xe0, 0xff,
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
    ss << "Invalid code: 0x" << std::hex << code;

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
    // MDRP, too wide range of possible values
    } else if (code >= 0x8f && code <= 0xaf) {
	return invalidCode (code);
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
	    //std::cerr << "line " << instr_lst.size () << " got instr: " << instr << " nums needed " << nums_needed << std::endl;
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
	    //std::cerr << "got num: " << str_code << " now needed " << nums_needed << std::endl;
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

