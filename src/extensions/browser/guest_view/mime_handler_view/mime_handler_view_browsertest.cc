// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/base_paths.h"
#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_timeouts.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/guest_view/browser/test_guest_view_manager.h"
#include "components/javascript_dialogs/app_modal_dialog_controller.h"
#include "components/javascript_dialogs/app_modal_dialog_view.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_renderer_host.h"
#include "content/public/test/test_utils.h"
#include "extensions/browser/api/extensions_api_client.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/guest_view/extensions_guest_view_manager_delegate.h"
#include "extensions/browser/guest_view/mime_handler_view/mime_handler_stream_manager.h"
#include "extensions/browser/guest_view/mime_handler_view/mime_handler_view_guest.h"
#include "extensions/browser/guest_view/mime_handler_view/test_mime_handler_view_guest.h"
#include "extensions/browser/process_manager.h"
#include "extensions/common/constants.h"
#include "extensions/common/guest_view/extensions_guest_view_messages.h"
#include "extensions/common/mojom/guest_view.mojom.h"
#include "extensions/test/result_catcher.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "ui/base/ui_base_features.h"
#include "url/url_constants.h"

using extensions::ExtensionsAPIClient;
using extensions::MimeHandlerStreamManager;
using extensions::MimeHandlerViewGuest;
using extensions::TestMimeHandlerViewGuest;
using guest_view::GuestViewManager;
using guest_view::GuestViewManagerDelegate;
using guest_view::TestGuestViewManager;
using guest_view::TestGuestViewManagerFactory;

class MimeHandlerViewTest : public extensions::ExtensionApiTest {
 public:
  MimeHandlerViewTest() {
    GuestViewManager::set_factory_for_testing(&factory_);
  }

  ~MimeHandlerViewTest() override {}

  void SetUpOnMainThread() override {
    extensions::ExtensionApiTest::SetUpOnMainThread();

    embedded_test_server()->ServeFilesFromDirectory(
        test_data_dir_.AppendASCII("mime_handler_view"));
    embedded_test_server()->RegisterRequestMonitor(base::BindRepeating(
        &MimeHandlerViewTest::MonitorRequest, base::Unretained(this)));
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(StartEmbeddedTestServer());
  }

  // TODO(paulmeyer): This function is implemented over and over by the
  // different GuestView test classes. It really needs to be refactored out to
  // some kind of GuestViewTest base class.
  TestGuestViewManager* GetGuestViewManager() const {
    TestGuestViewManager* manager = static_cast<TestGuestViewManager*>(
        TestGuestViewManager::FromBrowserContext(browser()->profile()));
    // TestGuestViewManager::WaitForSingleGuestCreated can and will get called
    // before a guest is created. Since GuestViewManager is usually not created
    // until the first guest is created, this means that |manager| will be
    // nullptr if trying to use the manager to wait for the first guest. Because
    // of this, the manager must be created here if it does not already exist.
    if (!manager) {
      manager = static_cast<TestGuestViewManager*>(
          GuestViewManager::CreateWithDelegate(
              browser()->profile(),
              ExtensionsAPIClient::Get()->CreateGuestViewManagerDelegate(
                  browser()->profile())));
    }
    return manager;
  }

  content::WebContents* GetEmbedderWebContents() {
    return browser()->tab_strip_model()->GetWebContentsAt(0);
  }

  MimeHandlerViewGuest* GetLastGuestView() const {
    return MimeHandlerViewGuest::FromWebContents(
        GetGuestViewManager()->GetLastGuestCreated());
  }

  const extensions::Extension* LoadTestExtension() {
    const extensions::Extension* extension =
        LoadExtension(test_data_dir_.AppendASCII("mime_handler_view"));
    if (!extension)
      return nullptr;

    EXPECT_EQ(std::string(extension_misc::kMimeHandlerPrivateTestExtensionId),
              extension->id());

    return extension;
  }

  void RunTestWithUrl(const GURL& url) {
    // Use the testing subclass of MimeHandlerViewGuest.
    GetGuestViewManager()->RegisterTestGuestViewType<MimeHandlerViewGuest>(
        base::BindRepeating(&TestMimeHandlerViewGuest::Create));

    const extensions::Extension* extension = LoadTestExtension();
    ASSERT_TRUE(extension);

    extensions::ResultCatcher catcher;

    ui_test_utils::NavigateToURL(browser(), url);

    if (!catcher.GetNextResult())
      FAIL() << catcher.message();
  }

  void RunTest(const std::string& path) {
    RunTestWithUrl(embedded_test_server()->GetURL("/" + path));
  }

  int basic_count() const { return basic_count_; }

 private:
  void MonitorRequest(const net::test_server::HttpRequest& request) {
    if (request.relative_url == "/testBasic.csv")
      basic_count_++;
  }

  TestGuestViewManagerFactory factory_;
  base::test::ScopedFeatureList scoped_feature_list_;
  int basic_count_ = 0;
};

class UserActivationUpdateWaiter {
 public:
  explicit UserActivationUpdateWaiter(content::WebContents* web_contents) {
    user_activation_interceptor_.Init(web_contents->GetMainFrame());
  }
  ~UserActivationUpdateWaiter() = default;

  void Wait() {
    if (user_activation_interceptor_.update_user_activation_state())
      return;
    base::RunLoop run_loop;
    user_activation_interceptor_.set_quit_handler(run_loop.QuitClosure());
    run_loop.Run();
  }

 private:
  content::UpdateUserActivationStateInterceptor user_activation_interceptor_;
};

IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest, Embedded) {
  RunTest("test_embedded.html");
  // Sanity check. Navigate the page and verify the guest goes away.
  ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL));
  auto* gv_manager = GetGuestViewManager();
  gv_manager->WaitForAllGuestsDeleted();
  EXPECT_EQ(1U, gv_manager->num_guests_created());
}

// This test start with an <object> that has a content frame. Then the content
// frame (plugin frame) is navigated to a cross-origin target page. After the
// navigation is completed, the <object> is set to render MimeHandlerView by
// setting its |data| and |type| attributes accordingly.
IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest, EmbedWithInitialCrossOriginFrame) {
  const std::string kTestName = "test_cross_origin_frame";
  std::string cross_origin_url =
      embedded_test_server()->GetURL("b.com", "/test_page.html").spec();
  auto test_url = embedded_test_server()->GetURL(
      "a.com",
      base::StringPrintf("/test_object_with_frame.html?test_data=%s,%s,%s",
                         kTestName.c_str(), cross_origin_url.c_str(),
                         "testEmbedded.csv"));
  RunTestWithUrl(test_url);
}

// This test verifies that navigations on the plugin frame before setting it to
// load MimeHandlerView do not race with the creation of the guest. The test
// loads a page with an <object> which is first navigated to some cross-origin
// domain and then immediately after load, the page triggers a navigation of its
// own to another cross-origin domain. Meanwhile the embedder sets the <object>
// to load a MimeHandlerView. The test passes if MHV loads. This is to catch the
// potential race between the cross-origin renderer initiated navigation and
// the navigation to "about:blank" started from the browser.
//
// Disabled on Linux due to flakiness: https://crbug.com/1002788.
#if defined(OS_LINUX)
#define MAYBE_NavigationRaceFromEmbedder DISABLED_NavigationRaceFromEmbedder
#else
#define MAYBE_NavigationRaceFromEmbedder NavigationRaceFromEmbedder
#endif
IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest, MAYBE_NavigationRaceFromEmbedder) {
  const std::string kTestName = "test_navigation_race_embedder";
  auto cross_origin_url =
      embedded_test_server()->GetURL("b.com", "/test_page.html").spec();
  auto test_url = embedded_test_server()->GetURL(
      "a.com",
      base::StringPrintf("/test_object_with_frame.html?test_data=%s,%s,%s",
                         kTestName.c_str(), cross_origin_url.c_str(),
                         "testEmbedded.csv"));
  RunTestWithUrl(test_url);
}

// TODO(ekaramad): Without proper handling of navigation to 'about:blank', this
// test would be flaky. Use TestNavigationManager class and possibly break the
// test into more sub-tests for various scenarios (https://crbug.com/659750).
// This test verifies that (almost) concurrent navigations in a cross-process
// frame inside an <embed> which is transitioning to a MimeHandlerView will
// not block creation of MimeHandlerView. The test will load some cross-origin
// content in <object> which right after loading will navigate it self to some
// other cross-origin content. On the embedder side, when the first page loads,
// the <object> loads some text/csv content to create a MimeHandlerViewGuest.
// The test passes if MHV loads.
// TODO(crbug.com/1182355): Disabled due to flakes.
IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest,
                       DISABLED_NavigationRaceFromCrossProcessRenderer) {
  const std::string kTestName = "test_navigation_race_cross_origin";
  auto cross_origin_url =
      embedded_test_server()->GetURL("b.com", "/test_page.html").spec();
  auto other_cross_origin_url =
      embedded_test_server()->GetURL("c.com", "/test_page.html").spec();
  auto test_url = embedded_test_server()->GetURL(
      "a.com",
      base::StringPrintf("/test_object_with_frame.html?test_data=%s,%s,%s,%s",
                         kTestName.c_str(), cross_origin_url.c_str(),
                         other_cross_origin_url.c_str(), "testEmbedded.csv"));
  RunTestWithUrl(test_url);
}

// This test verifies that removing embedder RenderFrame will not crash the
// renderer (for context see https://crbug.com/930803).
IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest, EmbedderFrameRemovedNoCrash) {
  RunTest("test_iframe_basic.html");
  auto* guest_view = GuestViewBase::FromWebContents(
      GetGuestViewManager()->WaitForSingleGuestCreated());
  ASSERT_TRUE(guest_view);
  int32_t element_instance_id = guest_view->element_instance_id();
  auto* embedder_web_contents = GetEmbedderWebContents();
  auto* child_frame =
      content::ChildFrameAt(embedder_web_contents->GetMainFrame(), 0);
  content::RenderFrameDeletedObserver render_frame_observer(child_frame);
  ASSERT_TRUE(
      content::ExecJs(embedder_web_contents,
                      "document.querySelector('iframe').outerHTML = ''"));
  render_frame_observer.WaitUntilDeleted();
  // Send the IPC. During destruction MHVFC would cause a UaF since it was not
  // removed from the global map.
  mojo::AssociatedRemote<extensions::mojom::MimeHandlerViewContainerManager>
      container_manager;
  embedder_web_contents->GetMainFrame()
      ->GetRemoteAssociatedInterfaces()
      ->GetInterface(&container_manager);
  container_manager->DestroyFrameContainer(element_instance_id);
  // Running the following JS code fails if the renderer has crashed.
  ASSERT_TRUE(content::ExecJs(embedder_web_contents, "window.name = 'foo'"));
}

// TODO(ekaramad): Somehow canceling a first dialog in a setup similar to the
// test below pops up another dialog. This is likely due to the navigation to
// about:blank from both the browser side and the embedder side in the method
// HTMLPlugInElement::RequestObjectInternal. Find out the issue and add another
// test here where the dialog is dismissed and the guest not created.
// (https://crbug.com/659750).
// This test verifies that transitioning a plugin element from text/html to
// application/pdf respects 'beforeunload'. The test specifically checks that
// 'beforeunload' dialog is shown to the user and if the user decides to
// proceed with the transition, MimeHandlerViewGuest is created.
IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest,
                       EmbedWithInitialFrameAcceptBeforeUnloadDialog) {
  // Use the testing subclass of MimeHandlerViewGuest.
  GetGuestViewManager()->RegisterTestGuestViewType<MimeHandlerViewGuest>(
      base::BindRepeating(&TestMimeHandlerViewGuest::Create));
  const extensions::Extension* extension = LoadTestExtension();
  ASSERT_TRUE(extension);
  ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("a.com", "/test_object_with_frame.html"));
  auto* main_frame =
      browser()->tab_strip_model()->GetWebContentsAt(0)->GetMainFrame();
  auto url_with_beforeunload =
      embedded_test_server()->GetURL("b.com", "/test_page.html?beforeunload");
  bool result = false;
  ASSERT_TRUE(content::ExecuteScriptAndExtractBool(
      main_frame,
      base::StringPrintf(
          "object.data = '%s';"
          " object.onload = () => window.domAutomationController.send(true);",
          url_with_beforeunload.spec().c_str()),
      &result));
  ASSERT_TRUE(result);
  // Give user gesture to the frame, set the <object> to text/csv resource and
  // handle the "beforeunload" dialog.
  content::PrepContentsForBeforeUnloadTest(
      browser()->tab_strip_model()->GetWebContentsAt(0));
  ASSERT_TRUE(content::ExecuteScript(main_frame,
                                     "object.data = './testEmbedded.csv';"
                                     "object.type = 'text/csv';"));
  javascript_dialogs::AppModalDialogController* alert =
      ui_test_utils::WaitForAppModalDialog();
  ASSERT_TRUE(alert->is_before_unload_dialog());
  alert->view()->AcceptAppModalDialog();

  EXPECT_TRUE(GetGuestViewManager()->WaitForSingleGuestCreated());
}
// The following tests will eventually converted into a parametric version which
// will run on both BrowserPlugin-based and cross-process-frame-based
// MimeHandlerView (https://crbug.com/659750).
IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest, PostMessage) {
  RunTest("test_postmessage.html");
}

IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest, Basic) {
  RunTest("testBasic.csv");
  // Verify that for a navigation to a MimeHandlerView MIME type, exactly one
  // stream is intercepted. This means we do not create a PluginDocument. If a
  // PluginDocument was created here, the |view_id| associated with the
  // stream intercepted from navigation response would be lost (
  // PluginDocument does not talk to a MimeHandlerViewFrameContainer). Then,
  // the newly added <embed> by the PluginDocument would send its own request
  // leading to a total of 2 intercepted streams. The first one (from
  // navigation) would never be released.
  EXPECT_EQ(0U, MimeHandlerStreamManager::Get(
                    GetEmbedderWebContents()->GetBrowserContext())
                    ->streams_.size());
}

IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest, Iframe) {
  RunTest("test_iframe.html");
}

IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest, NonAsciiHeaders) {
  RunTest("testNonAsciiHeaders.csv");
}

IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest, DataUrl) {
  const char* kDataUrlCsv = "data:text/csv;base64,Y29udGVudCB0byByZWFkCg==";
  RunTestWithUrl(GURL(kDataUrlCsv));
}

IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest, EmbeddedDataUrlObject) {
  RunTest("test_embedded_data_url_object.html");
}

IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest, EmbeddedDataUrlEmbed) {
  RunTest("test_embedded_data_url_embed.html");
}

IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest, EmbeddedDataUrlLong) {
  RunTest("test_embedded_data_url_long.html");
}

// Regression test for crbug.com/587709.
IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest, SingleRequest) {
  GURL url(embedded_test_server()->GetURL("/testBasic.csv"));
  RunTest("testBasic.csv");
  EXPECT_EQ(1, basic_count());
}

// Test that a mime handler view can keep a background page alive.
IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest, BackgroundPage) {
  extensions::ProcessManager::SetEventPageIdleTimeForTesting(1);
  extensions::ProcessManager::SetEventPageSuspendingTimeForTesting(1);
  RunTest("testBackgroundPage.csv");
}

IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest, TargetBlankAnchor) {
  RunTest("testTargetBlankAnchor.csv");
  ASSERT_EQ(2, browser()->tab_strip_model()->count());
  EXPECT_TRUE(content::WaitForLoadStop(
      browser()->tab_strip_model()->GetWebContentsAt(1)));
  EXPECT_EQ(
      GURL(url::kAboutBlankURL),
      browser()->tab_strip_model()->GetWebContentsAt(1)->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest, BeforeUnload_NoDialog) {
  ASSERT_NO_FATAL_FAILURE(RunTest("testBeforeUnloadNoDialog.csv"));
  auto* web_contents = GetEmbedderWebContents();
  content::PrepContentsForBeforeUnloadTest(web_contents);

  // Wait for a round trip to the outer renderer to ensure any beforeunload
  // toggle IPC has had time to reach the browser.
  ExecuteScriptAndGetValue(web_contents->GetMainFrame(), "");

  // Try to navigate away from the page. If the beforeunload listener is
  // triggered and a dialog is shown, this navigation will never complete,
  // causing the test to timeout and fail.
  ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL));
}

IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest, BeforeUnload_ShowDialog) {
  ASSERT_NO_FATAL_FAILURE(RunTest("testBeforeUnloadShowDialog.csv"));
  auto* web_contents = GetEmbedderWebContents();
  content::PrepContentsForBeforeUnloadTest(web_contents);

  // Wait for a round trip to the outer renderer to ensure the beforeunload
  // toggle IPC has had time to reach the browser.
  ExecuteScriptAndGetValue(web_contents->GetMainFrame(), "");

  web_contents->GetController().LoadURL(GURL(url::kAboutBlankURL), {},
                                        ui::PAGE_TRANSITION_TYPED, "");

  javascript_dialogs::AppModalDialogController* before_unload_dialog =
      ui_test_utils::WaitForAppModalDialog();
  EXPECT_TRUE(before_unload_dialog->is_before_unload_dialog());
  EXPECT_FALSE(before_unload_dialog->is_reload());
  before_unload_dialog->OnAccept(std::u16string(), false);
}

IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest,
                       BeforeUnloadEnabled_WithoutUserActivation) {
  ASSERT_NO_FATAL_FAILURE(RunTest("testBeforeUnloadWithUserActivation.csv"));
  auto* web_contents = GetEmbedderWebContents();
  // Prepare frames but don't trigger user activation.
  content::PrepContentsForBeforeUnloadTest(web_contents, false);

  // Even though this test's JS setup enables BeforeUnload dialogs, the dialog
  // is still suppressed here because of lack of user activation.  As a result,
  // the following navigation away from the page works fine.  If a beforeunload
  // dialog were shown, this navigation would fail, causing the test to timeout.
  ui_test_utils::NavigateToURL(browser(), GURL(url::kAboutBlankURL));
}

IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest,
                       BeforeUnloadEnabled_WithUserActivation) {
  ASSERT_NO_FATAL_FAILURE(RunTest("testBeforeUnloadWithUserActivation.csv"));
  auto* web_contents = GetEmbedderWebContents();
  // Prepare frames but don't trigger user activation across all frames.
  content::PrepContentsForBeforeUnloadTest(web_contents, false);

  // Make sure we have a guestviewmanager.
  auto* guest_contents = GetGuestViewManager()->WaitForSingleGuestCreated();
  UserActivationUpdateWaiter activation_waiter(guest_contents);

  // Activate |guest_contents| through a click, then wait until the activation
  // IPC reaches the browser process.
  SimulateMouseClick(guest_contents, 0, blink::WebMouseEvent::Button::kLeft);
  activation_waiter.Wait();

  // Wait for a round trip to the outer renderer to ensure any beforeunload
  // toggle IPC has had time to reach the browser.
  ExecuteScriptAndGetValue(web_contents->GetMainFrame(), "");

  // Try to navigate away, this should invoke a beforeunload dialog.
  web_contents->GetController().LoadURL(GURL(url::kAboutBlankURL), {},
                                        ui::PAGE_TRANSITION_TYPED, "");

  javascript_dialogs::AppModalDialogController* before_unload_dialog =
      ui_test_utils::WaitForAppModalDialog();
  EXPECT_TRUE(before_unload_dialog->is_before_unload_dialog());
  EXPECT_FALSE(before_unload_dialog->is_reload());
  before_unload_dialog->OnAccept(std::u16string(), false);
}

// Helper class to wait for document load event in the main frame.
class DocumentLoadComplete : public content::WebContentsObserver {
 public:
  explicit DocumentLoadComplete(content::WebContents* web_contents)
      : content::WebContentsObserver(web_contents) {}
  ~DocumentLoadComplete() override {}

  void DocumentOnLoadCompletedInMainFrame(
      content::RenderFrameHost* render_frame_host) override {
    did_load_ = true;
    run_loop_.Quit();
  }

  void Wait() {
    if (!did_load_)
      run_loop_.Run();
  }

 private:
  bool did_load_ = false;
  base::RunLoop run_loop_;
};

IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest, ActivatePostMessageSupportOnce) {
  RunTest("test_embedded.html");
  // Attach a second <embed>.
  ASSERT_TRUE(content::ExecJs(GetEmbedderWebContents(),
                              "const e = document.createElement('embed');"
                              "e.src = './testEmbedded.csv'; e.type='text/csv';"
                              "document.body.appendChild(e);"));
  DocumentLoadComplete(GetGuestViewManager()->WaitForNextGuestCreated()).Wait();
  // After load, an IPC has been sent to the renderer to update routing IDs for
  // the guest frame and the content frame (and activate the
  // PostMessageSupport). Run some JS to Ensure no DCHECKs have fired in the
  // embedder process.
  ASSERT_TRUE(content::ExecJs(GetEmbedderWebContents(), "foo = 0;"));
}

// This is a minimized repro for a clusterfuzz crasher and is not really related
// to MimeHandlerView. The test verifies that when
// HTMLPlugInElement::PluginWrapper is called for a plugin with no node document
// frame, the renderer does not crash (see https://966371).
IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest, AdoptNodeInOnLoadDoesNotCrash) {
  ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("/adopt_node_in_onload_no_crash.html"));
  // Run some JavaScript in embedder and make sure it is not crashed.
  ASSERT_TRUE(content::ExecJs(GetEmbedderWebContents(), "true"));
}

// Verifies that sandboxed frames do not create GuestViews (plugins are
// blocked in sandboxed frames).
IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest, DoNotLoadInSandboxedFrame) {
  // Use the testing subclass of MimeHandlerViewGuest.
  GetGuestViewManager()->RegisterTestGuestViewType<MimeHandlerViewGuest>(
      base::BindRepeating(&TestMimeHandlerViewGuest::Create));

  const extensions::Extension* extension = LoadTestExtension();
  ASSERT_TRUE(extension);

  ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/test_sandboxed_frame.html"));

  auto* guest_view_manager = GetGuestViewManager();
  // The page contains three <iframes> where two are sandboxed. The expectation
  // is that the sandboxed frames do not end up creating a MimeHandlerView.
  // Therefore, it suffices to wait for one GuestView to be created, then remove
  // the non-sandboxed frame, and ensue there are no GuestViews left.
  if (guest_view_manager->num_guests_created() == 0)
    ASSERT_TRUE(guest_view_manager->WaitForNextGuestCreated());
  ASSERT_EQ(1U, guest_view_manager->num_guests_created());

  // Remove the non-sandboxed frame.
  content::RenderFrameHost* main_rfh = GetEmbedderWebContents()->GetMainFrame();
  ASSERT_TRUE(content::ExecJs(main_rfh, "remove_frame('notsandboxed');"));
  // The page is expected to embed only '1' GuestView. If there is GuestViews
  // embedded inside other frames we should be timing out here.
  guest_view_manager->WaitForAllGuestsDeleted();

  // Since 'sandbox1' has no fallback content, we would render an error page in
  // the iframe. Note that we can't access the contentDocument because error
  // pages have opaque origins (so it's using a different origin than the main
  // frame).
  EXPECT_EQ(false, content::EvalJs(main_rfh, "!!(sandbox1.contentDocument)"));
  // The error page will not be blank.
  EXPECT_EQ(true,
            content::EvalJs(ChildFrameAt(main_rfh, 0),
                            "!!(document.body && document.body.firstChild)"));

  // The document inside 'sandbox2' contains an <object> with fallback content.
  // The expectation is that the <object> fails to load the MimeHandlerView and
  // should show the fallback content instead.
  EXPECT_EQ(true, content::EvalJs(main_rfh, "!!(sandbox2.contentDocument)"));
  EXPECT_EQ(
      "Fallback",
      content::EvalJs(
          main_rfh,
          "sandbox2.contentDocument.getElementById('fallback').innerText"));
}

// Tests that a MimeHandlerViewGuest auto-rejects pointer lock requests.
IN_PROC_BROWSER_TEST_F(MimeHandlerViewTest, RejectPointLock) {
  GetGuestViewManager()->RegisterTestGuestViewType<MimeHandlerViewGuest>(
      base::BindRepeating(&TestMimeHandlerViewGuest::Create));

  auto* extension = LoadTestExtension();
  ASSERT_TRUE(extension);

  ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/test_embedded.html"));

  auto* guest_contents = GetGuestViewManager()->WaitForSingleGuestCreated();
  // Make sure the load has started, before waiting for it to stop.
  // This is a little hacky, but will unjank the test for now.
  while (!guest_contents->IsLoading() &&
         !guest_contents->GetController().GetLastCommittedEntry()) {
    base::RunLoop run_loop;
    base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
        FROM_HERE, run_loop.QuitClosure(), TestTimeouts::tiny_timeout());
    run_loop.Run();
  }
  EXPECT_TRUE(WaitForLoadStop(guest_contents));
  content::RenderFrameHost* guest_rfh = guest_contents->GetMainFrame();
  EXPECT_EQ(false, content::EvalJs(guest_rfh, R"code(
    var promise = new Promise((resolve, reject) => {
      document.addEventListener('pointerlockchange', () => resolve(true));
      document.addEventListener('pointerlockerror', () => resolve(false));
    });
    document.body.requestPointerLock();
    (async ()=> { return await promise; })();
  )code",
                                   content::EXECUTE_SCRIPT_DEFAULT_OPTIONS,
                                   1 /* world_id */));
}
