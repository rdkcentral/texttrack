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

#include "TextTrack.h"
#include <interfaces/IConfiguration.h>

namespace WPEFramework {
namespace Plugin {

static Metadata<TextTrack> metadata(
    // Version
    1, 2, 0,
    // Preconditions
    {},
    // Terminations
    {},
    // Controls
    {});

TextTrack::Notification::Notification(TextTrack *parent) : mParent(*parent) {
}

void TextTrack::Notification::Activated(RPC::IRemoteConnection *) {
}

void TextTrack::Notification::Deactivated(RPC::IRemoteConnection *) {
}

void TextTrack::Notification::OnClosedCaptionsStyleChanged(const ClosedCaptionsStyle &style) {
    Exchange::JTextTrackClosedCaptionsStyle::Event::OnClosedCaptionsStyleChanged(mParent, style);
}

void TextTrack::Notification::OnFontFamilyChanged(const FontFamily font) {
    Exchange::JTextTrackClosedCaptionsStyle::Event::OnFontFamilyChanged(mParent, font);
}

void TextTrack::Notification::OnFontSizeChanged(const FontSize size) {
    Exchange::JTextTrackClosedCaptionsStyle::Event::OnFontSizeChanged(mParent, size);
}

void TextTrack::Notification::OnFontColorChanged(const string &color) {
    Exchange::JTextTrackClosedCaptionsStyle::Event::OnFontColorChanged(mParent, color);
}

void TextTrack::Notification::OnFontOpacityChanged(const int8_t opacity) {
    Exchange::JTextTrackClosedCaptionsStyle::Event::OnFontOpacityChanged(mParent, opacity);
}

void TextTrack::Notification::OnFontEdgeChanged(const FontEdge edge) {
    Exchange::JTextTrackClosedCaptionsStyle::Event::OnFontEdgeChanged(mParent, edge);
}

void TextTrack::Notification::OnFontEdgeColorChanged(const string &color) {
    Exchange::JTextTrackClosedCaptionsStyle::Event::OnFontEdgeColorChanged(mParent, color);
}

void TextTrack::Notification::OnBackgroundColorChanged(const string &color) {
    Exchange::JTextTrackClosedCaptionsStyle::Event::OnBackgroundColorChanged(mParent, color);
}

void TextTrack::Notification::OnBackgroundOpacityChanged(const int8_t opacity) {
    Exchange::JTextTrackClosedCaptionsStyle::Event::OnBackgroundOpacityChanged(mParent, opacity);
}

void TextTrack::Notification::OnWindowColorChanged(const string &color) {
    Exchange::JTextTrackClosedCaptionsStyle::Event::OnWindowColorChanged(mParent, color);
}

void TextTrack::Notification::OnWindowOpacityChanged(const int8_t opacity) {
    Exchange::JTextTrackClosedCaptionsStyle::Event::OnWindowOpacityChanged(mParent, opacity);
}

TextTrack::TextTrack() : mNotification(this) {
}

const string TextTrack::Initialize(PluginHost::IShell *service) {
    ASSERT(mService == nullptr);
    ASSERT(mImplTextTrackStyle == nullptr);
    ASSERT(mImplTextTrackSessions == nullptr);
    TRACE(Trace::Initialisation, (_T("Initializing TextTrack plugin runnning in process %d"), Core::ProcessInfo().Id()));

    mService = service;
    mService->AddRef();
    // Do this early so we at least get the activated/deactivated events
    mService->Register(&mNotification);

    std::string ret{};
    // The implementation always implements the style interface and often the sessions interface
    mImplTextTrackStyle = mService->Root<Exchange::ITextTrackClosedCaptionsStyle>(mConnectionId, 5000, _T("TextTrackImplementation"));
    if (mImplTextTrackStyle != nullptr) {
        TRACE(Trace::Information, (_T("TextTrack plugin uses connection id %d"), mConnectionId));
        mImplTextTrackStyle->Register(&mNotification);
        if (auto *confI = mImplTextTrackStyle->QueryInterface<Exchange::IConfiguration>()) {
            if (confI->Configure(mService) != Core::ERROR_NONE) {
                TRACE(Trace::Error, (_T("Failed to configure TextTrackImplementation")));
                ret = "Failed to configure TextTrackImplementation";
            }
            confI->Release();
        }
        if (auto *ifSessions = mImplTextTrackStyle->QueryInterface<Exchange::ITextTrack>()) {
            mImplTextTrackSessions = ifSessions;
        }
#if ITEXTTRACK_VERSION >= 2
        if (auto *ifTtml = mImplTextTrackStyle->QueryInterface<Exchange::ITextTrackTtmlStyle>()) {
            mImplTextTrackTtmlStyle = ifTtml;
        }
#endif
        RegisterAllMethods();
    } else {
        TRACE(Trace::Error, (_T("Failed to initialize TextTrack plugin")));
        ret = "Failed to initialize TextTrack plugin";
    }

    return ret;
}

void TextTrack::Deinitialize(PluginHost::IShell *service) {
    TRACE(Trace::Initialisation, (_T("Deinitializing TextTrack plugin runnning in process %d"), Core::ProcessInfo().Id()));

    // mService and service should be identical, but at least service is safe to use always
    service->Unregister(&mNotification);
    UnregisterAllMethods();
    if (mImplTextTrackStyle) {
        mImplTextTrackStyle->Unregister(&mNotification);
        mImplTextTrackStyle->Release();
    }
    if (mImplTextTrackSessions) {
        mImplTextTrackSessions->Release();
    }
#if ITEXTTRACK_VERSION >= 2
    if (mImplTextTrackTtmlStyle) {
        mImplTextTrackTtmlStyle->Release();
        mImplTextTrackTtmlStyle = nullptr;
    }
#endif
    mConnectionId = 0;
    if (mService != nullptr) {
        mService->Release();
    }
    mService = nullptr;
    mImplTextTrackStyle = nullptr;
    mImplTextTrackSessions = nullptr;
}

string TextTrack::Information() const {
    return {};
}

void TextTrack::RegisterAllMethods() {
    if (mImplTextTrackStyle) {
        Exchange::JTextTrackClosedCaptionsStyle::Register(*this, mImplTextTrackStyle);
    }
    if (mImplTextTrackSessions) {
        Exchange::JTextTrack::Register(*this, mImplTextTrackSessions);
    }
#if ITEXTTRACK_VERSION >= 2
    if (mImplTextTrackTtmlStyle) {
        Exchange::JTextTrackTtmlStyle::Register(*this, mImplTextTrackTtmlStyle);
    }
#endif
}

void TextTrack::UnregisterAllMethods() {
    if (mImplTextTrackStyle) {
        Exchange::JTextTrackClosedCaptionsStyle::Unregister(*this);
    }
    if (mImplTextTrackSessions) {
        Exchange::JTextTrack::Unregister(*this);
    }
#if ITEXTTRACK_VERSION >= 2
    if (mImplTextTrackTtmlStyle) {
        Exchange::JTextTrackTtmlStyle::Unregister(*this);
    }
#endif
}

void TextTrack::Deactivated(RPC::IRemoteConnection *connection) {
    if (connection->Id() == mConnectionId) {
        TRACE(Trace::Information, (_T("TextTrack::Deactivated on connection %d"), mConnectionId));
        ASSERT(mService != nullptr);
        Core::IWorkerPool::Instance().Submit(PluginHost::IShell::Job::Create(mService, PluginHost::IShell::DEACTIVATED, PluginHost::IShell::FAILURE));
    }
}

// End methods

} // namespace Plugin
} // namespace WPEFramework
