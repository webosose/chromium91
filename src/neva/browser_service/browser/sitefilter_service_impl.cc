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

#include "neva/browser_service/browser/sitefilter_service_impl.h"

#include <algorithm>
#include <iostream>
#include <utility>

#include "base/logging.h"
#include "components/url_formatter/url_fixer.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"

namespace browser {

const std::string kAllowURLTableName = "allowed_urls";
const std::string kBlockURLTableName = "blocked_urls";
const std::string kWwwPrefix = "www.";

SiteFilterServiceImpl* SiteFilterServiceImpl::Get() {
  return base::Singleton<SiteFilterServiceImpl>::get();
}

bool SiteFilterServiceImpl::IsBlocked(const GURL& url, bool is_redirect) {
  if (type_ == Type::kDisabled) {
    VLOG(3) << "Site Filter type is OFF !";
    return false;
  }

  // If the current URL is redirected and the filter type is ALLOWED then no
  // need to check for the filter list as the original URL would have been
  // already allowed. Hence the URL should also be allowed.
  if (is_redirect && (type_ == Type::kApproved)) {
    return false;
  }
  if (url.is_empty() || url.host().empty()) {
    LOG(WARNING) << __func__ << "Empty or Invalid URL !";
    return false;
  }
  switch (type_) {
    case Type::kApproved:
      return !IsURLFound(GetDomain(url.spec()));
    case Type::kBlocked:
      return IsURLFound(GetDomain(url.spec()));
    default:
      LOG(WARNING) << __func__ << "Invalid Site Filter type !";
      break;
  }
  return false;
}

void SiteFilterServiceImpl::AddBinding(
    mojo::PendingReceiver<mojom::SiteFilterService> receiver) {
  receivers_.Add(this, std::move(receiver));
}

void SiteFilterServiceImpl::SetType(int32_t filter_type,
                                    SetTypeCallback callback) {
  Type type = static_cast<Type>(filter_type);
  if (type_ == type) {
    std::move(callback).Run(false);
    return;
  }

  type_ = type;
  if (type_ != Type::kDisabled) {
    url_list_table_.reset(new URLDatabase(GetTableName()));
    FillListFromDB();
  } else {
    url_list_.clear();
  }

  std::move(callback).Run(true);
}

void SiteFilterServiceImpl::GetURLs(GetURLsCallback callback) {
  std::vector<std::string> result;
  if (type_ == Type::kDisabled) {
    VLOG(3) << __func__ << " Site filter type is OFF !";
    std::move(callback).Run(result);
    return;
  }

  std::vector<std::string> url_list(url_list_.begin(), url_list_.end());
  std::sort(url_list.begin(), url_list.end());

  std::move(callback).Run(url_list);
}

void SiteFilterServiceImpl::AddURL(const std::string& url,
                                   AddURLCallback callback) {
  if (type_ == Type::kDisabled) {
    LOG(ERROR) << __func__ << " Site Filter type is OFF !";
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

  std::move(callback).Run(true);
}

void SiteFilterServiceImpl::DeleteURLs(const std::vector<std::string>& urls,
                                       DeleteURLsCallback callback) {
  if (type_ == Type::kDisabled) {
    LOG(ERROR) << __func__ << " Site Filter type is OFF !";
    std::move(callback).Run(false);
    return;
  }

  if (!url_list_table_->DeleteURLs(urls)) {
    LOG(ERROR) << __func__ << " Unable to Remove URLs from DB";
    std::move(callback).Run(false);
    return;
  }

  for (const auto& url : urls) {
    url_list_.erase(url);
  }

  std::move(callback).Run(true);
}

void SiteFilterServiceImpl::UpdateURL(const std::string& old_url,
                                      const std::string& new_url,
                                      UpdateURLCallback callback) {
  if (type_ == Type::kDisabled) {
    LOG(ERROR) << __func__ << " Site Filter type is OFF !";
    std::move(callback).Run(false);
    return;
  }

  std::string old_domain = GetDomain(old_url);
  std::string new_domain = GetDomain(new_url);
  if (old_domain.empty() || new_domain.empty()) {
    LOG(ERROR) << __func__ << "Unable to Update, Empty URL";
    std::move(callback).Run(false);
    return;
  }

  if (!IsURLFound(old_domain)) {
    LOG(ERROR) << __func__ << "Invalid old URL domain";
    std::move(callback).Run(false);
    return;
  }
  if (!url_list_table_->ModifyURL(old_domain, new_domain)) {
    LOG(ERROR) << __func__ << "Unable to update URL in DB";
    std::move(callback).Run(false);
    return;
  }

  url_list_.erase(old_domain);
  url_list_.insert(new_domain);

  std::move(callback).Run(true);
}

void SiteFilterServiceImpl::FillListFromDB() {
  url_list_.clear();

  std::vector<std::string> url_list;
  url_list_table_->GetAllURLs(url_list);

  if (url_list.empty()) {
    LOG(INFO) << __func__ << " url database is empty";
    return;
  }

  url_list_.insert(url_list.begin(), url_list.end());
}

std::string SiteFilterServiceImpl::GetDomain(const std::string& url) {
  GURL decoded_url = url_formatter::FixupURL(url, std::string());
  if (!(decoded_url.is_valid() && decoded_url.IsStandard()))
    return {};
  std::string domain = decoded_url.host();
  if (!domain.find(kWwwPrefix)) {
    domain = domain.substr(kWwwPrefix.length());
  }
  return domain;
}

std::string SiteFilterServiceImpl::GetTableName() const {
  switch (type_) {
    case Type::kApproved:
      return kAllowURLTableName;
    case Type::kBlocked:
      return kBlockURLTableName;
    default:
      break;
  }

  return {};
}

bool SiteFilterServiceImpl::IsURLFound(const std::string& url) {
  return url_list_.find(url) != url_list_.end();
}

}  // namespace browser
