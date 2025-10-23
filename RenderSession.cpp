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

#include "RenderSession.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <syscall.h>
#include <tracing/Logging.h>
#include <unistd.h>

#if TEXTTRACK_WITH_CHOWN_DOBBYAPP
#include <pwd.h>
#endif
#if TEXTTRACK_WITH_CCHAL
#include "closedcaption/ccDataReader.h"
#endif

#include <subttxrend/ctrl/CcSubController.hpp>
#include <subttxrend/ctrl/Configuration.hpp>
#include <subttxrend/ctrl/DvbSubController.hpp>
#include <subttxrend/ctrl/ScteSubController.hpp>
#include <subttxrend/ctrl/TtmlController.hpp>
#include <subttxrend/ctrl/TtxController.hpp>
#include <subttxrend/ctrl/WebvttController.hpp>
#include <subttxrend/gfx/Factory.hpp>
#include <subttxrend/protocol/PacketData.hpp>
#include <subttxrend/protocol/PacketMute.hpp>
#include <subttxrend/protocol/PacketPause.hpp>
#include <subttxrend/protocol/PacketResetAll.hpp>
#include <subttxrend/protocol/PacketResetChannel.hpp>
#include <subttxrend/protocol/PacketResume.hpp>
#include <subttxrend/protocol/PacketSetCCAttributes.hpp>
#include <subttxrend/protocol/PacketSubtitleSelection.hpp>
#include <subttxrend/protocol/PacketTeletextSelection.hpp>
#include <subttxrend/protocol/PacketTimestamp.hpp>
#include <subttxrend/protocol/PacketTtmlInfo.hpp>
#include <subttxrend/protocol/PacketTtmlSelection.hpp>
#include <subttxrend/protocol/PacketTtmlTimestamp.hpp>
#include <subttxrend/protocol/PacketUnmute.hpp>
#include <subttxrend/protocol/PacketWebvttSelection.hpp>
#include <subttxrend/protocol/PacketWebvttTimestamp.hpp>
#include <subttxrend/socksrc/UnixSocketSourceFactory.hpp>

namespace WPEFramework {
namespace Plugin {

// A session has a socket, coded as socksrc::UnixSocketSource (own thread)
// That passes data to a socksrc::PacketReceiver (like subttxrend-app's Controller class)
// That can pass data on to actual processing
//   NB: we can get control-packets that way as well as data-packets. The Ctrl might want to perform the same operations as the API functions.

RenderSession::RenderSession(subttxrend::ctrl::Configuration &configuration, std::string displayName, std::string socketName)
    : mLogger("App", "RenderSession", this),
      mConfiguration(configuration),
      mDisplayName(std::move(displayName)),
      mSocketName(std::move(socketName)),
      mLastActiveTime(std::chrono::steady_clock::now()),
      mFontCache{std::make_shared<subttxrend::gfx::PrerenderedFontCache>()} {
    mLogger.osinfo(__LOGGER_FUNC__, " - creating GFX engine for ", mDisplayName);
    mGfxEngine = subttxrend::gfx::Factory::createEngine();
    mGfxEngine->init(mDisplayName);
}

RenderSession::RenderSession(subttxrend::ctrl::Configuration &configuration, std::string displayName)
    : RenderSession(configuration, std::move(displayName), std::string()) {
}

RenderSession::~RenderSession() {
    mLogger.osinfo(__LOGGER_FUNC__, " stops GFX engine");
    mGfxEngine->shutdown();
    mGfxEngine.reset();
}

void RenderSession::start() {
    if (mStarted) {
        return;
    }
    mStarted = true;
    // Create graphics etc
    mLogger.osinfo(__LOGGER_FUNC__, " - creating GFX window");
    mGfxWindow = mGfxEngine->createWindow();
    mGfxEngine->attach(mGfxWindow);

    if (!mSocketName.empty()) {
        mLogger.osinfo(__LOGGER_FUNC__, " - creating socket source ", mSocketName);
        mSocket = subttxrend::socksrc::UnixSocketSourceFactory().create(mSocketName);
        if (!mSocket) {
            mLogger.osfatal(__LOGGER_FUNC__, " - Cannot create socket source");
            throw std::runtime_error("error while creating source");
        }

        mLogger.ostrace(__LOGGER_FUNC__, " - Starting source");
        mSocket->start(this);

#if TEXTTRACK_WITH_CHOWN_DOBBYAPP
        {
            struct passwd user;
            struct passwd *result{nullptr};
            char extra_info[8192];
            mLogger.osinfo(__LOGGER_FUNC__, " - Change owner of socket source to dobbyapp");
            if (getpwnam_r("dobbyapp", &user, extra_info, sizeof(extra_info), &result) == 0 && result != nullptr) {
                // Our challenge now is that creation of the socket is asynchronous in the socket source thread
                // and there is no callback to know when it has been created. Our only choice is to try over and
                // over until we have the socket. This usually happens in ~10ms.
                // If for some reason the socket thread fails, the socket will never be created, so never wait
                // for longer than ~200ms before just moving on with whatever owner the socket may have or not have.
                // Also move on if the error is not ENOENT
                int ret = 0;
                for (int i = 0; i != 20; ++i) {
                    ret = chown(mSocketName.c_str(), user.pw_uid, user.pw_gid);
                    if (ret == 0 || errno != ENOENT) {
                        break;
                    }
                    usleep(10000);
                }
                if (ret != 0) {
                    mLogger.oserror(__LOGGER_FUNC__, " - chown failed, errno=", errno);
                }
            } else {
                mLogger.oserror(__LOGGER_FUNC__, " - Unable to lookup uid of dobbyapp");
            }
        }
#endif
    }

    mLogger.ostrace(__LOGGER_FUNC__, " - Starting render thread");
    {
        LockGuard lock{mRenderMutex};
        mQuitRenderThread = false;
    }
    mRenderThread = std::thread(&RenderSession::processLoop, this);
}

void RenderSession::close() {
#if TEXTTRACK_WITH_CCHAL
    dissociateVideoDecoder();
#endif
}

void RenderSession::stop() {
    mLogger.osinfo(__LOGGER_FUNC__);
    if (!mStarted) {
        return;
    }
    mStarted = false;
    if (!mSocketName.empty() && mSocket) {
        mSocket->stop();
        mSocket.reset();
        unlink(mSocketName.c_str());
    }
#if TEXTTRACK_WITH_CCHAL
    dissociateVideoDecoder();
#endif
    {
        LockGuard lock{mRenderMutex};
        mQuitRenderThread = true;
    }
    mRenderCond.notify_one();
    mLogger.osinfo(__LOGGER_FUNC__, " joins render thread");
    if (mRenderThread.joinable()) {
        mRenderThread.join();
    }
    {
        LockGuard lock{mDataMutex};
        mDataQueue.clear();
    }
    mLogger.osinfo(__LOGGER_FUNC__, " resets decoder");
    {
        LockGuard lock{mDecoderMutex};
        if (mDecoder) {
            mDecoder->deactivate();
            mDecoder.reset();
        }
    }
    mLogger.osinfo(__LOGGER_FUNC__, " detaches GFX window");
    if (mGfxWindow) {
        mGfxEngine->detach(mGfxWindow);
        mGfxWindow.reset();
    }
}

std::string RenderSession::getDisplayName() const {
    return mDisplayName;
}

std::string RenderSession::getSocketName() const {
    return mSocketName;
}

RenderSession::SessionType RenderSession::getSessionType() const {
    return mSessionType;
}

void RenderSession::touchTime() {
    mLastActiveTime = std::chrono::steady_clock::now();
}

std::chrono::steady_clock::time_point RenderSession::getLastActiveTime() const {
    return mLastActiveTime;
}

namespace {
struct BuildPacket {
    static std::atomic<uint32_t> sCounter;
    subttxrend::common::DataBufferPtr pBuffer;
    BuildPacket(subttxrend::protocol::Packet::Type type) : pBuffer(new subttxrend::common::DataBuffer) {
        this->operator()(static_cast<uint32_t>(type))(++sCounter)(0)(1);
    }
    BuildPacket &operator()(uint32_t v) {
        pBuffer->insert(pBuffer->end(), reinterpret_cast<char *>(&v), reinterpret_cast<char *>(&v) + sizeof(v));
        return *this;
    }
    BuildPacket &type(subttxrend::protocol::Packet::Type type) {
        if (pBuffer->size() >= 4) {
            *reinterpret_cast<uint32_t *>(pBuffer->data() + 0) = static_cast<uint32_t>(type);
        }
        return *this;
    }
    void done() {
        if (pBuffer->size() >= 12) {
            *reinterpret_cast<uint32_t *>(pBuffer->data() + 8) = pBuffer->size() - 12;
        }
    }
    operator subttxrend::common::DataBufferPtr() {
        done();
        return std::move(pBuffer);
    }
};
std::atomic<uint32_t> BuildPacket::sCounter{0};
} // namespace

bool RenderSession::sendData(DataType type, const std::string &data, int64_t offsetMs) {
    mLogger.ostrace(__LOGGER_FUNC__, " data is ", data.size(), " bytes, type ", static_cast<unsigned>(type));
    using namespace subttxrend::common;
    using namespace subttxrend::protocol;
    BuildPacket bp(Packet::Type::INVALID);
    switch (type) {
        case DataType::PES: {
            bp.type(Packet::Type::PES_DATA);
            bp(0); // TODO channeltype
            break;
        }
        case DataType::TTML: {
            bp.type(Packet::Type::TTML_DATA);
            // subttxrend-ttml interprets positive as "later"
            bp(offsetMs & UINT32_MAX)(offsetMs >> 32); // 64-bit display-offset
            break;
        }
        case DataType::WEBVTT: {
            bp.type(Packet::Type::WEBVTT_DATA);
            // subttxrend-webvtt interprets positive as "earlier". We flip that.
            offsetMs = -offsetMs;
            bp(offsetMs & UINT32_MAX)(offsetMs >> 32); // 64-bit display-offset
            break;
        }
        case DataType::CC: {
            bp.type(Packet::Type::CC_DATA);
            bp(3 /*channel type*/)(0 /*no pts*/)(0 /*pts*/);
            break;
        }
        default:
            mLogger.osinfo(__LOGGER_FUNC__, " bad type");
            return false;
    }
    std::copy(data.begin(), data.end(), std::back_inserter(*bp.pBuffer));
    addBuffer(bp);
    return true;
}

// Generally, all packets are sent as Packet (except Data, which is buffer)

void RenderSession::sendTimestamp(uint64_t iMediaTimestampMs) {
    switch (mSessionType) {
        case SessionType::WEBVTT: {
            BuildPacket bp(subttxrend::protocol::Packet::Type::WEBVTT_TIMESTAMP);
            bp(iMediaTimestampMs & 0xffffffff)(iMediaTimestampMs >> 32);
            onPacketReceived(mParser.parse(bp));
            break;
        }
        case SessionType::TTML: {
            BuildPacket bp(subttxrend::protocol::Packet::Type::TTML_TIMESTAMP);
            bp(iMediaTimestampMs & 0xffffffff)(iMediaTimestampMs >> 32);
            onPacketReceived(mParser.parse(bp));
            break;
        }
        default:; // Unhandled
    }
}

void RenderSession::pause() {
    BuildPacket bp(subttxrend::protocol::Packet::Type::PAUSE);
    onPacketReceived(mParser.parse(bp));
}

void RenderSession::resume() {
    BuildPacket bp(subttxrend::protocol::Packet::Type::RESUME);
    onPacketReceived(mParser.parse(bp));
}

void RenderSession::reset() {
    close();
    BuildPacket bp(subttxrend::protocol::Packet::Type::RESET_CHANNEL);
    onPacketReceived(mParser.parse(bp));
}

void RenderSession::mute() {
    BuildPacket bp(subttxrend::protocol::Packet::Type::MUTE);
    onPacketReceived(mParser.parse(bp));
}

void RenderSession::unmute() {
    BuildPacket bp(subttxrend::protocol::Packet::Type::UNMUTE);
    onPacketReceived(mParser.parse(bp));
}

void RenderSession::setCcAttributes(uint32_t fontColor, uint32_t fontOpacity, uint32_t fontStyle, uint32_t fontSize, uint32_t edgeType, uint32_t edgeColor,
                                    uint32_t backgroundColor, uint32_t backgroundOpacity, uint32_t windowColor, uint32_t windowOpacity) {
    BuildPacket bp(subttxrend::protocol::Packet::Type::SET_CC_ATTRIBUTES);
    bp(1); // CC type, appears to be unused
    // Attribute mask for which attributes are set. We always set (almost) all of them.
    static const uint32_t attributeMask = static_cast<uint32_t>(subttxrend::protocol::PacketSetCCAttributes::CcAttribType::FONT_COLOR) |
                                          static_cast<uint32_t>(subttxrend::protocol::PacketSetCCAttributes::CcAttribType::BACKGROUND_COLOR) |
                                          static_cast<uint32_t>(subttxrend::protocol::PacketSetCCAttributes::CcAttribType::FONT_OPACITY) |
                                          static_cast<uint32_t>(subttxrend::protocol::PacketSetCCAttributes::CcAttribType::BACKGROUND_OPACITY) |
                                          static_cast<uint32_t>(subttxrend::protocol::PacketSetCCAttributes::CcAttribType::FONT_STYLE) |
                                          static_cast<uint32_t>(subttxrend::protocol::PacketSetCCAttributes::CcAttribType::FONT_SIZE) |
                                          static_cast<uint32_t>(subttxrend::protocol::PacketSetCCAttributes::CcAttribType::FONT_ITALIC) |
                                          static_cast<uint32_t>(subttxrend::protocol::PacketSetCCAttributes::CcAttribType::FONT_UNDERLINE) |
                                          static_cast<uint32_t>(subttxrend::protocol::PacketSetCCAttributes::CcAttribType::WIN_COLOR) |
                                          static_cast<uint32_t>(subttxrend::protocol::PacketSetCCAttributes::CcAttribType::WIN_OPACITY) |
                                          static_cast<uint32_t>(subttxrend::protocol::PacketSetCCAttributes::CcAttribType::EDGE_TYPE) |
                                          static_cast<uint32_t>(subttxrend::protocol::PacketSetCCAttributes::CcAttribType::EDGE_COLOR);
    bp(attributeMask);
    bp(fontColor);         // PacketSetCCAttributes::CcAttribType::FONT_COLOR,
    bp(backgroundColor);   // PacketSetCCAttributes::CcAttribType::BACKGROUND_COLOR,
    bp(fontOpacity);       // PacketSetCCAttributes::CcAttribType::FONT_OPACITY,
    bp(backgroundOpacity); // PacketSetCCAttributes::CcAttribType::BACKGROUND_OPACITY,
    bp(fontStyle);         // PacketSetCCAttributes::CcAttribType::FONT_STYLE,
    bp(fontSize);          // PacketSetCCAttributes::CcAttribType::FONT_SIZE,
    bp(-1);                // PacketSetCCAttributes::CcAttribType::FONT_ITALIC,
    bp(-1);                // PacketSetCCAttributes::CcAttribType::FONT_UNDERLINE,
    bp(-1);                // PacketSetCCAttributes::CcAttribType::BORDER_TYPE,
    bp(0xff000000);        // PacketSetCCAttributes::CcAttribType::BORDER_COLOR,
    bp(windowColor);       // PacketSetCCAttributes::CcAttribType::WIN_COLOR,
    bp(windowOpacity);     // PacketSetCCAttributes::CcAttribType::WIN_OPACITY,
    bp(edgeType);          // PacketSetCCAttributes::CcAttribType::EDGE_TYPE,
    bp(edgeColor);         // PacketSetCCAttributes::CcAttribType::EDGE_COLOR
    onPacketReceived(mParser.parse(bp));
    {
        LockGuard lock{mDecoderMutex};
        // If we have a preview text, refresh it to make the style take effect
        if (!mPreviewText.empty() && mDecoder && mSessionType == SessionType::CC) {
            if (auto *const ccDecoder = static_cast<subttxrend::ctrl::CcSubController *>(mDecoder.get())) {
                ccDecoder->setTextForPreview(mPreviewText);
            }
        }
    }
}

void RenderSession::selectCcService(CcServiceType type, uint32_t serviceId) {
    BuildPacket bp(subttxrend::protocol::Packet::Type::SUBTITLE_SELECTION);
    bp(subttxrend::protocol::PacketSubtitleSelection::SUBTITLES_TYPE_CC)(static_cast<uint32_t>(type))(serviceId);
    onPacketReceived(mParser.parse(bp));
}

void RenderSession::selectTtxService(uint16_t page) {
    const uint32_t ttxMagazine = page >= 800 ? 0 : page / 100;
    const uint32_t ttxPage = page % 100;
    BuildPacket bp(subttxrend::protocol::Packet::Type::SUBTITLE_SELECTION);
    bp(subttxrend::protocol::PacketSubtitleSelection::SUBTITLES_TYPE_TELETEXT)(ttxMagazine)(ttxPage);
    onPacketReceived(mParser.parse(bp));
}

void RenderSession::selectDvbService(uint16_t compositionPageId, uint16_t ancillaryPageId) {
    mLogger.osinfo(__LOGGER_FUNC__, " unimplemented");
    // BuildPacket bp(subttxrend::protocol::Packet::Type::WEBVTT_SELECTION);
    // bp(iVideoWidth)(iVideoHeight);
    // onPacketReceived(mParser.parse(bp));
}

void RenderSession::selectWebvttService(uint32_t iVideoWidth, uint32_t iVideoHeight) {
    BuildPacket bp(subttxrend::protocol::Packet::Type::WEBVTT_SELECTION);
    bp(iVideoWidth)(iVideoHeight);
    onPacketReceived(mParser.parse(bp));
}

void RenderSession::selectTtmlService(uint32_t iVideoWidth, uint32_t iVideoHeight) {
    BuildPacket bp(subttxrend::protocol::Packet::Type::TTML_SELECTION);
    bp(iVideoWidth)(iVideoHeight);
    onPacketReceived(mParser.parse(bp));
}

void RenderSession::selectScteService() {
    mLogger.osinfo(__LOGGER_FUNC__, " unimplemented");
}

void RenderSession::setTextForClosedCaptionPreview(const std::string &text) {
    UniqueLock lock{mDecoderMutex};
    mLogger.osinfo(__LOGGER_FUNC__, " mDecoder=", mDecoder.get(), " mSessionType=", static_cast<int>(mSessionType));
    mPreviewText = text;
    if (mDecoder && mSessionType == SessionType::CC) {
        if (auto *const ccDecoder = static_cast<subttxrend::ctrl::CcSubController *>(mDecoder.get())) {
            ccDecoder->setTextForPreview(text);
        }
    }
}

void RenderSession::setCustomTtmlStyling(const std::string &styling) {
    UniqueLock lock{mDecoderMutex};
    mLogger.osinfo(__LOGGER_FUNC__, " styling=", styling, " mSessionType=", static_cast<int>(mSessionType));
    if (mDecoder && mSessionType == SessionType::TTML) {
        auto *const ttmlDecoder = static_cast<subttxrend::ctrl::TtmlController *>(mDecoder.get());
        ttmlDecoder->setCustomTtmlStyling(styling);
        mHasCustomTtmlStyling = !styling.empty();
    }
}

bool RenderSession::hasCustomTtmlStyling() const {
    return mHasCustomTtmlStyling;
}

// Thread function
void RenderSession::processLoop() {
    UniqueLock lock(mRenderMutex);
    while (!mQuitRenderThread) {
        mRenderCond.wait(lock, [this]() { return mQuitRenderThread || (isRenderingActive() && isDataQueued()); });

        while (!mQuitRenderThread && isRenderingActive()) {
            const auto processWaitTime = processData();
            mGfxEngine->execute();

            if (processWaitTime == std::chrono::milliseconds::zero()) {
                break;
            }
            mRenderCond.wait_for(lock, processWaitTime, [this]() { return mQuitRenderThread; });
        }
        mLogger.osdebug(__LOGGER_FUNC__, " no active controller, clearing the data queue");
        LockGuard datalock{mDataMutex};
        mDataQueue.clear();
    }
}

void RenderSession::onPacketReceived(const subttxrend::protocol::Packet &packet) {
    doOnPacketReceived(packet);
    mRenderCond.notify_one();
}

void RenderSession::addBuffer(subttxrend::common::DataBufferPtr buffer) {
    if (isRenderingActive()) {
        {
            LockGuard lock{mDataMutex};
            mDataQueue.emplace_back(std::move(buffer));
        }
        mRenderCond.notify_one();
    }
}

void RenderSession::onStreamBroken() {
    mLogger.oserror(__LOGGER_FUNC__, " something wrong with the stream");
}

std::chrono::milliseconds RenderSession::processData() {
    while (true) {
        subttxrend::common::DataBufferPtr buffer;
        {
            LockGuard lock{mDataMutex};
            if (!mDataQueue.empty()) {
                buffer = std::move(mDataQueue.front());
                mDataQueue.pop_front();
            } else {
                break;
            }
        }
        if (buffer) {
            const auto &packet = mParser.parse(std::move(buffer));
            doOnPacketReceived(packet);
        }
    }
    {
        LockGuard lock{mDecoderMutex};
        if (mDecoder) {
            mDecoder->process();
            return mDecoder->getWaitTime();
        }
        return std::chrono::milliseconds::zero();
    }
}

void RenderSession::doOnPacketReceived(const subttxrend::protocol::Packet &packet) {
    using namespace subttxrend;

    LockGuard lock{mDecoderMutex};
    touchTime();
    switch (packet.getType()) {
        case protocol::Packet::Type::SUBTITLE_SELECTION:
        case protocol::Packet::Type::TELETEXT_SELECTION:
        case protocol::Packet::Type::TTML_SELECTION:
        case protocol::Packet::Type::WEBVTT_SELECTION: {
            processDecoderSelection(static_cast<const protocol::PacketChannelSpecific &>(packet));
            break;
        }
        case protocol::Packet::Type::PES_DATA:
        case protocol::Packet::Type::TTML_DATA:
        case protocol::Packet::Type::WEBVTT_DATA:
        case protocol::Packet::Type::CC_DATA: {
            processDataPacket(static_cast<const protocol::PacketData &>(packet));
            break;
        }
        case protocol::Packet::Type::RESET_ALL: {
            processResetAll(static_cast<const protocol::PacketResetAll &>(packet));
            break;
        }
        case protocol::Packet::Type::RESET_CHANNEL: {
            processResetChannel(static_cast<const protocol::PacketResetChannel &>(packet));
            break;
        }
        case protocol::Packet::Type::TIMESTAMP: {
            const auto &timestampPacket = static_cast<const protocol::PacketTimestamp &>(packet);
            mStcProvider.processTimestamp(timestampPacket.getStc(), timestampPacket.getTimestamp());
            break;
        }
        case protocol::Packet::Type::TTML_TIMESTAMP: {
            processTtmlTimestamp(static_cast<const protocol::PacketTtmlTimestamp &>(packet));
            break;
        }
        case protocol::Packet::Type::WEBVTT_TIMESTAMP: {
            processWebvttTimestamp(static_cast<const protocol::PacketWebvttTimestamp &>(packet));
            break;
        }
        case protocol::Packet::Type::PAUSE: {
            processPause(static_cast<const protocol::PacketPause &>(packet));
            break;
        }
        case protocol::Packet::Type::RESUME: {
            processResume(static_cast<const protocol::PacketResume &>(packet));
            break;
        }
        case protocol::Packet::Type::MUTE: {
            processMutePacket(static_cast<const protocol::PacketMute &>(packet));
            break;
        }
        case protocol::Packet::Type::UNMUTE: {
            processUnmutePacket(static_cast<const protocol::PacketUnmute &>(packet));
            break;
        }
        case protocol::Packet::Type::TTML_INFO: {
            processTtmlInfo(static_cast<const protocol::PacketTtmlInfo &>(packet));
            break;
        }
        case protocol::Packet::Type::SET_CC_ATTRIBUTES: {
            processSetCCAttributes(static_cast<const protocol::PacketSetCCAttributes &>(packet));
            break;
        }
        default: {
            mLogger.oserror(__LOGGER_FUNC__, " - Invalid packet type (type: ", static_cast<unsigned int>(packet.getType()), ")");
            break;
        }
    }
}

void RenderSession::processDecoderSelection(const subttxrend::protocol::PacketChannelSpecific &packet) {
    using namespace subttxrend;
    if (mDecoder) {
        mDecoder->deactivate();
    }
    switch (packet.getType()) {
        case protocol::Packet::Type::SUBTITLE_SELECTION: {
            const uint32_t subtitleType = static_cast<const protocol::PacketSubtitleSelection &>(packet).getSubtitlesType();
            switch (subtitleType) {
                case protocol::PacketSubtitleSelection::SUBTITLES_TYPE_DVB:
                    mDecoder.reset(new ctrl::DvbSubController(packet, mGfxWindow, mGfxEngine, mStcProvider));
                    mSessionType = SessionType::DVB;
                    break;
                case protocol::PacketSubtitleSelection::SUBTITLES_TYPE_SCTE:
                    mDecoder.reset(new ctrl::ScteSubController(packet, mGfxWindow, mStcProvider));
                    mSessionType = SessionType::SCTE;
                    break;
                case protocol::PacketSubtitleSelection::SUBTITLES_TYPE_CC:
                    mFontCache = std::make_shared<subttxrend::gfx::PrerenderedFontCache>();
                    mDecoder.reset(new ctrl::CcSubController(packet, mGfxWindow, mFontCache));
                    mSessionType = SessionType::CC;
                    break;
                case protocol::PacketSubtitleSelection::SUBTITLES_TYPE_TELETEXT:
                    mDecoder.reset(new ctrl::TtxController(packet, mConfiguration.getTeletextConfig(), mGfxWindow, mGfxEngine, mStcProvider));
                    mSessionType = SessionType::TTX;
                    break;
                default:
                    mLogger.oserror(__LOGGER_FUNC__, " unknown subtitle type=", subtitleType);
                    mDecoder.reset();
                    break;
            }
            break;
        }
        case protocol::Packet::Type::TELETEXT_SELECTION:
            mDecoder.reset(new ctrl::TtxController(packet, mConfiguration.getTeletextConfig(), mGfxWindow, mGfxEngine, mStcProvider));
            mSessionType = SessionType::TTX;
            break;
        case protocol::Packet::Type::TTML_SELECTION:
            mDecoder.reset(new ctrl::TtmlController(packet, mConfiguration.getTtmlConfig(), mGfxWindow, {}));
            mSessionType = SessionType::TTML;
            break;
        case protocol::Packet::Type::WEBVTT_SELECTION:
            mDecoder.reset(new ctrl::WebvttController(packet, mConfiguration.getWebvttConfig(), mGfxWindow));
            mSessionType = SessionType::WEBVTT;
            break;
        default:
            mLogger.oserror(__LOGGER_FUNC__, " unknown subtitle selection type=", packet.getType());
            mDecoder.reset();
            break;
    }
    mLogger.osinfo("DecoderSelection ends mDecoder=", mDecoder.get(), " mSessionType=", static_cast<int>(mSessionType));
}

void RenderSession::processDataPacket(const subttxrend::protocol::PacketData &packet) {
    if (mDecoder) {
        mDecoder->addData(packet);
    }
}

void RenderSession::processMutePacket(const subttxrend::protocol::PacketMute &packet) {
    if (mDecoder) {
        mDecoder->mute(true);
    }
}

void RenderSession::processUnmutePacket(const subttxrend::protocol::PacketUnmute &packet) {
    mLogger.osinfo("Unmute mDecoder=", mDecoder.get(), " mSessionType=", static_cast<int>(mSessionType));
    if (mDecoder) {
        mDecoder->mute(false);
    }
}

void RenderSession::processSetCCAttributes(const subttxrend::protocol::PacketSetCCAttributes &packet) {
    if (mDecoder && mSessionType == SessionType::CC) {
        mDecoder->processSetCCAttributesPacket(packet);
    }
}

void RenderSession::processResetAll(const subttxrend::protocol::PacketResetAll &packet) {
    if (mDecoder) {
        {
            LockGuard lock{mDataMutex};
            mDataQueue.clear();
        }
        mDecoder->deactivate();
        mDecoder.reset();
    }
}

void RenderSession::processResetChannel(const subttxrend::protocol::PacketResetChannel &packet) {
    if (mDecoder) {
        if (mDecoder->wantsData(packet)) {
            mDecoder->deactivate();
            mDecoder.reset();
        }
    }
}

void RenderSession::processPause(const subttxrend::protocol::PacketPause &packet) {
    if (mDecoder) {
        mDecoder->pause();
    }
}

void RenderSession::processResume(const subttxrend::protocol::PacketResume &packet) {
    if (mDecoder) {
        mDecoder->resume();
    }
}

void RenderSession::processTtmlTimestamp(const subttxrend::protocol::PacketTtmlTimestamp &packet) {
    if (mDecoder) {
        mDecoder->processTimestamp(packet);
    }
}

void RenderSession::processWebvttTimestamp(const subttxrend::protocol::PacketWebvttTimestamp &packet) {
    if (mDecoder) {
        mDecoder->processTimestamp(packet);
    }
}

void RenderSession::processTtmlInfo(const subttxrend::protocol::PacketTtmlInfo &packet) {
    if (mDecoder) {
        mDecoder->processInfo(packet);
    }
}

bool RenderSession::isRenderingActive() const {
    LockGuard lock{mDecoderMutex};
    return mDecoder.get() != nullptr;
}

bool RenderSession::isDataQueued() const {
    LockGuard lock{mDataMutex};
    return !mDataQueue.empty();
}

#if TEXTTRACK_WITH_CCHAL
extern "C" {
// The callbacks we need for CC HAL
void dataCallback(void *context, int decoderIndex, VL_CC_DATA_TYPE eType, unsigned char *ccData, unsigned dataLength, int sequenceNumber, long long localPts) {
    if (dataLength > 0) {
        reinterpret_cast<RenderSession *>(context)->sendData(RenderSession::DataType::CC, std::string(reinterpret_cast<char *>(ccData), dataLength), 0);
    }
}
void decodeCallback(void *context, int decoderIndex, int event) {
}
}

bool RenderSession::associateVideoDecoder(const std::string &handle) {
    if (!mHasAssociatedVideoDecoder) {
        if (vlhal_cc_Register(0, this, dataCallback, decodeCallback) != 0) {
            return false;
        }
        mHasAssociatedVideoDecoder = true;
    } else {
        media_closeCaptionStop();
    }

    {
        uintptr_t value = 0;
        std::stringstream iss(handle);
        iss >> value;
        if (media_closeCaptionStart(reinterpret_cast<void *>(value)) == 0) {
            return true;
        }
    }
    return false;
}

void RenderSession::dissociateVideoDecoder() {
    if (mHasAssociatedVideoDecoder) {
        media_closeCaptionStop();
        vlhal_cc_Register(0, this, nullptr, nullptr);
        mHasAssociatedVideoDecoder = false;
    }
}
#endif

} // namespace Plugin
} // namespace WPEFramework
