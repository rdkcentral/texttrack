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

// include order matters
// clang-format off
#include "Module.h"
// clang-format on

#include <interfaces/IConfiguration.h>
#include <interfaces/IStore.h>
#include <interfaces/ITextTrack.h>

#include <condition_variable>
#include <optional>

#include <subttxrend/ctrl/Configuration.hpp>
#include <subttxrend/ctrl/Options.hpp>
#include "TextTrackConfiguration.h"

// Sanity check
#if TEXTTRACK_WITH_CCHAL && ITEXTTRACK_VERSION < 3
#error The included ITextTrack does not support CC HAL usage - retry compile without CCHAL
#endif

namespace WPEFramework {
namespace Plugin {

class RenderSession;
class SubttxClosedCaptionsStyle;

class TextTrackImplementation : public Exchange::ITextTrack, public Exchange::ITextTrackClosedCaptionsStyle, public Exchange::IConfiguration
#if ITEXTTRACK_VERSION >= 2
, public Exchange::ITextTrackTtmlStyle
#endif
{
public:
    TextTrackImplementation();
    ~TextTrackImplementation() override;

    TextTrackImplementation(const TextTrackImplementation &) = delete;
    TextTrackImplementation &operator=(const TextTrackImplementation &) = delete;

    BEGIN_INTERFACE_MAP(TextTrackImplementation)
    INTERFACE_ENTRY(Exchange::ITextTrack)
    INTERFACE_ENTRY(Exchange::ITextTrackClosedCaptionsStyle)
#if ITEXTTRACK_VERSION >= 2
    INTERFACE_ENTRY(Exchange::ITextTrackTtmlStyle)
#endif
    INTERFACE_ENTRY(Exchange::IConfiguration)
    END_INTERFACE_MAP

    // Implement IConfiguration
    Core::hresult Configure(PluginHost::IShell *shell) override;

    // @{ @group ITextTrackClosedCaptionsStyle

    Core::hresult Register(ITextTrackClosedCaptionsStyle::INotification *notification) override;
    Core::hresult Unregister(const ITextTrackClosedCaptionsStyle::INotification *notification) override;

    Core::hresult SetClosedCaptionsStyle(const ClosedCaptionsStyle &style) override;
    Core::hresult GetClosedCaptionsStyle(ClosedCaptionsStyle &style) const override;
    Core::hresult SetFontFamily(const FontFamily font) override;
    Core::hresult GetFontFamily(FontFamily &font) const override;
    Core::hresult SetFontSize(const FontSize size) override;
    Core::hresult GetFontSize(FontSize &size) const override;
    Core::hresult SetFontColor(const string &color) override;
    Core::hresult GetFontColor(string &color) const override;
    Core::hresult SetFontOpacity(const int8_t opacity) override;
    Core::hresult GetFontOpacity(int8_t &opacity) const override;
    Core::hresult SetFontEdge(const FontEdge edge) override;
    Core::hresult GetFontEdge(FontEdge &edge) const override;
    Core::hresult SetFontEdgeColor(const string &color) override;
    Core::hresult GetFontEdgeColor(string &color) const override;
    Core::hresult SetBackgroundColor(const string &color) override;
    Core::hresult GetBackgroundColor(string &color) const override;
    Core::hresult SetBackgroundOpacity(const int8_t opacity) override;
    Core::hresult GetBackgroundOpacity(int8_t &opacity) const override;
    Core::hresult SetWindowColor(const string &color) override;
    Core::hresult GetWindowColor(string &color) const override;
    Core::hresult SetWindowOpacity(const int8_t opacity) override;
    Core::hresult GetWindowOpacity(int8_t &opacity) const override;

    // @}

    // @{ @group ITextTrack

    Core::hresult OpenSession(const string &displayName, uint32_t &sessionId) override;
    Core::hresult CloseSession(uint32_t sessionId) override;
    Core::hresult ResetSession(uint32_t sessionId) override;
    Core::hresult PauseSession(uint32_t sessionId) override;
    Core::hresult ResumeSession(uint32_t sessionId) override;
    Core::hresult MuteSession(uint32_t sessionId) override;
    Core::hresult UnMuteSession(uint32_t sessionId) override;
    Core::hresult SendSessionData(uint32_t sessionId, DataType type, int64_t displayOffsetMs, const string &data) override;
    Core::hresult SendSessionTimestamp(uint32_t sessionId, uint64_t mediaTimestampMs) override;
    Core::hresult ApplyCustomClosedCaptionsStyleToSession(uint32_t sessionId, const ClosedCaptionsStyle &style) override;
    Core::hresult SetPreviewText(uint32_t sessionId, const std::string &text) override;
    Core::hresult SetSessionClosedCaptionsService(uint32_t sessionId, const string &service) override;
    Core::hresult SetSessionTeletextSelection(uint32_t sessionId, uint16_t page) override;
    Core::hresult SetSessionDvbSubtitleSelection(uint32_t sessionId, uint16_t compositionPageId, uint16_t ancillaryPageId) override;
    Core::hresult SetSessionWebVTTSelection(uint32_t sessionId) override;
    Core::hresult SetSessionTTMLSelection(uint32_t sessionId) override;
    Core::hresult SetSessionSCTESelection(uint32_t sessionId) override;
#if ITEXTTRACK_VERSION >= 2
    Core::hresult ApplyCustomTtmlStyleOverridesToSession(uint32_t sessionId, const string &styling) override;
#endif
#if TEXTTRACK_WITH_CCHAL
    Core::hresult AssociateVideoDecoder(uint32_t sessionIdi, const string &handle) override;
#endif

    // @}

#if ITEXTTRACK_VERSION >= 2
    // @{ @group ITextTrackTtmlStyle

    Core::hresult Register(ITextTrackTtmlStyle::INotification *notification) override;
    Core::hresult Unregister(const ITextTrackTtmlStyle::INotification *notification) override;

    Core::hresult SetTtmlStyleOverrides(const string &style) override;
    Core::hresult GetTtmlStyleOverrides(string& style) const override;

    // @}
#endif

private:
    void ReadStyleSettings();
    void ApplyClosedCaptionsStyle(const ClosedCaptionsStyle &style);
    void ApplyClosedCaptionsStyle(RenderSession &session, const SubttxClosedCaptionsStyle &style);

    // Call with mConfigMutex taken
    void ReadClosedCaptionsStyle(ClosedCaptionsStyle &style) const;
    // Call with mConfigMutex taken
    void WriteClosedCaptionsStyle(const ClosedCaptionsStyle &style);
    // Call with mConfigMutex taken
    bool CheckWhetherClosedCaptionsStyleChanged(const ClosedCaptionsStyle &style, const ClosedCaptionsStyle &oldStyle);
    void RaiseOnClosedCaptionsStyleChanged(const ClosedCaptionsStyle &style);
    void RaiseOnFontFamilyChanged(const FontFamily font);
    void RaiseOnFontSizeChanged(const FontSize size);
    void RaiseOnFontColorChanged(const string &color);
    void RaiseOnFontOpacityChanged(const int8_t opacity);
    void RaiseOnFontEdgeChanged(const FontEdge edge);
    void RaiseOnFontEdgeColorChanged(const string &color);
    void RaiseOnBackgroundColorChanged(const string &color);
    void RaiseOnBackgroundOpacityChanged(const int8_t opacity);
    void RaiseOnWindowColorChanged(const string &color);
    void RaiseOnWindowOpacityChanged(const int8_t opacity);

    // Call with mConfigMutex taken
    void ReadTtmlStyleOverrides(string &style) const;
    // Call with mConfigMutex taken
    void WriteTtmlStyleOverrides(const string &style);
    void ApplyTtmlStyleOverrides(const string &style);
    bool ApplyTtmlStyleOverrides(RenderSession &session, const string &style);
    void RaiseOnTtmlStyleOverridesChanged(const string &style);

    struct SessionInfo {
        std::unique_ptr<RenderSession> session;
    };
    subttxrend::ctrl::Options mOptions;
    subttxrend::ctrl::Configuration mConfiguration;
    mutable std::mutex mSessionsMutex;
    std::map<unsigned, SessionInfo> mSessions;
    std::atomic<unsigned> mSessionNumber{0};

    std::vector<ITextTrackClosedCaptionsStyle::INotification *> mNotificationCallbacks;
#if ITEXTTRACK_VERSION >= 2
    std::vector<ITextTrackTtmlStyle::INotification *> mTtmlCallbacks;
#endif
    std::mutex mNotificationMutex;

    // Interface for storing TextTrack configuration
    mutable Exchange::IStore *mConfigStore{nullptr};
    mutable RPC::SmartInterfaceType<Exchange::IStore> mConfigPlugin;
    // Cached values for easier Get*
    mutable std::optional<ClosedCaptionsStyle> mCachedStyle;
    mutable std::optional<std::string> mCachedTtmlStyleOverrides;

    // Protect calls to the config store
    mutable std::mutex mConfigMutex;

    // Parsing WPE plugin JSON configuration
    TextTrackConfiguration mPluginConfig;

#if TEXTTRACK_WITH_RDKSHELL
    // We might need the RDKShell on some devices in order to create a display
    typedef WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement> LinkType;
    LinkType *mpRdkShell{nullptr};

    bool EnsureDisplayIsCreated(std::string const &displayName);
#endif
};

} // namespace Plugin
} // namespace WPEFramework
