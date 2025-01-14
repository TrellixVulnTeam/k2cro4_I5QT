// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/dom_storage/dom_storage_database.h"

#include "base/file_path.h"
#include "base/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "base/utf_string_conversions.h"
#include "sql/statement.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dom_storage {

void CreateV1Table(sql::Connection* db) {
  ASSERT_TRUE(db->is_open());
  ASSERT_TRUE(db->Execute("DROP TABLE IF EXISTS ItemTable"));
  ASSERT_TRUE(db->Execute(
      "CREATE TABLE ItemTable ("
      "key TEXT UNIQUE ON CONFLICT REPLACE, "
      "value TEXT NOT NULL ON CONFLICT FAIL)"));
}

void CreateV2Table(sql::Connection* db) {
  ASSERT_TRUE(db->is_open());
  ASSERT_TRUE(db->Execute("DROP TABLE IF EXISTS ItemTable"));
  ASSERT_TRUE(db->Execute(
      "CREATE TABLE ItemTable ("
      "key TEXT UNIQUE ON CONFLICT REPLACE, "
      "value BLOB NOT NULL ON CONFLICT FAIL)"));
}

void CreateInvalidKeyColumnTable(sql::Connection* db) {
  // Create a table with the key type as FLOAT - this is "invalid"
  // as far as the DOM Storage db is concerned.
  ASSERT_TRUE(db->is_open());
  ASSERT_TRUE(db->Execute("DROP TABLE IF EXISTS ItemTable"));
  ASSERT_TRUE(db->Execute(
      "CREATE TABLE IF NOT EXISTS ItemTable ("
      "key FLOAT UNIQUE ON CONFLICT REPLACE, "
      "value BLOB NOT NULL ON CONFLICT FAIL)"));
}
void CreateInvalidValueColumnTable(sql::Connection* db) {
  // Create a table with the value type as FLOAT - this is "invalid"
  // as far as the DOM Storage db is concerned.
  ASSERT_TRUE(db->is_open());
  ASSERT_TRUE(db->Execute("DROP TABLE IF EXISTS ItemTable"));
  ASSERT_TRUE(db->Execute(
      "CREATE TABLE IF NOT EXISTS ItemTable ("
      "key TEXT UNIQUE ON CONFLICT REPLACE, "
      "value FLOAT NOT NULL ON CONFLICT FAIL)"));
}

void InsertDataV1(sql::Connection* db,
                  const string16& key,
                  const string16& value) {
  sql::Statement statement(db->GetCachedStatement(SQL_FROM_HERE,
      "INSERT INTO ItemTable VALUES (?,?)"));
  statement.BindString16(0, key);
  statement.BindString16(1, value);
  ASSERT_TRUE(statement.is_valid());
  statement.Run();
}

void CheckValuesMatch(DomStorageDatabase* db,
                      const ValuesMap& expected) {
  ValuesMap values_read;
  db->ReadAllValues(&values_read);
  EXPECT_EQ(expected.size(), values_read.size());

  ValuesMap::const_iterator it = values_read.begin();
  for (; it != values_read.end(); ++it) {
    string16 key = it->first;
    NullableString16 value = it->second;
    NullableString16 expected_value = expected.find(key)->second;
    EXPECT_EQ(expected_value.string(), value.string());
    EXPECT_EQ(expected_value.is_null(), value.is_null());
  }
}

void CreateMapWithValues(ValuesMap* values) {
  string16 kCannedKeys[] = {
      ASCIIToUTF16("test"),
      ASCIIToUTF16("company"),
      ASCIIToUTF16("date"),
      ASCIIToUTF16("empty")
  };
  NullableString16 kCannedValues[] = {
      NullableString16(ASCIIToUTF16("123"), false),
      NullableString16(ASCIIToUTF16("Google"), false),
      NullableString16(ASCIIToUTF16("18-01-2012"), false),
      NullableString16(string16(), false)
  };
  for (unsigned i = 0; i < sizeof(kCannedKeys) / sizeof(kCannedKeys[0]); i++)
    (*values)[kCannedKeys[i]] = kCannedValues[i];
}

TEST(DomStorageDatabaseTest, SimpleOpenAndClose) {
  DomStorageDatabase db;
  EXPECT_FALSE(db.IsOpen());
  ASSERT_TRUE(db.LazyOpen(true));
  EXPECT_TRUE(db.IsOpen());
  EXPECT_EQ(DomStorageDatabase::V2, db.DetectSchemaVersion());
  db.Close();
  EXPECT_FALSE(db.IsOpen());
}

TEST(DomStorageDatabaseTest, CloseEmptyDatabaseDeletesFile) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_name = temp_dir.path().AppendASCII("TestDomStorageDatabase.db");
  ValuesMap storage;
  CreateMapWithValues(&storage);

  // First test the case that explicitly clearing the database will
  // trigger its deletion from disk.
  {
    DomStorageDatabase db(file_name);
    EXPECT_EQ(file_name, db.file_path());
    ASSERT_TRUE(db.CommitChanges(false, storage));
  }
  EXPECT_TRUE(file_util::PathExists(file_name));

  {
    // Check that reading an existing db with data in it
    // keeps the DB on disk on close.
    DomStorageDatabase db(file_name);
    ValuesMap values;
    db.ReadAllValues(&values);
    EXPECT_EQ(storage.size(), values.size());
  }

  EXPECT_TRUE(file_util::PathExists(file_name));
  storage.clear();

  {
    DomStorageDatabase db(file_name);
    ASSERT_TRUE(db.CommitChanges(true, storage));
  }
  EXPECT_FALSE(file_util::PathExists(file_name));

  // Now ensure that a series of updates and removals whose net effect
  // is an empty database also triggers deletion.
  CreateMapWithValues(&storage);
  {
    DomStorageDatabase db(file_name);
    ASSERT_TRUE(db.CommitChanges(false, storage));
  }

  EXPECT_TRUE(file_util::PathExists(file_name));

  {
    DomStorageDatabase db(file_name);
    ASSERT_TRUE(db.CommitChanges(false, storage));
    ValuesMap::iterator it = storage.begin();
    for (; it != storage.end(); ++it)
      it->second = NullableString16(true);
    ASSERT_TRUE(db.CommitChanges(false, storage));
  }
  EXPECT_FALSE(file_util::PathExists(file_name));
}

TEST(DomStorageDatabaseTest, TestLazyOpenIsLazy) {
  // This test needs to operate with a file on disk to ensure that we will
  // open a file that already exists when only invoking ReadAllValues.
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_name = temp_dir.path().AppendASCII("TestDomStorageDatabase.db");

  DomStorageDatabase db(file_name);
  EXPECT_FALSE(db.IsOpen());
  ValuesMap values;
  db.ReadAllValues(&values);
  // Reading an empty db should not open the database.
  EXPECT_FALSE(db.IsOpen());

  values[ASCIIToUTF16("key")] = NullableString16(ASCIIToUTF16("value"), false);
  db.CommitChanges(false, values);
  // Writing content should open the database.
  EXPECT_TRUE(db.IsOpen());

  db.Close();
  ASSERT_FALSE(db.IsOpen());

  // Reading from an existing database should open the database.
  CheckValuesMatch(&db, values);
  EXPECT_TRUE(db.IsOpen());
}

TEST(DomStorageDatabaseTest, TestDetectSchemaVersion) {
  DomStorageDatabase db;
  db.db_.reset(new sql::Connection());
  ASSERT_TRUE(db.db_->OpenInMemory());

  CreateInvalidValueColumnTable(db.db_.get());
  EXPECT_EQ(DomStorageDatabase::INVALID, db.DetectSchemaVersion());

  CreateInvalidKeyColumnTable(db.db_.get());
  EXPECT_EQ(DomStorageDatabase::INVALID, db.DetectSchemaVersion());

  CreateV1Table(db.db_.get());
  EXPECT_EQ(DomStorageDatabase::V1, db.DetectSchemaVersion());

  CreateV2Table(db.db_.get());
  EXPECT_EQ(DomStorageDatabase::V2, db.DetectSchemaVersion());
}

TEST(DomStorageDatabaseTest, TestLazyOpenUpgradesDatabase) {
  // This test needs to operate with a file on disk so that we
  // can create a table at version 1 and then close it again
  // so that LazyOpen sees there is work to do (LazyOpen will return
  // early if the database is already open).
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_name = temp_dir.path().AppendASCII("TestDomStorageDatabase.db");

  DomStorageDatabase db(file_name);
  db.db_.reset(new sql::Connection());
  ASSERT_TRUE(db.db_->Open(file_name));
  CreateV1Table(db.db_.get());
  db.Close();

  EXPECT_TRUE(db.LazyOpen(true));
  EXPECT_EQ(DomStorageDatabase::V2, db.DetectSchemaVersion());
}

TEST(DomStorageDatabaseTest, SimpleWriteAndReadBack) {
  DomStorageDatabase db;

  ValuesMap storage;
  CreateMapWithValues(&storage);

  EXPECT_TRUE(db.CommitChanges(false, storage));
  CheckValuesMatch(&db, storage);
}

TEST(DomStorageDatabaseTest, WriteWithClear) {
  DomStorageDatabase db;

  ValuesMap storage;
  CreateMapWithValues(&storage);

  ASSERT_TRUE(db.CommitChanges(false, storage));
  CheckValuesMatch(&db, storage);

  // Insert some values, clearing the database first.
  storage.clear();
  storage[ASCIIToUTF16("another_key")] =
      NullableString16(ASCIIToUTF16("test"), false);
  ASSERT_TRUE(db.CommitChanges(true, storage));
  CheckValuesMatch(&db, storage);

  // Now clear the values without inserting any new ones.
  storage.clear();
  ASSERT_TRUE(db.CommitChanges(true, storage));
  CheckValuesMatch(&db, storage);
}

TEST(DomStorageDatabaseTest, UpgradeFromV1ToV2WithData) {
  const string16 kCannedKey = ASCIIToUTF16("foo");
  const NullableString16 kCannedValue(ASCIIToUTF16("bar"), false);
  ValuesMap expected;
  expected[kCannedKey] = kCannedValue;

  DomStorageDatabase db;
  db.db_.reset(new sql::Connection());
  ASSERT_TRUE(db.db_->OpenInMemory());
  CreateV1Table(db.db_.get());
  InsertDataV1(db.db_.get(), kCannedKey, kCannedValue.string());

  ASSERT_TRUE(db.UpgradeVersion1To2());

  EXPECT_EQ(DomStorageDatabase::V2, db.DetectSchemaVersion());

  CheckValuesMatch(&db, expected);
}

TEST(DomStorageDatabaseTest, TestSimpleRemoveOneValue) {
  DomStorageDatabase db;

  ASSERT_TRUE(db.LazyOpen(true));
  const string16 kCannedKey = ASCIIToUTF16("test");
  const NullableString16 kCannedValue(ASCIIToUTF16("data"), false);
  ValuesMap expected;
  expected[kCannedKey] = kCannedValue;

  // First write some data into the database.
  ASSERT_TRUE(db.CommitChanges(false, expected));
  CheckValuesMatch(&db, expected);

  ValuesMap values;
  // A null string in the map should mean that that key gets
  // removed.
  values[kCannedKey] = NullableString16(true);
  EXPECT_TRUE(db.CommitChanges(false, values));

  expected.clear();
  CheckValuesMatch(&db, expected);
}

TEST(DomStorageDatabaseTest, TestCanOpenAndReadWebCoreDatabase) {
  FilePath webcore_database;
  PathService::Get(base::DIR_SOURCE_ROOT, &webcore_database);
  webcore_database = webcore_database.AppendASCII("webkit");
  webcore_database = webcore_database.AppendASCII("data");
  webcore_database = webcore_database.AppendASCII("dom_storage");
  webcore_database =
      webcore_database.AppendASCII("webcore_test_database.localstorage");

  ASSERT_TRUE(file_util::PathExists(webcore_database));

  DomStorageDatabase db(webcore_database);
  ValuesMap values;
  db.ReadAllValues(&values);
  EXPECT_TRUE(db.IsOpen());
  EXPECT_EQ(2u, values.size());

  ValuesMap::const_iterator it =
      values.find(ASCIIToUTF16("value"));
  EXPECT_TRUE(it != values.end());
  EXPECT_EQ(ASCIIToUTF16("I am in local storage!"), it->second.string());

  it = values.find(ASCIIToUTF16("timestamp"));
  EXPECT_TRUE(it != values.end());
  EXPECT_EQ(ASCIIToUTF16("1326738338841"), it->second.string());

  it = values.find(ASCIIToUTF16("not_there"));
  EXPECT_TRUE(it == values.end());
}

TEST(DomStorageDatabaseTest, TestCanOpenFileThatIsNotADatabase) {
  // Write into the temporary file first.
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  FilePath file_name = temp_dir.path().AppendASCII("TestDomStorageDatabase.db");

  const char kData[] = "I am not a database.";
  file_util::WriteFile(file_name, kData, strlen(kData));

  {
    // Try and open the file. As it's not a database, we should end up deleting
    // it and creating a new, valid file, so everything should actually
    // succeed.
    DomStorageDatabase db(file_name);
    ValuesMap values;
    CreateMapWithValues(&values);
    EXPECT_TRUE(db.CommitChanges(true, values));
    EXPECT_TRUE(db.CommitChanges(false, values));
    EXPECT_TRUE(db.IsOpen());

    CheckValuesMatch(&db, values);
  }

  {
    // Try to open a directory, we should fail gracefully and not attempt
    // to delete it.
    DomStorageDatabase db(temp_dir.path());
    ValuesMap values;
    CreateMapWithValues(&values);
    EXPECT_FALSE(db.CommitChanges(true, values));
    EXPECT_FALSE(db.CommitChanges(false, values));
    EXPECT_FALSE(db.IsOpen());

    values.clear();

    db.ReadAllValues(&values);
    EXPECT_EQ(0u, values.size());
    EXPECT_FALSE(db.IsOpen());

    EXPECT_TRUE(file_util::PathExists(temp_dir.path()));
  }
}

}  // namespace dom_storage
