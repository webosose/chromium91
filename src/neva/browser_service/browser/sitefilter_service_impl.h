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

#ifndef NEVA_BROWSER_SERVICE_BROWSER_SITE_FILTER_SERVICE_IMPL_H_
#define NEVA_BROWSER_SERVICE_BROWSER_SITE_FILTER_SERVICE_IMPL_H_

#include <list>
#include <memory>
#include <string>
#include <vector>

#include "base/memory/singleton.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "neva/browser_service/browser/url_database.h"
#include "neva/browser_service/public/mojom/sitefilter_service.mojom.h"
#include "url/gurl.h"

namespace browser {

// This class provides an utility to manage the
// Site filter feature by getting requests from WebView to
// Add/Delete/Modify/GetAllURLs URLs as per user's request.
// And performing corresponding DB perations to manage the
// URLs in the DB.
class SiteFilterServiceImpl : public mojom::SiteFilterService {
 public:
  static SiteFilterServiceImpl* Get();

  // Check if the current URL should be blocked or allowed
  // to be loaded(as per the filter type set by User).
  bool IsBlocked(const GURL& url, bool is_redirect);

  void AddBinding(mojo::PendingReceiver<mojom::SiteFilterService> receiver);

  // mojom::SiteFilterService
  void SetType(int32_t filter_type, SetTypeCallback callback) override;
  void GetURLs(GetURLsCallback callback) override;
  void AddURL(const std::string& url, AddURLCallback callback) override;
  void DeleteURLs(const std::vector<std::string>& urls,
                  DeleteURLsCallback callback) override;
  void UpdateURL(const std::string& old_url,
                 const std::string& new_url,
                 UpdateURLCallback callback) override;

 private:
  friend struct base::DefaultSingletonTraits<SiteFilterServiceImpl>;

  enum class Type {
    kDisabled = 0,
    kApproved,
    kBlocked,
  };

  SiteFilterServiceImpl() = default;
  SiteFilterServiceImpl(const SiteFilterServiceImpl&) = delete;
  SiteFilterServiceImpl& operator=(const SiteFilterServiceImpl&) = delete;
  ~SiteFilterServiceImpl() = default;

  // Read the URL lists from the DB and keep a local copy of both the lists.
  void FillListFromDB();

  // Get the domain name of an URL
  std::string GetDomain(const std::string& url);

  // Get the table name for a filter type(Type)
  std::string GetTableName() const;

  // Check if the URL is found in the local URL list
  bool IsURLFound(const std::string& url);

  Type type_ = Type::kDisabled;

  std::unordered_set<std::string> url_list_;
  std::unique_ptr<URLDatabase> url_list_table_;

  mojo::ReceiverSet<mojom::SiteFilterService> receivers_;
};

}  // namespace browser

#endif  // NEVA_BROWSER_SERVICE_BROWSER_SITE_FILTER_SERVICE_H_
