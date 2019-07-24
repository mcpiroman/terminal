// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "ConhostConnection.h"
#include "windows.h"
#include <sstream>

#include "ConhostConnection.g.cpp"

#include <conpty-universal.h>
#include "../../types/inc/Utils.hpp"
#include "../../types/inc/UTF8OutPipeReader.hpp"

using namespace ::Microsoft::Console;

namespace winrt::Microsoft::Terminal::TerminalConnection::implementation
{
    ConhostConnection::ConhostConnection(const hstring& commandline,
                                         const hstring& startingDirectory,
                                         const uint32_t initialRows,
                                         const uint32_t initialCols,
                                         const guid& initialGuid) :
        _initialRows{ initialRows },
        _initialCols{ initialCols },
        _commandline{ commandline },
        _startingDirectory{ startingDirectory },
        _guid{ initialGuid }
    {
        if (_guid == guid{})
        {
            _guid = Utils::CreateGuid();
        }
    }

    winrt::guid ConhostConnection::Guid() const noexcept
    {
        return _guid;
    }

    winrt::event_token ConhostConnection::TerminalOutput(Microsoft::Terminal::TerminalConnection::TerminalOutputEventArgs const& handler)
    {
        return _outputHandlers.add(handler);
    }

    void ConhostConnection::TerminalOutput(winrt::event_token const& token) noexcept
    {
        _outputHandlers.remove(token);
    }

    winrt::event_token ConhostConnection::TerminalDisconnected(Microsoft::Terminal::TerminalConnection::TerminalDisconnectedEventArgs const& handler)
    {
        return _disconnectHandlers.add(handler);
    }

    void ConhostConnection::TerminalDisconnected(winrt::event_token const& token) noexcept
    {
        _disconnectHandlers.remove(token);
    }

    bool ConhostConnection::Start()
    {
        std::wstring cmdline{ _commandline.c_str() };
        std::optional<std::wstring> startingDirectory;
        if (!_startingDirectory.empty())
        {
            startingDirectory = _startingDirectory;
        }

        EnvironmentVariableMapW extraEnvVars;
        {
            // Convert connection Guid to string and ignore the enclosing '{}'.
            std::wstring wsGuid{ Utils::GuidToString(_guid) };
            wsGuid.pop_back();

            const wchar_t* const pwszGuid{ wsGuid.data() + 1 };

            // Ensure every connection has the unique identifier in the environment.
            extraEnvVars.emplace(L"WT_SESSION", pwszGuid);
        }

        HANDLE processInfoPipe;
        THROW_IF_FAILED(
            CreateConPty(cmdline,
                         startingDirectory,
                         static_cast<short>(_initialCols),
                         static_cast<short>(_initialRows),
                         &_inPipe,
                         &_outPipe,
                         &_signalPipe,
                         &processInfoPipe,
                         &_piConhost,
                         CREATE_SUSPENDED,
                         extraEnvVars));

        _hJob.reset(CreateJobObjectW(nullptr, nullptr));
        THROW_LAST_ERROR_IF_NULL(_hJob);

        // We want the conhost and all associated descendant processes
        // to be terminated when the tab is closed. GUI applications
        // spawned from the shell tend to end up in their own jobs.
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobExtendedInformation{};
        jobExtendedInformation.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

        THROW_IF_WIN32_BOOL_FALSE(SetInformationJobObject(_hJob.get(),
                                                          JobObjectExtendedLimitInformation,
                                                          &jobExtendedInformation,
                                                          sizeof(jobExtendedInformation)));

        THROW_IF_WIN32_BOOL_FALSE(AssignProcessToJobObject(_hJob.get(), _piConhost.hProcess));

        // Create our own output handling thread
        // Each connection needs to make sure to drain the output from its backing host.
        _hOutputThread.reset(CreateThread(nullptr,
                                          0,
                                          StaticOutputThreadProc,
                                          this,
                                          0,
                                          nullptr));

        THROW_LAST_ERROR_IF_NULL(_hOutputThread);

        // Wind up the conhost! We only do this after we've got everything in place.
        THROW_LAST_ERROR_IF(-1 == ResumeThread(_piConhost.hThread));
        _connected = true;

        THROW_IF_WIN32_BOOL_FALSE(ConnectNamedPipe(processInfoPipe, nullptr));

        DWORD msg[2];
        THROW_IF_WIN32_BOOL_FALSE(ReadFile(processInfoPipe, &msg, sizeof(msg), nullptr, nullptr));

        THROW_IF_WIN32_BOOL_FALSE(DisconnectNamedPipe(processInfoPipe));

        _processStartupErrorCode = msg[0];
        bool connectionSuccess = _processStartupErrorCode == ERROR_SUCCESS;
        if (connectionSuccess)
        {
            DWORD processId = msg[1];
            _processHandle.reset(OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, processId));
        }

        return connectionSuccess;
    }

    void ConhostConnection::WriteInput(hstring const& data)
    {
        if (!_connected || _closing.load())
        {
            return;
        }

        // convert from UTF-16LE to UTF-8 as ConPty expects UTF-8
        std::string str = winrt::to_string(data);
        bool fSuccess = !!WriteFile(_inPipe.get(), str.c_str(), (DWORD)str.length(), nullptr, nullptr);
        fSuccess;
    }

    void ConhostConnection::Resize(uint32_t rows, uint32_t columns)
    {
        if (!_connected)
        {
            _initialRows = rows;
            _initialCols = columns;
        }
        else if (!_closing.load())
        {
            SignalResizeWindow(_signalPipe.get(), Utils::ClampToShortMax(columns, 1), Utils::ClampToShortMax(rows, 1));
        }
    }

    void ConhostConnection::Close()
    {
        if (!_connected)
        {
            return;
        }

        if (!_closing.exchange(true))
        {
            // It is imperative that the signal pipe be closed first; this triggers the
            // pseudoconsole host's teardown. See PtySignalInputThread.cpp.
            _signalPipe.reset();
            _inPipe.reset();
            _outPipe.reset();

            // Tear down our output thread -- now that the output pipe was closed on the
            // far side, we can run down our local reader.
            WaitForSingleObject(_hOutputThread.get(), INFINITE);
            _hOutputThread.reset();

            // Wait for conhost to terminate.
            WaitForSingleObject(_piConhost.hProcess, INFINITE);

            _hJob.reset(); // This is a formality.
            _piConhost.reset();
        }
    }

    hstring ConhostConnection::GetConnectionFailatureMessage()
    {
        wil::unique_hlocal_ptr<WCHAR[]> errorMsgBuf;
        size_t errorMsgSize = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            0,
            _processStartupErrorCode,
            0,
            (LPWSTR)&errorMsgBuf,
            0,
            NULL);
        std::wstring_view errorMsg(errorMsgBuf.get(), errorMsgSize - 2); // Remove last "\r\n"

        std::wostringstream ss;
        ss << L"[Failed to start process]\r\n";
        ss << L"  Error code \x1B[37m" << _processStartupErrorCode << L"\x1B[0m: \x1B[37m" << errorMsg << L"\x1B[0m\r\n";
        ss << L"  Command line: \x1B[37m" << std::wstring{ _commandline } << L"\x1B[0m";
        return hstring(ss.str());
    }

    hstring ConhostConnection::GetConnectionFailatureTabTitle()
    {
        return _commandline;
    }

    hstring ConhostConnection::GetDisconnectionMessage()
    {
        std::wostringstream ss;
        ss << L"[Process exited with code \x1B[37m" << _processExitCode << L"\x1B[0m]";
        return hstring(ss.str());
    }

    hstring ConhostConnection::GetDisconnectionTabTitle(hstring previousTitle)
    {
        std::wostringstream ss;
        ss << L"[" << _processExitCode << L"] " << std::wstring{ previousTitle };
        return hstring(ss.str());
    }

    DWORD WINAPI ConhostConnection::StaticOutputThreadProc(LPVOID lpParameter)
    {
        ConhostConnection* const pInstance = (ConhostConnection*)lpParameter;
        return pInstance->_OutputThread();
    }

    DWORD ConhostConnection::_OutputThread()
    {
        UTF8OutPipeReader pipeReader{ _outPipe.get() };
        std::string_view strView{};

        // process the data of the output pipe in a loop
        while (true)
        {
            HRESULT result = pipeReader.Read(strView);
            if (FAILED(result) || result == S_FALSE)
            {
                if (_closing.load())
                {
                    // This is okay, break out to kill the thread
                    return 0;
                }

                if (_processHandle)
                {
                    // Wait for application process to terminate.
                    WaitForSingleObject(_processHandle.get(), INFINITE);
                    THROW_IF_WIN32_BOOL_FALSE(GetExitCodeProcess(_processHandle.get(), &_processExitCode));
                    _processHandle.reset();
                }

                _disconnectHandlers();
                return (DWORD)-1;
            }

            if (strView.empty())
            {
                return 0;
            }

            // Convert buffer to hstring
            auto hstr{ winrt::to_hstring(strView) };

            // Pass the output to our registered event handlers
            _outputHandlers(hstr);
        }

        return 0;
    }
}
