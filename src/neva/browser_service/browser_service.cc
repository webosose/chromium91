// Copyright 2022 LG Electronics, Inc.
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

#include "neva/browser_service/browser_service.h"

namespace browser {

BrowserService* BrowserService::GetBrowserService() {
  return base::Singleton<BrowserService>::get();
}

void BrowserService::BindCookieManagerService(
    mojo::PendingReceiver<mojom::CookieManagerService> receiver) {
  browser::CookieManagerServiceImpl::Get()->AddBinding(std::move(receiver));
}

void BrowserService::BindPopupBlockerService(
    mojo::PendingReceiver<mojom::PopupBlockerService> receiver) {
  browser::PopupBlockerServiceImpl::GetInstance()->AddBinding(
      std::move(receiver));
}

void BrowserService::BindSiteFilterService(
    mojo::PendingReceiver<mojom::SiteFilterService> receiver) {
  browser::SiteFilterServiceImpl::Get()->AddBinding(std::move(receiver));
}

}  // namespace browser
