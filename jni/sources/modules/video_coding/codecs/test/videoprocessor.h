/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_VIDEO_CODING_CODECS_TEST_VIDEOPROCESSOR_H_
#define WEBRTC_MODULES_VIDEO_CODING_CODECS_TEST_VIDEOPROCESSOR_H_

#include <string>

#include "common_video/libyuv/include/webrtc_libyuv.h"
#include "common_video/libyuv/include/scaler.h"
#include "modules/interface/module_common_types.h"
#include "modules/video_coding/codecs/interface/video_codec_interface.h"
#include "modules/video_coding/codecs/test/packet_manipulator.h"
#include "modules/video_coding/codecs/test/stats.h"
#include "system_wrappers/interface/tick_util.h"
#include "testsupport/frame_reader.h"
#include "testsupport/frame_writer.h"

namespace webrtc {
namespace test {

// Defines which frame types shall be excluded from packet loss and when.
enum ExcludeFrameTypes {
  // Will exclude the first keyframe in the video sequence from packet loss.
  // Following keyframes will be targeted for packet loss.
  kExcludeOnlyFirstKeyFrame,
  // Exclude all keyframes from packet loss, no matter where in the video
  // sequence they occur.
  kExcludeAllKeyFrames
};
// Returns a string representation of the enum value.
const char* ExcludeFrameTypesToStr(ExcludeFrameTypes e);

// Test configuration for a test run
struct TestConfig {
  TestConfig()
    : name(""), description(""), test_number(0),
      input_filename(""), output_filename(""), output_dir("out"),
      networking_config(), exclude_frame_types(kExcludeOnlyFirstKeyFrame),
      frame_length_in_bytes(-1), use_single_core(false), keyframe_interval(0),
      codec_settings(NULL), verbose(true) {
  };

  // Name of the test. This is purely metadata and does not affect
  // the test in any way.
  std::string name;

  // More detailed description of the test. This is purely metadata and does
  // not affect the test in any way.
  std::string description;

  // Number of this test. Useful if multiple runs of the same test with
  // different configurations shall be managed.
  int test_number;

  // File to process for the test. This must be a video file in the YUV format.
  std::string input_filename;

  // File to write to during processing for the test. Will be a video file
  // in the YUV format.
  std::string output_filename;

  // Path to the directory where encoded files will be put
  // (absolute or relative to the executable). Default: "out".
  std::string output_dir;

  // Configurations related to networking.
  NetworkingConfig networking_config;

  // Decides how the packet loss simulations shall exclude certain frames
  // from packet loss. Default: kExcludeOnlyFirstKeyFrame.
  ExcludeFrameTypes exclude_frame_types;

  // The length of a single frame of the input video file. This value is
  // calculated out of the width and height according to the video format
  // specification. Must be set before processing.
  int frame_length_in_bytes;

  // Force the encoder and decoder to use a single core for processing.
  // Using a single core is necessary to get a deterministic behavior for the
  // encoded frames - using multiple cores will produce different encoded frames
  // since multiple cores are competing to consume the byte budget for each
  // frame in parallel.
  // If set to false, the maximum number of available cores will be used.
  // Default: false.
  bool use_single_core;

  // If set to a value >0 this setting forces the encoder to create a keyframe
  // every Nth frame. Note that the encoder may create a keyframe in other
  // locations in addition to the interval that is set using this parameter.
  // Forcing key frames may also affect encoder planning optimizations in
  // a negative way, since it will suddenly be forced to produce an expensive
  // key frame.
  // Default: 0.
  int keyframe_interval;

  // The codec settings to use for the test (target bitrate, video size,
  // framerate and so on). This struct must be created and filled in using
  // the VideoCodingModule::Codec() method.
  webrtc::VideoCodec* codec_settings;

  // If printing of information to stdout shall be performed during processing.
  bool verbose;
};

// Returns a string representation of the enum value.
const char* VideoCodecTypeToStr(webrtc::VideoCodecType e);

// Handles encoding/decoding of video using the VideoEncoder/VideoDecoder
// interfaces. This is done in a sequential manner in order to be able to
// measure times properly.
// The class processes a frame at the time for the configured input file.
// It maintains state of where in the source input file the processing is at.
//
// Regarding packet loss: Note that keyframes are excluded (first or all
// depending on the ExcludeFrameTypes setting). This is because if key frames
// would be altered, all the following delta frames would be pretty much
// worthless. VP8 has an error-resilience feature that makes it able to handle
// packet loss in key non-first keyframes, which is why only the first is
// excluded by default.
// Packet loss in such important frames is handled on a higher level in the
// Video Engine, where signaling would request a retransmit of the lost packets,
// since they're so important.
//
// Note this class is not thread safe in any way and is meant for simple testing
// purposes.
class VideoProcessor {
 public:
  virtual ~VideoProcessor() {}

  // Performs initial calculations about frame size, sets up callbacks etc.
  // Returns false if an error has occurred, in addition to printing to stderr.
  virtual bool Init() = 0;

  // Processes a single frame. Returns true as long as there's more frames
  // available in the source clip.
  // Frame number must be an integer >=0.
  virtual bool ProcessFrame(int frame_number) = 0;

  // Updates the encoder with the target bit rate and the frame rate.
  virtual void SetRates(int bit_rate, int frame_rate) = 0;

  // Return the size of the encoded frame in bytes. Dropped frames by the
  // encoder are regarded as zero size.
  virtual int EncodedFrameSize() = 0;

  // Return the number of dropped frames.
  virtual int NumberDroppedFrames() = 0;

  // Return the number of spatial resizes.
  virtual int NumberSpatialResizes() = 0;
};

class VideoProcessorImpl : public VideoProcessor {
 public:
  VideoProcessorImpl(webrtc::VideoEncoder* encoder,
                     webrtc::VideoDecoder* decoder,
                     FrameReader* frame_reader,
                     FrameWriter* frame_writer,
                     PacketManipulator* packet_manipulator,
                     const TestConfig& config,
                     Stats* stats);
  virtual ~VideoProcessorImpl();
  virtual bool Init();
  virtual bool ProcessFrame(int frame_number);

 private:
  // Invoked by the callback when a frame has completed encoding.
  void FrameEncoded(webrtc::EncodedImage* encodedImage);
  // Invoked by the callback when a frame has completed decoding.
  void FrameDecoded(const webrtc::VideoFrame& image);
  // Used for getting a 32-bit integer representing time
  // (checks the size is within signed 32-bit bounds before casting it)
  int GetElapsedTimeMicroseconds(const webrtc::TickTime& start,
                                 const webrtc::TickTime& stop);
  // Updates the encoder with the target bit rate and the frame rate.
  void SetRates(int bit_rate, int frame_rate);
  // Return the size of the encoded frame in bytes.
  int EncodedFrameSize();
  // Return the number of dropped frames.
  int NumberDroppedFrames();
  // Return the number of spatial resizes.
  int NumberSpatialResizes();

  webrtc::VideoEncoder* encoder_;
  webrtc::VideoDecoder* decoder_;
  FrameReader* frame_reader_;
  FrameWriter* frame_writer_;
  PacketManipulator* packet_manipulator_;
  const TestConfig& config_;
  Stats* stats_;

  EncodedImageCallback* encode_callback_;
  DecodedImageCallback* decode_callback_;
  // Buffer used for reading the source video file:
  WebRtc_UWord8* source_buffer_;
  // Keep track of the last successful frame, since we need to write that
  // when decoding fails:
  WebRtc_UWord8* last_successful_frame_buffer_;
  webrtc::VideoFrame source_frame_;
  // To keep track of if we have excluded the first key frame from packet loss:
  bool first_key_frame_has_been_excluded_;
  // To tell the decoder previous frame have been dropped due to packet loss:
  bool last_frame_missing_;
  // If Init() has executed successfully.
  bool initialized_;
  int encoded_frame_size_;
  int prev_time_stamp_;
  int num_dropped_frames_;
  int num_spatial_resizes_;
  int last_encoder_frame_width_;
  int last_encoder_frame_height_;
  Scaler scaler_;

  // Statistics
  double bit_rate_factor_;  // multiply frame length with this to get bit rate
  webrtc::TickTime encode_start_;
  webrtc::TickTime decode_start_;

  // Callback class required to implement according to the VideoEncoder API.
  class VideoProcessorEncodeCompleteCallback
    : public webrtc::EncodedImageCallback {
   public:
      explicit VideoProcessorEncodeCompleteCallback(VideoProcessorImpl* vp)
        : video_processor_(vp) {
    }
    WebRtc_Word32 Encoded(
        webrtc::EncodedImage& encoded_image,
        const webrtc::CodecSpecificInfo* codec_specific_info = NULL,
        const webrtc::RTPFragmentationHeader* fragmentation = NULL);

   private:
    VideoProcessorImpl* video_processor_;
  };

  // Callback class required to implement according to the VideoDecoder API.
  class VideoProcessorDecodeCompleteCallback
    : public webrtc::DecodedImageCallback {
   public:
      explicit VideoProcessorDecodeCompleteCallback(VideoProcessorImpl* vp)
      : video_processor_(vp) {
    }
    WebRtc_Word32 Decoded(webrtc::VideoFrame& image);

   private:
    VideoProcessorImpl* video_processor_;
  };
};

}  // namespace test
}  // namespace webrtc

#endif  // WEBRTC_MODULES_VIDEO_CODING_CODECS_TEST_VIDEOPROCESSOR_H_
