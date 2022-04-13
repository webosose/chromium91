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

#include "neva/browser_service/browser/cookiemanager_service_impl.h"

#include "base/logging.h"
#include "content/public/browser/browser_context.h"
#include "services/network/cookie_manager.h"

namespace browser {

// static
CookieManagerServiceImpl* CookieManagerServiceImpl::Get() {
  return base::Singleton<CookieManagerServiceImpl>::get();
}

void CookieManagerServiceImpl::SetNetworkCookieManager(
    network::mojom::CookieManager* cookie_manager) {
  network_cookie_manager_ = cookie_manager;
}

void CookieManagerServiceImpl::AddBinding(
    mojo::PendingReceiver<mojom::CookieManagerService> receiver) {
  receivers_.Add(this, std::move(receiver));
}

void CookieManagerServiceImpl::SetCookieOption(
    int32_t option,
    SetCookieOptionCallback callback) {
  // If same cookie_type is set already, return false
  if (static_cast<int>(cookie_option_) == option) {
    VLOG(3) << "This Cookie option is already set!!";
    std::move(callback).Run(false);
    return;
  }
  if (!network_cookie_manager_) {
    LOG(WARNING) << __func__ << "Invalid Cookie Manager Instance";
    std::move(callback).Run(false);
    return;
  }
  switch (static_cast<CookieOption>(option)) {
    case CookieOption::kAllowedAll:
      first_party_cookie_ = true;
      SetThirdPartyCookies(false);
      break;
    case CookieOption::kBlockedAll:
      first_party_cookie_ = false;
      SetThirdPartyCookies(true);
      break;
    case CookieOption::kBlockedThirdParty:
      first_party_cookie_ = true;
      SetThirdPartyCookies(true);
      break;
    default:
      LOG(WARNING) << __func__ << "Invalid Cookie Option!!";
      std::move(callback).Run(false);
      return;
  }
  cookie_option_ = static_cast<CookieOption>(option);
  std::move(callback).Run(true);
}

void CookieManagerServiceImpl::SetThirdPartyCookies(bool is_blocked) {
  if (third_party_cookie_blocked_ == is_blocked)
    return;

  third_party_cookie_blocked_ = is_blocked;
  network_cookie_manager_->BlockThirdPartyCookies(is_blocked);
}

void CookieManagerServiceImpl::ClearAllCookies(
    ClearAllCookiesCallback callback) {
  if (!network_cookie_manager_) {
    LOG(WARNING) << __func__ << "Invalid Cookie Manager Instance";
    std::move(callback).Run(false);
    return;
  }

  auto filter = network::mojom::CookieDeletionFilter::New();
  network_cookie_manager_->DeleteCookies(
      std::move(filter),
      network::mojom::CookieManager::DeleteCookiesCallback());
  std::move(callback).Run(true);
}

void CookieManagerServiceImpl::GetAllCookiesForTesting(
    GetAllCookiesForTestingCallback callback) {
  std::vector<std::string> cookie_list;
  if (!network_cookie_manager_) {
    std::move(callback).Run(cookie_list);
    return;
  }

  get_all_cookies_callback_ = std::move(callback);
  network_cookie_manager_->GetAllCookies(base::BindOnce(
      &CookieManagerServiceImpl::OnGetAllCookies, base::Unretained(this)));
}

void CookieManagerServiceImpl::OnGetAllCookies(const net::CookieList& cookies) {
  std::vector<std::string> cookie_list;
  for (const net::CanonicalCookie& cookie : cookies) {
    std::string cookie_entry_str = "[" + cookie.Domain() + ": " +
                                   cookie.Name() + " / " + cookie.Value() +
                                   "], ";
    cookie_list.push_back(cookie_entry_str);
  }

  // Get and add all cookies to cookie_list;
  std::move(get_all_cookies_callback_).Run(cookie_list);
}

}  // namespace browser