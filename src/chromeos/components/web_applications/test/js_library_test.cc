// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/components/web_applications/test/js_library_test.h"

#include <string>
#include <utility>

#include "base/base_paths.h"
#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/memory/ref_counted_memory.h"
#include "base/path_service.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_controller_factory.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/webui/mojo_web_ui_controller.h"
#include "url/gurl.h"

namespace {

constexpr base::FilePath::CharType kRootDir[] =
    FILE_PATH_LITERAL("chromeos/components/system_apps/public/js/");

constexpr char kSystemAppTestHost[] = "system-app-test";
constexpr char kSystemAppTestURL[] = "chrome://system-app-test";
constexpr char kUntrustedSystemAppTestURL[] =
    "chrome-untrusted://system-app-test/";

bool IsSystemAppTestURL(const GURL& url) {
  return url.SchemeIs(content::kChromeUIScheme) &&
         url.host() == kSystemAppTestHost;
}

void HandleRequest(const std::string& url_path,
                   content::WebUIDataSource::GotDataCallback callback) {
  base::FilePath path;
  CHECK(base::PathService::Get(base::BasePathKey::DIR_SOURCE_ROOT, &path));
  path = path.Append(kRootDir);
  path = path.AppendASCII(url_path.substr(0, url_path.find("?")));

  std::string contents;
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    CHECK(base::ReadFileToString(path, &contents)) << path.value();
  }

  std::move(callback).Run(base::RefCountedString::TakeString(&contents));
}

void SetRequestFilterForDataSource(content::WebUIDataSource& data_source) {
  data_source.SetRequestFilter(
      base::BindRepeating([](const std::string& path) { return true; }),
      base::BindRepeating(&HandleRequest));
}

content::WebUIDataSource* CreateTrustedSysemAppTestDataSource() {
  auto* trusted_source = content::WebUIDataSource::Create(kSystemAppTestHost);

  // We need a CSP override to be able to embed a chrome-untrusted:// iframe.
  std::string csp =
      std::string("frame-src ") + kUntrustedSystemAppTestURL + ";";
  trusted_source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::FrameSrc, csp);

  SetRequestFilterForDataSource(*trusted_source);
  return trusted_source;
}

content::WebUIDataSource* CreateUntrustedSystemAppTestDataSource() {
  auto* untrusted_source =
      content::WebUIDataSource::Create(kUntrustedSystemAppTestURL);
  untrusted_source->AddFrameAncestor(GURL(kSystemAppTestURL));

  SetRequestFilterForDataSource(*untrusted_source);
  return untrusted_source;
}

class JsLibraryTestWebUIController : public ui::MojoWebUIController {
 public:
  explicit JsLibraryTestWebUIController(content::WebUI* web_ui)
      : ui::MojoWebUIController(web_ui) {
    auto* browser_context = web_ui->GetWebContents()->GetBrowserContext();

    content::WebUIDataSource::Add(browser_context,
                                  CreateTrustedSysemAppTestDataSource());
    content::WebUIDataSource::Add(browser_context,
                                  CreateUntrustedSystemAppTestDataSource());

    // Add ability to request chrome-untrusted: URLs
    web_ui->AddRequestableScheme(content::kChromeUIUntrustedScheme);
  }
};

class JsLibraryTestWebUIControllerFactory
    : public content::WebUIControllerFactory {
 public:
  JsLibraryTestWebUIControllerFactory() = default;
  ~JsLibraryTestWebUIControllerFactory() override = default;

  std::unique_ptr<content::WebUIController> CreateWebUIControllerForURL(
      content::WebUI* web_ui,
      const GURL& url) override {
    return std::make_unique<JsLibraryTestWebUIController>(web_ui);
  }

  content::WebUI::TypeID GetWebUIType(content::BrowserContext* browser_context,
                                      const GURL& url) override {
    if (IsSystemAppTestURL(url)) {
      return reinterpret_cast<content::WebUI::TypeID>(this);
    }
    return content::WebUI::kNoWebUI;
  }

  bool UseWebUIForURL(content::BrowserContext* browser_context,
                      const GURL& url) override {
    return IsSystemAppTestURL(url);
  }
};

}  // namespace

JsLibraryTest::JsLibraryTest()
    : factory_(std::make_unique<JsLibraryTestWebUIControllerFactory>()) {
  content::WebUIControllerFactory::RegisterFactory(factory_.get());
}

JsLibraryTest::~JsLibraryTest() {
  content::WebUIControllerFactory::UnregisterFactoryForTesting(factory_.get());
}
