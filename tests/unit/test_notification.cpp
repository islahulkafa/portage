#include "ndk/libandroid_thunks.hpp"
#include "notification/notification_manager.hpp"
#include "notification/notification_ndk.hpp"
#include "package/jni_env.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace portage::notification;

TEST(NotificationTest, NotificationChannelLifecycle)
{
    auto &mgr = NotificationManager::Instance();
    mgr.Reset();

    auto def_channel = mgr.GetNotificationChannel("default");
    ASSERT_TRUE(def_channel.has_value());
    EXPECT_EQ(def_channel->id, "default");
    EXPECT_EQ(def_channel->importance, IMPORTANCE_DEFAULT);

    NotificationChannel chat_channel{};
    chat_channel.id = "chat_messages";
    chat_channel.name = "Chat Messages";
    chat_channel.importance = IMPORTANCE_HIGH;
    chat_channel.description = "Incoming chat alerts";
    EXPECT_TRUE(mgr.CreateNotificationChannel(chat_channel));

    auto retrieved = mgr.GetNotificationChannel("chat_messages");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->id, "chat_messages");
    EXPECT_EQ(retrieved->name, "Chat Messages");
    EXPECT_EQ(retrieved->importance, IMPORTANCE_HIGH);
    EXPECT_EQ(retrieved->description, "Incoming chat alerts");

    auto all_channels = mgr.GetAllNotificationChannels();
    EXPECT_GE(all_channels.size(), 2u);

    EXPECT_FALSE(mgr.DeleteNotificationChannel("default"));
    EXPECT_TRUE(mgr.DeleteNotificationChannel("chat_messages"));
    EXPECT_FALSE(mgr.GetNotificationChannel("chat_messages").has_value());
}

TEST(NotificationTest, NotificationPostAndRetrieve)
{
    auto &mgr = NotificationManager::Instance();
    mgr.Reset();

    EXPECT_EQ(mgr.GetActiveNotificationCount(), 0u);

    NotificationRecord notif{};
    notif.channel_id = "default";
    notif.title = "Quest Completed";
    notif.text = "You earned 500 gold coins!";
    notif.small_icon = 101;
    notif.flags = FLAG_AUTO_CANCEL;

    EXPECT_TRUE(mgr.PostNotification("game_events", 1, notif));
    EXPECT_EQ(mgr.GetActiveNotificationCount(), 1u);

    auto active = mgr.GetActiveNotifications();
    ASSERT_EQ(active.size(), 1u);
    EXPECT_EQ(active[0].id, 1);
    EXPECT_EQ(active[0].tag, "game_events");
    EXPECT_EQ(active[0].title, "Quest Completed");
    EXPECT_EQ(active[0].text, "You earned 500 gold coins!");
    EXPECT_EQ(active[0].channel_id, "default");
    EXPECT_EQ(active[0].small_icon, 101);
}

TEST(NotificationTest, NotificationCancelAndCancelAll)
{
    auto &mgr = NotificationManager::Instance();
    mgr.Reset();

    NotificationRecord n1{};
    n1.title = "Message 1";
    NotificationRecord n2{};
    n2.title = "Message 2";
    NotificationRecord n3{};
    n3.title = "Message 3";

    EXPECT_TRUE(mgr.PostNotification("chat", 1, n1));
    EXPECT_TRUE(mgr.PostNotification("chat", 2, n2));
    EXPECT_TRUE(mgr.PostNotification("system", 3, n3));
    EXPECT_EQ(mgr.GetActiveNotificationCount(), 3u);

    EXPECT_TRUE(mgr.CancelNotification("chat", 1));
    EXPECT_EQ(mgr.GetActiveNotificationCount(), 2u);

    EXPECT_FALSE(mgr.CancelNotification("chat", 1));

    mgr.CancelAllNotifications();
    EXPECT_EQ(mgr.GetActiveNotificationCount(), 0u);
}

TEST(NotificationTest, NotificationImportanceFiltering)
{
    auto &mgr = NotificationManager::Instance();
    mgr.Reset();

    NotificationChannel muted{};
    muted.id = "muted_channel";
    muted.name = "Muted";
    muted.importance = IMPORTANCE_NONE;
    EXPECT_TRUE(mgr.CreateNotificationChannel(muted));

    NotificationRecord n{};
    n.channel_id = "muted_channel";
    n.title = "Suppressed";
    n.text = "This should not be delivered";

    EXPECT_FALSE(mgr.PostNotification("", 100, n));
    EXPECT_EQ(mgr.GetActiveNotificationCount(), 0u);
}

TEST(NotificationTest, NotificationProgressBarLifecycle)
{
    auto &mgr = NotificationManager::Instance();
    mgr.Reset();

    NotificationRecord prog{};
    prog.channel_id = "default";
    prog.title = "Downloading Asset Pack";
    prog.text = "45% complete";
    prog.progress_max = 100;
    prog.progress_current = 45;
    prog.progress_indeterminate = false;

    EXPECT_TRUE(mgr.PostNotification("dl", 10, prog));
    auto active = mgr.GetActiveNotifications();
    ASSERT_EQ(active.size(), 1u);
    EXPECT_EQ(active[0].progress_max, 100);
    EXPECT_EQ(active[0].progress_current, 45);
    EXPECT_FALSE(active[0].progress_indeterminate);

    prog.progress_indeterminate = true;
    prog.text = "Unpacking files...";
    EXPECT_TRUE(mgr.PostNotification("dl", 10, prog));
    active = mgr.GetActiveNotifications();
    ASSERT_EQ(active.size(), 1u);
    EXPECT_TRUE(active[0].progress_indeterminate);
}

TEST(NotificationTest, ToastLifecycle)
{
    auto &mgr = NotificationManager::Instance();
    mgr.Reset();

    EXPECT_EQ(mgr.GetRecentToasts().size(), 0u);

    mgr.ShowToast("Game Saved Successfully", TOAST_LENGTH_SHORT);
    mgr.ShowToast("Network Disconnected", TOAST_LENGTH_LONG);

    auto toasts = mgr.GetRecentToasts();
    ASSERT_EQ(toasts.size(), 2u);
    EXPECT_EQ(toasts[0].text, "Game Saved Successfully");
    EXPECT_EQ(toasts[0].duration, TOAST_LENGTH_SHORT);
    EXPECT_GT(toasts[0].timestamp_ms, 0);

    EXPECT_EQ(toasts[1].text, "Network Disconnected");
    EXPECT_EQ(toasts[1].duration, TOAST_LENGTH_LONG);
}

TEST(NotificationTest, SyntheticNotificationAndToastJniReflection)
{
    using namespace portage::package;
    auto &env = JniEnvironment::Instance();
    env.Reset();
    auto &mgr = NotificationManager::Instance();
    mgr.Reset();

    JNIEnv *jenv = env.GetJNIEnv();
    ASSERT_NE(jenv, nullptr);
    ASSERT_NE(jenv->functions, nullptr);

    jclass context_cls = reinterpret_cast<jclass>(const_cast<char *>("android/content/Context"));
    jmethodID ctor_mid = reinterpret_cast<jmethodID>(const_cast<char *>("<init>"));
    jobject context_obj = jenv->functions->NewObject(jenv, context_cls, ctor_mid);
    ASSERT_NE(context_obj, nullptr);

    jmethodID get_svc_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getSystemService"));
    jstring notif_str = jenv->functions->NewStringUTF(jenv, "notification");
    jobject notif_mgr =
        jenv->functions->CallObjectMethod(jenv, context_obj, get_svc_mid, notif_str);
    ASSERT_NE(notif_mgr, nullptr);

    jmethodID are_enabled_mid =
        reinterpret_cast<jmethodID>(const_cast<char *>("areNotificationsEnabled"));
    jboolean enabled = jenv->functions->CallBooleanMethod(jenv, notif_mgr, are_enabled_mid);
    EXPECT_EQ(enabled, JNI_TRUE);

    jclass channel_cls =
        reinterpret_cast<jclass>(const_cast<char *>("android/app/NotificationChannel"));
    jstring ch_id = jenv->functions->NewStringUTF(jenv, "game_alerts");
    jstring ch_name = jenv->functions->NewStringUTF(jenv, "Game Alerts");
    jobject ch_obj = jenv->functions->NewObject(jenv, channel_cls, ctor_mid, ch_id, ch_name,
                                                static_cast<uintptr_t>(IMPORTANCE_HIGH));
    ASSERT_NE(ch_obj, nullptr);

    jmethodID create_ch_mid =
        reinterpret_cast<jmethodID>(const_cast<char *>("createNotificationChannel"));
    jenv->functions->CallVoidMethod(jenv, notif_mgr, create_ch_mid, ch_obj);

    jmethodID get_ch_mid =
        reinterpret_cast<jmethodID>(const_cast<char *>("getNotificationChannel"));
    jobject fetched_ch = jenv->functions->CallObjectMethod(jenv, notif_mgr, get_ch_mid, ch_id);
    ASSERT_NE(fetched_ch, nullptr);

    jmethodID get_imp_mid = reinterpret_cast<jmethodID>(const_cast<char *>("getImportance"));
    jint imp = jenv->functions->CallIntMethod(jenv, fetched_ch, get_imp_mid);
    EXPECT_EQ(imp, IMPORTANCE_HIGH);

    jclass builder_cls =
        reinterpret_cast<jclass>(const_cast<char *>("android/app/Notification$Builder"));
    jobject builder_obj =
        jenv->functions->NewObject(jenv, builder_cls, ctor_mid, context_obj, ch_id);
    ASSERT_NE(builder_obj, nullptr);

    jmethodID set_title_mid = reinterpret_cast<jmethodID>(const_cast<char *>("setContentTitle"));
    jstring title_str = jenv->functions->NewStringUTF(jenv, "JNI Notification Title");
    jenv->functions->CallObjectMethod(jenv, builder_obj, set_title_mid, title_str);

    jmethodID set_text_mid = reinterpret_cast<jmethodID>(const_cast<char *>("setContentText"));
    jstring text_str = jenv->functions->NewStringUTF(jenv, "JNI Notification Body");
    jenv->functions->CallObjectMethod(jenv, builder_obj, set_text_mid, text_str);

    jmethodID build_mid = reinterpret_cast<jmethodID>(const_cast<char *>("build"));
    jobject built_notif = jenv->functions->CallObjectMethod(jenv, builder_obj, build_mid);
    ASSERT_NE(built_notif, nullptr);

    jmethodID notify_mid = reinterpret_cast<jmethodID>(const_cast<char *>("notify"));
    jenv->functions->CallVoidMethod(jenv, notif_mgr, notify_mid, static_cast<uintptr_t>(55),
                                    built_notif);

    EXPECT_EQ(mgr.GetActiveNotificationCount(), 1u);
    auto active = mgr.GetActiveNotifications();
    ASSERT_EQ(active.size(), 1u);
    EXPECT_EQ(active[0].id, 55);
    EXPECT_EQ(active[0].title, "JNI Notification Title");
    EXPECT_EQ(active[0].text, "JNI Notification Body");

    jmethodID cancel_mid = reinterpret_cast<jmethodID>(const_cast<char *>("cancel"));
    jenv->functions->CallVoidMethod(jenv, notif_mgr, cancel_mid, static_cast<uintptr_t>(55));
    EXPECT_EQ(mgr.GetActiveNotificationCount(), 0u);

    jclass toast_cls = reinterpret_cast<jclass>(const_cast<char *>("android/widget/Toast"));
    jmethodID make_toast_mid = reinterpret_cast<jmethodID>(const_cast<char *>("makeText"));
    jstring toast_str = jenv->functions->NewStringUTF(jenv, "JNI Toast Message");
    jobject toast_obj = jenv->functions->CallStaticObjectMethod(
        jenv, toast_cls, make_toast_mid, context_obj, toast_str, static_cast<uintptr_t>(0));
    ASSERT_NE(toast_obj, nullptr);

    jmethodID show_mid = reinterpret_cast<jmethodID>(const_cast<char *>("show"));
    jenv->functions->CallVoidMethod(jenv, toast_obj, show_mid);

    auto toasts = mgr.GetRecentToasts();
    ASSERT_EQ(toasts.size(), 1u);
    EXPECT_EQ(toasts[0].text, "JNI Toast Message");
}

TEST(NotificationTest, NdkSymbolRegistryLookup)
{
    auto &registry = portage::ndk::NdkSymbolRegistry::Instance();
    EXPECT_NE(registry.Lookup("ANotificationManager_create"), 0u);
    EXPECT_NE(registry.Lookup("ANotificationManager_destroy"), 0u);
    EXPECT_NE(registry.Lookup("ANotificationChannel_create"), 0u);
    EXPECT_NE(registry.Lookup("ANotificationChannel_destroy"), 0u);
    EXPECT_NE(registry.Lookup("ANotificationChannel_setDescription"), 0u);
    EXPECT_NE(registry.Lookup("ANotificationChannel_getImportance"), 0u);
    EXPECT_NE(registry.Lookup("ANotificationManager_createNotificationChannel"), 0u);
    EXPECT_NE(registry.Lookup("ANotificationManager_getNotificationChannel"), 0u);
    EXPECT_NE(registry.Lookup("ANotificationManager_deleteNotificationChannel"), 0u);
    EXPECT_NE(registry.Lookup("ANotification_create"), 0u);
    EXPECT_NE(registry.Lookup("ANotification_destroy"), 0u);
    EXPECT_NE(registry.Lookup("ANotification_setTitle"), 0u);
    EXPECT_NE(registry.Lookup("ANotification_setText"), 0u);
    EXPECT_NE(registry.Lookup("ANotification_setProgress"), 0u);
    EXPECT_NE(registry.Lookup("ANotification_setOngoing"), 0u);
    EXPECT_NE(registry.Lookup("ANotificationManager_notify"), 0u);
    EXPECT_NE(registry.Lookup("ANotificationManager_cancel"), 0u);
    EXPECT_NE(registry.Lookup("ANotificationManager_cancelAll"), 0u);
    EXPECT_NE(registry.Lookup("AToast_show"), 0u);
}
