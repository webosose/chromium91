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

#ifndef NEVA_BROWSER_SERVICE_BROWSER_COOKIE_MANAGER_SERVICE_IMPL_H_
#define NEVA_BROWSER_SERVICE_BROWSER_COOKIE_MANAGER_SERVICE_IMPL_H_

#include "base/memory/singleton.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "neva/browser_service/public/mojom/cookiemanager_service.mojom.h"
#include "services/network/public/mojom/cookie_manager.mojom.h"

class GURL;

namespace browser {

class CookieManagerServiceImpl : public mojom::CookieManagerService {
 public:
  static CookieManagerServiceImpl* Get();

  void SetNetworkCookieManager(network::mojom::CookieManager* cookie_manager);
  void AddBinding(mojo::PendingReceiver<mojom::CookieManagerService> receiver);

  // mojom::CookieManagerService impl
  void SetCookieOption(int32_t option,
                       SetCookieOptionCallback callback) override;
  void ClearAllCookies(ClearAllCookiesCallback callback) override;
  void GetAllCookiesForTesting(
      GetAllCookiesForTestingCallback callback) override;

  bool IsCookieEnabled() { return first_party_cookie_; }

 private:
  friend struct base::DefaultSingletonTraits<CookieManagerServiceImpl>;
  enum class CookieOption {
    kAllowedAll = 1,
    kBlockedAll,
    kBlockedThirdParty,
  };

  CookieManagerServiceImpl() = default;
  CookieManagerServiceImpl(const CookieManagerServiceImpl&) = delete;
  CookieManagerServiceImpl& operator=(const CookieManagerServiceImpl&) = delete;
  ~CookieManagerServiceImpl() = default;

  void OnGetAllCookies(const net::CookieList& cookies);
  void SetThirdPartyCookies(bool status);

  bool first_party_cookie_ = true;
  bool third_party_cookie_blocked_ = false;

  mojo::ReceiverSet<mojom::CookieManagerService> receivers_;

  GetAllCookiesForTestingCallback get_all_cookies_callback_;

  network::mojom::CookieManager* network_cookie_manager_ = nullptr;
  CookieOption cookie_option_ = CookieOption::kAllowedAll;
};

}  // namespace browser

#endif  // NEVA_BROWSER_SERVICE_BROWSER_COOKIE_MANAGER_SERVICE_IMPL_H_