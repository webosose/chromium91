// Copyright 2019 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "media/neva/webos/umedia_info_util_webos_gmp.h"

#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/values.h"

namespace media {

std::string SourceInfoToJson(const std::string& media_id,
                             const struct ums::source_info_t& value) {
  base::DictionaryValue eventInfo;
  base::DictionaryValue sourceInfo;
  std::string res;

  eventInfo.SetStringKey("type", "sourceInfo");
  eventInfo.SetStringKey("mediaId", media_id.c_str());

  sourceInfo.SetStringKey("container", value.container.c_str());
  sourceInfo.SetBoolKey("seekable", value.seekable);
  sourceInfo.SetIntKey("numPrograms", value.programs.size());

  base::ListValue programInfos;
  for (int i = 0; i < value.programs.size(); i++) {
    base::DictionaryValue programInfo;
    programInfo.SetDoubleKey("duration", value.duration);

    int numAudioTracks = 0;
    if (value.programs[i].audio_stream > 0 &&
        value.programs[i].audio_stream < value.audio_streams.size()) {
      numAudioTracks = 1;
    }
    programInfo.SetIntKey("numAudioTracks", numAudioTracks);
    base::ListValue audioTrackInfos;
    for (int j = 0; j < numAudioTracks; j++) {
      base::DictionaryValue audioTrackInfo;
      int asi = value.programs[i].audio_stream;

      audioTrackInfo.SetStringKey("codec",
                                  value.audio_streams[asi].codec.c_str());
      audioTrackInfo.SetIntKey("bitRate", value.audio_streams[asi].bit_rate);
      audioTrackInfo.SetIntKey("sampleRate",
                               value.audio_streams[asi].sample_rate);

      audioTrackInfos.Append(std::move(audioTrackInfo));
    }
    if (numAudioTracks)
      programInfo.SetKey("audioTrackInfo", std::move(audioTrackInfos));

    int numVideoTracks = 0;
    if (value.programs[i].video_stream > 0 &&
        value.programs[i].video_stream < value.video_streams.size()) {
      numVideoTracks = 1;
    }

    base::ListValue videoTrackInfos;
    for (int j = 0; j < numVideoTracks; j++) {
      base::DictionaryValue videoTrackInfo;
      int vsi = value.programs[i].video_stream;

      float frame_rate = ((float)value.video_streams[vsi].frame_rate.num) /
                         ((float)value.video_streams[vsi].frame_rate.den);

      videoTrackInfo.SetStringKey("codec",
                                  value.video_streams[vsi].codec.c_str());
      videoTrackInfo.SetIntKey("width", value.video_streams[vsi].width);
      videoTrackInfo.SetIntKey("height", value.video_streams[vsi].height);
      videoTrackInfo.SetDoubleKey("frameRate", frame_rate);

      videoTrackInfo.SetIntKey("bitRate", value.video_streams[vsi].bit_rate);

      videoTrackInfos.Append(std::move(videoTrackInfo));
    }
    if (numVideoTracks)
      programInfo.SetKey("videoTrackInfo", std::move(videoTrackInfos));

    programInfos.Append(std::move(programInfo));
  }
  sourceInfo.SetKey("programInfo", std::move(programInfos));

  eventInfo.SetKey("info", std::move(sourceInfo));
  if (!base::JSONWriter::Write(eventInfo, &res)) {
    LOG(ERROR) << __func__ << " Failed to write Source info to JSON";
    return std::string();
  }

  VLOG(2) << __func__ << " source_info=" << res;
  return res;
}

// refer to uMediaServer/include/public/dto_type.h
std::string VideoInfoToJson(const std::string& media_id,
                            const struct ums::video_info_t& value) {
  base::DictionaryValue eventInfo;
  base::DictionaryValue videoInfo;
  base::DictionaryValue frameRate;
  std::string res;

  eventInfo.SetStringKey("type", "videoInfo");
  eventInfo.SetStringKey("mediaId", media_id.c_str());

  videoInfo.SetIntKey("width", value.width);
  videoInfo.SetIntKey("height", value.height);
  frameRate.SetIntKey("num", value.frame_rate.num);
  frameRate.SetIntKey("den", value.frame_rate.den);
  videoInfo.SetKey("frameRate", std::move(frameRate));
  videoInfo.SetStringKey("codec", value.codec.c_str());
  videoInfo.SetIntKey("bitRate", value.bit_rate);

  eventInfo.SetKey("info", std::move(videoInfo));
  if (!base::JSONWriter::Write(eventInfo, &res)) {
    LOG(ERROR) << __func__ << " Failed to write Video info to JSON";
    return std::string();
  }

  VLOG(2) << __func__ << " video_info=" << res;
  return res;
}

std::string AudioInfoToJson(const std::string& media_id,
                            const struct ums::audio_info_t& value) {
  base::DictionaryValue eventInfo;
  base::DictionaryValue audioInfo;
  std::string res;

  eventInfo.SetStringKey("type", "audioInfo");
  eventInfo.SetStringKey("mediaId", media_id.c_str());

  audioInfo.SetIntKey("sampleRate", value.sample_rate);
  audioInfo.SetStringKey("codec", value.codec.c_str());
  audioInfo.SetIntKey("bitRate", value.bit_rate);

  eventInfo.SetKey("info", std::move(audioInfo));
  if (!base::JSONWriter::Write(eventInfo, &res)) {
    LOG(ERROR) << __func__ << " Failed to write Audio info to JSON";
    return std::string();
  }

  VLOG(2) << __func__ << " audio_info=" << res;
  return res;
}

}  // namespace media
