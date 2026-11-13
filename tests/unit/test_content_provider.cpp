#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "fs/fd_table.hpp"
#include "fs/vfs.hpp"
#include "mem/address_space.hpp"
#include "package/content_provider.hpp"
#include "package/jni_env.hpp"
#include "syscall/syscall_dispatcher.hpp"
#include "thread/thread_manager.hpp"

using namespace portage;
using namespace portage::package;
using namespace portage::fs;

class ContentProviderTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        test_root_ = std::filesystem::temp_directory_path() /
                     ("portage_saf_test_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        std::error_code ec;
        std::filesystem::remove_all(test_root_, ec);
        std::filesystem::create_directories(test_root_, ec);

        ContentProviderManager::Instance().Initialize(test_root_);

        JniEnvironment::Instance().Initialize("com.portage.unit",
                                              "/data/data/com.portage.unit/files",
                                              "/data/data/com.portage.unit/cache");
        syscall::SyscallDispatcher::Initialize(&as_, &vfs_, &fd_table_);
        thread::ThreadManager::Instance().RegisterMainThread();
    }

    void TearDown() override
    {
        syscall::SyscallDispatcher::Shutdown();
        ContentProviderManager::Instance().Reset();
        std::error_code ec;
        std::filesystem::remove_all(test_root_, ec);
    }

    std::filesystem::path test_root_{};
    mem::AddressSpace as_{};
    Vfs vfs_{};
    FileDescriptorTable fd_table_{};
};

TEST_F(ContentProviderTest, ResolveExternalStorageAndMediaUris)
{
    auto &cpm = ContentProviderManager::Instance();

    auto resolved_doc = cpm.ResolveUri(
        "content://com.android.externalstorage.documents/document/primary%3ADocuments%2Fnote.txt");
    ASSERT_TRUE(resolved_doc.has_value());
    EXPECT_EQ(resolved_doc->generic_string(),
              (test_root_ / "Documents" / "note.txt").generic_string());

    auto resolved_tree = cpm.ResolveUri(
        "content://com.android.externalstorage.documents/tree/primary%3APictures/document/"
        "primary%3APictures%2Fvacation.jpg");
    ASSERT_TRUE(resolved_tree.has_value());
    EXPECT_EQ(resolved_tree->generic_string(),
              (test_root_ / "Pictures" / "vacation.jpg").generic_string());

    auto media_target = test_root_ / "Media" / "photo42.jpg";
    cpm.RegisterMedia("42", media_target, "image/jpeg");
    auto resolved_media = cpm.ResolveUri("content://media/external/images/media/42");
    ASSERT_TRUE(resolved_media.has_value());
    EXPECT_EQ(resolved_media->generic_string(), media_target.generic_string());

    auto bad_traversal1 = cpm.ResolveUri("content://com.android.externalstorage.documents/document/"
                                         "primary%3A..%2F..%2Fwindows%2Fsystem32%2Fcalc.exe");
    EXPECT_FALSE(bad_traversal1.has_value());

    auto bad_traversal2 = cpm.ResolveUri("content://media/external/..%2F..%2Fsecret");
    EXPECT_FALSE(bad_traversal2.has_value());
}

TEST_F(ContentProviderTest, DocumentsContractCreateRenameDelete)
{
    auto &cpm = ContentProviderManager::Instance();

    auto dir_uri =
        cpm.CreateDocument("content://com.android.externalstorage.documents/document/primary:",
                           "vnd.android.document/directory", "Workspace");
    ASSERT_TRUE(dir_uri.has_value());
    EXPECT_TRUE(std::filesystem::is_directory(test_root_ / "Workspace"));

    auto file_uri = cpm.CreateDocument(*dir_uri, "text/plain", "report.txt");
    ASSERT_TRUE(file_uri.has_value());
    EXPECT_TRUE(std::filesystem::is_regular_file(test_root_ / "Workspace" / "report.txt"));

    auto renamed_uri = cpm.RenameDocument(*file_uri, "final_report.txt");
    ASSERT_TRUE(renamed_uri.has_value());
    EXPECT_FALSE(std::filesystem::exists(test_root_ / "Workspace" / "report.txt"));
    EXPECT_TRUE(std::filesystem::is_regular_file(test_root_ / "Workspace" / "final_report.txt"));

    bool deleted_file = cpm.DeleteDocument(*renamed_uri);
    EXPECT_TRUE(deleted_file);
    EXPECT_FALSE(std::filesystem::exists(test_root_ / "Workspace" / "final_report.txt"));

    bool deleted_dir = cpm.DeleteDocument(*dir_uri);
    EXPECT_TRUE(deleted_dir);
    EXPECT_FALSE(std::filesystem::exists(test_root_ / "Workspace"));
}

TEST_F(ContentProviderTest, ContentResolverQueryCursor)
{
    auto &cpm = ContentProviderManager::Instance();
    std::filesystem::path doc_dir = test_root_ / "Documents";
    std::filesystem::create_directories(doc_dir);

    {
        std::ofstream f1(doc_dir / "alpha.txt");
        f1 << "hello world";
        std::ofstream f2(doc_dir / "beta.json");
        f2 << "{\"test\": true}";
        std::ofstream f3(doc_dir / "gamma.png");
        f3 << "fake png bytes";
    }

    auto metas = cpm.Query(
        "content://com.android.externalstorage.documents/document/primary:Documents/children");
    EXPECT_EQ(metas.size(), 3);

    auto &jni = JniEnvironment::Instance();
    auto *env = jni.GetJNIEnv();
    ASSERT_NE(env, nullptr);

    jclass uri_cls = env->functions->FindClass(env, "android/net/Uri");
    ASSERT_NE(uri_cls, nullptr);
    jmethodID parse_mid = env->functions->GetStaticMethodID(
        env, uri_cls, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
    ASSERT_NE(parse_mid, nullptr);

    jstring uri_jstr = jni.CreateString(
        "content://com.android.externalstorage.documents/document/primary:Documents/children");
    jobject uri_obj = env->functions->CallStaticObjectMethod(env, uri_cls, parse_mid, uri_jstr);
    ASSERT_NE(uri_obj, nullptr);

    jclass cr_cls = env->functions->FindClass(env, "android/content/ContentResolver");
    ASSERT_NE(cr_cls, nullptr);
    jmethodID query_mid = env->functions->GetMethodID(
        env, cr_cls, "query",
        "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;Ljava/lang/"
        "String;)Landroid/database/Cursor;");
    ASSERT_NE(query_mid, nullptr);

    jobject cr_obj = jni.GetContentResolverObject();
    ASSERT_NE(cr_obj, nullptr);
    jobject cursor_obj = env->functions->CallObjectMethod(env, cr_obj, query_mid, uri_obj);
    ASSERT_NE(cursor_obj, nullptr);

    jclass cur_cls = env->functions->FindClass(env, "android/database/Cursor");
    ASSERT_NE(cur_cls, nullptr);
    jmethodID get_count_mid = env->functions->GetMethodID(env, cur_cls, "getCount", "()I");
    ASSERT_NE(get_count_mid, nullptr);
    jmethodID move_first_mid = env->functions->GetMethodID(env, cur_cls, "moveToFirst", "()Z");
    ASSERT_NE(move_first_mid, nullptr);
    jmethodID move_next_mid = env->functions->GetMethodID(env, cur_cls, "moveToNext", "()Z");
    ASSERT_NE(move_next_mid, nullptr);
    jmethodID get_col_mid =
        env->functions->GetMethodID(env, cur_cls, "getColumnIndex", "(Ljava/lang/String;)I");
    ASSERT_NE(get_col_mid, nullptr);
    jmethodID get_str_mid =
        env->functions->GetMethodID(env, cur_cls, "getString", "(I)Ljava/lang/String;");
    ASSERT_NE(get_str_mid, nullptr);
    jmethodID close_mid = env->functions->GetMethodID(env, cur_cls, "close", "()V");
    ASSERT_NE(close_mid, nullptr);

    jint count = env->functions->CallIntMethod(env, cursor_obj, get_count_mid);
    EXPECT_EQ(count, 3);

    jstring name_col = jni.CreateString("_display_name");
    jint name_idx = env->functions->CallIntMethod(env, cursor_obj, get_col_mid, name_col);
    EXPECT_GE(name_idx, 0);

    jboolean moved = env->functions->CallBooleanMethod(env, cursor_obj, move_first_mid);
    EXPECT_EQ(moved, JNI_TRUE);

    int seen = 0;
    do {
        jstring val = reinterpret_cast<jstring>(
            env->functions->CallObjectMethod(env, cursor_obj, get_str_mid, name_idx));
        const char *name_chars = jni.GetStringChars(val);
        EXPECT_NE(name_chars, nullptr);
        seen++;
    } while (env->functions->CallBooleanMethod(env, cursor_obj, move_next_mid) == JNI_TRUE);

    EXPECT_EQ(seen, 3);
    env->functions->CallVoidMethod(env, cursor_obj, close_mid);
}

TEST_F(ContentProviderTest, ParcelFileDescriptorAndVfsOpen)
{
    std::filesystem::path doc_path = test_root_ / "Documents" / "payload.bin";
    std::string test_data = "PORTAGE_SAF_TEST_PAYLOAD_123456789";
    {
        std::ofstream ofs(doc_path, std::ios::binary);
        ofs << test_data;
    }

    auto &jni = JniEnvironment::Instance();
    auto *env = jni.GetJNIEnv();

    jclass uri_cls = env->functions->FindClass(env, "android/net/Uri");
    jmethodID parse_mid = env->functions->GetStaticMethodID(
        env, uri_cls, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
    jstring uri_jstr = jni.CreateString("content://com.android.externalstorage.documents/document/"
                                        "primary%3ADocuments%2Fpayload.bin");
    jobject uri_obj = env->functions->CallStaticObjectMethod(env, uri_cls, parse_mid, uri_jstr);

    jclass cr_cls = env->functions->FindClass(env, "android/content/ContentResolver");
    jmethodID open_mid = env->functions->GetMethodID(
        env, cr_cls, "openFileDescriptor",
        "(Landroid/net/Uri;Ljava/lang/String;)Landroid/os/ParcelFileDescriptor;");
    jstring mode_jstr = jni.CreateString("r");
    jobject cr_obj = jni.GetContentResolverObject();

    jobject pfd_obj = env->functions->CallObjectMethod(env, cr_obj, open_mid, uri_obj, mode_jstr);
    ASSERT_NE(pfd_obj, nullptr);

    jclass pfd_cls = env->functions->FindClass(env, "android/os/ParcelFileDescriptor");
    ASSERT_NE(pfd_cls, nullptr);
    jmethodID get_fd_mid = env->functions->GetMethodID(env, pfd_cls, "getFd", "()I");
    jint fd = env->functions->CallIntMethod(env, pfd_obj, get_fd_mid);
    EXPECT_GE(fd, 0);

    auto desc = fd_table_.Get(fd);
    ASSERT_NE(desc, nullptr);

    jmethodID detach_mid = env->functions->GetMethodID(env, pfd_cls, "detachFd", "()I");
    jint detached_fd = env->functions->CallIntMethod(env, pfd_obj, detach_mid);
    EXPECT_EQ(detached_fd, fd);

    jmethodID close_mid = env->functions->GetMethodID(env, pfd_cls, "close", "()V");
    env->functions->CallVoidMethod(env, pfd_obj, close_mid);

    EXPECT_NE(fd_table_.Get(detached_fd), nullptr);
    fd_table_.Close(detached_fd);
    EXPECT_EQ(fd_table_.Get(detached_fd), nullptr);

    auto resolved_vfs = vfs_.Resolve("content://com.android.externalstorage.documents/document/"
                                     "primary%3ADocuments%2Fpayload.bin");
    EXPECT_EQ(resolved_vfs.dev_type, DeviceType::Regular);
    EXPECT_EQ(resolved_vfs.host_path.generic_string(), doc_path.generic_string());
}

TEST_F(ContentProviderTest, PersistableUriPermissionsRoundtrip)
{
    auto &cpm = ContentProviderManager::Instance();
    std::string test_uri =
        "content://com.android.externalstorage.documents/tree/primary%3AProjects";

    cpm.TakePersistableUriPermission(test_uri, FLAG_GRANT_READ_URI_PERMISSION |
                                                   FLAG_GRANT_WRITE_URI_PERMISSION);
    auto perms = cpm.GetPersistedUriPermissions();
    ASSERT_EQ(perms.size(), 1);
    EXPECT_EQ(perms[0].uri, test_uri);
    EXPECT_TRUE(perms[0].read);
    EXPECT_TRUE(perms[0].write);

    cpm.ReleasePersistableUriPermission(test_uri, FLAG_GRANT_READ_URI_PERMISSION);
    perms = cpm.GetPersistedUriPermissions();
    ASSERT_EQ(perms.size(), 1);
    EXPECT_FALSE(perms[0].read);
    EXPECT_TRUE(perms[0].write);

    cpm.ReleasePersistableUriPermission(test_uri, FLAG_GRANT_WRITE_URI_PERMISSION);
    perms = cpm.GetPersistedUriPermissions();
    EXPECT_EQ(perms.size(), 0);

    auto &jni = JniEnvironment::Instance();
    auto *env = jni.GetJNIEnv();

    jclass uri_cls = env->functions->FindClass(env, "android/net/Uri");
    jmethodID parse_mid = env->functions->GetStaticMethodID(
        env, uri_cls, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
    jstring uri_jstr = jni.CreateString(test_uri);
    jobject uri_obj = env->functions->CallStaticObjectMethod(env, uri_cls, parse_mid, uri_jstr);

    jclass cr_cls = env->functions->FindClass(env, "android/content/ContentResolver");
    jmethodID take_perm_mid = env->functions->GetMethodID(
        env, cr_cls, "takePersistableUriPermission", "(Landroid/net/Uri;I)V");
    jmethodID get_perms_mid = env->functions->GetMethodID(env, cr_cls, "getPersistedUriPermissions",
                                                          "()Ljava/util/List;");
    jobject cr_obj = jni.GetContentResolverObject();

    env->functions->CallVoidMethod(env, cr_obj, take_perm_mid, uri_obj,
                                   FLAG_GRANT_READ_URI_PERMISSION |
                                       FLAG_GRANT_WRITE_URI_PERMISSION);

    jobject list_obj = env->functions->CallObjectMethod(env, cr_obj, get_perms_mid);
    ASSERT_NE(list_obj, nullptr);

    auto list_syn = jni.LookupObject(list_obj);
    ASSERT_NE(list_syn, nullptr);
    EXPECT_EQ(list_syn->object_data.size(), 1);

    auto perm_syn = jni.LookupObject(list_syn->object_data[0]);
    ASSERT_NE(perm_syn, nullptr);
    EXPECT_EQ(perm_syn->bool_fields["isReadPermission"], 1);
    EXPECT_EQ(perm_syn->bool_fields["isWritePermission"], 1);
}
