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

#include "neva/browser_service/browser/popupblocker_service_impl.h"

#include <algorithm>
#include <iostream>
#include <utility>

#include "base/logging.h"
#include "components/url_formatter/url_fixer.h"
#include "extensions/shell/common/switches.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"

namespace browser {

const std::string kPopUpURLTableName = "popup_blocker_urls";
const std::string kWwwPrefix = "www.";

bool ConsiderForPopupBlocking(WindowOpenDisposition disposition) {
  return disposition == WindowOpenDisposition::NEW_POPUP ||
         disposition == WindowOpenDisposition::NEW_FOREGROUND_TAB ||
         disposition == WindowOpenDisposition::NEW_BACKGROUND_TAB ||
         disposition == WindowOpenDisposition::NEW_WINDOW;
}

PopupBlockerServiceImpl* PopupBlockerServiceImpl::GetInstance() {
  return base::Singleton<PopupBlockerServiceImpl>::get();
}

void PopupBlockerServiceImpl::AddBinding(
    mojo::PendingReceiver<mojom::PopupBlockerService> receiver) {
  receivers_.Add(this, std::move(receiver));
}

void PopupBlockerServiceImpl::SetEnabled(bool popup_state,
                                         SetEnabledCallback callback) {
  if (popup_state == popup_blocker_enabled_) {
    std::move(callback).Run(false);
    return;
  }

  popup_blocker_enabled_ = popup_state;
  if (popup_blocker_enabled_) {
    url_list_table_.reset(new URLDatabase(kPopUpURLTableName));
    FillListFromDB();
  } else {
    url_list_.clear();
  }
  std::move(callback).Run(true);
}

bool PopupBlockerServiceImpl::IsBlocked(
    const GURL& url,
    const bool& is_user_gesture,
    const WindowOpenDisposition& disposition) const {
  if (!popup_blocker_enabled_) {
    return false;
  }

  if (url.is_empty() || url.host().empty()) {
    LOG(WARNING) << __func__ << "Empty or Invalid URL !";
    return false;
  }
  if (is_user_gesture || !ConsiderForPopupBlocking(disposition)) {
    return false;
  }

  return !IsURLFound(GetDomain(url.spec()));
}

void PopupBlockerServiceImpl::GetURLs(GetURLsCallback callback) {
  std::vector<std::string> result;
  if (!popup_blocker_enabled_) {
    LOG(ERROR) << __func__ << "Unable to Add, PopUp blocker is OFF !";
    std::move(callback).Run(result);
    return;
  }

  std::vector<std::string> url_list(url_list_.begin(), url_list_.end());
  std::move(callback).Run(url_list);
}

void PopupBlockerServiceImpl::AddURL(const std::string& url,
                                     AddURLCallback callback) {
  if (!popup_blocker_enabled_) {
    LOG(ERROR) << __func__ << "Unable to Add, PopUp blocker is OFF !";
    std::move(callback).Run(false);
    return;
  }
  std::string domain = GetDomain(url);
  if (domain.empty()) {
    LOG(ERROR) << __func__ << "Invalid URL domain";
    std::move(callback).Run(false);
    return;
  }

  if (IsURLFound(domain)) {
    VLOG(3) << __func__ << "URL domain already exists in database";
    std::move(callback).Run(false);
    return;
  }
  if (!url_list_table_->InsertURL(domain)) {
    LOG(ERROR) << __func__ << "Unable to Add URL in DB !";
    std::move(callback).Run(false);
    return;
  }
  url_list_.insert(domain);
  VLOG(3) << __func__ << "URL is added in the popup exception list";
  std::move(callback).Run(true);
}

void PopupBlockerServiceImpl::DeleteURLs(const std::vector<std::string>& urls,
                                         DeleteURLsCallback callback) {
  if (!popup_blocker_enabled_) {
    LOG(ERROR) << __func__ << "Unable to Remove, PopUp blocker is OFF !";
    std::move(callback).Run(false);
    return;
  }

  if (!url_list_table_->DeleteURLs(urls)) {
    LOG(ERROR) << __func__ << "Unable to Remove URLs from DB";
    std::move(callback).Run(false);
    return;
  }

  for (auto& url : urls) {
    url_list_.erase(url);
  }

  VLOG(3) << __func__ << "URLs are removed from the popup exception list";
  std::move(callback).Run(true);
}

void PopupBlockerServiceImpl::updateURL(const std::string& old_url,
                                        const std::string& new_url,
                                        updateURLCallback callback) {
  if (!popup_blocker_enabled_) {
    LOG(ERROR) << __func__ << "Unable to Update, PopUp blocker is OFF !";
    std::move(callback).Run(false);
    return;
  }

  std::string old_url_domain = GetDomain(old_url);
  std::string new_url_domain = GetDomain(new_url);
  if (old_url_domain.empty() || new_url_domain.empty()) {
    LOG(ERROR) << __func__ << "Unable to Update, Empty URL";
    std::move(callback).Run(false);
    return;
  }

  if (!IsURLFound(old_url_domain)) {
    LOG(ERROR) << __func__ << "Invalid old URL domain";
    std::move(callback).Run(false);
    return;
  }
  if (!url_list_table_->ModifyURL(old_url_domain, new_url_domain)) {
    LOG(ERROR) << __func__ << "Unable to update URL in DB";
    std::move(callback).Run(false);
    return;
  }
  url_list_.erase(old_url_domain);
  url_list_.insert(new_url_domain);
  VLOG(3) << __func__ << "URL is modified in the popup exception list";
  std::move(callback).Run(true);
}

PopupBlockerServiceImpl::PopupBlockerServiceImpl() {}

PopupBlockerServiceImpl::~PopupBlockerServiceImpl() {}

void PopupBlockerServiceImpl::FillListFromDB() {
  std::vector<std::string> url_list;
  url_list_table_->GetAllURLs(url_list);
  url_list_.clear();
  if (url_list.empty()) {
    LOG(WARNING) << __func__ << "Can not load url list from from DB";
    return;
  }
  url_list_.insert(url_list.begin(), url_list.end());
}

std::string PopupBlockerServiceImpl::GetDomain(const std::string& url) const {
  GURL decoded_url = url_formatter::FixupURL(url, std::string());
  if (!(decoded_url.is_valid() && decoded_url.IsStandard()))
    return {};
  std::string domain = decoded_url.host();
  if (!domain.find(kWwwPrefix)) {
    domain = domain.substr(kWwwPrefix.length());
  }
  return domain;
}

bool PopupBlockerServiceImpl::IsURLFound(const std::string& url) const {
  return url_list_.find(url) != url_list_.end();
}

}  // namespace browser