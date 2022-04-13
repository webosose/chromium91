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

#ifndef NEVA_BROWSER_SERVICE_BROWSER_POPUP_BLOCKER_SERVICE_IMPL_H_
#define NEVA_BROWSER_SERVICE_BROWSER_POPUP_BLOCKER_SERVICE_IMPL_H_

#include "base/memory/singleton.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "neva/browser_service/browser/url_database.h"
#include "neva/browser_service/public/mojom/popupblocker_service.mojom.h"
#include "ui/base/window_open_disposition.h"

class GURL;

namespace browser {
// This class provides an utility to manage the
// popup blocker feature by getting requests from WebView to
// turn on/off the feature and also to provide exception URLs
// which gets stored in the "popup_blocker_urls" table of the DB.
// And performing corresponding DB operations to manage the
// URLs in the DB.
class PopupBlockerServiceImpl : public mojom::PopupBlockerService {
 public:
  static PopupBlockerServiceImpl* GetInstance();
  // Check if the site should be blocked from opening
  // any popup window based on the feature state and presence
  // of the URL in exception list.
  bool IsBlocked(const GURL& url,
                 const bool& user_gesture,
                 const WindowOpenDisposition& disposition) const;

  void AddBinding(mojo::PendingReceiver<mojom::PopupBlockerService> receiver);

  // mojom::PopupBlockerService
  // Set the popup blocker feature state On/Off
  void SetEnabled(bool popup_state, SetEnabledCallback callback) override;
  // Get the list of exception URLs from the DB
  void GetURLs(GetURLsCallback callback) override;
  // Add an exception URL in to the "popup_blocker_urls" table of the DB.
  void AddURL(const std::string& url, AddURLCallback callback) override;
  // Remove a list of exception URLs from the "popup_blocker_urls"
  // table of the DB.
  void DeleteURLs(const std::vector<std::string>& urls,
                  DeleteURLsCallback callback) override;
  // Modify an existing exception URL with a new URL in the
  // "popup_blocker_urls" table of the DB.
  void updateURL(const std::string& old_url,
                 const std::string& new_url,
                 updateURLCallback callback) override;

 private:
  friend struct base::DefaultSingletonTraits<PopupBlockerServiceImpl>;
  PopupBlockerServiceImpl();
  PopupBlockerServiceImpl(const PopupBlockerServiceImpl&) = delete;

  ~PopupBlockerServiceImpl();
  // Read the URL lists from the DB and keep a local copy of the exception
  // lists.
  void FillListFromDB();
  std::string GetDomain(const std::string& url) const;
  bool IsURLFound(const std::string& url) const;

  bool popup_blocker_enabled_ = false;
  std::unordered_set<std::string> url_list_;
  std::unique_ptr<URLDatabase> url_list_table_;

  mojo::ReceiverSet<mojom::PopupBlockerService> receivers_;
};

}  // namespace browser

#endif  // NEVA_BROWSER_SERVICE_BROWSER_POPUP_BLOCKER_SERVICE_IMPL_H_
