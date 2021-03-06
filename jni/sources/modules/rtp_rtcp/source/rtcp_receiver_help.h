/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_RTP_RTCP_SOURCE_RTCP_RECEIVER_HELP_H_
#define WEBRTC_MODULES_RTP_RTCP_SOURCE_RTCP_RECEIVER_HELP_H_

#include <vector>

#include "modules/rtp_rtcp/interface/rtp_rtcp_defines.h"  // RTCPReportBlock
#include "modules/rtp_rtcp/source/rtcp_utility.h"
#include "modules/rtp_rtcp/source/tmmbr_help.h"
#include "typedefs.h"

namespace webrtc {
namespace RTCPHelp
{

class RTCPPacketInformation
{
public:
    RTCPPacketInformation();
    ~RTCPPacketInformation();

    void AddVoIPMetric(const RTCPVoIPMetric*  metric);

    void AddApplicationData(const WebRtc_UWord8* data,
                            const WebRtc_UWord16 size);

    void AddNACKPacket(const WebRtc_UWord16 packetID);
    void ResetNACKPacketIdArray();

    void AddReportInfo(const WebRtc_UWord8 fractionLost,
                       const WebRtc_UWord16 rtt,
                       const WebRtc_UWord32 extendedHighSeqNum,
                       const WebRtc_UWord32 jitter);

    WebRtc_UWord32  rtcpPacketTypeFlags; // RTCPPacketTypeFlags bit field
    WebRtc_UWord32  remoteSSRC;

    WebRtc_UWord16* nackSequenceNumbers;
    WebRtc_UWord16  nackSequenceNumbersLength;

    WebRtc_UWord8   applicationSubType;
    WebRtc_UWord32  applicationName;
    WebRtc_UWord8*  applicationData;
    WebRtc_UWord16  applicationLength;

    bool            reportBlock;
    WebRtc_UWord8   fractionLost;
    WebRtc_UWord16  roundTripTime;
    WebRtc_UWord32  lastReceivedExtendedHighSeqNum;
    WebRtc_UWord32  jitter;

    WebRtc_UWord32  interArrivalJitter;

    WebRtc_UWord8   sliPictureId;
    WebRtc_UWord64  rpsiPictureId;
    WebRtc_UWord32  receiverEstimatedMaxBitrate;

    uint32_t ntp_secs;
    uint32_t ntp_frac;
    uint32_t rtp_timestamp;

    RTCPVoIPMetric*  VoIPMetric;
};


class RTCPReportBlockInformation
{
public:
    RTCPReportBlockInformation();
    ~RTCPReportBlockInformation();

    // Statistics
    RTCPReportBlock remoteReceiveBlock;
    WebRtc_UWord32        remoteMaxJitter;

    // RTT
    WebRtc_UWord16    RTT;
    WebRtc_UWord16    minRTT;
    WebRtc_UWord16    maxRTT;
    WebRtc_UWord16    avgRTT;
    WebRtc_UWord32    numAverageCalcs;
};

class RTCPReceiveInformation
{
public:
    RTCPReceiveInformation();
    ~RTCPReceiveInformation();

    void VerifyAndAllocateBoundingSet(const WebRtc_UWord32 minimumSize);
    void VerifyAndAllocateTMMBRSet(const WebRtc_UWord32 minimumSize);

    void InsertTMMBRItem(const WebRtc_UWord32 senderSSRC,
                         const RTCPUtility::RTCPPacketRTPFBTMMBRItem& TMMBRItem,
                         const WebRtc_Word64 currentTimeMS);

    // get
    WebRtc_Word32 GetTMMBRSet(const WebRtc_UWord32 sourceIdx,
                              const WebRtc_UWord32 targetIdx,
                              TMMBRSet* candidateSet,
                              const WebRtc_Word64 currentTimeMS);

    WebRtc_Word64 lastTimeReceived;

    // FIR
    WebRtc_Word32 lastFIRSequenceNumber;
    WebRtc_Word64 lastFIRRequest;

    // TMMBN
    TMMBRSet        TmmbnBoundingSet;

    // TMMBR
    TMMBRSet        TmmbrSet;

    bool            readyForDelete;
private:
    std::vector<WebRtc_Word64> _tmmbrSetTimeouts;
};

} // end namespace RTCPHelp
} // namespace webrtc

#endif // WEBRTC_MODULES_RTP_RTCP_SOURCE_RTCP_RECEIVER_HELP_H_
