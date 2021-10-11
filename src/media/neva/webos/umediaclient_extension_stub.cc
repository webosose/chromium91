// Copyright 2021 LG Electronics, Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/neva/webos/umediaclient_extension_stub.h"

namespace media {

std::unique_ptr<UMediaClientExtension> UMediaClientExtension::Create(
    const base::WeakPtr<UMediaClientImpl>& umedia_client,
    const scoped_refptr<base::SingleThreadTaskRunner>& main_task_runner) {
  return std::make_unique<UMediaClientExtensionStub>();
}

UMediaClientExtensionStub::UMediaClientExtensionStub() = default;

UMediaClientExtensionStub::~UMediaClientExtensionStub() = default;

bool UMediaClientExtensionStub::SendCustomMessage(const std::string& message) {
    return true;
}

}  // namespace media
