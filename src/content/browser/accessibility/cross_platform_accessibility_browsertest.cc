// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <unordered_set>
#include <vector>

#include "base/callback_helpers.h"
#include "base/macros.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_feature_list.h"
#include "base/timer/elapsed_timer.h"
#include "build/build_config.h"
#include "build/chromecast_buildflags.h"
#include "content/browser/accessibility/browser_accessibility.h"
#include "content/browser/accessibility/browser_accessibility_manager.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/test/accessibility_notification_waiter.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/shell/browser/shell.h"
#include "net/base/escape.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/blink/public/common/features.h"
#include "ui/accessibility/accessibility_features.h"
#include "ui/accessibility/accessibility_switches.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_tree.h"
#include "ui/accessibility/ax_tree_id.h"

#if defined(OS_WIN)
#include "base/win/atl.h"
#include "base/win/scoped_com_initializer.h"
#include "ui/base/win/atl_module.h"
#endif

using ::testing::ElementsAre;
using ::testing::Pair;

#if defined(NDEBUG) && !defined(ADDRESS_SANITIZER) &&              \
    !defined(LEAK_SANITIZER) && !defined(MEMORY_SANITIZER) &&      \
    !defined(THREAD_SANITIZER) && !defined(UNDEFINED_SANITIZER) && \
    !defined(OS_ANDROID)
#define IS_FAST_BUILD
constexpr int kDelayForDeferredUpdatesAfterPageLoad = 150;
#endif

namespace content {

class CrossPlatformAccessibilityBrowserTest : public ContentBrowserTest {
 public:
  CrossPlatformAccessibilityBrowserTest() = default;
  ~CrossPlatformAccessibilityBrowserTest() override = default;

  // Make sure each node in the tree has a unique id.
  void RecursiveAssertUniqueIds(const ui::AXNode* node,
                                std::unordered_set<int>* ids) const {
    ASSERT_TRUE(ids->find(node->id()) == ids->end());
    ids->insert(node->id());
    for (const auto* child : node->children())
      RecursiveAssertUniqueIds(child, ids);
  }

  void SetUp() override;
  void SetUpOnMainThread() override;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    ContentBrowserTest::SetUpCommandLine(command_line);
    // kDisableAXMenuList is true on Chrome OS by default. Make it consistent
    // for these cross-platform tests.
    base::CommandLine::ForCurrentProcess()->AppendSwitchASCII(
        switches::kDisableAXMenuList, "false");
  }

 protected:
  // Choose which feature flags to enable or disable.
  virtual void ChooseFeatures(std::vector<base::Feature>* enabled_features,
                              std::vector<base::Feature>* disabled_features);

  void ExecuteScript(const char* script) {
    shell()->web_contents()->GetMainFrame()->ExecuteJavaScriptForTests(
        base::ASCIIToUTF16(script), base::NullCallback());
  }

  void LoadInitialAccessibilityTreeFromHtml(const std::string& html) {
    AccessibilityNotificationWaiter waiter(shell()->web_contents(),
                                           ui::kAXModeComplete,
                                           ax::mojom::Event::kLoadComplete);
    GURL html_data_url(
        net::EscapeExternalHandlerValue("data:text/html," + html));
    ASSERT_TRUE(NavigateToURL(shell(), html_data_url));
    waiter.WaitForNotification();
  }

  void LoadInitialAccessibilityTreeFromHtmlFilePath(
      const std::string& html_file_path) {
    if (!embedded_test_server()->Started())
      ASSERT_TRUE(embedded_test_server()->Start());
    ASSERT_TRUE(embedded_test_server()->Started());
    AccessibilityNotificationWaiter waiter(shell()->web_contents(),
                                           ui::kAXModeComplete,
                                           ax::mojom::Event::kLoadComplete);
    ASSERT_TRUE(
        NavigateToURL(shell(), embedded_test_server()->GetURL(html_file_path)));
    waiter.WaitForNotification();
  }

  BrowserAccessibilityManager* GetManager() const {
    WebContentsImpl* web_contents =
        static_cast<WebContentsImpl*>(shell()->web_contents());
    return web_contents->GetRootBrowserAccessibilityManager();
  }

  const ui::AXTree& GetAXTree() const {
    const ui::AXTree* ax_tree = GetManager()->ax_tree();
    EXPECT_NE(nullptr, ax_tree);
    return *ax_tree;
  }

  BrowserAccessibility* FindNode(const std::string& name_or_value) {
    return FindNodeInSubtree(*GetManager()->GetRoot(), name_or_value);
  }

  BrowserAccessibility* FindNodeInSubtree(BrowserAccessibility& node,
                                          const std::string& name_or_value) {
    const std::string& name =
        node.GetStringAttribute(ax::mojom::StringAttribute::kName);
    // Note that in the case of a text field,
    // "BrowserAccessibility::GetValueForControl" has the added functionality
    // of computing the value of an ARIA text box from its inner text.
    //
    // <div contenteditable="true" role="textbox">Hello world.</div>
    // Will expose no HTML value attribute, but some screen readers, such as
    // Jaws, VoiceOver and Talkback, require one to be computed.
    const std::string& value = base::UTF16ToUTF8(node.GetValueForControl());
    if (name == name_or_value || value == name_or_value) {
      return &node;
    }

    for (unsigned int i = 0; i < node.PlatformChildCount(); ++i) {
      BrowserAccessibility* result =
          FindNodeInSubtree(*node.PlatformGetChild(i), name_or_value);
      if (result)
        return result;
    }

    return nullptr;
  }

  std::string GetAttr(const ui::AXNode* node,
                      const ax::mojom::StringAttribute attr);
  int GetIntAttr(const ui::AXNode* node, const ax::mojom::IntAttribute attr);
  bool GetBoolAttr(const ui::AXNode* node, const ax::mojom::BoolAttribute attr);

  void PressTabAndWaitForFocusChange() {
    AccessibilityNotificationWaiter waiter(
        shell()->web_contents(), ui::kAXModeComplete,
        ui::AXEventGenerator::Event::FOCUS_CHANGED);
    SimulateKeyPress(shell()->web_contents(), ui::DomKey::TAB, ui::DomCode::TAB,
                     ui::VKEY_TAB, false, false, false, false);
    waiter.WaitForNotification();
  }

  std::string GetNameOfFocusedNode() {
    ui::AXNodeData focused_node_data =
        content::GetFocusedAccessibilityNodeInfo(shell()->web_contents());
    return focused_node_data.GetStringAttribute(
        ax::mojom::StringAttribute::kName);
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;

#if defined(OS_WIN)
  std::unique_ptr<base::win::ScopedCOMInitializer> com_initializer_;
#endif

  DISALLOW_COPY_AND_ASSIGN(CrossPlatformAccessibilityBrowserTest);
};

void CrossPlatformAccessibilityBrowserTest::SetUp() {
  std::vector<base::Feature> enabled_features;
  std::vector<base::Feature> disabled_features;
  ChooseFeatures(&enabled_features, &disabled_features);

  scoped_feature_list_.InitWithFeatures(enabled_features, disabled_features);

  // The <input type="color"> popup tested in
  // AccessibilityInputColorWithPopupOpen requires the ability to read pixels
  // from a Canvas, so we need to be able to produce pixel output.
  EnablePixelOutput();

  ContentBrowserTest::SetUp();
}

void CrossPlatformAccessibilityBrowserTest::ChooseFeatures(
    std::vector<base::Feature>* enabled_features,
    std::vector<base::Feature>* disabled_features) {
  enabled_features->emplace_back(
      features::kEnableAccessibilityExposeHTMLElement);
}

void CrossPlatformAccessibilityBrowserTest::SetUpOnMainThread() {
#if defined(OS_WIN)
  com_initializer_ = std::make_unique<base::win::ScopedCOMInitializer>();
  ui::win::CreateATLModuleIfNeeded();
#endif
}

// Convenience method to get the value of a particular AXNode
// attribute as a UTF-8 string.
std::string CrossPlatformAccessibilityBrowserTest::GetAttr(
    const ui::AXNode* node,
    const ax::mojom::StringAttribute attr) {
  const ui::AXNodeData& data = node->data();
  for (size_t i = 0; i < data.string_attributes.size(); ++i) {
    if (data.string_attributes[i].first == attr)
      return data.string_attributes[i].second;
  }
  return std::string();
}

// Convenience method to get the value of a particular AXNode
// integer attribute.
int CrossPlatformAccessibilityBrowserTest::GetIntAttr(
    const ui::AXNode* node,
    const ax::mojom::IntAttribute attr) {
  const ui::AXNodeData& data = node->data();
  for (size_t i = 0; i < data.int_attributes.size(); ++i) {
    if (data.int_attributes[i].first == attr)
      return data.int_attributes[i].second;
  }
  return -1;
}

// Convenience method to get the value of a particular AXNode
// boolean attribute.
bool CrossPlatformAccessibilityBrowserTest::GetBoolAttr(
    const ui::AXNode* node,
    const ax::mojom::BoolAttribute attr) {
  const ui::AXNodeData& data = node->data();
  for (size_t i = 0; i < data.bool_attributes.size(); ++i) {
    if (data.bool_attributes[i].first == attr)
      return data.bool_attributes[i].second;
  }
  return false;
}

namespace {

// Convenience method to find a node by its role value.
BrowserAccessibility* FindNodeByRole(BrowserAccessibility* root,
                                     ax::mojom::Role role) {
  if (root->GetRole() == role)
    return root;
  for (uint32_t i = 0; i < root->InternalChildCount(); ++i) {
    BrowserAccessibility* child = root->InternalGetChild(i);
    DCHECK(child);
    if (BrowserAccessibility* result = FindNodeByRole(child, role))
      return result;
  }
  return nullptr;
}

}  // namespace

IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       WebpageAccessibility) {
  const std::string url_str(R"HTML(
      <!DOCTYPE html>
      <html>
      <head>
        <title>Accessibility Test</title>
      </head>
      <body>
        <input type="button" value="push">
        <input type="checkbox">
      </body>
      </html>)HTML");
  LoadInitialAccessibilityTreeFromHtml(url_str);

  const ui::AXTree& tree = GetAXTree();
  const ui::AXNode* root = tree.root();

  // Check properties of the tree.
  EXPECT_EQ(net::EscapeExternalHandlerValue("data:text/html," + url_str),
            tree.data().url);
  EXPECT_EQ("Accessibility Test", tree.data().title);
  EXPECT_EQ("html", tree.data().doctype);
  EXPECT_EQ("text/html", tree.data().mimetype);

  // Check properties of the root element of the tree.
  EXPECT_EQ("Accessibility Test",
            GetAttr(root, ax::mojom::StringAttribute::kName));
  EXPECT_EQ(ax::mojom::Role::kRootWebArea, root->data().role);

  // Check properties of the BODY element.
  ASSERT_EQ(1u, root->GetUnignoredChildCount());
  const ui::AXNode* body = root->GetUnignoredChildAtIndex(0);
  EXPECT_EQ(ax::mojom::Role::kGenericContainer, body->data().role);
  EXPECT_EQ("body", GetAttr(body, ax::mojom::StringAttribute::kHtmlTag));
  EXPECT_EQ("block", GetAttr(body, ax::mojom::StringAttribute::kDisplay));

  // Check properties of the two children of the BODY element.
  ASSERT_EQ(2u, body->GetUnignoredChildCount());

  const ui::AXNode* button = body->GetUnignoredChildAtIndex(0);
  EXPECT_EQ(ax::mojom::Role::kButton, button->data().role);
  EXPECT_EQ("input", GetAttr(button, ax::mojom::StringAttribute::kHtmlTag));
  EXPECT_EQ("push", GetAttr(button, ax::mojom::StringAttribute::kName));
  EXPECT_EQ("inline-block",
            GetAttr(button, ax::mojom::StringAttribute::kDisplay));
  EXPECT_THAT(button->data().html_attributes,
              ElementsAre(Pair("type", "button"), Pair("value", "push")));

  const ui::AXNode* checkbox = body->GetUnignoredChildAtIndex(1);
  EXPECT_EQ(ax::mojom::Role::kCheckBox, checkbox->data().role);
  EXPECT_EQ("input", GetAttr(checkbox, ax::mojom::StringAttribute::kHtmlTag));
  EXPECT_EQ("inline-block",
            GetAttr(checkbox, ax::mojom::StringAttribute::kDisplay));
  EXPECT_THAT(checkbox->data().html_attributes,
              ElementsAre(Pair("type", "checkbox")));
}

IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       UnselectedEditableTextAccessibility) {
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
      <body>
        <input value="Hello, world.">
      </body>
      </html>)HTML");

  const ui::AXTree& tree = GetAXTree();
  const ui::AXNode* root = tree.root();
  ASSERT_EQ(1u, root->GetUnignoredChildCount());
  const ui::AXNode* body = root->GetUnignoredChildAtIndex(0);
  ASSERT_EQ(1u, body->GetUnignoredChildCount());
  const ui::AXNode* text = body->GetUnignoredChildAtIndex(0);
  EXPECT_EQ(ax::mojom::Role::kTextField, text->data().role);
  EXPECT_STREQ("input",
               GetAttr(text, ax::mojom::StringAttribute::kHtmlTag).c_str());
  EXPECT_EQ(0, GetIntAttr(text, ax::mojom::IntAttribute::kTextSelStart));
  EXPECT_EQ(0, GetIntAttr(text, ax::mojom::IntAttribute::kTextSelEnd));
  EXPECT_STREQ("Hello, world.", text->GetValueForControl().c_str());

  // TODO(dmazzoni): as soon as more accessibility code is cross-platform,
  // this code should test that the accessible info is dynamically updated
  // if the selection or value changes.
}

IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       SelectedEditableTextAccessibility) {
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
      <body onload="document.body.children[0].select();">
        <input value="Hello, world.">
      </body>
      </html>)HTML");

  const ui::AXTree& tree = GetAXTree();
  const ui::AXNode* root = tree.root();
  ASSERT_EQ(1u, root->GetUnignoredChildCount());
  const ui::AXNode* body = root->GetUnignoredChildAtIndex(0);
  ASSERT_EQ(1u, body->GetUnignoredChildCount());
  const ui::AXNode* text = body->GetUnignoredChildAtIndex(0);
  EXPECT_EQ(ax::mojom::Role::kTextField, text->data().role);
  EXPECT_STREQ("input",
               GetAttr(text, ax::mojom::StringAttribute::kHtmlTag).c_str());
  EXPECT_EQ(0, GetIntAttr(text, ax::mojom::IntAttribute::kTextSelStart));
  EXPECT_EQ(13, GetIntAttr(text, ax::mojom::IntAttribute::kTextSelEnd));
  EXPECT_STREQ("Hello, world.", text->GetValueForControl().c_str());
}

IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       MultipleInheritanceAccessibility2) {
  // Here's a html snippet where Blink puts the same node as a child
  // of two different parents. Instead of checking the exact output, just
  // make sure that no id is reused in the resulting tree.
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
        <script>
          document.writeln('<q><section></section></q><q><li>');
          setTimeout(function() {
            document.close();
          }, 1);
        </script>
      </html>)HTML");

  const ui::AXTree& tree = GetAXTree();
  const ui::AXNode* root = tree.root();
  std::unordered_set<int> ids;
  RecursiveAssertUniqueIds(root, &ids);
}

IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       IframeAccessibility) {
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
      <body>
        <button>Button 1</button>
        <iframe srcdoc="
          <!DOCTYPE html>
          <html>
          <body>
            <button>Button 2</button>
          </body>
          </html>
        "></iframe>
        <button>Button 3</button>
      </body>
      </html>)HTML");

  WaitForAccessibilityTreeToContainNodeWithName(shell()->web_contents(),
                                                "Button 2");

  const ui::AXTree& tree = GetAXTree();
  const ui::AXNode* root = tree.root();
  ASSERT_EQ(1u, root->children().size());

  const ui::AXNode* html_element = root->children()[0];
  ASSERT_EQ(1u, html_element->GetUnignoredChildCount());

  const ui::AXNode* body = html_element->children()[0];
  ASSERT_EQ(3u, body->GetUnignoredChildCount());

  const ui::AXNode* button1 = body->GetUnignoredChildAtIndex(0);
  EXPECT_EQ(ax::mojom::Role::kButton, button1->data().role);
  EXPECT_STREQ("Button 1",
               GetAttr(button1, ax::mojom::StringAttribute::kName).c_str());

  const ui::AXNode* iframe = body->GetUnignoredChildAtIndex(1);
  EXPECT_STREQ("iframe",
               GetAttr(iframe, ax::mojom::StringAttribute::kHtmlTag).c_str());

  // Iframes loaded via the "srcdoc" attribute, (or the now deprecated method of
  // "src=data:text/html,..."), create a new origin context and are thus loaded
  // into a separate accessibility tree. (See "out-of-process cross-origin
  // iframes in Chromium documentation.)
  ASSERT_EQ(0u, iframe->children().size());
  const ui::AXTreeID iframe_tree_id = ui::AXTreeID::FromString(
      GetAttr(iframe, ax::mojom::StringAttribute::kChildTreeId));
  const BrowserAccessibilityManager* iframe_manager =
      BrowserAccessibilityManager::FromID(iframe_tree_id);
  ASSERT_NE(nullptr, iframe_manager);

  const ui::AXNode* sub_document = iframe_manager->GetRootAsAXNode();
  EXPECT_EQ(ax::mojom::Role::kRootWebArea, sub_document->data().role);
  ASSERT_EQ(1u, sub_document->children().size());

  const ui::AXNode* sub_html_element = sub_document->children()[0];
  ASSERT_EQ(1u, sub_html_element->GetUnignoredChildCount());

  const ui::AXNode* sub_body = sub_html_element->children()[0];
  ASSERT_EQ(1u, sub_body->GetUnignoredChildCount());

  const ui::AXNode* button2 = sub_body->GetUnignoredChildAtIndex(0);
  EXPECT_EQ(ax::mojom::Role::kButton, button2->data().role);
  EXPECT_STREQ("Button 2",
               GetAttr(button2, ax::mojom::StringAttribute::kName).c_str());

  const ui::AXNode* button3 = body->GetUnignoredChildAtIndex(2);
  EXPECT_EQ(ax::mojom::Role::kButton, button3->data().role);
  EXPECT_STREQ("Button 3",
               GetAttr(button3, ax::mojom::StringAttribute::kName).c_str());
}

IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       PlatformIframeAccessibility) {
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
      <body>
        <button>Button 1</button>
        <iframe srcdoc="
          <!DOCTYPE html>
          <html>
          <body>
            <button>Button 2</button>
          </body>
          </html>
        "></iframe>
        <button>Button 3</button>
      </body>
      </html>)HTML");

  WaitForAccessibilityTreeToContainNodeWithName(shell()->web_contents(),
                                                "Button 2");

  const BrowserAccessibility* root = GetManager()->GetRoot();
  ASSERT_EQ(1U, root->PlatformChildCount());
  const BrowserAccessibility* body = root->PlatformGetChild(0);
  ASSERT_EQ(3U, body->PlatformChildCount());

  const BrowserAccessibility* button1 = body->PlatformGetChild(0);
  EXPECT_EQ(ax::mojom::Role::kButton, button1->GetData().role);
  EXPECT_STREQ(
      "Button 1",
      GetAttr(button1->node(), ax::mojom::StringAttribute::kName).c_str());

  const BrowserAccessibility* iframe = body->PlatformGetChild(1);
  EXPECT_STREQ(
      "iframe",
      GetAttr(iframe->node(), ax::mojom::StringAttribute::kHtmlTag).c_str());
  EXPECT_EQ(1U, iframe->PlatformChildCount());

  const BrowserAccessibility* sub_document = iframe->PlatformGetChild(0);
  EXPECT_EQ(ax::mojom::Role::kRootWebArea, sub_document->GetData().role);
  ASSERT_EQ(1U, sub_document->PlatformChildCount());

  const BrowserAccessibility* sub_body = sub_document->PlatformGetChild(0);
  ASSERT_EQ(1U, sub_body->PlatformChildCount());

  const BrowserAccessibility* button2 = sub_body->PlatformGetChild(0);
  EXPECT_EQ(ax::mojom::Role::kButton, button2->GetData().role);
  EXPECT_STREQ(
      "Button 2",
      GetAttr(button2->node(), ax::mojom::StringAttribute::kName).c_str());

  const BrowserAccessibility* button3 = body->PlatformGetChild(2);
  EXPECT_EQ(ax::mojom::Role::kButton, button3->GetData().role);
  EXPECT_STREQ(
      "Button 3",
      GetAttr(button3->node(), ax::mojom::StringAttribute::kName).c_str());
}

// Android's text representation is different, so disable the test there.
#if !defined(OS_ANDROID)
IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       AXNodePositionTreeBoundary) {
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
      <body>Text before iframe<iframe srcdoc="
          <!DOCTYPE html>
          <html>
          <body>Text in iframe
          </body>
          </html>">
        </iframe>Text after iframe</body>
      </html>)HTML");

  WaitForAccessibilityTreeToContainNodeWithName(shell()->web_contents(),
                                                "Text in iframe");

  const BrowserAccessibility* root = GetManager()->GetRoot();
  ASSERT_NE(root, nullptr);
  const BrowserAccessibility* body = root->PlatformGetChild(0);
  ASSERT_NE(body, nullptr);
  const BrowserAccessibility* text_before_iframe =
      FindNode("Text before iframe");
  ASSERT_NE(text_before_iframe, nullptr);
  const BrowserAccessibility* iframe = body->PlatformGetChild(1);
  ASSERT_NE(iframe, nullptr);
  const BrowserAccessibility* sub_document = iframe->PlatformGetChild(0);
  ASSERT_NE(sub_document, nullptr);
  const BrowserAccessibility* sub_body = sub_document->PlatformGetChild(0);
  ASSERT_NE(sub_body, nullptr);

  const BrowserAccessibility* text_in_iframe = FindNode("Text in iframe");
  ASSERT_NE(text_in_iframe, nullptr);
  const BrowserAccessibility* text_after_iframe = FindNode("Text after iframe");
  ASSERT_NE(text_after_iframe, nullptr);

  // Start at the beginning of the document. Anchor IDs can vary across
  // platforms and test runs, so only check text offsets and tree IDs. In this
  // case, the tree ID of position should match test_position since a tree
  // boundary is not crossed.
  ui::AXNodePosition::AXPositionInstance position =
      text_before_iframe->CreateTextPositionAt(1);
  EXPECT_EQ(position->text_offset(), 1);
  EXPECT_FALSE(position->AtStartOfAXTree());
  EXPECT_FALSE(position->AtEndOfAXTree());
  ui::AXNodePosition::AXPositionInstance test_position =
      position->CreatePositionAtStartOfAXTree();
  EXPECT_EQ(test_position->tree_id(), position->tree_id());
  EXPECT_EQ(test_position->text_offset(), 0);
  EXPECT_TRUE(test_position->AtStartOfAXTree());
  EXPECT_FALSE(test_position->AtEndOfAXTree());
  test_position = position->CreatePositionAtEndOfAXTree();
  EXPECT_EQ(test_position->tree_id(), position->tree_id());
  EXPECT_EQ(test_position->text_offset(), 17);
  EXPECT_FALSE(test_position->AtStartOfAXTree());
  EXPECT_TRUE(test_position->AtEndOfAXTree());

  // Test inside iframe.
  position = text_in_iframe->CreateTextPositionAt(3);
  EXPECT_EQ(position->text_offset(), 3);
  EXPECT_NE(test_position->tree_id(), position->tree_id());
  EXPECT_FALSE(position->AtStartOfAXTree());
  EXPECT_FALSE(position->AtEndOfAXTree());
  test_position = position->CreatePositionAtStartOfAXTree();
  EXPECT_TRUE(test_position->AtStartOfAXTree());
  EXPECT_FALSE(test_position->AtEndOfAXTree());
  EXPECT_EQ(test_position->tree_id(), position->tree_id());
  EXPECT_EQ(test_position->text_offset(), 0);
  test_position = position->CreatePositionAtEndOfAXTree();
  EXPECT_EQ(test_position->tree_id(), position->tree_id());
  EXPECT_EQ(test_position->text_offset(), 14);
  EXPECT_FALSE(test_position->AtStartOfAXTree());
  EXPECT_TRUE(test_position->AtEndOfAXTree());

  // Test after iframe.
  position = text_after_iframe->CreateTextPositionAt(3);
  EXPECT_FALSE(position->AtStartOfAXTree());
  EXPECT_FALSE(position->AtEndOfAXTree());
  EXPECT_NE(test_position->tree_id(), position->tree_id());
  test_position = position->CreatePositionAtStartOfAXTree();
  EXPECT_EQ(test_position->tree_id(), position->tree_id());
  EXPECT_EQ(test_position->text_offset(), 0);
  EXPECT_TRUE(test_position->AtStartOfAXTree());
  EXPECT_FALSE(test_position->AtEndOfAXTree());
  test_position = position->CreatePositionAtEndOfAXTree();
  EXPECT_EQ(test_position->tree_id(), position->tree_id());
  EXPECT_EQ(test_position->text_offset(), 17);
  EXPECT_FALSE(test_position->AtStartOfAXTree());
  EXPECT_TRUE(test_position->AtEndOfAXTree());
}
#endif  // !defined(OS_ANDROID)

// Android's text representation is different, so disable the test there.
#if !defined(OS_ANDROID)
IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       NavigationSkipsCompositeItems) {
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
      <body>
        <input type="search" placeholder="Sample text">
      </body>
      </html>)HTML");

  WaitForAccessibilityTreeToContainNodeWithName(shell()->web_contents(),
                                                "Sample text");

  const BrowserAccessibility* root = GetManager()->GetRoot();
  ASSERT_NE(root, nullptr);
  const BrowserAccessibility* body = root->PlatformGetChild(0);
  ASSERT_NE(body, nullptr);
  const BrowserAccessibility* input_text = FindNode("Sample text");

  // Create a position rooted at the start of the search input, then perform
  // some AXPosition operations. This will crash if AsTreePosition() is
  // erroneously turned into a null position.
  ui::AXNodePosition::AXPositionInstance position =
      input_text->CreateTextPositionAt(0);
  EXPECT_TRUE(position->IsValid());
  ui::AXNodePosition::AXPositionInstance test_position =
      position->AsTreePosition();
  EXPECT_TRUE(test_position->IsValid());
  EXPECT_EQ(*test_position, *position);
  test_position = position->CreatePositionAtEndOfAnchor();
  EXPECT_TRUE(position->IsValid());
}
#endif  // !defined(OS_ANDROID)

IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       PlatformIterator) {
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
      <body>
        <button>Button 1</button>
        <iframe srcdoc="
          <!DOCTYPE html>
          <html>
          <body>
            <button>Button 2</button>
            <button>Button 3</button>
          </body>
          </html>">
        </iframe>
        <button>Button 4</button>
      </body>
      </html>)HTML");

  WaitForAccessibilityTreeToContainNodeWithName(shell()->web_contents(),
                                                "Button 2");
  const BrowserAccessibility* root = GetManager()->GetRoot();
  BrowserAccessibility::PlatformChildIterator it =
      root->PlatformChildrenBegin();
  EXPECT_EQ(ax::mojom::Role::kGenericContainer, (*it).GetData().role);
  it = (*it).PlatformChildrenBegin();
  EXPECT_STREQ(
      "Button 1",
      GetAttr((*it).node(), ax::mojom::StringAttribute::kName).c_str());
  ++it;
  EXPECT_STREQ(
      "iframe",
      GetAttr((*it).node(), ax::mojom::StringAttribute::kHtmlTag).c_str());
  EXPECT_EQ(1U, (*it).PlatformChildCount());
  auto iframe_iterator = (*it).PlatformChildrenBegin();
  EXPECT_EQ(ax::mojom::Role::kRootWebArea, (*iframe_iterator).GetData().role);
  iframe_iterator = (*iframe_iterator).PlatformChildrenBegin();
  EXPECT_EQ(ax::mojom::Role::kGenericContainer,
            (*iframe_iterator).GetData().role);
  iframe_iterator = (*iframe_iterator).PlatformChildrenBegin();
  EXPECT_STREQ("Button 2", GetAttr((*iframe_iterator).node(),
                                   ax::mojom::StringAttribute::kName)
                               .c_str());
  ++iframe_iterator;
  EXPECT_STREQ("Button 3", GetAttr((*iframe_iterator).node(),
                                   ax::mojom::StringAttribute::kName)
                               .c_str());
  ++it;
  EXPECT_STREQ(
      "Button 4",
      GetAttr((*it).node(), ax::mojom::StringAttribute::kName).c_str());
}

IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       DuplicateChildrenAccessibility) {
  // Here's another html snippet where WebKit has a parent node containing
  // two duplicate child nodes. Instead of checking the exact output, just
  // make sure that no id is reused in the resulting tree.
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
      <body>
        <em>
          <code >
            <h4 >
        </em>
      </body>
      </html>)HTML");

  const ui::AXTree& tree = GetAXTree();
  const ui::AXNode* root = tree.root();
  std::unordered_set<int> ids;
  RecursiveAssertUniqueIds(root, &ids);
}

IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest, WritableElement) {
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
      <body>
        <div role="textbox" tabindex="0">
          Some text
        </div>
      </body>
      </html>)HTML");

  const ui::AXTree& tree = GetAXTree();
  const ui::AXNode* root = tree.root();
  ASSERT_EQ(1u, root->GetUnignoredChildCount());
  const ui::AXNode* textbox = root->GetUnignoredChildAtIndex(0);
  EXPECT_TRUE(textbox->data().HasAction(ax::mojom::Action::kSetValue));
}

IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       AriaSortDirection) {
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
      <body>
        <table>
          <tr>
            <th scope="row" aria-sort="ascending">row header 1</th>
            <th scope="row" aria-sort="descending">row header 2</th>
            <th scope="col" aria-sort="custom">col header 1</th>
            <th scope="col" aria-sort="none">col header 2</th>
            <th scope="col">col header 3</th>
          </tr>
        </table>
      </body>
      </html>)HTML");

  const ui::AXTree& tree = GetAXTree();
  const ui::AXNode* root = tree.root();
  const ui::AXNode* table = root->GetUnignoredChildAtIndex(0);
  EXPECT_EQ(ax::mojom::Role::kTable, table->data().role);
  EXPECT_EQ(1u, table->GetUnignoredChildCount());
  const ui::AXNode* row = table->GetUnignoredChildAtIndex(0);
  EXPECT_EQ(5u, row->GetUnignoredChildCount());

  const ui::AXNode* header1 = row->GetUnignoredChildAtIndex(0);
  const ui::AXNode* header2 = row->GetUnignoredChildAtIndex(1);
  const ui::AXNode* header3 = row->GetUnignoredChildAtIndex(2);
  const ui::AXNode* header4 = row->GetUnignoredChildAtIndex(3);
  const ui::AXNode* header5 = row->GetUnignoredChildAtIndex(4);

  EXPECT_EQ(static_cast<int>(ax::mojom::SortDirection::kAscending),
            GetIntAttr(header1, ax::mojom::IntAttribute::kSortDirection));

  EXPECT_EQ(static_cast<int>(ax::mojom::SortDirection::kDescending),
            GetIntAttr(header2, ax::mojom::IntAttribute::kSortDirection));

  EXPECT_EQ(static_cast<int>(ax::mojom::SortDirection::kOther),
            GetIntAttr(header3, ax::mojom::IntAttribute::kSortDirection));

  EXPECT_EQ(-1, GetIntAttr(header4, ax::mojom::IntAttribute::kSortDirection));
  EXPECT_EQ(-1, GetIntAttr(header5, ax::mojom::IntAttribute::kSortDirection));
}

IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       LocalizedLandmarkType) {
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
      <body>
        <header aria-label="header"></header>
        <aside aria-label="aside"></aside>
        <footer aria-label="footer"></footer>
        <form aria-label="form"></form>
        <main aria-label="main"></main>
        <nav aria-label="nav"></nav>
        <section></section>
        <section aria-label="section"></section>
        <div role="banner" aria-label="banner"></div>
        <div role="complementary" aria-label="complementary"></div>
        <div role="contentinfo" aria-label="contentinfo"></div>
        <div role="form" aria-label="role_form"></div>
        <div role="main" aria-label="role_main"></div>
        <div role="navigation" aria-label="role_nav"></div>
        <div role="region"></div>
        <div role="region" aria-label="region"></div>
        <div role="search" aria-label="search"></div>
      </body>
      </html>)HTML");

  BrowserAccessibility* root = GetManager()->GetRoot();
  ASSERT_NE(nullptr, root);
  ASSERT_EQ(17u, root->PlatformChildCount());

  auto TestLocalizedLandmarkType =
      [root](int child_index, ax::mojom::Role expected_role,
             const std::string& expected_name,
             const std::u16string& expected_localized_landmark_type = {}) {
        BrowserAccessibility* node = root->PlatformGetChild(child_index);
        ASSERT_NE(nullptr, node);

        EXPECT_EQ(expected_role, node->GetRole());
        EXPECT_EQ(expected_name,
                  node->GetStringAttribute(ax::mojom::StringAttribute::kName));
        EXPECT_EQ(expected_localized_landmark_type,
                  node->GetLocalizedStringForLandmarkType());
      };

  // For testing purposes, assume we get en-US localized strings.
  TestLocalizedLandmarkType(0, ax::mojom::Role::kHeader, "header", u"banner");
  TestLocalizedLandmarkType(1, ax::mojom::Role::kComplementary, "aside",
                            u"complementary");
  TestLocalizedLandmarkType(2, ax::mojom::Role::kFooter, "footer",
                            u"content information");
  TestLocalizedLandmarkType(3, ax::mojom::Role::kForm, "form");
  TestLocalizedLandmarkType(4, ax::mojom::Role::kMain, "main");
  TestLocalizedLandmarkType(5, ax::mojom::Role::kNavigation, "nav");
  TestLocalizedLandmarkType(6, ax::mojom::Role::kSection, "");
  TestLocalizedLandmarkType(7, ax::mojom::Role::kSection, "section", u"region");

  TestLocalizedLandmarkType(8, ax::mojom::Role::kBanner, "banner", u"banner");
  TestLocalizedLandmarkType(9, ax::mojom::Role::kComplementary, "complementary",
                            u"complementary");
  TestLocalizedLandmarkType(10, ax::mojom::Role::kContentInfo, "contentinfo",
                            u"content information");
  TestLocalizedLandmarkType(11, ax::mojom::Role::kForm, "role_form");
  TestLocalizedLandmarkType(12, ax::mojom::Role::kMain, "role_main");
  TestLocalizedLandmarkType(13, ax::mojom::Role::kNavigation, "role_nav");
  TestLocalizedLandmarkType(14, ax::mojom::Role::kRegion, "");
  TestLocalizedLandmarkType(15, ax::mojom::Role::kRegion, "region", u"region");
  TestLocalizedLandmarkType(16, ax::mojom::Role::kSearch, "search");
}

// TODO(https://crbug.com/1020456) re-enable when crashing on linux is resolved.
#if defined(OS_LINUX) || defined(OS_CHROMEOS)
#define MAYBE_LocalizedRoleDescription DISABLED_LocalizedRoleDescription
#else
#define MAYBE_LocalizedRoleDescription LocalizedRoleDescription
#endif
IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       MAYBE_LocalizedRoleDescription) {
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
      <body>
        <article></article>
        <audio controls></audio>
        <details></details>
        <figure></figure>
        <footer></footer>
        <header></header>
        <input>
        <input type="color">
        <input type="date">
        <input type="datetime-local">
        <input type="email">
        <input type="month">
        <input type="tel">
        <input type="url">
        <input type="week">
        <mark></mark>
        <meter></meter>
        <output></output>
        <section></section>
        <section aria-label="section"></section>
        <time></time>
        <div role="contentinfo" aria-label="contentinfo"></div>
      </body>
      </html>)HTML");

  BrowserAccessibility* root = GetManager()->GetRoot();
  ASSERT_NE(nullptr, root);
  ASSERT_EQ(22u, root->PlatformChildCount());

  auto TestLocalizedRoleDescription =
      [root](int child_index,
             const std::u16string& expected_localized_role_description = {}) {
        BrowserAccessibility* node = root->PlatformGetChild(child_index);
        ASSERT_NE(nullptr, node);

        EXPECT_EQ(expected_localized_role_description,
                  node->GetLocalizedStringForRoleDescription());
      };

  // For testing purposes, assume we get en-US localized strings.
  TestLocalizedRoleDescription(0, u"article");
  TestLocalizedRoleDescription(1, u"audio");
  TestLocalizedRoleDescription(2, u"details");
  TestLocalizedRoleDescription(3, u"figure");
  TestLocalizedRoleDescription(4, u"footer");
  TestLocalizedRoleDescription(5, u"header");
  TestLocalizedRoleDescription(6, u"");
  TestLocalizedRoleDescription(7, u"color picker");
  TestLocalizedRoleDescription(8, u"date picker");
  TestLocalizedRoleDescription(9, u"local date and time picker");
  TestLocalizedRoleDescription(10, u"email");
  TestLocalizedRoleDescription(11, u"month picker");
  TestLocalizedRoleDescription(12, u"telephone");
  TestLocalizedRoleDescription(13, u"url");
  TestLocalizedRoleDescription(14, u"week picker");
  TestLocalizedRoleDescription(15, u"highlight");
  TestLocalizedRoleDescription(16, u"meter");
  TestLocalizedRoleDescription(17, u"output");
  TestLocalizedRoleDescription(18, u"");
  TestLocalizedRoleDescription(19, u"section");
  TestLocalizedRoleDescription(20, u"time");
  TestLocalizedRoleDescription(21, u"content information");
}

IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       GetStyleNameAttributeAsLocalizedString) {
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
      <body>
        <p>text <mark>mark text</mark></p>
      </body>
      </html>)HTML");

  BrowserAccessibility* root = GetManager()->GetRoot();
  ASSERT_NE(nullptr, root);
  ASSERT_EQ(1u, root->PlatformChildCount());

  auto TestGetStyleNameAttributeAsLocalizedString =
      [](BrowserAccessibility* node, ax::mojom::Role expected_role,
         const std::u16string& expected_localized_style_name_attribute = {}) {
        ASSERT_NE(nullptr, node);

        EXPECT_EQ(expected_role, node->GetRole());
        EXPECT_EQ(expected_localized_style_name_attribute,
                  node->GetStyleNameAttributeAsLocalizedString());
      };

  // For testing purposes, assume we get en-US localized strings.
  BrowserAccessibility* para_node = root->PlatformGetChild(0);
  ASSERT_EQ(2u, para_node->PlatformChildCount());
  TestGetStyleNameAttributeAsLocalizedString(para_node,
                                             ax::mojom::Role::kParagraph);

  BrowserAccessibility* text_node = para_node->PlatformGetChild(0);
  ASSERT_EQ(0u, text_node->PlatformChildCount());
  TestGetStyleNameAttributeAsLocalizedString(text_node,
                                             ax::mojom::Role::kStaticText);

  BrowserAccessibility* mark_node = para_node->PlatformGetChild(1);
  TestGetStyleNameAttributeAsLocalizedString(mark_node, ax::mojom::Role::kMark,
                                             u"highlight");

  // Android doesn't always have a child in this case.
  if (mark_node->PlatformChildCount() > 0u) {
    BrowserAccessibility* mark_text_node = mark_node->PlatformGetChild(0);
    ASSERT_EQ(0u, mark_text_node->PlatformChildCount());
    TestGetStyleNameAttributeAsLocalizedString(
        mark_text_node, ax::mojom::Role::kStaticText, u"highlight");
  }
}

IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       TooltipStringAttributeMutuallyExclusiveOfNameFromTitle) {
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
      <body>
        <input type="text" title="title">
        <input type="text" title="title" aria-labelledby="inputlabel">
        <div id="inputlabel">aria-labelledby</div>
      </body>
      </html>)HTML");

  const ui::AXTree& tree = GetAXTree();
  const ui::AXNode* root = tree.root();
  const ui::AXNode* input1 = root->GetUnignoredChildAtIndex(0);
  const ui::AXNode* input2 = root->GetUnignoredChildAtIndex(1);

  EXPECT_EQ(static_cast<int>(ax::mojom::NameFrom::kTitle),
            GetIntAttr(input1, ax::mojom::IntAttribute::kNameFrom));
  EXPECT_STREQ("title",
               GetAttr(input1, ax::mojom::StringAttribute::kName).c_str());
  EXPECT_STREQ("",
               GetAttr(input1, ax::mojom::StringAttribute::kTooltip).c_str());

  EXPECT_EQ(static_cast<int>(ax::mojom::NameFrom::kRelatedElement),
            GetIntAttr(input2, ax::mojom::IntAttribute::kNameFrom));
  EXPECT_STREQ("aria-labelledby",
               GetAttr(input2, ax::mojom::StringAttribute::kName).c_str());
  EXPECT_STREQ("title",
               GetAttr(input2, ax::mojom::StringAttribute::kTooltip).c_str());
}

IN_PROC_BROWSER_TEST_F(
    CrossPlatformAccessibilityBrowserTest,
    PlaceholderStringAttributeMutuallyExclusiveOfNameFromPlaceholder) {
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
      <body>
        <fieldset>
          <input type="text" placeholder="placeholder">
          <input type="text" placeholder="placeholder" aria-label="label">
        </fieldset>
      </body>
      </html>)HTML");

  const ui::AXTree& tree = GetAXTree();
  const ui::AXNode* root = tree.root();
  const ui::AXNode* group = root->GetUnignoredChildAtIndex(0);
  const ui::AXNode* input1 = group->children()[0];
  const ui::AXNode* input2 = group->children()[1];

  using ax::mojom::StringAttribute;

  EXPECT_EQ(static_cast<int>(ax::mojom::NameFrom::kPlaceholder),
            GetIntAttr(input1, ax::mojom::IntAttribute::kNameFrom));
  EXPECT_STREQ("placeholder", GetAttr(input1, StringAttribute::kName).c_str());
  EXPECT_STREQ("", GetAttr(input1, StringAttribute::kPlaceholder).c_str());

  EXPECT_EQ(static_cast<int>(ax::mojom::NameFrom::kAttribute),
            GetIntAttr(input2, ax::mojom::IntAttribute::kNameFrom));
  EXPECT_STREQ("label", GetAttr(input2, StringAttribute::kName).c_str());
  EXPECT_STREQ("placeholder",
               GetAttr(input2, StringAttribute::kPlaceholder).c_str());
}

// On Android root scroll offset is handled by the Java layer. The final rect
// bounds is device specific.
#if !defined(OS_ANDROID)
IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       GetBoundsRectUnclippedRootFrameFromIFrame) {
  LoadInitialAccessibilityTreeFromHtmlFilePath(
      "/accessibility/html/iframe-padding.html");
  WaitForAccessibilityTreeToContainNodeWithName(shell()->web_contents(),
                                                "Second Button");

  // Get the delegate for the iframe leaf of the top-level accessibility tree
  // for the second iframe.
  BrowserAccessibilityManager* browser_accessibility_manager = GetManager();
  ASSERT_NE(nullptr, browser_accessibility_manager);
  BrowserAccessibility* root_browser_accessibility =
      browser_accessibility_manager->GetRoot();
  ASSERT_NE(nullptr, root_browser_accessibility);
  BrowserAccessibility* leaf_iframe_browser_accessibility =
      root_browser_accessibility->InternalDeepestLastChild();
  ASSERT_NE(nullptr, leaf_iframe_browser_accessibility);
  ASSERT_EQ(ax::mojom::Role::kIframe,
            leaf_iframe_browser_accessibility->GetRole());

  // The frame coordinates of the iframe node within the top-level tree is
  // relative to the top level frame. That is why the top-level default padding
  // is included.
  ASSERT_EQ(gfx::Rect(30, 230, 300, 100).ToString(),
            leaf_iframe_browser_accessibility
                ->GetBoundsRect(ui::AXCoordinateSystem::kRootFrame,
                                ui::AXClippingBehavior::kUnclipped)
                .ToString());

  // Now get the root delegate of the iframe's accessibility tree.
  ui::AXTreeID iframe_tree_id = ui::AXTreeID::FromString(
      leaf_iframe_browser_accessibility->GetStringAttribute(
          ax::mojom::StringAttribute::kChildTreeId));
  BrowserAccessibilityManager* iframe_browser_accessibility_manager =
      BrowserAccessibilityManager::FromID(iframe_tree_id);
  ASSERT_NE(nullptr, iframe_browser_accessibility_manager);
  BrowserAccessibility* root_iframe_browser_accessibility =
      iframe_browser_accessibility_manager->GetRoot();
  ASSERT_NE(nullptr, root_iframe_browser_accessibility);
  ASSERT_EQ(ax::mojom::Role::kRootWebArea,
            root_iframe_browser_accessibility->GetRole());

  // The root frame bounds of the iframe are still relative to the top-level
  // frame.
  ASSERT_EQ(gfx::Rect(30, 230, 300, 100).ToString(),
            root_iframe_browser_accessibility
                ->GetBoundsRect(ui::AXCoordinateSystem::kRootFrame,
                                ui::AXClippingBehavior::kUnclipped)
                .ToString());
}

IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       GetBoundsRectUnclippedFrameFromIFrame) {
  LoadInitialAccessibilityTreeFromHtmlFilePath(
      "/accessibility/html/iframe-padding.html");
  WaitForAccessibilityTreeToContainNodeWithName(shell()->web_contents(),
                                                "Second Button");

  // Get the delegate for the iframe leaf of the top-level accessibility tree
  // for the second iframe.
  BrowserAccessibilityManager* browser_accessibility_manager = GetManager();
  ASSERT_NE(nullptr, browser_accessibility_manager);
  BrowserAccessibility* root_browser_accessibility =
      browser_accessibility_manager->GetRoot();
  ASSERT_NE(nullptr, root_browser_accessibility);
  BrowserAccessibility* leaf_iframe_browser_accessibility =
      root_browser_accessibility->InternalDeepestLastChild();
  ASSERT_NE(nullptr, leaf_iframe_browser_accessibility);
  ASSERT_EQ(ax::mojom::Role::kIframe,
            leaf_iframe_browser_accessibility->GetRole());

  // The frame coordinates of the iframe node within the top-level tree is
  // relative to the top level frame.
  ASSERT_EQ(gfx::Rect(30, 230, 300, 100).ToString(),
            leaf_iframe_browser_accessibility
                ->GetBoundsRect(ui::AXCoordinateSystem::kFrame,
                                ui::AXClippingBehavior::kUnclipped)
                .ToString());

  // Now get the root delegate of the iframe's accessibility tree.
  ui::AXTreeID iframe_tree_id = ui::AXTreeID::FromString(
      leaf_iframe_browser_accessibility->GetStringAttribute(
          ax::mojom::StringAttribute::kChildTreeId));
  BrowserAccessibilityManager* iframe_browser_accessibility_manager =
      BrowserAccessibilityManager::FromID(iframe_tree_id);
  ASSERT_NE(nullptr, iframe_browser_accessibility_manager);
  BrowserAccessibility* root_iframe_browser_accessibility =
      iframe_browser_accessibility_manager->GetRoot();
  ASSERT_NE(nullptr, root_iframe_browser_accessibility);
  ASSERT_EQ(ax::mojom::Role::kRootWebArea,
            root_iframe_browser_accessibility->GetRole());

  // The frame bounds of the iframe are now relative to itself.
  ASSERT_EQ(gfx::Rect(0, 0, 300, 100).ToString(),
            root_iframe_browser_accessibility
                ->GetBoundsRect(ui::AXCoordinateSystem::kFrame,
                                ui::AXClippingBehavior::kUnclipped)
                .ToString());
}

IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       ControlsIdsForDateTimePopup) {
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
      <body>
        <div style="margin-top: 100px;"></div>
        <input type="datetime-local" aria-label="datetime"
               aria-controls="button1">
        <button id="button1">button</button>
      </body>
      </html>)HTML");

  BrowserAccessibilityManager* manager = GetManager();
  ASSERT_NE(nullptr, manager);
  BrowserAccessibility* root = manager->GetRoot();
  ASSERT_NE(nullptr, root);

  // Find the input control, and the popup-button
  BrowserAccessibility* input_control =
      FindNodeByRole(root, ax::mojom::Role::kDateTime);
  ASSERT_NE(nullptr, input_control);
  BrowserAccessibility* popup_control =
      FindNodeByRole(input_control, ax::mojom::Role::kPopUpButton);
  ASSERT_NE(nullptr, popup_control);
  const BrowserAccessibility* sibling_button_control =
      FindNodeByRole(root, ax::mojom::Role::kButton);
  ASSERT_NE(nullptr, sibling_button_control);

  // Get the list of ControlsIds; should initially just point to the sibling
  // button control.
  {
    const auto& controls_ids = input_control->GetIntListAttribute(
        ax::mojom::IntListAttribute::kControlsIds);
    ASSERT_EQ(1u, controls_ids.size());
    EXPECT_EQ(controls_ids[0], sibling_button_control->GetId());
  }

  // Expand the popup, and wait for it to appear
  {
    AccessibilityNotificationWaiter waiter(shell()->web_contents(),
                                           ui::kAXModeComplete,
                                           ax::mojom::Event::kClicked);

    ui::AXActionData action_data;
    action_data.action = ax::mojom::Action::kDoDefault;
    popup_control->AccessibilityPerformAction(action_data);

    waiter.WaitForNotification();
  }

  // Get the list of ControlsIds again; should now also include the popup
  {
    const auto& controls_ids = input_control->GetIntListAttribute(
        ax::mojom::IntListAttribute::kControlsIds);
    ASSERT_EQ(2u, controls_ids.size());
    EXPECT_EQ(controls_ids[0], sibling_button_control->GetId());

    const BrowserAccessibility* popup_area =
        manager->GetFromID(controls_ids[1]);
    ASSERT_NE(nullptr, popup_area);
    EXPECT_EQ(ax::mojom::Role::kRootWebArea, popup_area->GetRole());

#if !BUILDFLAG(IS_CHROMECAST)
    // Ensure that the bounding box of the popup area is at least 100
    // pixels down the page.
    gfx::Rect popup_bounds = popup_area->GetUnclippedRootFrameBoundsRect();
    EXPECT_GT(popup_bounds.y(), 100);
#endif
  }
}

IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       ControlsIdsForColorPopup) {
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
      <body>
        <input type="color" aria-label="color" list="colorlist">
        <datalist id="colorlist">
          <option value="#ff0000">
          <option value="#00ff00">
          <option value="#0000ff">
        </datalist>
      </body>
      </html>)HTML");

  BrowserAccessibilityManager* manager = GetManager();
  ASSERT_NE(nullptr, manager);
  BrowserAccessibility* root = manager->GetRoot();
  ASSERT_NE(nullptr, root);

  // Find the input control
  BrowserAccessibility* input_control =
      FindNodeByRole(root, ax::mojom::Role::kColorWell);
  ASSERT_NE(nullptr, input_control);

  // Get the list of ControlsIds; should initially be empty.
  {
    const auto& controls_ids = input_control->GetIntListAttribute(
        ax::mojom::IntListAttribute::kControlsIds);
    ASSERT_EQ(0u, controls_ids.size());
  }

  // Expand the popup, and wait for it to appear
  {
    AccessibilityNotificationWaiter waiter(shell()->web_contents(),
                                           ui::kAXModeComplete,
                                           ax::mojom::Event::kClicked);

    ui::AXActionData action_data;
    action_data.action = ax::mojom::Action::kDoDefault;
    input_control->AccessibilityPerformAction(action_data);

    waiter.WaitForNotification();
  }

  // Get the list of ControlsIds again; should now include the popup
  {
    const auto& controls_ids = input_control->GetIntListAttribute(
        ax::mojom::IntListAttribute::kControlsIds);
    ASSERT_EQ(1u, controls_ids.size());

    const BrowserAccessibility* popup_area =
        manager->GetFromID(controls_ids[0]);
    ASSERT_NE(nullptr, popup_area);
    EXPECT_EQ(ax::mojom::Role::kRootWebArea, popup_area->GetRole());
  }
}

IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       TextFragmentAnchor) {
  AccessibilityNotificationWaiter anchor_waiter(
      shell()->web_contents(), ui::kAXModeComplete,
      ax::mojom::Event::kScrolledToAnchor);

  GURL url(net::EscapeExternalHandlerValue(R"HTML(data:text/html,
      <p>
        Some text
      </p>
      <p id="target" style="position: absolute; top: 1000px">
        Anchor text
      </p>
      #:~:text=Anchor text)HTML"));
  ASSERT_TRUE(NavigateToURL(shell(), url));

  anchor_waiter.WaitForNotification();
  WaitForAccessibilityTreeToContainNodeWithName(shell()->web_contents(),
                                                "Anchor text");

  const BrowserAccessibility* root = GetManager()->GetRoot();
  ASSERT_EQ(2u, root->PlatformChildCount());
  const BrowserAccessibility* target = root->PlatformGetChild(1);
  ASSERT_EQ(1u, target->PlatformChildCount());
  const BrowserAccessibility* text = target->PlatformGetChild(0);

  EXPECT_EQ(text->GetId(), anchor_waiter.event_target_id());
}

IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest, GeneratedText) {
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
      <head>
        <style>
          h1.generated::before {
            content: "   [   ";
          }
          h1.generated::after {
            content: "   ]    ";
          }
        </style>
      </head>
      <body>
        <h1 class="generated">Foo</h1>
      </body>
      </html>)HTML");

  const BrowserAccessibility* root = GetManager()->GetRoot();
  ASSERT_EQ(1U, root->PlatformChildCount());

  const BrowserAccessibility* heading = root->PlatformGetChild(0);
  ASSERT_EQ(3U, heading->PlatformChildCount());

  const BrowserAccessibility* static1 = heading->PlatformGetChild(0);
  EXPECT_EQ(ax::mojom::Role::kStaticText, static1->GetData().role);
  EXPECT_STREQ(
      "[ ",
      GetAttr(static1->node(), ax::mojom::StringAttribute::kName).c_str());

  const BrowserAccessibility* static2 = heading->PlatformGetChild(1);
  EXPECT_EQ(ax::mojom::Role::kStaticText, static2->GetData().role);
  EXPECT_STREQ(
      "Foo",
      GetAttr(static2->node(), ax::mojom::StringAttribute::kName).c_str());

  const BrowserAccessibility* static3 = heading->PlatformGetChild(2);
  EXPECT_EQ(ax::mojom::Role::kStaticText, static3->GetData().role);
  EXPECT_STREQ(
      " ]",
      GetAttr(static3->node(), ax::mojom::StringAttribute::kName).c_str());
}

IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       FocusFiresJavascriptOnfocus) {
  LoadInitialAccessibilityTreeFromHtmlFilePath(
      "/accessibility/html/iframe-focus.html");
  // There are two iframes in the test page, so wait for both of them to
  // complete loading before proceeding.
  WaitForAccessibilityTreeToContainNodeWithName(shell()->web_contents(),
                                                "Ordinary Button");
  WaitForAccessibilityTreeToContainNodeWithName(shell()->web_contents(),
                                                "Button with focus handler");

  BrowserAccessibilityManager* root_accessibility_manager = GetManager();
  ASSERT_NE(nullptr, root_accessibility_manager);
  BrowserAccessibility* root_browser_accessibility =
      root_accessibility_manager->GetRoot();
  ASSERT_NE(nullptr, root_browser_accessibility);

  // Focus the button within the second iframe to set focus on that document,
  // then set focus on the first iframe (with the Javascript onfocus handler)
  // and ensure onfocus fires there.
  BrowserAccessibility* second_iframe_browser_accessibility =
      root_browser_accessibility->InternalDeepestLastChild();
  ASSERT_NE(nullptr, second_iframe_browser_accessibility);
  BrowserAccessibility* second_iframe_root_browser_accessibility =
      second_iframe_browser_accessibility->PlatformGetChild(0);
  ASSERT_NE(nullptr, second_iframe_root_browser_accessibility);
  BrowserAccessibility* second_button = FindNodeByRole(
      second_iframe_root_browser_accessibility, ax::mojom::Role::kButton);
  ASSERT_NE(nullptr, second_button);
  AccessibilityNotificationWaiter waiter(
      shell()->web_contents(), ui::kAXModeComplete, ax::mojom::Event::kFocus);
  second_iframe_root_browser_accessibility->manager()->SetFocus(*second_button);
  waiter.WaitForNotification();
  EXPECT_EQ(second_button, root_accessibility_manager->GetFocus());

  BrowserAccessibility* first_iframe_browser_accessibility =
      root_browser_accessibility->InternalDeepestFirstChild();
  ASSERT_NE(nullptr, first_iframe_browser_accessibility);
  BrowserAccessibility* first_iframe_root_browser_accessibility =
      first_iframe_browser_accessibility->PlatformGetChild(0);
  ASSERT_NE(nullptr, first_iframe_root_browser_accessibility);
  BrowserAccessibility* first_button = FindNodeByRole(
      first_iframe_root_browser_accessibility, ax::mojom::Role::kButton);
  ASSERT_NE(nullptr, first_button);

  // The page in the first iframe will append the word "Focused" when onfocus is
  // fired, so wait for that node to be added.
  first_iframe_root_browser_accessibility->manager()->SetFocus(*first_button);
  WaitForAccessibilityTreeToContainNodeWithName(shell()->web_contents(),
                                                "Focused");
  EXPECT_EQ(first_button, root_accessibility_manager->GetFocus());
}

IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       IFrameContentHadFocus_ThenRootDocumentGainedFocus) {
  LoadInitialAccessibilityTreeFromHtmlFilePath(
      "/accessibility/html/iframe-padding.html");
  WaitForAccessibilityTreeToContainNodeWithName(shell()->web_contents(),
                                                "Second Button");

  // Get the root BrowserAccessibilityManager and BrowserAccessibility node.
  BrowserAccessibilityManager* root_accessibility_manager = GetManager();
  ASSERT_NE(nullptr, root_accessibility_manager);
  BrowserAccessibility* root_browser_accessibility =
      root_accessibility_manager->GetRoot();
  ASSERT_NE(nullptr, root_browser_accessibility);
  ASSERT_EQ(ax::mojom::Role::kRootWebArea,
            root_browser_accessibility->GetRole());

  // Focus the button within the iframe.
  {
    BrowserAccessibility* leaf_iframe_browser_accessibility =
        root_browser_accessibility->InternalDeepestLastChild();
    ASSERT_NE(nullptr, leaf_iframe_browser_accessibility);
    ASSERT_EQ(ax::mojom::Role::kIframe,
              leaf_iframe_browser_accessibility->GetRole());
    BrowserAccessibility* second_iframe_root_browser_accessibility =
        leaf_iframe_browser_accessibility->PlatformGetChild(0);
    ASSERT_NE(nullptr, second_iframe_root_browser_accessibility);
    ASSERT_EQ(ax::mojom::Role::kRootWebArea,
              second_iframe_root_browser_accessibility->GetRole());
    BrowserAccessibility* second_button = FindNodeByRole(
        second_iframe_root_browser_accessibility, ax::mojom::Role::kButton);
    ASSERT_NE(nullptr, second_button);

    AccessibilityNotificationWaiter waiter(
        shell()->web_contents(), ui::kAXModeComplete, ax::mojom::Event::kFocus);
    second_iframe_root_browser_accessibility->manager()->SetFocus(
        *second_button);
    waiter.WaitForNotification();
    ASSERT_EQ(second_button, root_accessibility_manager->GetFocus());
  }

  // Focusing the root Document should cause the iframe content to blur.
  // The Document Element becomes implicitly focused when the focus is cleared,
  // so there will not be a focus event.
  {
    AccessibilityNotificationWaiter waiter(
        shell()->web_contents(), ui::kAXModeComplete, ax::mojom::Event::kBlur);
    root_accessibility_manager->SetFocus(*root_browser_accessibility);
    waiter.WaitForNotification();
    ASSERT_EQ(root_browser_accessibility,
              root_accessibility_manager->GetFocus());
  }
}
#endif  // !defined(OS_ANDROID)

// This test is checking behavior when ImplicitRootScroller is enabled which
// applies only on Android.
// TODO(http://crbug.com/1137425): Re-enable the test after it gets fixed on
// Android O.
#if defined(OS_ANDROID)
IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       DISABLED_ImplicitRootScroller) {
  LoadInitialAccessibilityTreeFromHtmlFilePath(
      "/accessibility/scrolling/implicit-root-scroller.html");

  BrowserAccessibilityManager* manager = GetManager();
  const BrowserAccessibility* heading =
      FindNodeByRole(manager->GetRoot(), ax::mojom::Role::kHeading);

  // Ensure that this page has an implicit root scroller that's something
  // other than the root of the accessibility tree.
  ui::AXNodeID root_scroller_id = manager->GetTreeData().root_scroller_id;
  BrowserAccessibility* root_scroller = manager->GetFromID(root_scroller_id);
  ASSERT_TRUE(root_scroller);
  EXPECT_NE(root_scroller_id, manager->GetRoot()->GetId());

  // If we take the root scroll offsets into account (most platforms)
  // the heading should be scrolled above the top.
  manager->SetUseRootScrollOffsetsWhenComputingBoundsForTesting(true);
  gfx::Rect bounds = heading->GetUnclippedRootFrameBoundsRect();
  EXPECT_LT(bounds.y(), 0);

  // If we don't take the root scroll offsets into account (Android)
  // the heading should not have a negative top coordinate.
  manager->SetUseRootScrollOffsetsWhenComputingBoundsForTesting(false);
  bounds = heading->GetUnclippedRootFrameBoundsRect();
  EXPECT_GT(bounds.y(), 0);
}
#endif  // defined(OS_ANDROID)

#if defined(IS_FAST_BUILD)  // Avoid flakiness on slower debug/sanitizer builds.
IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       NonInteractiveChangesAreBatched) {
  // Ensure that normal DOM changes are batched together, and do not occur
  // more than once every kDelayForDeferredUpdatesAfterPageLoad.
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
      <body>
        <div id="foo">
        </div>
        <script>
          const startTime = performance.now();
          const fooElem = document.getElementById('foo');
          function addChild() {
            const newChild = document.createElement('div');
            newChild.innerHTML = '<button>x</button>';
            fooElem.appendChild(newChild);
            if (performance.now() - startTime < 1000) {
              requestAnimationFrame(addChild);
            } else {
              document.close();
            }
          }
          addChild();
        </script>
      </body>
      </html>)HTML");

  base::ElapsedTimer timer;
  int num_batches = 0;

  {
    AccessibilityNotificationWaiter waiter(shell()->web_contents(),
                                           ui::kAXModeComplete,
                                           ax::mojom::Event::kLayoutComplete);
    // Run test for 1 second, counting the number of layout completes.
    while (timer.Elapsed().InMilliseconds() < 1000) {
      waiter.WaitForNotificationWithTimeout(
          base::TimeDelta::FromMilliseconds(1000) - timer.Elapsed());
      ++num_batches;
    }
  }

  // In practice, num_batches lines up nicely with the top end expected,
  // so if kDelayForDeferredUpdatesAfterPageLoad == 150, 6-7 batches are likely.
  EXPECT_GT(num_batches, 1);
  EXPECT_LE(num_batches, 1000 / kDelayForDeferredUpdatesAfterPageLoad + 1);
}
#endif

#if defined(IS_FAST_BUILD)  // Avoid flakiness on slower debug/sanitizer builds.
IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       DocumentSelectionChangesAreNotBatched) {
  // Ensure that document selection changes are not batched, and occur faster
  // than once per kDelayForDeferredUpdatesAfterPageLoad.
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
      <body>
        <div id="foo">
        </div>
        <script>
          const startTime = performance.now();
          const fooElem = document.getElementById('foo');
          function addChild() {
            const newChild = document.createElement('div');
            newChild.innerHTML = '<button>x</button>';
            fooElem.appendChild(newChild);
            window.getSelection().selectAllChildren(newChild);
            if (performance.now() - startTime < 1000) {
              requestAnimationFrame(addChild);
            } else {
              document.close();
            }
          }
          addChild();
        </script>
      </body>
      </html>)HTML");

  base::ElapsedTimer timer;
  int num_batches = 0;

  AccessibilityNotificationWaiter waiter(
      shell()->web_contents(), ui::kAXModeComplete,
      ax::mojom::Event::kDocumentSelectionChanged);
  // Run test for 1 second, counting the number of selection changes.
  while (timer.Elapsed().InMilliseconds() < 1000) {
    waiter.WaitForNotificationWithTimeout(
        base::TimeDelta::FromMilliseconds(1000) - timer.Elapsed());
    ++num_batches;
  }

  // In practice, num_batches is about 50 on a fast Linux box.
  EXPECT_GT(num_batches, 1000 / kDelayForDeferredUpdatesAfterPageLoad);
}
#endif  // IS_FAST_BUILD

#if defined(IS_FAST_BUILD)  // Avoid flakiness on slower debug/sanitizer builds.
IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       ActiveDescendantChangesAreNotBatched) {
  // Ensure that active descendant changes are not batched, and occur faster
  // than once per kDelayForDeferredUpdatesAfterPageLoad.
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
      <body>
        <div id="foo" tabindex="0" autofocus>
        </div>
        <script>
          const startTime = performance.now();
          const fooElem = document.getElementById('foo');
          let count = 0;
          function addChild() {
            const newChild = document.createElement('div');
            ++count;
            newChild.innerHTML = '<button id=' + count + '>x</button>';
            fooElem.appendChild(newChild);
            fooElem.setAttribute('aria-activedescendant', count);
            if (performance.now() - startTime < 1000) {
              requestAnimationFrame(addChild);
            } else {
              document.close();
            }
          }
          addChild();
        </script>
      </body>
      </html>)HTML");

  base::ElapsedTimer timer;
  int num_batches = 0;

  {
    AccessibilityNotificationWaiter waiter(
        shell()->web_contents(), ui::kAXModeComplete,
        ui::AXEventGenerator::Event::ACTIVE_DESCENDANT_CHANGED);
    // Run test for 1 second, counting the number of active descendant changes.
    while (timer.Elapsed().InMilliseconds() < 1000) {
      waiter.WaitForNotificationWithTimeout(
          base::TimeDelta::FromMilliseconds(1000) - timer.Elapsed());
      ++num_batches;
    }
  }

  // In practice, num_batches is about 50 on a fast Linux box.
  EXPECT_GT(num_batches, 1000 / kDelayForDeferredUpdatesAfterPageLoad);
}
#endif  // IS_FAST_BUILD

IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       AccessibilityAddClickListener) {
  // This is a regression test for a bug where a node is ignored in the
  // accessibility tree (in this case the BODY), and then by adding a click
  // listener to it we can make it no longer ignored without correctly firing
  // the right notifications - with the end result being that the whole
  // accessibility tree is broken.
  LoadInitialAccessibilityTreeFromHtml(R"HTML(
      <!DOCTYPE html>
      <html>
      <body>
        <div>
          <button>This should be accessible</button>
        </div>
      </body>
      </html>)HTML");

  BrowserAccessibilityManager* browser_accessibility_manager = GetManager();
  BrowserAccessibility* root_browser_accessibility =
      browser_accessibility_manager->GetRoot();
  ASSERT_NE(root_browser_accessibility, nullptr);

  const ui::AXNode* root_node = root_browser_accessibility->node();
  ASSERT_NE(root_node, nullptr);
  const ui::AXNode* html_node = root_node->children()[0];
  ASSERT_NE(html_node, nullptr);
  const ui::AXNode* body_node = html_node->children()[0];
  ASSERT_NE(body_node, nullptr);

  // Make sure this is actually the body element.
  ASSERT_EQ(body_node->GetStringAttribute(ax::mojom::StringAttribute::kHtmlTag),
            "body");
  ASSERT_TRUE(body_node->IsIgnored());

  AccessibilityNotificationWaiter waiter(
      shell()->web_contents(), ui::kAXModeComplete,
      ui::AXEventGenerator::Event::IGNORED_CHANGED);
  ExecuteScript("document.body.addEventListener('mousedown', function() {});");
  waiter.WaitForNotification();

  // The body should no longer be ignored after adding a mouse button listener.
  ASSERT_FALSE(body_node->IsIgnored());
}

IN_PROC_BROWSER_TEST_F(CrossPlatformAccessibilityBrowserTest,
                       NavigateInIframe) {
  LoadInitialAccessibilityTreeFromHtmlFilePath(
      "/accessibility/regression/iframe-navigation.html");
  WaitForAccessibilityTreeToContainNodeWithName(shell()->web_contents(),
                                                "Go to Inner 2");

  // Keep pressing Tab until we get to the "Go to Inner 2" link in the
  // inner iframe.
  while (GetNameOfFocusedNode() != "Go to Inner 2") {
    PressTabAndWaitForFocusChange();
  }

  // Press enter to activate the link, wait for the second iframe to load.
  {
    AccessibilityNotificationWaiter waiter(
        shell()->web_contents(), ui::kAXModeComplete,
        ui::AXEventGenerator::Event::LOAD_COMPLETE);
    SimulateKeyPress(shell()->web_contents(), ui::DomKey::ENTER,
                     ui::DomCode::ENTER, ui::VKEY_RETURN, false, false, false,
                     false);
    waiter.WaitForNotification();
  }

  // Press Tab, we should eventually land on the last button within the
  // second iframe.
  while (GetNameOfFocusedNode() != "Bottom of Inner 2") {
    PressTabAndWaitForFocusChange();
  }
}

}  // namespace content
