/* Copyright (c) 2016, 2022, Oracle and/or its affiliates.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "uuid.h"

#include "lex_string.h"
#include "my_inttypes.h"
#include "plugin/keyring/common/keyring_impl.cc"
#include "plugin/keyring/keyring.cc"
#include "sql/sql_plugin_ref.h"
#include "unittest/gunit/keyring/mock_logger.h"

namespace keyring__api_unittest {
using ::testing::StrEq;
using namespace keyring;

class Keyring_api_test : public ::testing::Test {
 public:
  Keyring_api_test() = default;
  ~Keyring_api_test() override {
    delete[] plugin_name;
    delete[] keyring_filename;
  }

 protected:
  void SetUp() override {
    plugin_name = new char[strlen("FakeKeyring") + 1];
    strcpy(plugin_name, "FakeKeyring");
    keyring_filename = new char[strlen("./keyring") + 1];
    strcpy(keyring_filename, "./keyring");

    plugin_info.name.str = plugin_name;
    plugin_info.name.length = strlen(plugin_name);
    keyring_file_data_value = keyring_filename;

    remove(keyring_file_data_value);
    remove("./keyring.backup");

    keyring_init_with_mock_logger();

    key_memory_KEYRING = PSI_NOT_INSTRUMENTED;
    key_LOCK_keyring = PSI_NOT_INSTRUMENTED;
    sample_key_data = "Robi";
  }
  void TearDown() override {
    keyring_deinit_with_mock_logger();
    remove(keyring_file_data_value);
    remove("./keyring.backup");
  }

 protected:
  void keyring_init_with_mock_logger();
  void keyring_deinit_with_mock_logger();

  std::string sample_key_data;
  char *plugin_name;
  char *keyring_filename;
  st_plugin_int plugin_info;  // for Logger initialization
};

void Keyring_api_test::keyring_init_with_mock_logger() {
  ASSERT_TRUE(keyring_init(&plugin_info) == 0);
  // use MockLogger instead of Logger
  logger.reset(new Mock_logger());
}

void Keyring_api_test::keyring_deinit_with_mock_logger() {
  keyring_deinit(nullptr);
}

TEST_F(Keyring_api_test, StoreFetchRemove) {
  EXPECT_EQ(
      mysql_key_store("Robert_key", "AES", "Robert", sample_key_data.c_str(),
                      sample_key_data.length() + 1),
      0);
  char *key_type;
  size_t key_len;
  void *key;
  EXPECT_EQ(mysql_key_fetch("Robert_key", &key_type, "Robert", &key, &key_len),
            0);
  EXPECT_STREQ("AES", key_type);
  EXPECT_EQ(key_len, sample_key_data.length() + 1);
  ASSERT_TRUE(memcmp((char *)key, sample_key_data.c_str(), key_len) == 0);
  my_free(key_type);
  key_type = nullptr;
  my_free(key);
  key = nullptr;
  EXPECT_EQ(mysql_key_remove("Robert_key", "Robert"), 0);
  // make sure the key was removed - fetch it
  EXPECT_EQ(mysql_key_fetch("Robert_key", &key_type, "Robert", &key, &key_len),
            0);
  ASSERT_TRUE(key == nullptr);
}

TEST_F(Keyring_api_test, CheckIfInmemoryKeyIsXORed) {
  EXPECT_EQ(
      mysql_key_store("Robert_key", "AES", "Robert", sample_key_data.c_str(),
                      sample_key_data.length() + 1),
      0);

  Key key_id("Robert_key", nullptr, "Robert", nullptr, 0);
  IKey *fetched_key = keys->fetch_key(&key_id);
  ASSERT_TRUE(fetched_key != nullptr);
  std::string expected_key_signature = "Robert_keyRobert";
  EXPECT_STREQ(fetched_key->get_key_signature()->c_str(),
               expected_key_signature.c_str());
  EXPECT_EQ(fetched_key->get_key_signature()->length(),
            expected_key_signature.length());
  uchar *key_data_fetched = fetched_key->get_key_data();
  size_t key_data_fetched_size = fetched_key->get_key_data_size();
  EXPECT_STREQ("AES", fetched_key->get_key_type_as_string()->c_str());

  // make sure that the key was xored before it was put into keys_container,
  // i.e.  the fetched key data is not equal to the key data that was stored
  EXPECT_STRNE(sample_key_data.c_str(),
               reinterpret_cast<const char *>(key_data_fetched));
  ASSERT_TRUE(sample_key_data.length() + 1 == key_data_fetched_size);

  // now xor to get the data that was stored
  fetched_key->xor_data();
  EXPECT_STREQ(sample_key_data.c_str(),
               reinterpret_cast<const char *>(key_data_fetched));
  ASSERT_TRUE(sample_key_data.length() + 1 == key_data_fetched_size);
  my_free(fetched_key->release_key_data());
}

TEST_F(Keyring_api_test, FetchNotExisting) {
  char *key_type = nullptr;
  void *key = nullptr;
  size_t key_len = 0;
  EXPECT_EQ(mysql_key_fetch("Robert_key", &key_type, "Robert", &key, &key_len),
            0);
  ASSERT_TRUE(key == nullptr);
}

TEST_F(Keyring_api_test, RemoveNotExisting) {
  EXPECT_EQ(mysql_key_remove("Robert_key", "Robert"), 1);
}

TEST_F(Keyring_api_test, StoreFetchNotExisting) {
  EXPECT_EQ(
      mysql_key_store("Robert_key", "AES", "Robert", sample_key_data.c_str(),
                      sample_key_data.length() + 1),
      0);
  char *key_type;
  size_t key_len;
  void *key;
  EXPECT_EQ(mysql_key_fetch("NotExisting", &key_type, "Robert", &key, &key_len),
            0);
  ASSERT_TRUE(key == nullptr);
}

TEST_F(Keyring_api_test, StoreStoreStoreFetchRemove) {
  std::string key_data1("Robi1");
  std::string key_data2("Robi2");

  EXPECT_EQ(
      mysql_key_store("Robert_key", "AES", "Robert", sample_key_data.c_str(),
                      sample_key_data.length() + 1),
      0);
  EXPECT_EQ(mysql_key_store("Robert_key1", "AES", "Robert", key_data1.c_str(),
                            key_data1.length() + 1),
            0);
  EXPECT_EQ(mysql_key_store("Robert_key2", "AES", "Robert", key_data2.c_str(),
                            key_data2.length() + 1),
            0);
  char *key_type;
  size_t key_len;
  void *key;
  EXPECT_EQ(mysql_key_fetch("Robert_key1", &key_type, "Robert", &key, &key_len),
            0);
  EXPECT_STREQ("AES", key_type);
  EXPECT_EQ(key_len, key_data1.length() + 1);
  ASSERT_TRUE(memcmp((char *)key, key_data1.c_str(), key_len) == 0);
  my_free(key_type);
  key_type = nullptr;
  my_free(key);
  key = nullptr;
  EXPECT_EQ(mysql_key_remove("Robert_key2", "Robert"), 0);
  // make sure the key was removed - fetch it
  EXPECT_EQ(mysql_key_fetch("Robert_key2", &key_type, "Robert", &key, &key_len),
            0);
  ASSERT_TRUE(key == nullptr);
}

TEST_F(Keyring_api_test, StoreValidTypes) {
  EXPECT_EQ(
      mysql_key_store("Robert_key", "AES", "Robert", sample_key_data.c_str(),
                      sample_key_data.length() + 1),
      0);
  EXPECT_EQ(
      mysql_key_store("Robert_key3", "RSA", "Robert", sample_key_data.c_str(),
                      sample_key_data.length() + 1),
      0);
  EXPECT_EQ(
      mysql_key_store("Robert_key4", "DSA", "Robert", sample_key_data.c_str(),
                      sample_key_data.length() + 1),
      0);
}

TEST_F(Keyring_api_test, StoreInvalidType) {
  EXPECT_CALL(
      *((Mock_logger *)logger.get()),
      log(ERROR_LEVEL, StrEq("Error while storing key: invalid key_type")));
  EXPECT_EQ(
      mysql_key_store("Robert_key", "YYY", "Robert", sample_key_data.c_str(),
                      sample_key_data.length() + 1),
      1);
  char *key_type;
  size_t key_len;
  void *key;
  EXPECT_EQ(mysql_key_fetch("Robert_key", &key_type, "Robert", &key, &key_len),
            0);
  ASSERT_TRUE(key == nullptr);
}

TEST_F(Keyring_api_test, StoreTwiceTheSameDifferentTypes) {
  EXPECT_EQ(
      mysql_key_store("Robert_key", "AES", "Robert", sample_key_data.c_str(),
                      sample_key_data.length() + 1),
      0);
  EXPECT_EQ(
      mysql_key_store("Robert_key", "RSA", "Robert", sample_key_data.c_str(),
                      sample_key_data.length() + 1),
      1);
}

TEST_F(Keyring_api_test, StorePBStoreSKFetchPBRemovePB) {
  EXPECT_EQ(
      mysql_key_store("percona_binlog", "AES", NULL, sample_key_data.c_str(),
                      sample_key_data.length() + 1),
      0);
  EXPECT_EQ(mysql_key_store("percona_RGRGRG_1", "AES", NULL, "1234_",
                            strlen("1234_") + 1),
            0);
  char *key_type = NULL;
  size_t key_len = 0;
  void *key = NULL;
  EXPECT_EQ(mysql_key_fetch("percona_binlog", &key_type, NULL, &key, &key_len),
            0);
  EXPECT_STREQ("AES", key_type);
  std::string key_data_with_version = "1:" + sample_key_data;
  EXPECT_EQ(key_len, key_data_with_version.length() + 1);
  ASSERT_TRUE(memcmp(reinterpret_cast<char *>(key),
                     key_data_with_version.c_str(), key_len) == 0);
  my_free(key_type);
  key_type = NULL;
  my_free(key);
  key = NULL;
  EXPECT_EQ(mysql_key_remove("percona_binlog", NULL), 1);
  // make sure the key was not removed - fetch it
  EXPECT_EQ(mysql_key_fetch("percona_binlog", &key_type, NULL, &key, &key_len),
            0);
  EXPECT_STREQ("AES", key_type);
  EXPECT_EQ(key_len, key_data_with_version.length() + 1);
  ASSERT_TRUE(memcmp(reinterpret_cast<char *>(key),
                     key_data_with_version.c_str(), key_len) == 0);
  my_free(key_type);
  my_free(key);
}

TEST_F(Keyring_api_test, GeneratePBFetchPBRotatePBFetchPB) {
  EXPECT_EQ(mysql_key_generate("percona_binlog", "AES", NULL, 16), 0);

  char *key_type = NULL;
  size_t key_len = 0;
  void *key_ver1 = NULL;
  EXPECT_EQ(
      mysql_key_fetch("percona_binlog", &key_type, NULL, &key_ver1, &key_len),
      0);
  EXPECT_STREQ("AES", key_type);
  EXPECT_EQ(key_len, static_cast<size_t>(18));
  ASSERT_TRUE(memcmp(reinterpret_cast<char *>(key_ver1), "1:", 2) == 0);
  my_free(key_type);
  key_type = NULL;

  void *key_ver2 = NULL;
  EXPECT_EQ(mysql_key_generate("percona_binlog", "AES", NULL, 16), 0);

  EXPECT_EQ(
      mysql_key_fetch("percona_binlog", &key_type, NULL, &key_ver2, &key_len),
      0);
  EXPECT_STREQ("AES", key_type);
  EXPECT_EQ(key_len, static_cast<size_t>(18));
  ASSERT_TRUE(memcmp(reinterpret_cast<char *>(key_ver2), "2:", 2) == 0);
  my_free(key_type);
  key_type = NULL;

  // make sure that rotated key is different than the original one
  ASSERT_TRUE(memcmp(reinterpret_cast<char *>(key_ver1) + 2,
                     reinterpret_cast<char *>(key_ver2) + 2, 16) != 0);

  my_free(key_ver1);
  my_free(key_ver2);
}

TEST_F(Keyring_api_test, GeneratePBFetchPBRotatePBFetchPB_UUID_version) {
  std::string percona_binlog_key_id_with_uuid("percona_binlog_");
  percona_binlog_key_id_with_uuid += generate_uuid();

  EXPECT_EQ(mysql_key_generate(percona_binlog_key_id_with_uuid.c_str(), "AES",
                               NULL, 16),
            0);

  char *key_type = NULL;
  size_t key_len = 0;
  void *key_ver1 = NULL;
  EXPECT_EQ(mysql_key_fetch(percona_binlog_key_id_with_uuid.c_str(), &key_type,
                            NULL, &key_ver1, &key_len),
            0);
  EXPECT_STREQ("AES", key_type);
  EXPECT_EQ(key_len, static_cast<size_t>(18));
  ASSERT_TRUE(memcmp(reinterpret_cast<char *>(key_ver1), "1:", 2) == 0);
  my_free(key_type);
  key_type = NULL;

  void *key_ver2 = NULL;
  EXPECT_EQ(mysql_key_generate(percona_binlog_key_id_with_uuid.c_str(), "AES",
                               NULL, 16),
            0);

  EXPECT_EQ(mysql_key_fetch(percona_binlog_key_id_with_uuid.c_str(), &key_type,
                            NULL, &key_ver2, &key_len),
            0);
  EXPECT_STREQ("AES", key_type);
  EXPECT_EQ(key_len, static_cast<size_t>(18));
  ASSERT_TRUE(memcmp(reinterpret_cast<char *>(key_ver2), "2:", 2) == 0);
  my_free(key_type);
  key_type = NULL;

  // make sure that rotated key is different than the original one
  ASSERT_TRUE(memcmp(reinterpret_cast<char *>(key_ver1) + 2,
                     reinterpret_cast<char *>(key_ver2) + 2, 16) != 0);

  my_free(key_ver1);
  my_free(key_ver2);
}

TEST_F(Keyring_api_test, GeneratePBRotatePBFetchFirstVersionFetchLatestPB) {
  EXPECT_EQ(mysql_key_generate("percona_binlog", "AES", NULL, 16), 0);

  EXPECT_EQ(mysql_key_generate("percona_binlog", "AES", NULL, 16), 0);

  char *key_type = NULL;
  size_t key_len = 0;
  void *key_ver1 = NULL;

  EXPECT_EQ(
      mysql_key_fetch("percona_binlog:1", &key_type, NULL, &key_ver1, &key_len),
      0);
  EXPECT_STREQ("AES", key_type);
  EXPECT_EQ(key_len, static_cast<size_t>(16));
  my_free(key_type);
  key_type = NULL;

  void *key_ver2 = NULL;

  EXPECT_EQ(
      mysql_key_fetch("percona_binlog", &key_type, NULL, &key_ver2, &key_len),
      0);
  EXPECT_STREQ("AES", key_type);
  EXPECT_EQ(key_len, static_cast<size_t>(18));
  ASSERT_TRUE(memcmp(reinterpret_cast<char *>(key_ver2), "2:", 2) == 0);
  my_free(key_type);
  key_type = NULL;

  // make sure that rotated key is different than the original one
  // + 2 to skip key version of retrieved latest percona_binlog key
  ASSERT_TRUE(memcmp(reinterpret_cast<char *>(key_ver1),
                     reinterpret_cast<char *>(key_ver2) + 2, 16) != 0);

  my_free(key_ver1);
  my_free(key_ver2);
}

TEST_F(Keyring_api_test, StorePBRotatePBFetchFirstVersionFetchLatestPB) {
  std::string percona_binlog_key_ver1_data("key_ver1");

  EXPECT_EQ(mysql_key_store("percona_binlog", "AES", NULL,
                            percona_binlog_key_ver1_data.c_str(),
                            percona_binlog_key_ver1_data.length() + 1),
            0);

  std::string percona_binlog_key_ver2_data("key_ver2");

  EXPECT_EQ(mysql_key_store("percona_binlog", "AES", NULL,
                            percona_binlog_key_ver2_data.c_str(),
                            percona_binlog_key_ver2_data.length() + 1),
            0);

  char *key_type = NULL;
  size_t key_len = 0;
  void *key_ver1 = NULL;

  EXPECT_EQ(
      mysql_key_fetch("percona_binlog:1", &key_type, NULL, &key_ver1, &key_len),
      0);
  EXPECT_STREQ("AES", key_type);
  EXPECT_EQ(key_len, percona_binlog_key_ver1_data.length() + 1);
  ASSERT_TRUE(memcmp(reinterpret_cast<char *>(key_ver1),
                     percona_binlog_key_ver1_data.c_str(), key_len) == 0);
  my_free(key_type);
  key_type = NULL;

  void *key_ver2 = NULL;

  EXPECT_EQ(
      mysql_key_fetch("percona_binlog", &key_type, NULL, &key_ver2, &key_len),
      0);
  EXPECT_STREQ("AES", key_type);
  EXPECT_EQ(key_len, percona_binlog_key_ver2_data.length() + 3);
  std::string expected_percona_binlog_key_ver2_data_with_verion =
      "2:" + percona_binlog_key_ver2_data;
  ASSERT_TRUE(memcmp(reinterpret_cast<char *>(key_ver2),
                     expected_percona_binlog_key_ver2_data_with_verion.c_str(),
                     key_len) == 0);
  my_free(key_type);
  key_type = NULL;

  my_free(key_ver1);
  my_free(key_ver2);
}

TEST_F(Keyring_api_test,
       RotatePBStoreSKFetchPBRotatePBFetchPBRotatePBRotateSKFetchPBFetchSK) {
  std::string percona_binlog_key_data_1("key1");

  EXPECT_EQ(mysql_key_store("percona_binlog", "AES", NULL,
                            percona_binlog_key_data_1.c_str(),
                            percona_binlog_key_data_1.length() + 1),
            0);

  std::string percona_sk_data_1("system_key1");

  EXPECT_EQ(
      mysql_key_store("percona_sk", "AES", NULL, percona_sk_data_1.c_str(),
                      percona_sk_data_1.length() + 1),
      0);

  char *key_type = NULL;
  size_t key_len = 0;
  void *key = NULL;
  EXPECT_EQ(mysql_key_fetch("percona_binlog", &key_type, NULL, &key, &key_len),
            0);
  EXPECT_STREQ("AES", key_type);
  std::string key_data_with_version = "1:" + percona_binlog_key_data_1;
  EXPECT_EQ(key_len, key_data_with_version.length() + 1);
  ASSERT_TRUE(memcmp(reinterpret_cast<char *>(key),
                     key_data_with_version.c_str(), key_len) == 0);
  my_free(key_type);
  key_type = NULL;
  my_free(key);
  key = NULL;

  std::string percona_binlog_key_data_2("key2");

  EXPECT_EQ(mysql_key_store("percona_binlog", "AES", NULL,
                            percona_binlog_key_data_2.c_str(),
                            percona_binlog_key_data_2.length() + 1),
            0);

  EXPECT_EQ(mysql_key_fetch("percona_binlog", &key_type, NULL, &key, &key_len),
            0);
  EXPECT_STREQ("AES", key_type);
  key_data_with_version = "2:" + percona_binlog_key_data_2;
  EXPECT_EQ(key_len, key_data_with_version.length() + 1);
  ASSERT_TRUE(memcmp(reinterpret_cast<char *>(key),
                     key_data_with_version.c_str(), key_len) == 0);
  my_free(key_type);
  key_type = NULL;
  my_free(key);
  key = NULL;

  std::string percona_binlog_key_data_3("key3___");

  EXPECT_EQ(mysql_key_store("percona_binlog", "AES", NULL,
                            percona_binlog_key_data_3.c_str(),
                            percona_binlog_key_data_3.length() + 1),
            0);

  std::string percona_sk_data_2("percona_sk_data2");

  EXPECT_EQ(
      mysql_key_store("percona_sk", "AES", NULL, percona_sk_data_2.c_str(),
                      percona_sk_data_2.length() + 1),
      0);

  EXPECT_EQ(mysql_key_fetch("percona_binlog", &key_type, NULL, &key, &key_len),
            0);
  EXPECT_STREQ("AES", key_type);
  key_data_with_version = "3:" + percona_binlog_key_data_3;
  EXPECT_EQ(key_len, key_data_with_version.length() + 1);
  ASSERT_TRUE(memcmp(reinterpret_cast<char *>(key),
                     key_data_with_version.c_str(), key_len) == 0);
  my_free(key_type);
  key_type = NULL;
  my_free(key);
  key = NULL;

  EXPECT_EQ(mysql_key_fetch("percona_sk", &key_type, NULL, &key, &key_len), 0);
  EXPECT_STREQ("AES", key_type);
  key_data_with_version = "2:" + percona_sk_data_2;
  EXPECT_EQ(key_len, key_data_with_version.length() + 1);
  ASSERT_TRUE(memcmp(reinterpret_cast<char *>(key),
                     key_data_with_version.c_str(), key_len) == 0);
  my_free(key_type);
  key_type = NULL;
  my_free(key);
  key = NULL;
}

TEST_F(Keyring_api_test, FetchSystemKeyOnJustInitializedContainer) {
  char *key_type = NULL;
  size_t key_len = 0;
  void *key = NULL;

  EXPECT_EQ(mysql_key_fetch("percona_binlog", &key_type, NULL, &key, &key_len),
            0);
  ASSERT_TRUE(key == NULL);
}

TEST_F(Keyring_api_test, KeyGenerate) {
  EXPECT_EQ(mysql_key_generate("Robert_key", "AES", "Robert", 128), 0);
  char *key_type;
  size_t key_len;
  void *key;
  EXPECT_EQ(mysql_key_fetch("Robert_key", &key_type, "Robert", &key, &key_len),
            0);
  EXPECT_STREQ("AES", key_type);
  EXPECT_EQ(key_len, (size_t)128);
  // Try accessing the last byte of key
  char ch = ((char *)key)[key_len - 1];
  // Just to get rid of unused variable compiler error
  (void)ch;
  my_free(key);
  my_free(key_type);
}

TEST_F(Keyring_api_test, InitWithDifferentKeyringFile) {
  EXPECT_EQ(
      mysql_key_store("Robert_key", "AES", "Robert", sample_key_data.c_str(),
                      sample_key_data.length() + 1),
      0);
  char *key_type;
  size_t key_len;
  void *key;
  EXPECT_EQ(mysql_key_fetch("Robert_key", &key_type, "Robert", &key, &key_len),
            0);
  EXPECT_STREQ("AES", key_type);
  EXPECT_EQ(key_len, sample_key_data.length() + 1);
  ASSERT_TRUE(memcmp((char *)key, sample_key_data.c_str(), key_len) == 0);
  my_free(key_type);
  key_type = nullptr;
  my_free(key);
  key = nullptr;
  delete[] keyring_filename;
  keyring_filename = new char[strlen("./new_keyring") + 1];
  strcpy(keyring_filename, "./new_keyring");
  remove(keyring_filename);
  keyring_file_data_value = keyring_filename;
  keyring_deinit_with_mock_logger();
  keyring_init_with_mock_logger();
  EXPECT_EQ(mysql_key_fetch("Robert_key", &key_type, "Robert", &key, &key_len),
            0);
  ASSERT_TRUE(key == nullptr);
  EXPECT_EQ(
      mysql_key_store("Robert_key_new", "AES", "Robert",
                      sample_key_data.c_str(), sample_key_data.length() + 1),
      0);
  delete[] keyring_filename;
  keyring_filename = new char[strlen("./keyring") + 1];
  strcpy(keyring_filename, "./keyring");
  keyring_file_data_value = keyring_filename;
  keyring_deinit_with_mock_logger();
  keyring_init_with_mock_logger();
  EXPECT_EQ(
      mysql_key_fetch("Robert_key_new", &key_type, "Robert", &key, &key_len),
      0);
  ASSERT_TRUE(key == nullptr);
  EXPECT_EQ(mysql_key_fetch("Robert_key", &key_type, "Robert", &key, &key_len),
            0);
  EXPECT_STREQ("AES", key_type);
  EXPECT_EQ(key_len, sample_key_data.length() + 1);
  ASSERT_TRUE(memcmp((char *)key, sample_key_data.c_str(), key_len) == 0);
  my_free(key_type);
  key_type = nullptr;
  my_free(key);
  key = nullptr;
  delete[] keyring_filename;
  keyring_filename = new char[strlen("./new_keyring") + 1];
  strcpy(keyring_filename, "./new_keyring");
  keyring_file_data_value = keyring_filename;
  keyring_deinit_with_mock_logger();
  keyring_init_with_mock_logger();
  EXPECT_EQ(
      mysql_key_fetch("Robert_key_new", &key_type, "Robert", &key, &key_len),
      0);
  EXPECT_STREQ("AES", key_type);
  EXPECT_EQ(key_len, sample_key_data.length() + 1);
  ASSERT_TRUE(memcmp((char *)key, sample_key_data.c_str(), key_len) == 0);
  my_free(key_type);
  key_type = nullptr;
  my_free(key);
  key = nullptr;
  remove("./new_keyring");
  // backup will stay as adding percona_binlog key will be unsuccessful - we
  // have already added it in keyring
  remove("./new_keyring.backup");
}

TEST_F(Keyring_api_test, NullUser) {
  EXPECT_EQ(
      mysql_key_store("Robert_key", "AES", nullptr, sample_key_data.c_str(),
                      sample_key_data.length() + 1),
      0);
  char *key_type;
  size_t key_len;
  void *key;
  EXPECT_EQ(mysql_key_fetch("Robert_key", &key_type, nullptr, &key, &key_len),
            0);
  EXPECT_STREQ("AES", key_type);
  EXPECT_EQ(key_len, sample_key_data.length() + 1);
  ASSERT_TRUE(memcmp((char *)key, sample_key_data.c_str(), key_len) == 0);
  my_free(key_type);
  key_type = nullptr;
  my_free(key);
  key = nullptr;
  EXPECT_EQ(
      mysql_key_store("Robert_key", "RSA", nullptr, sample_key_data.c_str(),
                      sample_key_data.length() + 1),
      1);
  EXPECT_EQ(
      mysql_key_store("Kamil_key", "AES", nullptr, sample_key_data.c_str(),
                      sample_key_data.length() + 1),
      0);
  EXPECT_EQ(mysql_key_fetch("Kamil_key", &key_type, nullptr, &key, &key_len),
            0);
  EXPECT_STREQ("AES", key_type);
  EXPECT_EQ(key_len, sample_key_data.length() + 1);
  ASSERT_TRUE(memcmp((char *)key, sample_key_data.c_str(), key_len) == 0);
  my_free(key_type);
  key_type = nullptr;
  my_free(key);
  key = nullptr;
  std::string arturs_key_data = "Artur";
  EXPECT_EQ(
      mysql_key_store("Artur_key", "AES", "Artur", arturs_key_data.c_str(),
                      arturs_key_data.length() + 1),
      0);
  EXPECT_EQ(mysql_key_fetch("Artur_key", &key_type, "Artur", &key, &key_len),
            0);
  EXPECT_STREQ("AES", key_type);
  EXPECT_EQ(key_len, arturs_key_data.length() + 1);
  ASSERT_TRUE(memcmp((char *)key, arturs_key_data.c_str(), key_len) == 0);
  my_free(key_type);
  key_type = nullptr;
  my_free(key);
  key = nullptr;
  EXPECT_EQ(mysql_key_remove("Robert_key", nullptr), 0);
  EXPECT_EQ(mysql_key_fetch("Robert_key", &key_type, "Robert", &key, &key_len),
            0);
  ASSERT_TRUE(key == nullptr);
  EXPECT_EQ(mysql_key_fetch("Artur_key", &key_type, "Artur", &key, &key_len),
            0);
  EXPECT_STREQ("AES", key_type);
  EXPECT_EQ(key_len, arturs_key_data.length() + 1);
  ASSERT_TRUE(memcmp((char *)key, arturs_key_data.c_str(), key_len) == 0);
  my_free(key_type);
  key_type = nullptr;
  my_free(key);
  key = nullptr;
}

TEST_F(Keyring_api_test, NullKeyId) {
  EXPECT_CALL(*((Mock_logger *)logger.get()),
              log(ERROR_LEVEL,
                  StrEq("Error while storing key: key_id cannot be empty")));
  EXPECT_EQ(mysql_key_store(nullptr, "AES", "Robert", sample_key_data.c_str(),
                            sample_key_data.length() + 1),
            1);
  EXPECT_CALL(*((Mock_logger *)logger.get()),
              log(ERROR_LEVEL,
                  StrEq("Error while storing key: key_id cannot be empty")));
  EXPECT_EQ(mysql_key_store(nullptr, "AES", nullptr, sample_key_data.c_str(),
                            sample_key_data.length() + 1),
            1);
  EXPECT_CALL(*((Mock_logger *)logger.get()),
              log(ERROR_LEVEL,
                  StrEq("Error while storing key: key_id cannot be empty")));
  EXPECT_EQ(mysql_key_store("", "AES", "Robert", sample_key_data.c_str(),
                            sample_key_data.length() + 1),
            1);
  EXPECT_CALL(*((Mock_logger *)logger.get()),
              log(ERROR_LEVEL,
                  StrEq("Error while storing key: key_id cannot be empty")));
  EXPECT_EQ(mysql_key_store("", "AES", nullptr, sample_key_data.c_str(),
                            sample_key_data.length() + 1),
            1);
  char *key_type;
  size_t key_len;
  void *key;
  EXPECT_CALL(*((Mock_logger *)logger.get()),
              log(ERROR_LEVEL,
                  StrEq("Error while fetching key: key_id cannot be empty")));
  EXPECT_EQ(mysql_key_fetch(nullptr, &key_type, "Robert", &key, &key_len), 1);
  EXPECT_CALL(*((Mock_logger *)logger.get()),
              log(ERROR_LEVEL,
                  StrEq("Error while fetching key: key_id cannot be empty")));
  EXPECT_EQ(mysql_key_fetch(nullptr, &key_type, nullptr, &key, &key_len), 1);
  EXPECT_CALL(*((Mock_logger *)logger.get()),
              log(ERROR_LEVEL,
                  StrEq("Error while fetching key: key_id cannot be empty")));
  EXPECT_EQ(mysql_key_fetch("", &key_type, "Robert", &key, &key_len), 1);
  EXPECT_CALL(*((Mock_logger *)logger.get()),
              log(ERROR_LEVEL,
                  StrEq("Error while fetching key: key_id cannot be empty")));
  EXPECT_EQ(mysql_key_fetch("", &key_type, nullptr, &key, &key_len), 1);

  EXPECT_CALL(*((Mock_logger *)logger.get()),
              log(ERROR_LEVEL,
                  StrEq("Error while removing key: key_id cannot be empty")));
  EXPECT_EQ(mysql_key_remove(nullptr, "Robert"), 1);
  EXPECT_CALL(*((Mock_logger *)logger.get()),
              log(ERROR_LEVEL,
                  StrEq("Error while removing key: key_id cannot be empty")));
  EXPECT_EQ(mysql_key_remove(nullptr, nullptr), 1);
  EXPECT_CALL(*((Mock_logger *)logger.get()),
              log(ERROR_LEVEL,
                  StrEq("Error while removing key: key_id cannot be empty")));
  EXPECT_EQ(mysql_key_remove("", "Robert"), 1);
  EXPECT_CALL(*((Mock_logger *)logger.get()),
              log(ERROR_LEVEL,
                  StrEq("Error while removing key: key_id cannot be empty")));
  EXPECT_EQ(mysql_key_remove("", nullptr), 1);

  EXPECT_CALL(*((Mock_logger *)logger.get()),
              log(ERROR_LEVEL,
                  StrEq("Error while generating key: key_id cannot be empty")));
  EXPECT_EQ(mysql_key_generate(nullptr, "AES", "Robert", 128), 1);
  EXPECT_CALL(*((Mock_logger *)logger.get()),
              log(ERROR_LEVEL,
                  StrEq("Error while generating key: key_id cannot be empty")));
  EXPECT_EQ(mysql_key_generate(nullptr, "AES", nullptr, 128), 1);
  EXPECT_CALL(*((Mock_logger *)logger.get()),
              log(ERROR_LEVEL,
                  StrEq("Error while generating key: key_id cannot be empty")));
  EXPECT_EQ(mysql_key_generate("", "AES", "Robert", 128), 1);
  EXPECT_CALL(*((Mock_logger *)logger.get()),
              log(ERROR_LEVEL,
                  StrEq("Error while generating key: key_id cannot be empty")));
  EXPECT_EQ(mysql_key_generate("", "AES", nullptr, 128), 1);
}

}  // namespace keyring__api_unittest
