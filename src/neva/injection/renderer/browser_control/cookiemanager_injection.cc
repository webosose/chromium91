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

#include "neva/injection/renderer/browser_control/cookiemanager_injection.h"

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

const char kCookieManagerObjectName[] = "cookiemanager";

const char kSetCookieOptionMethodName[] = "setCookieOption";
const char kClearAllCookiesMethodName[] = "clearAllCookies";
const char kGetAllCookiesForTestingMethodName[] = "getAllCookiesForTesting";

// Returns true if |maybe| is both a value, and that value is true.
inline bool IsTrue(v8::Maybe<bool> maybe) {
  return maybe.IsJust() && maybe.FromJust();
}

}  // anonymous namespace

gin::WrapperInfo CookieManagerInjection::kWrapperInfo = {
    gin::kEmbedderNativeGin};

// static
void CookieManagerInjection::Install(blink::WebLocalFrame* frame) {
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

  v8::Local<v8::String> cookiemanager_name =
      gin::StringToV8(isolate, kCookieManagerObjectName);
  if (IsTrue(navigator->Has(context, cookiemanager_name)))
    return;

  CreateCookieManagerObject(isolate, navigator);
}

// static
void CookieManagerInjection::Uninstall(blink::WebLocalFrame* frame) {
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
    v8::Local<v8::String> cookiemanager_name =
        gin::StringToV8(isolate, kCookieManagerObjectName);
    if (IsTrue(navigator->Has(context, cookiemanager_name)))
      navigator->Delete(context, cookiemanager_name);
  }
}

// static
void CookieManagerInjection::CreateCookieManagerObject(
    v8::Isolate* isolate,
    v8::Local<v8::Object> parent) {
  gin::Handle<CookieManagerInjection> cookiemanager =
      gin::CreateHandle(isolate, new CookieManagerInjection());
  parent->Set(isolate->GetCurrentContext(),
              gin::StringToV8(isolate, kCookieManagerObjectName),
              cookiemanager.ToV8()).Check();
}

CookieManagerInjection::CookieManagerInjection() {
  blink::Platform::Current()->GetBrowserInterfaceBroker()->GetInterface(
      cookie_manager_service_.BindNewPipeAndPassReceiver());
}

CookieManagerInjection::~CookieManagerInjection() = default;

bool CookieManagerInjection::SetCookieOption(gin::Arguments* args) {
  int32_t cookie_option = false;
  if (!args->GetNext(&cookie_option)) {
    LOG(ERROR) << __func__ << ", wrong argument";
    return false;
  }

  bool result = false;
  cookie_manager_service_->SetCookieOption(cookie_option, &result);
  return result;
}

bool CookieManagerInjection::ClearAllCookies() {
  bool result = false;
  cookie_manager_service_->ClearAllCookies(&result);
  return result;
}

bool CookieManagerInjection::GetAllCookiesForTesting(gin::Arguments* args) {
  v8::Local<v8::Function> local_func;
  if (!args->GetNext(&local_func)) {
    LOG(ERROR) << __func__ << ", wrong argument";
    return false;
  }

  auto callback_ptr = std::make_unique<v8::Persistent<v8::Function>>(
      args->isolate(), local_func);
  cookie_manager_service_->GetAllCookiesForTesting(
      base::BindOnce(&CookieManagerInjection::OnGetAllCookiesResponse,
                     base::Unretained(this), std::move(callback_ptr)));
  return true;
}

gin::ObjectTemplateBuilder CookieManagerInjection::GetObjectTemplateBuilder(
    v8::Isolate* isolate) {
  return gin::Wrappable<CookieManagerInjection>::GetObjectTemplateBuilder(
             isolate)
      .SetMethod(kSetCookieOptionMethodName,
                 &CookieManagerInjection::SetCookieOption)
      .SetMethod(kClearAllCookiesMethodName,
                 &CookieManagerInjection::ClearAllCookies)
      .SetMethod(kGetAllCookiesForTestingMethodName,
                 &CookieManagerInjection::GetAllCookiesForTesting);
}

void CookieManagerInjection::OnGetAllCookiesResponse(
    std::unique_ptr<v8::Persistent<v8::Function>> callback,
    const std::vector<std::string>& cookie_list) {
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
  if (gin::TryConvertToV8(isolate, cookie_list, &result))
    local_callback->Call(context, wrapper, argc, &result);
}

}  // namespace injections
