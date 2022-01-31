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

#include <stdexcept>

using namespace std;
using std::runtime_error;

class FileAccessException : public std::runtime_error {
public:
    FileAccessException (const string &f, const string m = "Error: can't access file!"):
        runtime_error (m), _filename (f) , _message (m) {}
    ~FileAccessException () throw () {}

    const char* what () {
        return _message.c_str ();
    }

    const char* fileName () {
        return _filename.c_str ();
    }

protected:
   string _filename;
   string _message;
};

class TableDataCorruptException : public runtime_error {
public:
    TableDataCorruptException (const string &t, const string m = "Error: table data corrupted!"):
        runtime_error (m), _table (t) , _message (m){}
    ~TableDataCorruptException () throw () {};

    const char* what () {
        return _message.c_str ();
    }

    const char* table () {
        return _table.c_str ();
    }

protected:
   string _table;
   string _message;
};

class TableDataCompileException : public runtime_error {
public:
    TableDataCompileException (const string &t, const string m = "Error: cannot compile table!"):
        runtime_error (m), _table (t) , _message (m) {}
    ~TableDataCompileException () throw () {};

    const char* what () {
        return _message.c_str ();
    }

    const char* table () {
        return _table.c_str ();
    }

protected:
   string _table;
   string _message;
};

class FileNotFoundException : public FileAccessException {
public:
    FileNotFoundException (const string &f, const string m = "Error: could not find file!"):
        FileAccessException (f, m) {}
};

class CantBackupException : public FileAccessException {
public:
    CantBackupException (const string &f, const string m = "Error: can't backup file!"):
        FileAccessException (f, m) {}
};

class CantRestoreException : public FileAccessException {
public:
    CantRestoreException (const string &f, const string m = "Error: can't restore from backup!"):
        FileAccessException (f, m) {}
};

class FileDamagedException : public FileAccessException {
public:
    FileDamagedException (const string &f, const string m = "Error: the file is damaged."):
        FileAccessException (f, m) {}
};

class FileDuplicateException : public FileAccessException {
public:
    FileDuplicateException (const string &f, const string m = "Error: can't import the same file twice."):
        FileAccessException (f, m) {}
};

class FileLoadCanceledException : public FileAccessException {
public:
    FileLoadCanceledException (const string &f, const string m = "Error: loading file has been canceled."):
        FileAccessException (f, m) {}
};

