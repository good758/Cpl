/*
* Common Purpose Library (http://github.com/ermig1979/Cpl).
*
* Copyright (c) 2021-2021 Yermalayeu Ihar.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/

#pragma once

#include "Cpl/Defs.h"
#include "Cpl/String.h"
#include "Cpl/Console.h"

#include <mutex>
#include <map>
#include <thread>

#if defined(CPL_LOG_ENABLE)
namespace Cpl
{
    class Log
    {
    public:
        enum Level
        {
            None = 0,
            Error,
            Warning,
            Info,
            Verbose,
            Debug,
        };

        enum Flags
        {
            WriteThreadId = 1 << 0,
            WritePrefix = 1 << 1,
            PrettyThreadId = 1 << 2,
            ColorezedPrefix = 1 << 3,
            DefaultFlags = WriteThreadId | WritePrefix | PrettyThreadId,
            BashFlags = WriteThreadId | WritePrefix | PrettyThreadId | ColorezedPrefix,
        };

        typedef void(*Callback)(const char* msg, void* userData);

        typedef void(*CallbackRaw)(Level level, const char* msg, void* userData);

        Log()
            : _levelMax(None)
            , _flags(DefaultFlags)
            , _rawOnly(true)
            , _writerId(0)
        {
        }

        int AddWriter(Level level, Callback callback, void* userData)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _writers[++_writerId] = Writer(level, callback, NULL, userData);
            _levelMax = std::max(_levelMax, level);
            _rawOnly = false;
            return _writerId;
        }

        int AddWriter(Level level, CallbackRaw callbackRaw, void* userData)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _writers[++_writerId] = Writer(level, NULL, callbackRaw, userData);
            _levelMax = std::max(_levelMax, level);
            return _writerId;
        }

        int AddStdWriter(Level level)
        {
            return AddWriter(level, StdWrite, NULL);
        }

        int AddFileWriter(Level level, const String& fileName)
        {
            _files.emplace_back(std::ofstream(fileName));
            if (_files.back().is_open())
                return AddWriter(level, FileWrite, &_files.back());
            else
                return 0;
        }

        bool RemoveWriter(int id)
        {
            if (_writers.find(id) != _writers.end())
            {
                _writers.erase(id);
                return true;
            }
            else
                return false;
        }

        void SetFlags(Flags flags)
        {
            _flags = flags;
        }

        CPL_INLINE bool Enable(Level level) const
        {
            return level != None && _levelMax >= level;
        }

        void Write(Level level, const String& message) const
        {
            if (!Enable(level))
                return;

            std::stringstream ss;

            if (!_rawOnly)
            {
                bool pref = false;
                if (_flags & WriteThreadId)
                {
                    std::thread::id id = std::this_thread::get_id();
                    if (_flags & PrettyThreadId)
                    {
                        std::lock_guard<std::mutex> lock(_mutex);
                        if (_prettyThreadNames.find(id) == _prettyThreadNames.end())
                            _prettyThreadNames[id] = ToStr((int)_prettyThreadNames.size(), 3);
                        ss << "[" << _prettyThreadNames[id] << "]";
                    }
                    else
                        ss << "[" << id << "]";
                    pref = true;
                }
                if (_flags & WritePrefix)
                {
                    if (pref)
                        ss << " ";
                    level = std::min(level, Debug);
                    static const String prefixes[] = { "None", "Error", "Warning", "Info", "Verbose", "Debug" };
                    if (_flags & ColorezedPrefix)
                    {
                        using namespace Console;
                        static Foreground colors[] = { ForegroundBlack, ForegroundLightRed, ForegroundYellow, ForegroundGreen, ForegroundWhite, ForegroundLightGray };
                        ss << Stylized(prefixes[level], FormatDefault, colors[level]);
                    }
                    else
                        ss << prefixes[level];
                }
                if (pref)
                    ss << ": ";

                ss << message;
                ss << std::endl;
            }

            std::lock_guard<std::mutex> lock(_mutex);
            for (Writers::const_iterator it = _writers.begin(); it != _writers.end(); ++it)
            {
                const Writer& writer = it->second;
                if (level <= writer.level)
                {
                    if (writer.callback)
                        writer.callback(ss.str().c_str(), writer.userData);
                    else if (writer.callbackRaw)
                        writer.callbackRaw(level, message.c_str(), writer.userData);
                    else
                        assert(0);
                }
            }
        }

        static Log& Global()
        {
            static Log log;
            return log;
        }

    private:
        struct Writer
        {
            Level level;
            Callback callback;
            CallbackRaw callbackRaw;
            void* userData;

            Writer(Level l = None, Callback c = NULL, CallbackRaw cr = NULL, void* ud = NULL)
                : level(l)
                , callback(c)
                , callbackRaw(cr)
                , userData(ud)
            {
            }
        };
        typedef std::map<int, Writer> Writers;
        Writers _writers;
        int _writerId;

        mutable std::mutex _mutex;
        mutable std::map<std::thread::id, String> _prettyThreadNames;
        mutable std::vector<std::ofstream> _files;
        Level _levelMax;
        Flags _flags;
        bool _rawOnly;

        static void StdWrite(const char* msg, void*)
        {
            std::cout << msg << std::flush;
        }

        static void FileWrite(const char* msg, void* userData)
        {
            std::ofstream& ofs = *(std::ofstream*)userData;
            if(ofs.is_open())
                ofs << msg << std::flush;
        }
    };
}

#define CPL_LOG(level, msg) \
    Cpl::Log::Global().Write(Cpl::Log::level, msg);

#define CPL_LOG_SS(level, msg) \
    { \
        std::stringstream __ss; \
        __ss << msg; \
        Cpl::Log::Global().Write(Cpl::Log::level, __ss.str()); \
    }

#define CPL_IF_LOG_SS(cond, level, msg) \
    if(cond) \
    { \
        std::stringstream __ss; \
        __ss << msg; \
        Cpl::Log::Global().Write(Cpl::Log::level, __ss.str()); \
    }

#else

#define CPL_LOG(level, msg)
#define CPL_LOG_SS(level, msg)
#define CPL_IF_LOG_SS(cond, level, msg)

#endif
