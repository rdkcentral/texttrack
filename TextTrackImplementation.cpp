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

#include "TextTrackImplementation.h"

#include <syscall.h>
#include <tracing/Logging.h>

#include <fstream>
#include <subttxrend/cc/CcCommand.hpp>
#include <subttxrend/common/LoggerManager.hpp>

#include "RenderSession.h"

#if TEXTTRACK_WITH_RDKSHELL
// Assuming that use of RDKShell is only on devices with Thunder security enabled
#undef EXTERNAL // To avoid warning about redefinition
#include <securityagent/SecurityTokenUtil.h>
#endif

// This variable is expected by at least the Broadcom HAL implementation
int IsCCOnFlag = 1;

namespace WPEFramework {
namespace Plugin {
namespace {

const std::string STORE_NAMESPACE{"TextTrack"};

class JsonClosedCaptionsStyle : public Core::JSON::Container {
    JsonClosedCaptionsStyle(const JsonClosedCaptionsStyle &) = delete;
    JsonClosedCaptionsStyle &operator=(const JsonClosedCaptionsStyle &) = delete;
public:
    JsonClosedCaptionsStyle() : Core::JSON::Container() {
        Add(_T("fontFamily"), &fontFamily);
        Add(_T("fontSize"), &fontSize);
        Add(_T("fontColor"), &fontColor);
        Add(_T("fontOpacity"), &fontOpacity);
        Add(_T("fontEdge"), &fontEdge);
        Add(_T("fontEdgeColor"), &fontEdgeColor);
        Add(_T("backgroundColor"), &backgroundColor);
        Add(_T("backgroundOpacity"), &backgroundOpacity);
        Add(_T("windowColor"), &windowColor);
        Add(_T("windowOpacity"), &windowOpacity);
    }
    ~JsonClosedCaptionsStyle() = default;

    JsonClosedCaptionsStyle &operator=(const Exchange::ITextTrackClosedCaptionsStyle::ClosedCaptionsStyle &ccStyle) {
        fontFamily = static_cast<uint8_t>(ccStyle.fontFamily);
        fontSize = static_cast<int8_t>(ccStyle.fontSize);
        fontColor = ccStyle.fontColor;
        fontOpacity = ccStyle.fontOpacity;
        fontEdge = static_cast<int8_t>(ccStyle.fontEdge);
        fontEdgeColor = ccStyle.fontEdgeColor;
        backgroundColor = ccStyle.backgroundColor;
        backgroundOpacity = ccStyle.backgroundOpacity;
        windowColor = ccStyle.windowColor;
        windowOpacity = ccStyle.windowOpacity;
        return *this;
    }
    operator Exchange::ITextTrackClosedCaptionsStyle::ClosedCaptionsStyle() const {
        Exchange::ITextTrackClosedCaptionsStyle::ClosedCaptionsStyle ccStyle;
        ccStyle.fontFamily = static_cast<Exchange::ITextTrackClosedCaptionsStyle::FontFamily>(fontFamily.Value());
        ccStyle.fontSize = static_cast<Exchange::ITextTrackClosedCaptionsStyle::FontSize>(fontSize.Value());
        ccStyle.fontColor = fontColor;
        ccStyle.fontOpacity = fontOpacity;
        ccStyle.fontEdge = static_cast<Exchange::ITextTrackClosedCaptionsStyle::FontEdge>(fontEdge.Value());
        ccStyle.fontEdgeColor = fontEdgeColor;
        ccStyle.backgroundColor = backgroundColor;
        ccStyle.backgroundOpacity = backgroundOpacity;
        ccStyle.windowColor = windowColor;
        ccStyle.windowOpacity = windowOpacity;
        return ccStyle;
    }

    Core::JSON::DecUInt8 fontFamily = 0;
    Core::JSON::DecSInt8 fontSize = -1;

    Core::JSON::String fontColor;
    Core::JSON::DecSInt8 fontOpacity = -1;

    Core::JSON::DecSInt8 fontEdge = -1;
    Core::JSON::String fontEdgeColor;

    Core::JSON::String backgroundColor;
    Core::JSON::DecSInt8 backgroundOpacity = -1;

    Core::JSON::String windowColor;
    Core::JSON::DecSInt8 windowOpacity = -1;
};

SubttxClosedCaptionsStyle convertClosedCaptionsStyle(const Exchange::ITextTrackClosedCaptionsStyle::ClosedCaptionsStyle &style) {
    constexpr const uint32_t CONTENT_DEFAULT = -1;
    constexpr auto fParseRgbColor = [](const std::string &value) -> uint32_t {
        if (value.size() == 7 and value[0] == '#') {
            size_t pos = 0;
            const unsigned long convVal = std::stoul(value.substr(1), &pos, 16);
            if (pos == 6) {
                return convVal;
            }
        }
        // Subttx-value for "unset color"
        return 0xff000000;
    };
    constexpr auto fConvertOpacity = [](int8_t value) -> uint32_t {
        if (value >= 0) {
            if (value < 34) {
                return static_cast<uint32_t>(subttxrend::cc::Opacity::TRANSPARENT);
            } else if (value < 67) {
                return static_cast<uint32_t>(subttxrend::cc::Opacity::TRANSLUCENT);
            } else if (value <= 100) {
                return static_cast<uint32_t>(subttxrend::cc::Opacity::SOLID);
            }
        }
        return CONTENT_DEFAULT;
    };
    constexpr auto fConvertFontFamily = [](Exchange::ITextTrackClosedCaptionsStyle::FontFamily family) -> uint32_t {
        if (family != Exchange::ITextTrackClosedCaptionsStyle::FontFamily::CONTENT_DEFAULT) {
            return static_cast<uint32_t>(family);
        }
        return CONTENT_DEFAULT;
    };
    constexpr auto fConvertFontSize = [](Exchange::ITextTrackClosedCaptionsStyle::FontSize size) -> uint32_t { return static_cast<uint32_t>(size); };
    constexpr auto fConvertFontEdge = [](Exchange::ITextTrackClosedCaptionsStyle::FontEdge edge) -> uint32_t { return static_cast<uint32_t>(edge); };
    SubttxClosedCaptionsStyle target;
    target.fontColor = fParseRgbColor(style.fontColor);
    target.fontOpacity = fConvertOpacity(style.fontOpacity);
    target.fontStyle = fConvertFontFamily(style.fontFamily);
    target.fontSize = fConvertFontSize(style.fontSize);
    target.edgeType = fConvertFontEdge(style.fontEdge);
    target.edgeColor = fParseRgbColor(style.fontEdgeColor);
    target.backgroundColor = fParseRgbColor(style.backgroundColor);
    target.backgroundOpacity = fConvertOpacity(style.backgroundOpacity);
    target.windowColor = fParseRgbColor(style.windowColor);
    target.windowOpacity = fConvertOpacity(style.windowOpacity);
    return target;
}
} // namespace

SERVICE_REGISTRATION(TextTrackImplementation, 1, 0);

constexpr const uint ARGC = 2;
const char *args[ARGC] = {"TextTrack", "--config-file-path=" TEXTTRACK_CONFIG_FILE_PATH};

TextTrackImplementation::TextTrackImplementation() : mOptions(ARGC, const_cast<char **>(args)), mConfiguration(mOptions) {
    // Setup logging etc
    subttxrend::common::LoggerManager::getInstance()->init(&mConfiguration.getLoggerConfig());
}

TextTrackImplementation::~TextTrackImplementation() {
#if TEXTTRACK_WITH_RDKSHELL
    delete mpRdkShell;
#endif
    {
        std::unique_lock lock{mConfigMutex};
        if (mConfigStore) {
            mConfigStore->Release();
        }
        mConfigPlugin.Close(Core::infinite);
    }
    {
        std::unique_lock lock{mSessionsMutex};
        for (const auto &session : mSessions) {
            session.second.session->stop();
        }
    }
}

Core::hresult TextTrackImplementation::Register(ITextTrackClosedCaptionsStyle::INotification *notification) {
    std::unique_lock lockCfg{mConfigMutex};
    std::unique_lock lockNtf{mNotificationMutex};
    {
        auto exists = std::find(mNotificationCallbacks.begin(), mNotificationCallbacks.end(), notification);
        if (exists == mNotificationCallbacks.end()) {
            mNotificationCallbacks.emplace_back(notification);
            notification->AddRef();
        }
    }
    {
        ClosedCaptionsStyle style;
        ReadClosedCaptionsStyle(style);
        notification->OnClosedCaptionsStyleChanged(style);
        notification->OnFontSizeChanged(style.fontSize);
        notification->OnFontColorChanged(style.fontColor);
        notification->OnFontOpacityChanged(style.fontOpacity);
        notification->OnFontEdgeChanged(style.fontEdge);
        notification->OnFontEdgeColorChanged(style.fontEdgeColor);
        notification->OnBackgroundColorChanged(style.backgroundColor);
        notification->OnBackgroundOpacityChanged(style.backgroundOpacity);
        notification->OnWindowColorChanged(style.windowColor);
        notification->OnWindowOpacityChanged(style.windowOpacity);
    }
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::Unregister(const ITextTrackClosedCaptionsStyle::INotification *notification) {
    std::unique_lock lock{mNotificationMutex};
    auto exists = std::find(mNotificationCallbacks.begin(), mNotificationCallbacks.end(), notification);
    if (exists != mNotificationCallbacks.end()) {
        notification->Release();
        mNotificationCallbacks.erase(exists);
    }
    return Core::ERROR_NONE;
}

#if ITEXTTRACK_VERSION >= 2
Core::hresult TextTrackImplementation::Register(ITextTrackTtmlStyle::INotification *notification) {
    std::unique_lock lockCfg{mConfigMutex};
    std::unique_lock lockNtf{mNotificationMutex};
    {
        auto exists = std::find(mTtmlCallbacks.begin(), mTtmlCallbacks.end(), notification);
        if (exists == mTtmlCallbacks.end()) {
            mTtmlCallbacks.emplace_back(notification);
            notification->AddRef();
        }
    }
    {
        string style;
        ReadTtmlStyleOverrides(style);
        notification->OnTtmlStyleOverridesChanged(style);
    }
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::Unregister(const ITextTrackTtmlStyle::INotification *notification) {
    std::unique_lock lock{mNotificationMutex};
    auto exists = std::find(mTtmlCallbacks.begin(), mTtmlCallbacks.end(), notification);
    if (exists != mTtmlCallbacks.end()) {
        notification->Release();
        mTtmlCallbacks.erase(exists);
    }
    return Core::ERROR_NONE;
}
#endif

// Called during single-threaded initialization only
Core::hresult TextTrackImplementation::Configure(PluginHost::IShell *shell) {
    mPluginConfig.FromString(shell->ConfigLine());

    std::string standardDisplay;
    std::string standardSocket;
    // First read values from plugin configuration
    if (mPluginConfig.StandardDisplay.IsSet()) {
        standardDisplay = mPluginConfig.StandardDisplay;
    }
    if (mPluginConfig.StandardSocket.IsSet()) {
        standardSocket = mPluginConfig.StandardSocket;
    }
    // Displayname falls back to environment
    if (standardDisplay.empty()) {
        if (const char *env_display = getenv("WAYLAND_DISPLAY")) {
            standardDisplay = env_display;
        }
    }
    // Socketname falls back to value in subttxrend configuration file
    if (standardSocket.empty()) {
        standardSocket = mConfiguration.getMainContextSocketPath();
    }
    // Construct connection to PersistentStore, open it and acquire its interface.
    std::string persistentStorePluginName{"org.rdk.PersistentStore"};
    if (mPluginConfig.PersistentStore.IsSet()) {
        persistentStorePluginName = mPluginConfig.PersistentStore;
    }
    if (!persistentStorePluginName.empty()) {
        const Core::hresult configResult = mConfigPlugin.Open(RPC::CommunicationTimeOut, mConfigPlugin.Connector(), persistentStorePluginName);
        if (configResult != Core::ERROR_NONE) {
            TRACE(Trace::Error, (_T("Could not open PersistentStore '%s' error=%u msg=%s"), persistentStorePluginName.c_str(), configResult,
                                 Core::ErrorToString(configResult)));
            return Core::ERROR_GENERAL;
        }
        if (!mConfigPlugin.IsOperational()) {
            TRACE(Trace::Error, (_T("Could not get PersistentStore Interface for '%'"), persistentStorePluginName.c_str()));
            return Core::ERROR_GENERAL;
        }
        mConfigStore = mConfigPlugin.Interface();
        // Make sure we've read and cached the settings from storage, so the first read doesn't have to
        ReadStyleSettings();
    } else {
        TRACE(Trace::Error, (_T("PersistentStore configuration is empty")));
        return Core::ERROR_GENERAL;
    }
#if TEXTTRACK_WITH_SESSIONS
    // Create render session for backwards compatibility
    if (!standardDisplay.empty() && !standardSocket.empty()) {
#if TEXTTRACK_WITH_RDKSHELL
        if (!EnsureDisplayIsCreated(standardDisplay)) {
            TRACE(Trace::Error, (_T("failed to use RDKShell to create display")));
            return Core::ERROR_GENERAL;
        }
#endif
        std::unique_lock lock{mSessionsMutex};
        auto compatible = std::make_unique<RenderSession>(mConfiguration, standardDisplay, standardSocket);
        TRACE(Trace::Information, (_T("starts standard session on %s with %s"), standardDisplay.c_str(), standardSocket.c_str()));
        compatible->start();
        // No timeout for this session
        mSessions.emplace(++mSessionNumber, SessionInfo{std::move(compatible)});
    }
#endif
    return Core::ERROR_NONE;
}

#if TEXTTRACK_WITH_RDKSHELL
bool TextTrackImplementation::EnsureDisplayIsCreated(const std::string &displayName) {
    TRACE(Trace::Information, (_T("Ensure Display %s with RDKShell"), displayName.c_str()));
    if (!mpRdkShell) {
        std::string securityToken;
        try {
            unsigned char buffer[1024] = {0};
            int ret = GetSecurityToken(sizeof(buffer), buffer);
            if (ret > 0) {
                securityToken = "token=" + std::string(reinterpret_cast<char *>(buffer));
            }
        } catch (const std::exception &e) {
            TRACE(Trace::Error, (_T("Caught exception from GetSecurityToken: %s"), e.what()));
            return false;
        }
        mpRdkShell = new LinkType(_T("org.rdk.RDKShell.1"), false, securityToken);
    }
    // Query whether display/client already exists
    {
        JsonObject params;
        JsonObject result;
        const Core::hresult err = mpRdkShell->Invoke<JsonObject, JsonObject>(3000, "getClients", params, result);
        if (err != Core::ERROR_NONE || !result["success"].Boolean()) {
            TRACE(Trace::Error, (_T("Could not query display clients")));
            return false;
        }
        bool found_display = false;
        auto clients = result["clients"].Array();
        auto iter = clients.Elements();
        while (iter.Next()) {
            if (iter.Current().String() == displayName) {
                found_display = true;
                break;
            }
        }
        if (found_display) {
            // Good, already created
            return true;
        }
    }
    // Create display
    {
        TRACE(Trace::Information, (_T("Creating display %s"), displayName.c_str()));
        JsonObject params;
        params["client"] = displayName;
        params["displayName"] = displayName;
        JsonObject result;
        const Core::hresult err = mpRdkShell->Invoke<JsonObject, JsonObject>(3000, "createDisplay", params, result);
        if (err != Core::ERROR_NONE || !result["success"].Boolean()) {
            TRACE(Trace::Error, (_T("Could not create display %s"), displayName.c_str()));
            return false;
        }
    }
    // Set display as topmost
    {
        JsonObject params;
        params["client"] = displayName;
        params["topmost"] = true;
        JsonObject result;
        const Core::hresult err = mpRdkShell->Invoke<JsonObject, JsonObject>(3000, "setTopmost", params, result);
        if (err != Core::ERROR_NONE || !result["success"].Boolean()) {
            TRACE(Trace::Error, (_T("Could not set display %s as topmost"), displayName.c_str()));
            return false;
        }
    }
    return true;
}
#endif

void TextTrackImplementation::ReadStyleSettings() {
    std::unique_lock lock{mConfigMutex};
    ClosedCaptionsStyle ccStyle;
    ReadClosedCaptionsStyle(ccStyle);
    std::string ttmlStyle;
    ReadTtmlStyleOverrides(ttmlStyle);
}

// Reading/writing from ConfigStore
void TextTrackImplementation::ReadClosedCaptionsStyle(ClosedCaptionsStyle &style) const {
    // The style is stored as a JSON object (string'ed)
    if (mCachedStyle) {
        style = *mCachedStyle;
    } else if (mConfigStore) {
        std::string temp;
        JsonClosedCaptionsStyle parsedStyle;
        const Core::hresult ret = mConfigStore->GetValue(STORE_NAMESPACE, "ClosedCaptionsStyle", temp);
        if (ret == Core::ERROR_NONE) {
            if (parsedStyle.FromString(temp)) {
                mCachedStyle = parsedStyle;
            }
        }
        style = parsedStyle;
    } else {
        // Defaults cannot be coded into ClosedCaptionsStyle itself, so we use our Json overlay for that
        style = JsonClosedCaptionsStyle{};
    }
}

void TextTrackImplementation::WriteClosedCaptionsStyle(const ClosedCaptionsStyle &style) {
    if (mConfigStore) {
        JsonClosedCaptionsStyle jsonStyle;
        jsonStyle = style;

        std::string stringStyle;
        if (jsonStyle.ToString(stringStyle)) {
            if (mConfigStore->SetValue(STORE_NAMESPACE, "ClosedCaptionsStyle", stringStyle) != Core::ERROR_NONE) {
                TRACE(Trace::Error, (_T("Unable to write ClosedCaptionsStyle")));
            }
            mCachedStyle = style;
        }
    }
}

void TextTrackImplementation::ReadTtmlStyleOverrides(std::string &style) const {
    style.clear();
    if (mCachedTtmlStyleOverrides) {
        style = *mCachedTtmlStyleOverrides;
    } else if (mConfigStore) {
        std::string temp;
        const Core::hresult ret = mConfigStore->GetValue(STORE_NAMESPACE, "TtmlStyleOverrides", temp);
        if (ret == Core::ERROR_NONE) {
            style = temp;
            mCachedTtmlStyleOverrides = temp;
        }
    }
    if (style.empty()) {
        // Check /etc/device.properties
        std::ifstream props("/etc/device.properties");
        if (props) {
            const char overridesKey[32] = "TEXTTRACK_TTML_STYLE_OVERRIDES=";
            std::string propLine;
            while (std::getline(props, propLine)) {
                if (propLine.compare(0, sizeof(overridesKey) - 1, overridesKey, sizeof(overridesKey) - 1) == 0) {
                    style = propLine.substr(sizeof(overridesKey) - 1);
                    // Trim leading/trailing "
                    if (style[0] == '"') {
                        style.erase(0, 1);
                    }
                    if (style[style.size() - 1] == '"') {
                        style.erase(style.size() - 1);
                    }
                    mCachedTtmlStyleOverrides = style;
                }
            }
        }
    }
}

void TextTrackImplementation::WriteTtmlStyleOverrides(const string &style) {
    if (mConfigStore) {
        if (mConfigStore->SetValue(STORE_NAMESPACE, "TtmlStyleOverrides", style) != Core::ERROR_NONE) {
            TRACE(Trace::Error, (_T("Unable to write TtmlStyleOverrides")));
        }
    }
    mCachedTtmlStyleOverrides = style;
}

// Functions from ITextTrackClosedCaptionsStyle interface

Core::hresult TextTrackImplementation::SetClosedCaptionsStyle(const ClosedCaptionsStyle &style) {
    std::unique_lock lockSes{mSessionsMutex};
    {
        std::unique_lock lockCfg{mConfigMutex};
        ClosedCaptionsStyle oldStyle;
        ReadClosedCaptionsStyle(oldStyle);
        if (CheckWhetherClosedCaptionsStyleChanged(style, oldStyle)) {
            WriteClosedCaptionsStyle(style);
        }
    }
    ApplyClosedCaptionsStyle(style);
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::GetClosedCaptionsStyle(ClosedCaptionsStyle &style) const {
    std::unique_lock lock{mConfigMutex};
    ReadClosedCaptionsStyle(style);
    return Core::ERROR_NONE;
}

bool TextTrackImplementation::CheckWhetherClosedCaptionsStyleChanged(const ClosedCaptionsStyle &style, const ClosedCaptionsStyle &oldStyle) {
    bool anyStyleChanged = false;
    if (style.fontFamily != oldStyle.fontFamily) {
        RaiseOnFontFamilyChanged(style.fontFamily);
        anyStyleChanged = true;
    }
    if (style.fontSize != oldStyle.fontSize) {
        RaiseOnFontSizeChanged(style.fontSize);
        anyStyleChanged = true;
    }
    if (style.fontColor != oldStyle.fontColor) {
        RaiseOnFontColorChanged(style.fontColor);
        anyStyleChanged = true;
    }
    if (style.fontOpacity != oldStyle.fontOpacity) {
        RaiseOnFontOpacityChanged(style.fontOpacity);
        anyStyleChanged = true;
    }
    if (style.fontEdge != oldStyle.fontEdge) {
        RaiseOnFontEdgeChanged(style.fontEdge);
        anyStyleChanged = true;
    }
    if (style.fontEdgeColor != oldStyle.fontEdgeColor) {
        RaiseOnFontEdgeColorChanged(style.fontEdgeColor);
        anyStyleChanged = true;
    }
    if (style.backgroundColor != oldStyle.backgroundColor) {
        RaiseOnBackgroundColorChanged(style.backgroundColor);
        anyStyleChanged = true;
    }
    if (style.backgroundOpacity != oldStyle.backgroundOpacity) {
        RaiseOnBackgroundOpacityChanged(style.backgroundOpacity);
        anyStyleChanged = true;
    }
    if (style.windowColor != oldStyle.windowColor) {
        RaiseOnWindowColorChanged(style.windowColor);
        anyStyleChanged = true;
    }
    if (style.windowOpacity != oldStyle.windowOpacity) {
        RaiseOnWindowOpacityChanged(style.windowOpacity);
        anyStyleChanged = true;
    }
    if (anyStyleChanged) {
        RaiseOnClosedCaptionsStyleChanged(style);
    }
    return anyStyleChanged;
}

Core::hresult TextTrackImplementation::SetFontFamily(const FontFamily font) {
    std::unique_lock lockSes{mSessionsMutex};
    ClosedCaptionsStyle style;
    {
        std::unique_lock lockCfg{mConfigMutex};
        ClosedCaptionsStyle oldStyle;
        ReadClosedCaptionsStyle(oldStyle);
        style = oldStyle;
        style.fontFamily = font;
        if (CheckWhetherClosedCaptionsStyleChanged(style, oldStyle)) {
            WriteClosedCaptionsStyle(style);
        }
    }
    ApplyClosedCaptionsStyle(style);
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::GetFontFamily(FontFamily &font) const {
    std::unique_lock lock{mConfigMutex};
    ClosedCaptionsStyle style;
    ReadClosedCaptionsStyle(style);
    font = style.fontFamily;
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::SetFontSize(const FontSize size) {
    std::unique_lock lockSes{mSessionsMutex};
    ClosedCaptionsStyle style;
    {
        std::unique_lock lockCfg{mConfigMutex};
        ClosedCaptionsStyle oldStyle;
        ReadClosedCaptionsStyle(oldStyle);
        style = oldStyle;
        style.fontSize = size;
        if (CheckWhetherClosedCaptionsStyleChanged(style, oldStyle)) {
            WriteClosedCaptionsStyle(style);
        }
    }
    ApplyClosedCaptionsStyle(style);
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::GetFontSize(FontSize &size) const {
    std::unique_lock lock{mConfigMutex};
    ClosedCaptionsStyle style;
    ReadClosedCaptionsStyle(style);
    size = style.fontSize;
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::SetFontColor(const string &color) {
    std::unique_lock lockSes{mSessionsMutex};
    ClosedCaptionsStyle style;
    {
        std::unique_lock lockCfg{mConfigMutex};
        ClosedCaptionsStyle oldStyle;
        ReadClosedCaptionsStyle(oldStyle);
        style = oldStyle;
        style.fontColor = color;
        if (CheckWhetherClosedCaptionsStyleChanged(style, oldStyle)) {
            WriteClosedCaptionsStyle(style);
        }
    }
    ApplyClosedCaptionsStyle(style);
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::GetFontColor(string &color) const {
    std::unique_lock lock{mConfigMutex};
    ClosedCaptionsStyle style;
    ReadClosedCaptionsStyle(style);
    color = style.fontColor;
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::SetFontOpacity(const int8_t opacity) {
    std::unique_lock lockSes{mSessionsMutex};
    ClosedCaptionsStyle style;
    {
        std::unique_lock lockCfg{mConfigMutex};
        ClosedCaptionsStyle oldStyle;
        ReadClosedCaptionsStyle(oldStyle);
        style = oldStyle;
        style.fontOpacity = opacity;
        if (CheckWhetherClosedCaptionsStyleChanged(style, oldStyle)) {
            WriteClosedCaptionsStyle(style);
        }
    }
    ApplyClosedCaptionsStyle(style);
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::GetFontOpacity(int8_t &opacity) const {
    std::unique_lock lock{mConfigMutex};
    ClosedCaptionsStyle style;
    ReadClosedCaptionsStyle(style);
    opacity = style.fontOpacity;
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::SetFontEdge(const FontEdge edge) {
    std::unique_lock lockSes{mSessionsMutex};
    ClosedCaptionsStyle style;
    {
        std::unique_lock lockCfg{mConfigMutex};
        ClosedCaptionsStyle oldStyle;
        ReadClosedCaptionsStyle(oldStyle);
        style = oldStyle;
        style.fontEdge = edge;
        if (CheckWhetherClosedCaptionsStyleChanged(style, oldStyle)) {
            WriteClosedCaptionsStyle(style);
        }
    }
    ApplyClosedCaptionsStyle(style);
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::GetFontEdge(FontEdge &edge) const {
    std::unique_lock lock{mConfigMutex};
    ClosedCaptionsStyle style;
    ReadClosedCaptionsStyle(style);
    edge = style.fontEdge;
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::SetFontEdgeColor(const string &color) {
    std::unique_lock lockSes{mSessionsMutex};
    ClosedCaptionsStyle style;
    {
        std::unique_lock lockCfg{mConfigMutex};
        ClosedCaptionsStyle oldStyle;
        ReadClosedCaptionsStyle(oldStyle);
        style = oldStyle;
        style.fontEdgeColor = color;
        if (CheckWhetherClosedCaptionsStyleChanged(style, oldStyle)) {
            WriteClosedCaptionsStyle(style);
        }
    }
    ApplyClosedCaptionsStyle(style);
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::GetFontEdgeColor(string &color) const {
    std::unique_lock lock{mConfigMutex};
    ClosedCaptionsStyle style;
    ReadClosedCaptionsStyle(style);
    color = style.fontEdgeColor;
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::SetBackgroundColor(const string &color) {
    std::unique_lock lockSes{mSessionsMutex};
    ClosedCaptionsStyle style;
    {
        std::unique_lock lockCfg{mConfigMutex};
        ClosedCaptionsStyle oldStyle;
        ReadClosedCaptionsStyle(oldStyle);
        style = oldStyle;
        style.backgroundColor = color;
        if (CheckWhetherClosedCaptionsStyleChanged(style, oldStyle)) {
            WriteClosedCaptionsStyle(style);
        }
    }
    ApplyClosedCaptionsStyle(style);
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::GetBackgroundColor(string &color) const {
    std::unique_lock lock{mConfigMutex};
    ClosedCaptionsStyle style;
    ReadClosedCaptionsStyle(style);
    color = style.backgroundColor;
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::SetBackgroundOpacity(const int8_t opacity) {
    std::unique_lock lockSes{mSessionsMutex};
    ClosedCaptionsStyle style;
    {
        std::unique_lock lockCfg{mConfigMutex};
        ClosedCaptionsStyle oldStyle;
        ReadClosedCaptionsStyle(oldStyle);
        style = oldStyle;
        style.backgroundOpacity = opacity;
        if (CheckWhetherClosedCaptionsStyleChanged(style, oldStyle)) {
            WriteClosedCaptionsStyle(style);
        }
    }
    ApplyClosedCaptionsStyle(style);
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::GetBackgroundOpacity(int8_t &opacity) const {
    std::unique_lock lock{mConfigMutex};
    ClosedCaptionsStyle style;
    ReadClosedCaptionsStyle(style);
    opacity = style.backgroundOpacity;
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::SetWindowColor(const string &color) {
    std::unique_lock lockSes{mSessionsMutex};
    ClosedCaptionsStyle style;
    {
        std::unique_lock lockCfg{mConfigMutex};
        ClosedCaptionsStyle oldStyle;
        ReadClosedCaptionsStyle(oldStyle);
        style = oldStyle;
        style.windowColor = color;
        if (CheckWhetherClosedCaptionsStyleChanged(style, oldStyle)) {
            WriteClosedCaptionsStyle(style);
        }
    }
    ApplyClosedCaptionsStyle(style);
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::GetWindowColor(string &color) const {
    std::unique_lock lock{mConfigMutex};
    ClosedCaptionsStyle style;
    ReadClosedCaptionsStyle(style);
    color = style.windowColor;
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::SetWindowOpacity(const int8_t opacity) {
    std::unique_lock lockSes{mSessionsMutex};
    ClosedCaptionsStyle style;
    {
        std::unique_lock lockCfg{mConfigMutex};
        ClosedCaptionsStyle oldStyle;
        ReadClosedCaptionsStyle(oldStyle);
        style = oldStyle;
        style.windowOpacity = opacity;
        if (CheckWhetherClosedCaptionsStyleChanged(style, oldStyle)) {
            WriteClosedCaptionsStyle(style);
        }
    }
    ApplyClosedCaptionsStyle(style);
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::GetWindowOpacity(int8_t &opacity) const {
    std::unique_lock lock{mConfigMutex};
    ClosedCaptionsStyle style;
    ReadClosedCaptionsStyle(style);
    opacity = style.windowOpacity;
    return Core::ERROR_NONE;
}

#if ITEXTTRACK_VERSION >= 2
// Functions from ITextTrackTtmlStyle interface

Core::hresult TextTrackImplementation::SetTtmlStyleOverrides(const string &style) {
    std::unique_lock lockSes{mSessionsMutex};
    {
        std::unique_lock lockCfg{mConfigMutex};
        string oldStyle;
        ReadTtmlStyleOverrides(oldStyle);
        if (style != oldStyle) {
            RaiseOnTtmlStyleOverridesChanged(style);
            WriteTtmlStyleOverrides(style);
        }
    }
    ApplyTtmlStyleOverrides(style);
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::GetTtmlStyleOverrides(string &style) const {
    std::unique_lock lock{mConfigMutex};
    ReadTtmlStyleOverrides(style);
    return Core::ERROR_NONE;
}

void TextTrackImplementation::RaiseOnTtmlStyleOverridesChanged(const string &style) {
    std::unique_lock lock{mNotificationMutex};
    for (const auto callback : mTtmlCallbacks) {
        callback->OnTtmlStyleOverridesChanged(style);
    }
}
#endif

// Functions from ITextTrack interface

Core::hresult TextTrackImplementation::OpenSession(const std::string &iDisplayName, uint32_t &oSessionId) {
    TRACE(Trace::Information, (_T("OpenSession on %s"), iDisplayName.c_str()));
    if (iDisplayName.empty()) {
        return Core::ERROR_GENERAL;
    }
    std::unique_lock lock{mSessionsMutex};
    const auto existingSession =
        std::find_if(mSessions.begin(), mSessions.end(), [&iDisplayName](const auto &elm) { return elm.second.session->getDisplayName() == iDisplayName; });
    if (existingSession != mSessions.end()) {
        oSessionId = existingSession->first;
        existingSession->second.session->start();
        return Core::ERROR_NONE;
    }
    oSessionId = ++mSessionNumber;
    try {
#if TEXTTRACK_WITH_RDKSHELL
        if (!EnsureDisplayIsCreated(iDisplayName)) {
            TRACE(Trace::Error, (_T("failed to use RDKShell to create display")));
            return Core::ERROR_GENERAL;
        }
#endif
        auto newSession = std::make_unique<RenderSession>(mConfiguration, iDisplayName);
        newSession->start();
        mSessions.emplace(oSessionId, SessionInfo{std::move(newSession)});
    } catch (const std::exception &e) {
        TRACE(Trace::Error, (_T("caught exception %s"), e.what()));
    } catch (...) {
        TRACE(Trace::Error, (_T("caught something")));
    }
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::CloseSession(uint32_t iSessionId) {
    TRACE(Trace::Information, (_T("CloseSession %u"), iSessionId));
    std::unique_lock lock{mSessionsMutex};
    auto ses_it = mSessions.find(iSessionId);
    if (ses_it == mSessions.end()) {
        return Core::ERROR_GENERAL;
    }
    ses_it->second.session->mute();
    ses_it->second.session->touchTime();
    // Don't stop the session, as EGL handles restarts really badly
    ses_it->second.session->close();
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::PauseSession(uint32_t iSessionId) {
    std::unique_lock lock{mSessionsMutex};
    auto ses_it = mSessions.find(iSessionId);
    if (ses_it != mSessions.end()) {
        ses_it->second.session->pause();
        return Core::ERROR_NONE;
    }
    return Core::ERROR_GENERAL;
}

Core::hresult TextTrackImplementation::ResumeSession(uint32_t iSessionId) {
    std::unique_lock lock{mSessionsMutex};
    auto ses_it = mSessions.find(iSessionId);
    if (ses_it != mSessions.end()) {
        ses_it->second.session->resume();
        return Core::ERROR_NONE;
    }
    return Core::ERROR_GENERAL;
}

Core::hresult TextTrackImplementation::MuteSession(uint32_t iSessionId) {
    std::unique_lock lock{mSessionsMutex};
    auto ses_it = mSessions.find(iSessionId);
    if (ses_it != mSessions.end()) {
        ses_it->second.session->mute();
        return Core::ERROR_NONE;
    }
    return Core::ERROR_GENERAL;
}

Core::hresult TextTrackImplementation::UnMuteSession(uint32_t iSessionId) {
    std::unique_lock lock{mSessionsMutex};
    auto ses_it = mSessions.find(iSessionId);
    if (ses_it != mSessions.end()) {
        ses_it->second.session->unmute();
        return Core::ERROR_NONE;
    }
    return Core::ERROR_GENERAL;
}

Core::hresult TextTrackImplementation::SendSessionData(uint32_t sessionId, DataType type, int64_t displayOffsetMs, const string &data) {
    constexpr auto fTypeConvert = [](DataType type_) -> RenderSession::DataType {
        switch (type_) {
            case DataType::PES:
                return RenderSession::DataType::PES;
            case DataType::TTML:
                return RenderSession::DataType::TTML;
            case DataType::CC:
                return RenderSession::DataType::CC;
            case DataType::WEBVTT:
                return RenderSession::DataType::WEBVTT;
        }
        return RenderSession::DataType::CC; // Compiler says we need to return something
    };
    std::unique_lock lock{mSessionsMutex};
    auto ses_it = mSessions.find(sessionId);
    if (ses_it == mSessions.end()) {
        return Core::ERROR_GENERAL;
    }
    // displayOffsetMs will not be valid for all types of session
    ses_it->second.session->sendData(fTypeConvert(type), data, displayOffsetMs);
    return Core::ERROR_NONE;
}

Core::hresult TextTrackImplementation::SendSessionTimestamp(uint32_t iSessionId, uint64_t iMediaTimestampMs) {
    std::unique_lock lock{mSessionsMutex};
    auto ses_it = mSessions.find(iSessionId);
    if (ses_it != mSessions.end()) {
        ses_it->second.session->sendTimestamp(iMediaTimestampMs);
        return Core::ERROR_NONE;
    }
    return Core::ERROR_GENERAL;
}

Core::hresult TextTrackImplementation::ApplyCustomClosedCaptionsStyleToSession(uint32_t sessionId, const ClosedCaptionsStyle &style) {
    std::unique_lock lock{mSessionsMutex};
    auto ses_it = mSessions.find(sessionId);
    if (ses_it != mSessions.end()) {
        ses_it->second.session->setCustomCcStyling(convertClosedCaptionsStyle(style));
        return Core::ERROR_NONE;
    }
    return Core::ERROR_GENERAL;
}

Core::hresult TextTrackImplementation::SetPreviewText(uint32_t sessionId, const std::string &text) {
    std::unique_lock lock{mSessionsMutex};
    auto ses_it = mSessions.find(sessionId);
    if (ses_it != mSessions.end()) {
        if (ses_it->second.session->getSessionType() == RenderSession::SessionType::CC) {
            ses_it->second.session->touchTime();
            ses_it->second.session->setTextForClosedCaptionPreview(text);
            return Core::ERROR_NONE;
        } else {
            return Core::ERROR_NOT_SUPPORTED;
        }
    }
    return Core::ERROR_GENERAL;
}

Core::hresult TextTrackImplementation::ResetSession(uint32_t iSessionId) {
    std::unique_lock lock{mSessionsMutex};
    auto ses_it = mSessions.find(iSessionId);
    if (ses_it != mSessions.end()) {
        ses_it->second.session->reset();
        return Core::ERROR_NONE;
    }
    return Core::ERROR_GENERAL;
}

Core::hresult TextTrackImplementation::SetSessionClosedCaptionsService(uint32_t iSessionId, const std::string &service) {
    std::unique_lock lockSes{mSessionsMutex};
    auto ses_it = mSessions.find(iSessionId);
    if (ses_it != mSessions.end()) {
        RenderSession::CcServiceType type;
        uint32_t serviceId = 0;
        bool parsed = true;
        if (service.size() > 7 && service.compare(0, 7, "SERVICE", 7) == 0) {
            type = RenderSession::CcServiceType::CEA708;
            serviceId = std::stoul(service.substr(7));
        } else if (service.size() > 2 && service.compare(0, 2, "CC", 2) == 0) {
            type = RenderSession::CcServiceType::CEA608;
            // service 1 translates to 1000 for subtec
            serviceId = std::stoul(service.substr(2)) + 1000 - 1;
        } else if (service.size() > 2 && service.compare(0, 4, "TEXT", 4) == 0) {
            type = RenderSession::CcServiceType::CEA608;
            // service 1 translates to 1004 for subtec
            serviceId = std::stoul(service.substr(4)) + 1004 - 1;
        } else {
            parsed = false;
        }
        if (parsed) {
            ses_it->second.session->selectCcService(type, serviceId);
            ClosedCaptionsStyle presetStyle;
            {
                std::unique_lock lock{mConfigMutex};
                ReadClosedCaptionsStyle(presetStyle);
            }
            ApplyClosedCaptionsStyle(*ses_it->second.session, convertClosedCaptionsStyle(presetStyle));
            return Core::ERROR_NONE;
        }
    }
    return Core::ERROR_GENERAL;
}

// CC style settings in the format that subttxrend wants them
void TextTrackImplementation::ApplyClosedCaptionsStyle(RenderSession &session, const SubttxClosedCaptionsStyle &style) {
    if (!session.hasCustomCcStyling()) {
        session.applyCcStyling(style);
        session.refreshClosedCaptionPreview();
    }
}

void TextTrackImplementation::ApplyClosedCaptionsStyle(const ClosedCaptionsStyle &style) {
    const SubttxClosedCaptionsStyle subttxStyle = convertClosedCaptionsStyle(style);

    for (const auto &sess : mSessions) {
        ApplyClosedCaptionsStyle(*sess.second.session, subttxStyle);
    }
}

Core::hresult TextTrackImplementation::SetSessionTeletextSelection(uint32_t iSessionId, uint16_t page) {
    std::unique_lock lock{mSessionsMutex};
    auto ses_it = mSessions.find(iSessionId);
    if (ses_it != mSessions.end()) {
        ses_it->second.session->selectTtxService(page);
        return Core::ERROR_NONE;
    }
    return Core::ERROR_GENERAL;
}

Core::hresult TextTrackImplementation::SetSessionDvbSubtitleSelection(uint32_t sessionId, uint16_t compositionPageId, uint16_t ancillaryPageId) {
    std::unique_lock lock{mSessionsMutex};
    auto ses_it = mSessions.find(sessionId);
    if (ses_it != mSessions.end()) {
        ses_it->second.session->selectDvbService(compositionPageId, ancillaryPageId);
        return Core::ERROR_NONE;
    }
    return Core::ERROR_GENERAL;
}

Core::hresult TextTrackImplementation::SetSessionWebVTTSelection(uint32_t iSessionId) {
    std::unique_lock lock{mSessionsMutex};
    auto ses_it = mSessions.find(iSessionId);
    if (ses_it != mSessions.end()) {
        ses_it->second.session->selectWebvttService(1920, 1080);
        return Core::ERROR_NONE;
    }
    return Core::ERROR_GENERAL;
}

Core::hresult TextTrackImplementation::SetSessionTTMLSelection(uint32_t iSessionId) {
    std::unique_lock lockSes{mSessionsMutex};
    auto ses_it = mSessions.find(iSessionId);
    if (ses_it != mSessions.end()) {
        ses_it->second.session->selectTtmlService(1920, 1080);
        if (!ses_it->second.session->hasCustomTtmlStyling()) {
            string style;
            std::unique_lock lockCfg{mConfigMutex};
            ReadTtmlStyleOverrides(style);
            if (!style.empty()) {
                ses_it->second.session->applyTtmlStyling(style);
            }
        }
        return Core::ERROR_NONE;
    }
    return Core::ERROR_GENERAL;
}

#if ITEXTTRACK_VERSION >= 2
Core::hresult TextTrackImplementation::ApplyCustomTtmlStyleOverridesToSession(uint32_t iSessionId, const string &styling) {
    std::unique_lock lock{mSessionsMutex};
    auto ses_it = mSessions.find(iSessionId);
    if (ses_it != mSessions.end()) {
        if (ses_it->second.session->setCustomTtmlStyling(styling)) {
            return Core::ERROR_NONE;
        } else {
            return Core::ERROR_NOT_SUPPORTED;
        }
    }
    return Core::ERROR_GENERAL;
}
#endif

void TextTrackImplementation::ApplyTtmlStyleOverrides(const string &style) {
    // Apply to all TTML sessions, unless they already have a style override
    for (const auto &sess : mSessions) {
        if (!sess.second.session->hasCustomTtmlStyling()) {
            sess.second.session->applyTtmlStyling(style);
        }
    }
}

Core::hresult TextTrackImplementation::SetSessionSCTESelection(uint32_t iSessionId) {
    std::unique_lock lock{mSessionsMutex};
    auto ses_it = mSessions.find(iSessionId);
    if (ses_it != mSessions.end()) {
        ses_it->second.session->selectScteService();
        return Core::ERROR_NONE;
    }
    return Core::ERROR_GENERAL;
}

void TextTrackImplementation::RaiseOnClosedCaptionsStyleChanged(const ClosedCaptionsStyle &style) {
    std::unique_lock lock{mNotificationMutex};
    for (const auto callback : mNotificationCallbacks) {
        callback->OnClosedCaptionsStyleChanged(style);
    }
}

void TextTrackImplementation::RaiseOnFontFamilyChanged(const FontFamily font) {
    std::unique_lock lock{mNotificationMutex};
    for (const auto callback : mNotificationCallbacks) {
        callback->OnFontFamilyChanged(font);
    }
}

void TextTrackImplementation::RaiseOnFontSizeChanged(const FontSize size) {
    std::unique_lock lock{mNotificationMutex};
    for (const auto callback : mNotificationCallbacks) {
        callback->OnFontSizeChanged(size);
    }
}

void TextTrackImplementation::RaiseOnFontColorChanged(const string &color) {
    std::unique_lock lock{mNotificationMutex};
    for (const auto callback : mNotificationCallbacks) {
        callback->OnFontColorChanged(color);
    }
}

void TextTrackImplementation::RaiseOnFontOpacityChanged(const int8_t opacity) {
    std::unique_lock lock{mNotificationMutex};
    for (const auto callback : mNotificationCallbacks) {
        callback->OnFontOpacityChanged(opacity);
    }
}

void TextTrackImplementation::RaiseOnFontEdgeChanged(const FontEdge edge) {
    std::unique_lock lock{mNotificationMutex};
    for (const auto callback : mNotificationCallbacks) {
        callback->OnFontEdgeChanged(edge);
    }
}

void TextTrackImplementation::RaiseOnFontEdgeColorChanged(const string &color) {
    std::unique_lock lock{mNotificationMutex};
    for (const auto callback : mNotificationCallbacks) {
        callback->OnFontEdgeColorChanged(color);
    }
}

void TextTrackImplementation::RaiseOnBackgroundColorChanged(const string &color) {
    std::unique_lock lock{mNotificationMutex};
    for (const auto callback : mNotificationCallbacks) {
        callback->OnBackgroundColorChanged(color);
    }
}

void TextTrackImplementation::RaiseOnBackgroundOpacityChanged(const int8_t opacity) {
    std::unique_lock lock{mNotificationMutex};
    for (const auto callback : mNotificationCallbacks) {
        callback->OnBackgroundOpacityChanged(opacity);
    }
}

void TextTrackImplementation::RaiseOnWindowColorChanged(const string &color) {
    std::unique_lock lock{mNotificationMutex};
    for (const auto callback : mNotificationCallbacks) {
        callback->OnWindowColorChanged(color);
    }
}

void TextTrackImplementation::RaiseOnWindowOpacityChanged(const int8_t opacity) {
    std::unique_lock lock{mNotificationMutex};
    for (const auto callback : mNotificationCallbacks) {
        callback->OnWindowOpacityChanged(opacity);
    }
}

#if TEXTTRACK_WITH_CCHAL
Core::hresult TextTrackImplementation::AssociateVideoDecoder(uint32_t iSessionId, const string &handle) {
    std::unique_lock lock{mSessionsMutex};
    auto ses_it = mSessions.find(iSessionId);
    if (ses_it != mSessions.end()) {
        if (!handle.empty()) {
            if (ses_it->second.session->associateVideoDecoder(handle)) {
                return Core::ERROR_NONE;
            }
        } else {
            ses_it->second.session->dissociateVideoDecoder();
            return Core::ERROR_NONE;
        }
    }
    return Core::ERROR_GENERAL;
}
#endif

// End methods

} // namespace Plugin
} // namespace WPEFramework
