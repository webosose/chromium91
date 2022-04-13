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

#ifndef NEVA_BROWSER_SERVICE_BROWSER_SERVICE_H_
#define NEVA_BROWSER_SERVICE_BROWSER_SERVICE_H_

#include "base/component_export.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "neva/browser_service/browser/cookiemanager_service_impl.h"
#include "neva/browser_service/browser/popupblocker_service_impl.h"
#include "neva/browser_service/browser/sitefilter_service_impl.h"

namespace browser {

COMPONENT_EXPORT(BROWSER_SERVICE)
class BrowserService {
public:
static BrowserService* GetBrowserService();
void BindSiteFilterService(
    mojo::PendingReceiver<mojom::SiteFilterService> receiver);
void BindPopupBlockerService(
    mojo::PendingReceiver<mojom::PopupBlockerService> receiver);
void BindCookieManagerService(
    mojo::PendingReceiver<mojom::CookieManagerService> receiver);

private:
  friend struct base::DefaultSingletonTraits<BrowserService>;
  BrowserService(){}
  BrowserService(const BrowserService&) = delete;

  ~BrowserService(){}
};

}  // namespace browser

#endif  // NEVA_BROWSER_SERVICE_BROWSER_SERVICE_H_
