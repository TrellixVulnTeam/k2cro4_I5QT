// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/browser_plugin/browser_plugin_browsertest.h"

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/memory/singleton.h"
#include "base/path_service.h"
#include "content/common/browser_plugin_messages.h"
#include "content/public/common/content_constants.h"
#include "content/renderer/browser_plugin/browser_plugin.h"
#include "content/renderer/browser_plugin/browser_plugin_manager_factory.h"
#include "content/renderer/browser_plugin/mock_browser_plugin.h"
#include "content/renderer/browser_plugin/mock_browser_plugin_manager.h"
#include "content/renderer/render_thread_impl.h"
#include "content/renderer/renderer_webkitplatformsupport_impl.h"
#include "skia/ext/platform_canvas.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebCursorInfo.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebInputEvent.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebScriptSource.h"

namespace {
const char kHTMLForBrowserPluginObject[] =
  "<object id='browserplugin' width='640px' height='480px'"
  "  src='foo' type='%s'>";

const char kHTMLForSourcelessPluginObject[] =
  "<object id='browserplugin' width='640px' height='480px' type='%s'>";

const char kHTMLForPartitionedPluginObject[] =
  "<object id='browserplugin' width='640px' height='480px'"
  "  src='foo' type='%s' partition='someid'>";

const char kHTMLForInvalidPartitionedPluginObject[] =
  "<object id='browserplugin' width='640px' height='480px'"
  "  type='%s' partition='persist:'>";

const char kHTMLForPartitionedPersistedPluginObject[] =
  "<object id='browserplugin' width='640px' height='480px'"
  "  src='foo' type='%s' partition='persist:someid'>";

std::string GetHTMLForBrowserPluginObject() {
  return StringPrintf(kHTMLForBrowserPluginObject,
                      content::kBrowserPluginMimeType);
}

}  // namespace

namespace content {

// Test factory for creating test instances of BrowserPluginManager.
class TestBrowserPluginManagerFactory : public BrowserPluginManagerFactory {
 public:
  virtual MockBrowserPluginManager* CreateBrowserPluginManager(
      RenderViewImpl* render_view) OVERRIDE {
    return new MockBrowserPluginManager(render_view);
  }

  // Singleton getter.
  static TestBrowserPluginManagerFactory* GetInstance() {
    return Singleton<TestBrowserPluginManagerFactory>::get();
  }

 protected:
  TestBrowserPluginManagerFactory() {}
  virtual ~TestBrowserPluginManagerFactory() {}

 private:
  // For Singleton.
  friend struct DefaultSingletonTraits<TestBrowserPluginManagerFactory>;

  DISALLOW_COPY_AND_ASSIGN(TestBrowserPluginManagerFactory);
};

BrowserPluginTest::BrowserPluginTest() {}

BrowserPluginTest::~BrowserPluginTest() {}

void BrowserPluginTest::SetUp() {
  GetContentClient()->set_renderer_for_testing(&content_renderer_client_);
  BrowserPluginManager::set_factory_for_testing(
      TestBrowserPluginManagerFactory::GetInstance());
  content::RenderViewTest::SetUp();

}

void BrowserPluginTest::TearDown() {
  browser_plugin_manager()->Cleanup();
  BrowserPluginManager::set_factory_for_testing(
      TestBrowserPluginManagerFactory::GetInstance());
  content::RenderViewTest::TearDown();
}

std::string BrowserPluginTest::ExecuteScriptAndReturnString(
    const std::string& script) {
  v8::Handle<v8::Value>  value = GetMainFrame()->executeScriptAndReturnValue(
      WebKit::WebScriptSource(WebKit::WebString::fromUTF8(script.c_str())));
  if (value.IsEmpty() || !value->IsString())
    return std::string();

  v8::Local<v8::String> v8_str = value->ToString();
  int length = v8_str->Utf8Length() + 1;
  scoped_array<char> str(new char[length]);
  v8_str->WriteUtf8(str.get(), length);
  return str.get();
}

int BrowserPluginTest::ExecuteScriptAndReturnInt(
    const std::string& script) {
  v8::Handle<v8::Value>  value = GetMainFrame()->executeScriptAndReturnValue(
      WebKit::WebScriptSource(WebKit::WebString::fromUTF8(script.c_str())));
  if (value.IsEmpty() || !value->IsInt32())
    return 0;

  return value->Int32Value();
}

// This test verifies that an initial resize occurs when we instantiate the
// browser plugin. This test also verifies that the browser plugin is waiting
// for a BrowserPluginMsg_UpdateRect in response. We issue an UpdateRect, and
// we observe an UpdateRect_ACK, with the resize_pending_ reset, indiciating
// that the BrowserPlugin is not waiting for any more UpdateRects to
// satisfy its resize request.
TEST_F(BrowserPluginTest, InitialResize) {
  LoadHTML(GetHTMLForBrowserPluginObject().c_str());
  // Verify that the information based on ResizeGuest is correct, and
  // use its TransportDIB::Id to paint.
  const IPC::Message* msg =
      browser_plugin_manager()->sink().GetUniqueMessageMatching(
          BrowserPluginHostMsg_ResizeGuest::ID);
  ASSERT_TRUE(msg);
  PickleIterator iter = IPC::SyncMessage::GetDataIterator(msg);
  BrowserPluginHostMsg_ResizeGuest::SendParam  resize_params;
  ASSERT_TRUE(IPC::ReadParam(msg, &iter, &resize_params));
  int instance_id = resize_params.a;
  BrowserPluginHostMsg_ResizeGuest_Params params(resize_params.b);
  EXPECT_EQ(640, params.width);
  EXPECT_EQ(480, params.height);
  // Verify that the browser plugin wasn't already waiting on a resize when this
  // resize happened.
  EXPECT_FALSE(params.resize_pending);

  MockBrowserPlugin* browser_plugin =
      static_cast<MockBrowserPlugin*>(
          browser_plugin_manager()->GetBrowserPlugin(instance_id));
  ASSERT_TRUE(browser_plugin);
  // Now the browser plugin is expecting a UpdateRect resize.
  EXPECT_TRUE(browser_plugin->resize_pending_);

  // Send the BrowserPlugin an UpdateRect equal to its container size.
  // That should clear the resize_pending_ flag.
  BrowserPluginMsg_UpdateRect_Params update_rect_params;
  update_rect_params.view_size = gfx::Size(640, 480);
  update_rect_params.scale_factor = 1.0f;
  update_rect_params.is_resize_ack = true;
  browser_plugin->UpdateRect(0, update_rect_params);
  EXPECT_FALSE(browser_plugin->resize_pending_);
}

// Verify that the src attribute on the browser plugin works as expected.
TEST_F(BrowserPluginTest, SrcAttribute) {
  LoadHTML(GetHTMLForBrowserPluginObject().c_str());
  // Verify that we're reporting the correct URL to navigate to based on the
  // src attribute.
  {
    // Ensure we get a CreateGuest on the initial navigation.
    const IPC::Message* create_msg =
    browser_plugin_manager()->sink().GetUniqueMessageMatching(
        BrowserPluginHostMsg_CreateGuest::ID);
    ASSERT_TRUE(create_msg);

    const IPC::Message* msg =
    browser_plugin_manager()->sink().GetUniqueMessageMatching(
        BrowserPluginHostMsg_NavigateGuest::ID);
    ASSERT_TRUE(msg);

    int instance_id;
    std::string src;
    BrowserPluginHostMsg_ResizeGuest_Params resize_params;
    BrowserPluginHostMsg_NavigateGuest::Read(
        msg,
        &instance_id,
        &src,
        &resize_params);
    EXPECT_EQ("foo", src);
  }

  browser_plugin_manager()->sink().ClearMessages();
  // Navigate to bar and observe the associated
  // BrowserPluginHostMsg_NavigateGuest message.
  // Verify that the src attribute is updated as well.
  ExecuteJavaScript("document.getElementById('browserplugin').src = 'bar'");
  {
    // Verify that we do not get a CreateGuest on subsequent navigations.
    const IPC::Message* create_msg =
    browser_plugin_manager()->sink().GetUniqueMessageMatching(
        BrowserPluginHostMsg_CreateGuest::ID);
    ASSERT_FALSE(create_msg);

    const IPC::Message* msg =
      browser_plugin_manager()->sink().GetUniqueMessageMatching(
          BrowserPluginHostMsg_NavigateGuest::ID);
    ASSERT_TRUE(msg);

    int instance_id;
    std::string src;
    BrowserPluginHostMsg_ResizeGuest_Params resize_params;
    BrowserPluginHostMsg_NavigateGuest::Read(
        msg,
        &instance_id,
        &src,
        &resize_params);
    EXPECT_EQ("bar", src);
    std::string src_value =
        ExecuteScriptAndReturnString(
            "document.getElementById('browserplugin').src");
    EXPECT_EQ("bar", src_value);
  }
}

TEST_F(BrowserPluginTest, ResizeFlowControl) {
  LoadHTML(GetHTMLForBrowserPluginObject().c_str());
  browser_plugin_manager()->sink().ClearMessages();

  // Resize the browser plugin three times.
  ExecuteJavaScript("document.getElementById('browserplugin').width = '641px'");
  ProcessPendingMessages();
  ExecuteJavaScript("document.getElementById('browserplugin').width = '642px'");
  ProcessPendingMessages();
  ExecuteJavaScript("document.getElementById('browserplugin').width = '643px'");
  ProcessPendingMessages();

  // Expect to see three messsages in the sink.
  EXPECT_EQ(3u, browser_plugin_manager()->sink().message_count());
  const IPC::Message* msg =
      browser_plugin_manager()->sink().GetFirstMessageMatching(
          BrowserPluginHostMsg_ResizeGuest::ID);
  ASSERT_TRUE(msg);
  PickleIterator iter = IPC::SyncMessage::GetDataIterator(msg);
  BrowserPluginHostMsg_ResizeGuest::SendParam  resize_params;
  ASSERT_TRUE(IPC::ReadParam(msg, &iter, &resize_params));
  int instance_id = resize_params.a;
  BrowserPluginHostMsg_ResizeGuest_Params params(resize_params.b);
  EXPECT_EQ(641, params.width);
  EXPECT_EQ(480, params.height);
  // This indicates that the BrowserPlugin has sent out a previous resize
  // request but has not yet received an UpdateRect for that request.
  // We send this resize regardless to update the damage buffer in the
  // browser process, so it's ready when the guest sends the appropriate
  // UpdateRect.
  EXPECT_TRUE(params.resize_pending);

  MockBrowserPlugin* browser_plugin =
      static_cast<MockBrowserPlugin*>(
          browser_plugin_manager()->GetBrowserPlugin(instance_id));
  ASSERT_TRUE(browser_plugin);
  {
    // We send a stale UpdateRect to the BrowserPlugin.
    BrowserPluginMsg_UpdateRect_Params update_rect_params;
    update_rect_params.view_size = gfx::Size(640, 480);
    update_rect_params.scale_factor = 1.0f;
    update_rect_params.is_resize_ack = true;
    browser_plugin->UpdateRect(0, update_rect_params);
    // This tells us that the BrowserPlugin is still expecting another
    // UpdateRect with the most recent size.
    EXPECT_TRUE(browser_plugin->resize_pending_);
  }
  {
    BrowserPluginMsg_UpdateRect_Params update_rect_params;
    update_rect_params.view_size = gfx::Size(643, 480);
    update_rect_params.scale_factor = 1.0f;
    update_rect_params.is_resize_ack = true;
    browser_plugin->UpdateRect(0, update_rect_params);
    // The BrowserPlugin has finally received an UpdateRect that satisifes
    // its current size, and so it is happy.
    EXPECT_FALSE(browser_plugin->resize_pending_);
  }
}

TEST_F(BrowserPluginTest, GuestCrash) {
  LoadHTML(GetHTMLForBrowserPluginObject().c_str());

  // Grab the BrowserPlugin's instance ID from its resize message.
  const IPC::Message* msg =
      browser_plugin_manager()->sink().GetFirstMessageMatching(
          BrowserPluginHostMsg_ResizeGuest::ID);
  ASSERT_TRUE(msg);
  PickleIterator iter = IPC::SyncMessage::GetDataIterator(msg);
  BrowserPluginHostMsg_ResizeGuest::SendParam  resize_params;
  ASSERT_TRUE(IPC::ReadParam(msg, &iter, &resize_params));
  int instance_id = resize_params.a;

  MockBrowserPlugin* browser_plugin =
      static_cast<MockBrowserPlugin*>(
          browser_plugin_manager()->GetBrowserPlugin(instance_id));
  ASSERT_TRUE(browser_plugin);

  WebKit::WebCursorInfo cursor_info;
  // Send an event and verify that the event is deported.
  browser_plugin->handleInputEvent(WebKit::WebMouseEvent(),
                                   cursor_info);
  EXPECT_TRUE(browser_plugin_manager()->sink().GetUniqueMessageMatching(
      BrowserPluginHostMsg_HandleInputEvent::ID));
  browser_plugin_manager()->sink().ClearMessages();

  const char* kAddEventListener =
    "var msg;"
    "function exitListener(e) {"
    "  msg = JSON.parse(e.detail).reason;"
    "}"
    "document.getElementById('browserplugin')."
    "    addEventListener('-internal-exit', exitListener);";

  ExecuteJavaScript(kAddEventListener);

  // Pretend that the guest has terminated normally.
  browser_plugin->GuestGone(0, base::TERMINATION_STATUS_NORMAL_TERMINATION);

  // Verify that our event listener has fired.
  EXPECT_EQ("normal", ExecuteScriptAndReturnString("msg"));

  // Pretend that the guest has crashed.
  browser_plugin->GuestGone(0, base::TERMINATION_STATUS_PROCESS_CRASHED);

  // Verify that our event listener has fired.
  EXPECT_EQ("crashed", ExecuteScriptAndReturnString("msg"));

  // Send an event and verify that events are no longer deported.
  browser_plugin->handleInputEvent(WebKit::WebMouseEvent(),
                                   cursor_info);
  EXPECT_FALSE(browser_plugin_manager()->sink().GetUniqueMessageMatching(
      BrowserPluginHostMsg_HandleInputEvent::ID));
}

TEST_F(BrowserPluginTest, RemovePlugin) {
  LoadHTML(GetHTMLForBrowserPluginObject().c_str());
  EXPECT_FALSE(browser_plugin_manager()->sink().GetUniqueMessageMatching(
      BrowserPluginHostMsg_PluginDestroyed::ID));
  ExecuteJavaScript("x = document.getElementById('browserplugin'); "
                    "x.parentNode.removeChild(x);");
  ProcessPendingMessages();
  EXPECT_TRUE(browser_plugin_manager()->sink().GetUniqueMessageMatching(
      BrowserPluginHostMsg_PluginDestroyed::ID));
}

TEST_F(BrowserPluginTest, CustomEvents) {
  const char* kAddEventListener =
    "var url;"
    "function nav(e) {"
    "  url = JSON.parse(e.detail).url;"
    "}"
    "document.getElementById('browserplugin')."
    "    addEventListener('-internal-loadcommit', nav);";
  const char* kRemoveEventListener =
    "document.getElementById('browserplugin')."
    "    removeEventListener('-internal-loadcommit', nav);";
  const char* kGetProcessID =
      "document.getElementById('browserplugin').getProcessId()";
  const char* kGoogleURL = "http://www.google.com/";
  const char* kGoogleNewsURL = "http://news.google.com/";

  LoadHTML(GetHTMLForBrowserPluginObject().c_str());
  ExecuteJavaScript(kAddEventListener);
  // Grab the BrowserPlugin's instance ID from its resize message.
  const IPC::Message* msg =
      browser_plugin_manager()->sink().GetFirstMessageMatching(
          BrowserPluginHostMsg_ResizeGuest::ID);
  ASSERT_TRUE(msg);
  PickleIterator iter = IPC::SyncMessage::GetDataIterator(msg);
  BrowserPluginHostMsg_ResizeGuest::SendParam resize_params;
  ASSERT_TRUE(IPC::ReadParam(msg, &iter, &resize_params));
  int instance_id = resize_params.a;

  MockBrowserPlugin* browser_plugin =
      static_cast<MockBrowserPlugin*>(
          browser_plugin_manager()->GetBrowserPlugin(instance_id));
  ASSERT_TRUE(browser_plugin);

  {
    BrowserPluginMsg_LoadCommit_Params navigate_params;
    navigate_params.url = GURL(kGoogleURL);
    navigate_params.process_id = 1337;
    browser_plugin->LoadCommit(navigate_params);
    EXPECT_EQ(kGoogleURL, ExecuteScriptAndReturnString("url"));
    EXPECT_EQ(1337, ExecuteScriptAndReturnInt(kGetProcessID));
  }
  ExecuteJavaScript(kRemoveEventListener);
  {
    BrowserPluginMsg_LoadCommit_Params navigate_params;
    navigate_params.url = GURL(kGoogleNewsURL);
    navigate_params.process_id = 42;
    browser_plugin->LoadCommit(navigate_params);
    // The URL variable should not change because we've removed the event
    // listener.
    EXPECT_EQ(kGoogleURL, ExecuteScriptAndReturnString("url"));
    EXPECT_EQ(42, ExecuteScriptAndReturnInt(kGetProcessID));
  }
}

TEST_F(BrowserPluginTest, StopMethod) {
  const char* kCallStop =
    "document.getElementById('browserplugin').stop();";
  LoadHTML(GetHTMLForBrowserPluginObject().c_str());
  ExecuteJavaScript(kCallStop);
  EXPECT_TRUE(browser_plugin_manager()->sink().GetUniqueMessageMatching(
      BrowserPluginHostMsg_Stop::ID));
}

TEST_F(BrowserPluginTest, ReloadMethod) {
  const char* kCallReload =
    "document.getElementById('browserplugin').reload();";
  LoadHTML(GetHTMLForBrowserPluginObject().c_str());
  ExecuteJavaScript(kCallReload);
  EXPECT_TRUE(browser_plugin_manager()->sink().GetUniqueMessageMatching(
      BrowserPluginHostMsg_Reload::ID));
}


// Verify that the 'partition' attribute on the browser plugin is parsed
// correctly.
TEST_F(BrowserPluginTest, PartitionAttribute) {
  std::string html = StringPrintf(kHTMLForPartitionedPluginObject,
                                  content::kBrowserPluginMimeType);
  LoadHTML(html.c_str());
  std::string partition_value = ExecuteScriptAndReturnString(
      "document.getElementById('browserplugin').partition");
  EXPECT_STREQ("someid", partition_value.c_str());

  html = StringPrintf(kHTMLForPartitionedPersistedPluginObject,
                      content::kBrowserPluginMimeType);
  LoadHTML(html.c_str());
  partition_value = ExecuteScriptAndReturnString(
      "document.getElementById('browserplugin').partition");
  EXPECT_STREQ("persist:someid", partition_value.c_str());

  // Verify that once HTML has defined a source and partition, we cannot change
  // the partition anymore.
  ExecuteJavaScript(
      "try {"
      "  document.getElementById('browserplugin').partition = 'foo';"
      "  document.title = 'success';"
      "} catch (e) { document.title = e.message; }");
  std::string title = ExecuteScriptAndReturnString("document.title");
  EXPECT_STREQ(
      "The object has already navigated, so its partition cannot be changed.",
      title.c_str());

  // Load a browser tag without 'src' defined.
  html = StringPrintf(kHTMLForSourcelessPluginObject,
                      content::kBrowserPluginMimeType);
  LoadHTML(html.c_str());

  // Ensure we don't parse just "persist:" string and return exception.
  ExecuteJavaScript(
      "try {"
      "  document.getElementById('browserplugin').partition = 'persist:';"
      "  document.title = 'success';"
      "} catch (e) { document.title = e.message; }");
  title = ExecuteScriptAndReturnString("document.title");
  EXPECT_STREQ("Invalid partition attribute.", title.c_str());
}

// This test verifies that BrowserPlugin enters an error state when the
// partition attribute is invalid.
TEST_F(BrowserPluginTest, InvalidPartition) {
  std::string html = StringPrintf(kHTMLForInvalidPartitionedPluginObject,
                                  content::kBrowserPluginMimeType);
  LoadHTML(html.c_str());
  // Attempt to navigate with an invalid partition.
  {
    ExecuteJavaScript(
        "try {"
        "  document.getElementById('browserplugin').src = 'bar';"
        "  document.title = 'success';"
        "} catch (e) { document.title = e.message; }");
    std::string title = ExecuteScriptAndReturnString("document.title");
    EXPECT_STREQ("Invalid partition attribute.", title.c_str());
    // Verify that the 'src' attribute has not been updated.
    EXPECT_EQ("", ExecuteScriptAndReturnString(
        "document.getElementById('browserplugin').src"));
  }

  // Verify that the BrowserPlugin accepts changes to its src attribue after
  // setting the partition to a valid value.
  ExecuteJavaScript(
      "document.getElementById('browserplugin').partition = 'persist:foo'");
  ExecuteJavaScript("document.getElementById('browserplugin').src = 'bar'");
  EXPECT_EQ("bar", ExecuteScriptAndReturnString(
      "document.getElementById('browserplugin').src"));
  // Verify that the BrowserPlugin does not 'deadlock': it can recover from
  // the partition ID error state.
  {
    ExecuteJavaScript(
        "try {"
        "  document.getElementById('browserplugin').partition = 'persist:1337';"
        "  document.title = 'success';"
        "} catch (e) { document.title = e.message; }");
    std::string title = ExecuteScriptAndReturnString("document.title");
    EXPECT_STREQ(
        "The object has already navigated, so its partition cannot be changed.",
        title.c_str());
    ExecuteJavaScript("document.getElementById('browserplugin').src = '42'");
    EXPECT_EQ("42", ExecuteScriptAndReturnString(
        "document.getElementById('browserplugin').src"));
  }
}

// Test to verify that after the first navigation, the partition attribute
// cannot be modified.
TEST_F(BrowserPluginTest, ImmutableAttributesAfterNavigation) {
  std::string html = StringPrintf(kHTMLForSourcelessPluginObject,
                                  content::kBrowserPluginMimeType);
  LoadHTML(html.c_str());

  ExecuteJavaScript(
      "document.getElementById('browserplugin').partition = 'storage'");
  std::string partition_value = ExecuteScriptAndReturnString(
      "document.getElementById('browserplugin').partition");
  EXPECT_STREQ("storage", partition_value.c_str());

  std::string src_value = ExecuteScriptAndReturnString(
      "document.getElementById('browserplugin').src");
  EXPECT_STREQ("", src_value.c_str());

  ExecuteJavaScript("document.getElementById('browserplugin').src = 'bar'");
  {
    const IPC::Message* create_msg =
    browser_plugin_manager()->sink().GetUniqueMessageMatching(
        BrowserPluginHostMsg_CreateGuest::ID);
    ASSERT_TRUE(create_msg);

    int create_instance_id;
    BrowserPluginHostMsg_CreateGuest_Params params;
    BrowserPluginHostMsg_CreateGuest::Read(
        create_msg,
        &create_instance_id,
        &params);
    EXPECT_STREQ("storage", params.storage_partition_id.c_str());
    EXPECT_FALSE(params.persist_storage);

    const IPC::Message* msg =
        browser_plugin_manager()->sink().GetUniqueMessageMatching(
            BrowserPluginHostMsg_NavigateGuest::ID);
    ASSERT_TRUE(msg);

    int instance_id;
    std::string src;
    BrowserPluginHostMsg_ResizeGuest_Params resize_params;
    BrowserPluginHostMsg_NavigateGuest::Read(
        msg,
        &instance_id,
        &src,
        &resize_params);
    EXPECT_STREQ("bar", src.c_str());
    EXPECT_EQ(create_instance_id, instance_id);
  }

  // Setting the partition should throw an exception and the value should not
  // change.
  ExecuteJavaScript(
      "try {"
      "  document.getElementById('browserplugin').partition = 'someid';"
      "  document.title = 'success';"
      "} catch (e) { document.title = e.message; }");

  std::string title = ExecuteScriptAndReturnString("document.title");
  EXPECT_STREQ(
      "The object has already navigated, so its partition cannot be changed.",
      title.c_str());

  partition_value = ExecuteScriptAndReturnString(
      "document.getElementById('browserplugin').partition");
  EXPECT_STREQ("storage", partition_value.c_str());
}

// This test verifies that we can mutate the event listener vector
// within an event listener.
TEST_F(BrowserPluginTest, RemoveEventListenerInEventListener) {
  const char* kAddEventListener =
    "var url;"
    "function nav(e) {"
    "  url = JSON.parse(e.detail).url;"
    "  document.getElementById('browserplugin')."
    "      removeEventListener('-internal-loadcommit', nav);"
    "}"
    "document.getElementById('browserplugin')."
    "    addEventListener('-internal-loadcommit', nav);";
  const char* kGoogleURL = "http://www.google.com/";
  const char* kGoogleNewsURL = "http://news.google.com/";
  const char* kGetProcessID =
      "document.getElementById('browserplugin').getProcessId()";

  LoadHTML(GetHTMLForBrowserPluginObject().c_str());
  ExecuteJavaScript(kAddEventListener);
  // Grab the BrowserPlugin's instance ID from its resize message.
  const IPC::Message* msg =
      browser_plugin_manager()->sink().GetFirstMessageMatching(
          BrowserPluginHostMsg_ResizeGuest::ID);
  ASSERT_TRUE(msg);
  PickleIterator iter = IPC::SyncMessage::GetDataIterator(msg);
  BrowserPluginHostMsg_ResizeGuest::SendParam  resize_params;
  ASSERT_TRUE(IPC::ReadParam(msg, &iter, &resize_params));
  int instance_id = resize_params.a;

  MockBrowserPlugin* browser_plugin =
      static_cast<MockBrowserPlugin*>(
          browser_plugin_manager()->GetBrowserPlugin(instance_id));
  ASSERT_TRUE(browser_plugin);

  {
    BrowserPluginMsg_LoadCommit_Params navigate_params;
    navigate_params.url = GURL(kGoogleURL);
    navigate_params.process_id = 1337;
    browser_plugin->LoadCommit(navigate_params);
    EXPECT_EQ(kGoogleURL, ExecuteScriptAndReturnString("url"));
    EXPECT_EQ(1337, ExecuteScriptAndReturnInt(kGetProcessID));
  }
  {
    BrowserPluginMsg_LoadCommit_Params navigate_params;
    navigate_params.url = GURL(kGoogleNewsURL);
    navigate_params.process_id = 42;
    browser_plugin->LoadCommit(navigate_params);
    // The URL variable should not change because we've removed the event
    // listener.
    EXPECT_EQ(kGoogleURL, ExecuteScriptAndReturnString("url"));
    EXPECT_EQ(42, ExecuteScriptAndReturnInt(kGetProcessID));
  }
}

// This test verifies that multiple event listeners fire that are registered
// on a single event type.
TEST_F(BrowserPluginTest, MultipleEventListeners) {
  const char* kAddEventListener =
    "var count = 0;"
    "function nava(u) {"
    "  count++;"
    "}"
    "function navb(u) {"
    "  count++;"
    "}"
    "document.getElementById('browserplugin')."
    "    addEventListener('-internal-loadcommit', nava);"
    "document.getElementById('browserplugin')."
    "    addEventListener('-internal-loadcommit', navb);";
  const char* kGoogleURL = "http://www.google.com/";
  const char* kGetProcessID =
      "document.getElementById('browserplugin').getProcessId()";

  LoadHTML(GetHTMLForBrowserPluginObject().c_str());
  ExecuteJavaScript(kAddEventListener);
  // Grab the BrowserPlugin's instance ID from its resize message.
  const IPC::Message* msg =
      browser_plugin_manager()->sink().GetFirstMessageMatching(
          BrowserPluginHostMsg_ResizeGuest::ID);
  ASSERT_TRUE(msg);
  PickleIterator iter = IPC::SyncMessage::GetDataIterator(msg);
  BrowserPluginHostMsg_ResizeGuest::SendParam  resize_params;
  ASSERT_TRUE(IPC::ReadParam(msg, &iter, &resize_params));
  int instance_id = resize_params.a;

  MockBrowserPlugin* browser_plugin =
      static_cast<MockBrowserPlugin*>(
          browser_plugin_manager()->GetBrowserPlugin(instance_id));
  ASSERT_TRUE(browser_plugin);

  {
    BrowserPluginMsg_LoadCommit_Params navigate_params;
    navigate_params.url = GURL(kGoogleURL);
    navigate_params.process_id = 1337;
    browser_plugin->LoadCommit(navigate_params);
    EXPECT_EQ(2, ExecuteScriptAndReturnInt("count"));
    EXPECT_EQ(1337, ExecuteScriptAndReturnInt(kGetProcessID));
  }
}

TEST_F(BrowserPluginTest, RemoveBrowserPluginOnExit) {
  LoadHTML(GetHTMLForBrowserPluginObject().c_str());

  // Grab the BrowserPlugin's instance ID from its resize message.
  const IPC::Message* msg =
      browser_plugin_manager()->sink().GetFirstMessageMatching(
          BrowserPluginHostMsg_ResizeGuest::ID);
  ASSERT_TRUE(msg);
  PickleIterator iter = IPC::SyncMessage::GetDataIterator(msg);
  BrowserPluginHostMsg_ResizeGuest::SendParam  resize_params;
  ASSERT_TRUE(IPC::ReadParam(msg, &iter, &resize_params));
  int instance_id = resize_params.a;

  MockBrowserPlugin* browser_plugin =
      static_cast<MockBrowserPlugin*>(
          browser_plugin_manager()->GetBrowserPlugin(instance_id));
  ASSERT_TRUE(browser_plugin);

  const char* kAddEventListener =
    "function exitListener(e) {"
    "  if (JSON.parse(e.detail).reason == 'killed') {"
    "    var bp = document.getElementById('browserplugin');"
    "    bp.parentNode.removeChild(bp);"
    "  }"
    "}"
    "document.getElementById('browserplugin')."
    "    addEventListener('-internal-exit', exitListener);";

  ExecuteJavaScript(kAddEventListener);

  // Pretend that the guest has crashed.
  browser_plugin->GuestGone(0, base::TERMINATION_STATUS_PROCESS_WAS_KILLED);

  ProcessPendingMessages();

  EXPECT_EQ(NULL, browser_plugin_manager()->GetBrowserPlugin(instance_id));
}

TEST_F(BrowserPluginTest, AutoSizeAttributes) {
  std::string html = StringPrintf(kHTMLForSourcelessPluginObject,
                                  content::kBrowserPluginMimeType);
  LoadHTML(html.c_str());
  const char* kSetAutoSizeParametersAndNavigate =
    "var browserplugin = document.getElementById('browserplugin');"
    "browserplugin.autoSize = true;"
    "browserplugin.minWidth = 42;"
    "browserplugin.minHeight = 43;"
    "browserplugin.maxWidth = 1337;"
    "browserplugin.maxHeight = 1338;"
    "browserplugin.src = 'foobar';";
  const char* kDisableAutoSize =
    "document.getElementById('browserplugin').autoSize = false;";

  // Set some autosize parameters before navigating then navigate.
  // Verify that the BrowserPluginHostMsg_CreateGuest message contains
  // the correct autosize parameters.
  ExecuteJavaScript(kSetAutoSizeParametersAndNavigate);
  ProcessPendingMessages();
  {
    const IPC::Message* create_msg =
    browser_plugin_manager()->sink().GetUniqueMessageMatching(
        BrowserPluginHostMsg_CreateGuest::ID);
    ASSERT_TRUE(create_msg);

    int create_instance_id;
    BrowserPluginHostMsg_CreateGuest_Params params;
    BrowserPluginHostMsg_CreateGuest::Read(
        create_msg,
        &create_instance_id,
        &params);
     EXPECT_TRUE(params.auto_size.enable);
     EXPECT_EQ(42, params.auto_size.min_size.width());
     EXPECT_EQ(43, params.auto_size.min_size.height());
     EXPECT_EQ(1337, params.auto_size.max_size.width());
     EXPECT_EQ(1338, params.auto_size.max_size.height());
  }
  // Disable autosize and verify that the BrowserPlugin issues a
  // BrowserPluginHostMsg_SetAutoSize with the change.
  ExecuteJavaScript(kDisableAutoSize);
  ProcessPendingMessages();
  {
    const IPC::Message* auto_size_msg =
    browser_plugin_manager()->sink().GetUniqueMessageMatching(
        BrowserPluginHostMsg_SetAutoSize::ID);
    ASSERT_TRUE(auto_size_msg);

    PickleIterator iter = IPC::SyncMessage::GetDataIterator(auto_size_msg);
    BrowserPluginHostMsg_SetAutoSize::SendParam set_auto_size_params;
    ASSERT_TRUE(IPC::ReadParam(auto_size_msg, &iter, &set_auto_size_params));
    const BrowserPluginHostMsg_AutoSize_Params& auto_size_params =
        set_auto_size_params.b;
    EXPECT_FALSE(auto_size_params.enable);
    EXPECT_EQ(42, auto_size_params.min_size.width());
    EXPECT_EQ(43, auto_size_params.min_size.height());
    EXPECT_EQ(1337, auto_size_params.max_size.width());
    EXPECT_EQ(1338, auto_size_params.max_size.height());
  }
}

}  // namespace content
