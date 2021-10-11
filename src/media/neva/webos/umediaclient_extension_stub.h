// Copyright 2021 LG Electronics, Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_NEVA_WEBOS_UMEDIACLIENT_EXTENSION_STUB_H_
#define MEDIA_NEVA_WEBOS_UMEDIACLIENT_EXTENSION_STUB_H_

#include "media/neva/webos/umediaclient_extension.h"

namespace media {
class UMediaClientExtensionStub : public UMediaClientExtension {
 public:
  UMediaClientExtensionStub();
  ~UMediaClientExtensionStub();

  // Update subtitle information on video resume
  void UpdateSubtitleIfNeeded() override {}
  // Disable subtitle setting on video suspend
  void SuspendSubtitleIfNeeded() override {}
  // Enable subtitle setting on video resume
  void ResumeSubtitleIfNeeded() override {}

  bool SendCustomMessage(const std::string& message) override;
};

}  // namespace media
#endif  // MEDIA_NEVA_WEBOS_UMEDIACLIENT_EXTENSION_STUB_H_
