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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <subttxrend/common/Logger.hpp>
#include <subttxrend/ctrl/ControllerInterface.hpp>
#include <subttxrend/ctrl/StcProvider.hpp>
#include <subttxrend/gfx/Engine.hpp>
#include <subttxrend/protocol/PacketData.hpp>
#include <subttxrend/protocol/PacketMute.hpp>
#include <subttxrend/protocol/PacketParser.hpp>
#include <subttxrend/protocol/PacketPause.hpp>
#include <subttxrend/protocol/PacketResetAll.hpp>
#include <subttxrend/protocol/PacketResetChannel.hpp>
#include <subttxrend/protocol/PacketResume.hpp>
#include <subttxrend/protocol/PacketSetCCAttributes.hpp>
#include <subttxrend/protocol/PacketTtmlInfo.hpp>
#include <subttxrend/protocol/PacketTtmlSelection.hpp>
#include <subttxrend/protocol/PacketTtmlTimestamp.hpp>
#include <subttxrend/protocol/PacketUnmute.hpp>
#include <subttxrend/protocol/PacketWebvttSelection.hpp>
#include <subttxrend/protocol/PacketWebvttTimestamp.hpp>
#include <subttxrend/socksrc/PacketReceiver.hpp>
#include <subttxrend/socksrc/Source.hpp>
#include <thread>

namespace subttxrend::ctrl {
class Configuration;
}

namespace WPEFramework {
namespace Plugin {

class RenderSession : public subttxrend::socksrc::PacketReceiver {
public:
    enum class SessionType {
        NONE,
        CC,
        TTX,
        DVB,
        WEBVTT,
        TTML,
        SCTE
    };
    enum class DataType {
        PES,
        TTML,
        CC,
        WEBVTT
    };
    enum class CcServiceType {
        CEA608 = 0,
        CEA708 = 1
    };
    RenderSession(subttxrend::ctrl::Configuration &configuration, std::string displayName, std::string socketName);
    RenderSession(subttxrend::ctrl::Configuration &configuration, std::string displayName);
    virtual ~RenderSession();
    RenderSession(const RenderSession &) = delete;
    RenderSession &operator=(const RenderSession &) = delete;

    // Start the resources for rendering
    void start();
    // Close the "safe-to-stop" resources
    void close();
    // Stop the resources for rendering - a restart may not be possible
    void stop();
    std::string getDisplayName() const;
    std::string getSocketName() const;
    void touchTime();
    std::chrono::steady_clock::time_point getLastActiveTime() const;
    bool sendData(DataType type, const std::string &data, int64_t offsetMs);
    void sendTimestamp(uint64_t iMediaTimestampMs);
    void pause();
    void resume();
    void mute();
    void unmute();
    void reset();
    void setCcAttributes(uint32_t fontColor, uint32_t fontOpacity, uint32_t fontStyle, uint32_t fontSize, uint32_t edgeType, uint32_t edgeColor,
                         uint32_t backgroundColor, uint32_t backgroundOpacity, uint32_t windowColor, uint32_t windowOpacity);
    void selectCcService(CcServiceType type, uint32_t iServiceId);
    void selectTtxService(uint16_t page);
    void selectDvbService(uint16_t compositionPageId, uint16_t ancillaryPageId);
    void selectWebvttService(uint32_t iVideoWidth, uint32_t iVideoHeight);
    void selectTtmlService(uint32_t iVideoWidth, uint32_t iVideoHeight);
    void selectScteService();
    void setTextForClosedCaptionPreview(const std::string &text);
    bool isRenderingActive() const;
    SessionType getSessionType() const;
    // Only applies to TTML session
    void setCustomTtmlStyling(const std::string &styling);
    bool hasCustomTtmlStyling() const;

    // Functions from PacketReceiver interface
    virtual void onPacketReceived(const subttxrend::protocol::Packet &packet) override;
    virtual void addBuffer(subttxrend::common::DataBufferPtr buffer) override;
    virtual void onStreamBroken() override;

#if TEXTTRACK_WITH_CCHAL
    bool associateVideoDecoder(const std::string &handle);
    void dissociateVideoDecoder();
#endif

private:
    using LockGuard = std::lock_guard<std::mutex>;
    using UniqueLock = std::unique_lock<std::mutex>;

    void processLoop();
    std::chrono::milliseconds processData();
    bool isDataQueued() const;
    void doOnPacketReceived(const subttxrend::protocol::Packet &packet);
    void processDecoderSelection(const subttxrend::protocol::PacketChannelSpecific &packet);
    void processDataPacket(const subttxrend::protocol::PacketData &packet);
    void processMutePacket(const subttxrend::protocol::PacketMute &packet);
    void processUnmutePacket(const subttxrend::protocol::PacketUnmute &packet);
    void processSetCCAttributes(const subttxrend::protocol::PacketSetCCAttributes &packet);
    void processResetAll(const subttxrend::protocol::PacketResetAll &packet);
    void processResetChannel(const subttxrend::protocol::PacketResetChannel &packet);
    void processTtmlTimestamp(const subttxrend::protocol::PacketTtmlTimestamp &packet);
    void processWebvttTimestamp(const subttxrend::protocol::PacketWebvttTimestamp &packet);
    void processPause(const subttxrend::protocol::PacketPause &packet);
    void processResume(const subttxrend::protocol::PacketResume &packet);
    void processTtmlInfo(const subttxrend::protocol::PacketTtmlInfo &packet);

    SessionType mSessionType = SessionType::NONE;
    subttxrend::common::Logger mLogger;
    subttxrend::ctrl::Configuration &mConfiguration;
    std::string mDisplayName;
    std::string mSocketName;
    bool mStarted = false;
    std::atomic<std::chrono::steady_clock::time_point> mLastActiveTime;
    subttxrend::socksrc::SourcePtr mSocket;
    subttxrend::ctrl::StcProvider mStcProvider;
    std::string mPreviewText;
    bool mHasCustomTtmlStyling = false;
    // Protects mDecoder, ...
    mutable std::mutex mDecoderMutex;
    std::unique_ptr<subttxrend::ctrl::ControllerInterface> mDecoder;
    subttxrend::protocol::PacketParser mParser;
    subttxrend::gfx::EnginePtr mGfxEngine;
    subttxrend::gfx::WindowPtr mGfxWindow;
    std::shared_ptr<subttxrend::gfx::PrerenderedFontCache> mFontCache;
    // Protects mQuitRenderThread, mRenderCond, ...
    mutable std::mutex mRenderMutex;
    bool mQuitRenderThread = false;
    std::thread mRenderThread;
    std::condition_variable mRenderCond;
    // Protects mDataQueue, ...
    mutable std::mutex mDataMutex;
    std::deque<subttxrend::common::DataBufferPtr> mDataQueue;
    bool mHasAssociatedVideoDecoder = false;
};

} // namespace Plugin
} // namespace WPEFramework
