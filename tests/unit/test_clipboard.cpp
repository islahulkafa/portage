#include "clipboard/clipboard_manager.hpp"
#include "clipboard/clipboard_ndk.hpp"
#include "ndk/libandroid_thunks.hpp"
#include "package/jni_env.hpp"

#include <gtest/gtest.h>
#include <atomic>
#include <string>
#include <vector>

using namespace portage::clipboard;

TEST(ClipboardTest, ClipboardManagerPlainTextLifecycle)
{
    auto &mgr = ClipboardManager::Instance();
    mgr.SetInteractiveSync(false);
    mgr.Reset();

    EXPECT_FALSE(mgr.HasPrimaryClip());
    EXPECT_TRUE(mgr.GetText().empty());

    EXPECT_TRUE(mgr.SetText("Hello Portage Clipboard", "sample_label"));
    EXPECT_TRUE(mgr.HasPrimaryClip());
    EXPECT_EQ(mgr.GetText(), "Hello Portage Clipboard");

    ClipData clip = mgr.GetPrimaryClip();
    EXPECT_EQ(clip.label, "sample_label");
    ASSERT_EQ(clip.items.size(), 1u);
    EXPECT_EQ(clip.items[0].text, "Hello Portage Clipboard");
    ASSERT_FALSE(clip.mime_types.empty());
    EXPECT_EQ(clip.mime_types[0], MIMETYPE_TEXT_PLAIN);

    mgr.ClearPrimaryClip();
    EXPECT_FALSE(mgr.HasPrimaryClip());
    EXPECT_TRUE(mgr.GetText().empty());
}

TEST(ClipboardTest, ClipboardManagerHtmlLifecycle)
{
    auto &mgr = ClipboardManager::Instance();
    mgr.SetInteractiveSync(false);
    mgr.Reset();

    const std::string html = "<b>Rich Text Formatting</b>";
    const std::string plain = "Rich Text Formatting";

    EXPECT_TRUE(mgr.SetHtml(html, plain, "html_label"));
    EXPECT_TRUE(mgr.HasPrimaryClip());
    EXPECT_EQ(mgr.GetHtml(), html);
    EXPECT_EQ(mgr.GetText(), plain);

    ClipData clip = mgr.GetPrimaryClip();
    EXPECT_EQ(clip.label, "html_label");
    ASSERT_EQ(clip.items.size(), 1u);
    EXPECT_EQ(clip.items[0].html_text, html);
    EXPECT_EQ(clip.items[0].text, plain);

    bool has_html_mime = false;
    for (const auto &mime : clip.mime_types) {
        if (mime == MIMETYPE_TEXT_HTML) {
            has_html_mime = true;
            break;
        }
    }
    EXPECT_TRUE(has_html_mime);
}

TEST(ClipboardTest, ClipboardManagerUriLifecycle)
{
    auto &mgr = ClipboardManager::Instance();
    mgr.SetInteractiveSync(false);
    mgr.Reset();

    const std::string uri = "content://media/external/images/media/42";
    EXPECT_TRUE(mgr.SetUri(uri, "uri_label"));
    EXPECT_TRUE(mgr.HasPrimaryClip());
    EXPECT_EQ(mgr.GetUri(), uri);

    ClipData clip = mgr.GetPrimaryClip();
    EXPECT_EQ(clip.label, "uri_label");
    ASSERT_EQ(clip.items.size(), 1u);
    EXPECT_EQ(clip.items[0].uri, uri);
    ASSERT_FALSE(clip.mime_types.empty());
    EXPECT_EQ(clip.mime_types[0], MIMETYPE_TEXT_URILIST);
}

TEST(ClipboardTest, ClipboardManagerImageBinaryLifecycle)
{
    auto &mgr = ClipboardManager::Instance();
    mgr.SetInteractiveSync(false);
    mgr.Reset();

    std::vector<uint8_t> sample_png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    EXPECT_TRUE(mgr.SetImage(sample_png, MIMETYPE_IMAGE_PNG, "image_clip"));
    EXPECT_TRUE(mgr.HasPrimaryClip());

    ClipData clip = mgr.GetPrimaryClip();
    EXPECT_EQ(clip.label, "image_clip");
    ASSERT_EQ(clip.items.size(), 1u);
    EXPECT_EQ(clip.items[0].binary_data, sample_png);
    ASSERT_FALSE(clip.mime_types.empty());
    EXPECT_EQ(clip.mime_types[0], MIMETYPE_IMAGE_PNG);
}

namespace {
struct ClipListenerCtx {
    std::atomic<int> call_count{0};
};

__attribute__((sysv_abi)) void OnClipChanged(void *context)
{
    if (context) {
        auto *ctx = static_cast<ClipListenerCtx *>(context);
        ctx->call_count.fetch_add(1);
    }
}
} // namespace

TEST(ClipboardTest, ClipboardManagerClipChangedListener)
{
    auto &mgr = ClipboardManager::Instance();
    mgr.SetInteractiveSync(false);
    mgr.Reset();

    ClipListenerCtx ctx;
    mgr.AddPrimaryClipChangedListener(&OnClipChanged, &ctx);

    mgr.SetText("Trigger Listener Text");
    EXPECT_EQ(ctx.call_count.load(), 1);

    mgr.ClearPrimaryClip();
    EXPECT_EQ(ctx.call_count.load(), 2);

    mgr.RemovePrimaryClipChangedListener(&OnClipChanged);
    mgr.SetText("Ignored Update");
    EXPECT_EQ(ctx.call_count.load(), 2);
}

namespace {
struct DropListenerCtx {
    std::atomic<int> call_count{0};
    float last_x{0.0f};
    float last_y{0.0f};
    std::string last_uri;
};

__attribute__((sysv_abi)) void OnDropCallback(float x, float y, const char *uri, void *context)
{
    if (context) {
        auto *ctx = static_cast<DropListenerCtx *>(context);
        ctx->last_x = x;
        ctx->last_y = y;
        if (uri) {
            ctx->last_uri = uri;
        }
        ctx->call_count.fetch_add(1);
    }
}
} // namespace

TEST(ClipboardTest, ClipboardManagerDropFilesDispatch)
{
    auto &mgr = ClipboardManager::Instance();
    mgr.SetInteractiveSync(false);
    mgr.Reset();

    DropListenerCtx ctx;
    mgr.AddDropListener(&OnDropCallback, &ctx);

    mgr.DispatchDropEvent(320.0f, 240.0f, "C:\\Games\\Portage\\save.dat");
    EXPECT_EQ(ctx.call_count.load(), 1);
    EXPECT_FLOAT_EQ(ctx.last_x, 320.0f);
    EXPECT_FLOAT_EQ(ctx.last_y, 240.0f);
    EXPECT_EQ(ctx.last_uri, "C:\\Games\\Portage\\save.dat");

    mgr.RemoveDropListener(&OnDropCallback);
    mgr.DispatchDropEvent(100.0f, 100.0f, "ignored.txt");
    EXPECT_EQ(ctx.call_count.load(), 1);
}

TEST(ClipboardTest, SyntheticClipboardJniReflection)
{
    using namespace portage::package;
    auto &env = JniEnvironment::Instance();
    JNIEnv *jenv = env.GetJNIEnv();
    ASSERT_NE(jenv, nullptr);

    jclass context_cls = reinterpret_cast<jclass>(const_cast<char *>("android/content/Context"));
    jmethodID ctor_mid = reinterpret_cast<jmethodID>(const_cast<char *>("<init>"));
    jobject context_obj = jenv->functions->NewObject(jenv, context_cls, ctor_mid);
    ASSERT_NE(context_obj, nullptr);

    jmethodID get_svc_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getSystemService"));
    jstring clip_str = jenv->functions->NewStringUTF(jenv, "clipboard");
    jobject clip_mgr = jenv->functions->CallObjectMethod(jenv, context_obj, get_svc_mid, clip_str);
    ASSERT_NE(clip_mgr, nullptr);

    jclass clip_data_cls = reinterpret_cast<jclass>(const_cast<char *>("android/content/ClipData"));
    jmethodID new_plain_mid = reinterpret_cast<jmethodID>(const_cast<char *>("newPlainText"));
    jstring label_str = jenv->functions->NewStringUTF(jenv, "sample");
    jstring text_str = jenv->functions->NewStringUTF(jenv, "Synthetic JNI Text");

    jobject created_clip = jenv->functions->CallStaticObjectMethod(
        jenv, clip_data_cls, new_plain_mid, label_str, text_str);
    ASSERT_NE(created_clip, nullptr);

    jmethodID set_clip_mid = reinterpret_cast<jmethodID>(const_cast<char *>("setPrimaryClip"));
    jenv->functions->CallVoidMethod(jenv, clip_mgr, set_clip_mid, created_clip);

    jmethodID has_clip_mid = reinterpret_cast<jmethodID>(const_cast<char *>("hasPrimaryClip"));
    jboolean has_clip = jenv->functions->CallBooleanMethod(jenv, clip_mgr, has_clip_mid);
    EXPECT_EQ(has_clip, JNI_TRUE);

    jmethodID get_clip_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getPrimaryClip"));
    jobject fetched_clip = jenv->functions->CallObjectMethod(jenv, clip_mgr, get_clip_mid);
    ASSERT_NE(fetched_clip, nullptr);

    jmethodID get_count_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getItemCount"));
    jint item_count = jenv->functions->CallIntMethod(jenv, fetched_clip, get_count_mid);
    EXPECT_EQ(item_count, 1);

    jmethodID get_item_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getItemAt"));
    jobject item_obj = jenv->functions->CallObjectMethod(jenv, fetched_clip, get_item_mid, 0);
    ASSERT_NE(item_obj, nullptr);

    jmethodID get_text_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getText"));
    jstring text_res =
        reinterpret_cast<jstring>(jenv->functions->CallObjectMethod(jenv, item_obj, get_text_mid));
    ASSERT_NE(text_res, nullptr);
    const char *chars = env.GetStringChars(text_res);
    EXPECT_STREQ(chars, "Synthetic JNI Text");

    jmethodID get_desc_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getDescription"));
    jobject desc_obj = jenv->functions->CallObjectMethod(jenv, fetched_clip, get_desc_mid);
    ASSERT_NE(desc_obj, nullptr);

    jmethodID has_mime_mid = reinterpret_cast<jmethodID>(const_cast<char *>("hasMimeType"));
    jstring mime_plain = jenv->functions->NewStringUTF(jenv, "text/plain");
    jboolean has_plain =
        jenv->functions->CallBooleanMethod(jenv, desc_obj, has_mime_mid, mime_plain);
    EXPECT_EQ(has_plain, JNI_TRUE);
}

TEST(ClipboardTest, NdkSymbolRegistryLookup)
{
    auto &registry = portage::ndk::NdkSymbolRegistry::Instance();
    EXPECT_NE(registry.Lookup("AClipboard_create"), 0u);
    EXPECT_NE(registry.Lookup("AClipboard_destroy"), 0u);
    EXPECT_NE(registry.Lookup("AClipboard_hasText"), 0u);
    EXPECT_NE(registry.Lookup("AClipboard_getText"), 0u);
    EXPECT_NE(registry.Lookup("AClipboard_setText"), 0u);
    EXPECT_NE(registry.Lookup("AClipboard_hasPrimaryClip"), 0u);
    EXPECT_NE(registry.Lookup("AClipboard_clear"), 0u);
    EXPECT_NE(registry.Lookup("AClipboard_addPrimaryClipChangedListener"), 0u);
    EXPECT_NE(registry.Lookup("AClipboard_removePrimaryClipChangedListener"), 0u);
    EXPECT_NE(registry.Lookup("ADragDrop_registerDropListener"), 0u);
    EXPECT_NE(registry.Lookup("ADragDrop_unregisterDropListener"), 0u);
}
