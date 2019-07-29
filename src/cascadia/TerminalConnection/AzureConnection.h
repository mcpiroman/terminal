// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "AzureConnection.g.h"

#include <cpprest/http_client.h>
#include <cpprest/http_listener.h>
#include <cpprest/ws_client.h>
#include <mutex>
#include <condition_variable>

// FIXME: idk how to include this form cppwinrt_utils.h
#define DECLARE_EVENT(name, eventHandler, args)          \
public:                                                  \
    winrt::event_token name(args const& handler);        \
    void name(winrt::event_token const& token) noexcept; \
                                                         \
private:                                                 \
    winrt::event<args> eventHandler;

namespace winrt::Microsoft::Terminal::TerminalConnection::implementation
{
    struct AzureConnection : AzureConnectionT<AzureConnection>
    {
        AzureConnection(const uint32_t rows, const uint32_t cols);

        void Start();
        void WriteInput(hstring const& data);
        void Resize(uint32_t rows, uint32_t columns);
        void Close();

        bool AllowsUserInput();
        VisualConnectionState VisualConnectionState();
        hstring GetTabTitle(hstring terminalTitle);

        DECLARE_EVENT(TerminalOutput, _outputHandlers, TerminalConnection::TerminalOutputEventArgs);
        DECLARE_EVENT(TerminalDisconnected, _disconnectHandlers, TerminalConnection::TerminalDisconnectedEventArgs);
        DECLARE_EVENT(StateChanged, _stateChangedHandlers, TerminalConnection::StateChangedEventArgs);

    private:
        uint32_t _initialRows{};
        uint32_t _initialCols{};
        int _storedNumber{ -1 };
        int _maxStored;
        int _tenantNumber{ -1 };
        int _maxSize;
        std::condition_variable _canProceed;
        std::mutex _commonMutex;

        enum class State
        {
            AccessStored,
            DeviceFlow,
            TenantChoice,
            StoreTokens,
            TermConnecting,
            TermConnected,
            NoConnect
        };

        State _state{ State::AccessStored };

        std::optional<bool> _store;
        std::optional<bool> _removeOrNew;

        bool _open{};
        std::atomic<bool> _closing{ false };
        bool _allowsUserInput{ false };
        TerminalConnection::VisualConnectionState _visualConnectionState{ VisualConnectionState::NotConnected };

        wil::unique_handle _hOutputThread;

        static DWORD WINAPI StaticOutputThreadProc(LPVOID lpParameter);
        DWORD _OutputThread();
        HRESULT _AccessHelper();
        HRESULT _DeviceFlowHelper();
        HRESULT _TenantChoiceHelper();
        HRESULT _StoreHelper();
        HRESULT _ConnectHelper();

        const utility::string_t _loginUri{ U("https://login.microsoftonline.com/") };
        const utility::string_t _resourceUri{ U("https://management.azure.com/") };
        const utility::string_t _wantedResource{ U("https://management.core.windows.net/") };
        int _expireLimit{ 2700 };
        web::json::value _tenantList;
        utility::string_t _displayName;
        utility::string_t _tenantID;
        utility::string_t _accessToken;
        utility::string_t _refreshToken;
        int _expiry;
        utility::string_t _cloudShellUri;
        utility::string_t _terminalID;

        web::json::value _RequestHelper(web::http::client::http_client theClient, web::http::http_request theRequest);
        web::json::value _GetDeviceCode();
        web::json::value _WaitForUser(utility::string_t deviceCode, int pollInterval, int expiresIn);
        web::json::value _GetTenants();
        web::json::value _RefreshTokens();
        web::json::value _GetCloudShellUserSettings();
        utility::string_t _GetCloudShell();
        utility::string_t _GetTerminal(utility::string_t shellType);
        void _HeaderHelper(web::http::http_request theRequest);
        void _StoreCredential();
        void _RemoveCredentials();
        std::wstring _StrFormatHelper(const wchar_t* const format, int i, const wchar_t* name, const wchar_t* ID);

        web::websockets::client::websocket_client _cloudShellSocket;
    };
}

namespace winrt::Microsoft::Terminal::TerminalConnection::factory_implementation
{
    struct AzureConnection : AzureConnectionT<AzureConnection, implementation::AzureConnection>
    {
    };
}
