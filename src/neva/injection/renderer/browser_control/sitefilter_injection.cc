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

#include "neva/injection/renderer/browser_control/sitefilter_injection.h"

#include "base/bind.h"
#include "base/macros.h"
#include "gin/arguments.h"
#include "gin/handle.h"
#include "third_party/blink/public/common/thread_safe_browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/web/blink.h"
#include "third_party/blink/public/web/web_local_frame.h"

namespace injections {

namespace {

const char kSiteFilterObjectName[] = "sitefilter";

const char kSetTypeMethodName[] = "setType";
const char kGetURLsMethodName[] = "getURLs";
const char kAddURLMethodName[] = "addURL";
const char kDeleteURLsMethodName[] = "deleteURLs";
const char kUpdateURLMethodName[] = "updateURL";

// Returns true if |maybe| is both a value, and that value is true.
inline bool IsTrue(v8::Maybe<bool> maybe) {
  return maybe.IsJust() && maybe.FromJust();
}

}  // anonymous namespace

gin::WrapperInfo SiteFilterInjection::kWrapperInfo = {gin::kEmbedderNativeGin};

// static
void SiteFilterInjection::Install(blink::WebLocalFrame* frame) {
  v8::Isolate* isolate = blink::MainThreadIsolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Context> context = frame->MainWorldScriptContext();
  if (context.IsEmpty())
    return;

  v8::Local<v8::Object> global = context->Global();
  v8::Context::Scope context_scope(context);

  v8::Local<v8::String> navigator_name = gin::StringToV8(isolate, "navigator");
  v8::Local<v8::Object> navigator;
  if (!gin::Converter<v8::Local<v8::Object>>::FromV8(
          isolate, global->Get(context, navigator_name).ToLocalChecked(),
          &navigator))
    return;

  v8::Local<v8::String> sitefilter_name =
      gin::StringToV8(isolate, kSiteFilterObjectName);
  if (IsTrue(navigator->Has(context, sitefilter_name)))
    return;

  CreateSiteFilterObject(isolate, navigator);
}

// static
void SiteFilterInjection::Uninstall(blink::WebLocalFrame* frame) {
  v8::Isolate* isolate = blink::MainThreadIsolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Context> context = frame->MainWorldScriptContext();
  if (context.IsEmpty())
    return;

  v8::Local<v8::Object> global = context->Global();
  v8::Context::Scope context_scope(context);

  v8::Local<v8::String> navigator_name = gin::StringToV8(isolate, "navigator");
  v8::Local<v8::Object> navigator;
  if (gin::Converter<v8::Local<v8::Object>>::FromV8(
          isolate, global->Get(context, navigator_name).ToLocalChecked(),
          &navigator)) {
    v8::Local<v8::String> sitefilter_name =
        gin::StringToV8(isolate, kSiteFilterObjectName);
    if (IsTrue(navigator->Has(context, sitefilter_name)))
      navigator->Delete(context, sitefilter_name);
  }
}

// static
void SiteFilterInjection::CreateSiteFilterObject(
    v8::Isolate* isolate,
    v8::Local<v8::Object> parent) {
  gin::Handle<SiteFilterInjection> sitefilter =
      gin::CreateHandle(isolate, new SiteFilterInjection());
  parent
      ->Set(isolate->GetCurrentContext(),
            gin::StringToV8(isolate, kSiteFilterObjectName), sitefilter.ToV8())
      .Check();
}

SiteFilterInjection::SiteFilterInjection() {
  blink::Platform::Current()->GetBrowserInterfaceBroker()->GetInterface(
      remote_sitefilter_.BindNewPipeAndPassReceiver());
}

SiteFilterInjection::~SiteFilterInjection() = default;

bool SiteFilterInjection::SetType(gin::Arguments* args) {
  int32_t site_filter_type = -1;
  if (!args->GetNext(&site_filter_type)) {
    LOG(ERROR) << __func__ << ", wrong argument";
    return false;
  }

  if (site_filter_type < 0 || site_filter_type > 2) {
    LOG(ERROR) << __func__ << ", Invalid state";
    return false;
  }

  bool result = false;
  remote_sitefilter_->SetType(site_filter_type, &result);
  return result;
}

bool SiteFilterInjection::GetURLs(gin::Arguments* args) {
  v8::Local<v8::Function> local_func;
  if (!args->GetNext(&local_func)) {
    LOG(ERROR) << __func__ << ", wrong argument";
    return false;
  }

  auto callback_ptr = std::make_unique<v8::Persistent<v8::Function>>(
      args->isolate(), local_func);
  remote_sitefilter_->GetURLs(
      base::BindOnce(&SiteFilterInjection::OnGetURLsRespond,
                     base::Unretained(this), std::move(callback_ptr)));
  return true;
}

bool SiteFilterInjection::AddURL(gin::Arguments* args) {
  std::string new_url;
  if (!args->GetNext(&new_url)) {
    LOG(ERROR) << __func__ << ", wrong argument";
    return false;
  }

  bool result = false;
  remote_sitefilter_->AddURL(new_url, &result);
  return result;
}

bool SiteFilterInjection::DeleteURLs(gin::Arguments* args) {
  std::vector<std::string> url_list;
  if (!args->GetNext(&url_list)) {
    LOG(ERROR) << __func__ << ", wrong argument";
    return false;
  }

  v8::Local<v8::Function> local_func;
  if (!args->GetNext(&local_func)) {
    LOG(ERROR) << __func__ << ", wrong argument";
    return false;
  }

  auto callback_ptr = std::make_unique<v8::Persistent<v8::Function>>(
      args->isolate(), local_func);
  remote_sitefilter_->DeleteURLs(
      url_list,
      base::BindOnce(&SiteFilterInjection::OnDeleteURLsRespond,
                     base::Unretained(this), std::move(callback_ptr)));
  return true;
}

bool SiteFilterInjection::UpdateURL(gin::Arguments* args) {
  std::string old_url;
  std::string new_url;
  if (!args->GetNext(&old_url) || !args->GetNext(&new_url)) {
    LOG(ERROR) << __func__ << ", wrong argument";
    return false;
  }

  bool result = false;
  remote_sitefilter_->UpdateURL(old_url, new_url, &result);
  return result;
}

gin::ObjectTemplateBuilder SiteFilterInjection::GetObjectTemplateBuilder(
    v8::Isolate* isolate) {
  return gin::Wrappable<SiteFilterInjection>::GetObjectTemplateBuilder(isolate)
      .SetMethod(kSetTypeMethodName, &SiteFilterInjection::SetType)
      .SetMethod(kGetURLsMethodName, &SiteFilterInjection::GetURLs)
      .SetMethod(kAddURLMethodName, &SiteFilterInjection::AddURL)
      .SetMethod(kDeleteURLsMethodName, &SiteFilterInjection::DeleteURLs)
      .SetMethod(kUpdateURLMethodName, &SiteFilterInjection::UpdateURL);
}

void SiteFilterInjection::OnGetURLsRespond(
    std::unique_ptr<v8::Persistent<v8::Function>> callback,
    const std::vector<std::string>& url_list) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Object> wrapper;
  if (!GetWrapper(isolate).ToLocal(&wrapper)) {
    LOG(ERROR) << __func__ << "(): can not get wrapper";
    return;
  }

  v8::Local<v8::Context> context = wrapper->CreationContext();
  v8::Context::Scope context_scope(context);
  v8::Local<v8::Function> local_callback = callback->Get(isolate);

  const int argc = 1;
  v8::Local<v8::Value> result;
  if (gin::TryConvertToV8(isolate, url_list, &result))
    local_callback->Call(context, wrapper, argc, &result);
}

void SiteFilterInjection::OnDeleteURLsRespond(
    std::unique_ptr<v8::Persistent<v8::Function>> callback,
    bool is_success) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Object> wrapper;
  if (!GetWrapper(isolate).ToLocal(&wrapper)) {
    LOG(ERROR) << __func__ << "(): can not get wrapper";
    return;
  }

  v8::Local<v8::Context> context = wrapper->CreationContext();
  v8::Context::Scope context_scope(context);
  v8::Local<v8::Function> local_callback = callback->Get(isolate);

  const int argc = 1;
  v8::Local<v8::Value> result;
  if (gin::TryConvertToV8(isolate, is_success, &result))
    local_callback->Call(context, wrapper, argc, &result);
}

}  // namespace injections
