// SPDX-License-Identifier: Apache-2.0
/*
 *  If not stated otherwise in this file or this component's LICENSE
 *  file the following copyright and licenses apply:
 *
 *  Copyright 2024 Comcast Cable Communications Management, LLC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

// Include order matters
// clang-format off
#include "Module.h"
// clang-format on

#include <interfaces/ITextTrack.h>
#include <interfaces/json/JTextTrack.h>
#include <interfaces/json/JTextTrackClosedCaptionsStyle.h>
#if ITEXTTRACK_VERSION >= 2
#include <interfaces/json/JTextTrackTtmlStyle.h>
#endif

namespace WPEFramework {
namespace Plugin {
using namespace JsonData::TextTrack;

class TextTrack : public PluginHost::IPlugin, public PluginHost::JSONRPC {
public:
    TextTrack();
    TextTrack(const TextTrack &) = delete;
    TextTrack &operator=(const TextTrack &) = delete;
    ~TextTrack() override = default;

    BEGIN_INTERFACE_MAP(TextTrack)
    INTERFACE_ENTRY(PluginHost::IPlugin)
    INTERFACE_ENTRY(PluginHost::IDispatcher)
    INTERFACE_AGGREGATE(Exchange::ITextTrack, mImplTextTrackSessions)
    INTERFACE_AGGREGATE(Exchange::ITextTrackClosedCaptionsStyle, mImplTextTrackStyle)
#if ITEXTTRACK_VERSION >= 2
    INTERFACE_AGGREGATE(Exchange::ITextTrackTtmlStyle, mImplTextTrackTtmlStyle)
#endif
    END_INTERFACE_MAP

    // IPlugin methods
    const string Initialize(PluginHost::IShell *service) override;
    void Deinitialize(PluginHost::IShell *service) override;
    string Information() const override;
private:
    // For JSON-RPC setup
    void RegisterAllMethods();
    void UnregisterAllMethods();

    // Notification/Events
    class Notification : public RPC::IRemoteConnection::INotification, public Exchange::ITextTrackClosedCaptionsStyle::INotification {
        Notification() = delete;
        Notification(const Notification &) = delete;
        Notification &operator=(const Notification &) = delete;
        using FontFamily = Exchange::ITextTrackClosedCaptionsStyle::FontFamily;
        using FontSize = Exchange::ITextTrackClosedCaptionsStyle::FontSize;
        using FontEdge = Exchange::ITextTrackClosedCaptionsStyle::FontEdge;
        using ClosedCaptionsStyle = Exchange::ITextTrackClosedCaptionsStyle::ClosedCaptionsStyle;
    public:
        explicit Notification(TextTrack *parent);
        ~Notification() = default;
        // Activated/Deactivated are from IRemoteConnection::INotification, called when Thunder detects such on the COM-RPC link
        // Action is voluntary.
        void Activated(RPC::IRemoteConnection *connection) override;
        void Deactivated(RPC::IRemoteConnection *connection) override;
        // Notifications from ITextTrackClosedCaptionsStyle
        void OnClosedCaptionsStyleChanged(const ClosedCaptionsStyle &style) override;
        void OnFontFamilyChanged(const FontFamily font) override;
        void OnFontSizeChanged(const FontSize size) override;
        void OnFontColorChanged(const string &color) override;
        void OnFontOpacityChanged(const int8_t opacity) override;
        void OnFontEdgeChanged(const FontEdge edge) override;
        void OnFontEdgeColorChanged(const string &color) override;
        void OnBackgroundColorChanged(const string &color) override;
        void OnBackgroundOpacityChanged(const int8_t opacity) override;
        void OnWindowColorChanged(const string &color) override;
        void OnWindowOpacityChanged(const int8_t opacity) override;

        BEGIN_INTERFACE_MAP(INotification)
        INTERFACE_ENTRY(Exchange::ITextTrackClosedCaptionsStyle::INotification)
        INTERFACE_ENTRY(RPC::IRemoteConnection::INotification)
        END_INTERFACE_MAP
    private:
        TextTrack &mParent;
    };
    // Called-back when the plugin is deactivated
    void Deactivated(RPC::IRemoteConnection *connection);

    uint32_t mConnectionId = 0;
    PluginHost::IShell *mService = nullptr;
    Exchange::ITextTrack *mImplTextTrackSessions = nullptr;
    Exchange::ITextTrackClosedCaptionsStyle *mImplTextTrackStyle = nullptr;
#if ITEXTTRACK_VERSION >= 2
    Exchange::ITextTrackTtmlStyle *mImplTextTrackTtmlStyle = nullptr;
#endif
    Core::Sink<Notification> mNotification;
};

} // namespace Plugin
} // namespace WPEFramework
