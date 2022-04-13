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

#include "neva/browser_service/browser/url_database.h"

#include "base/command_line.h"
#include "base/logging.h"
#include "base/strings/stringprintf.h"
#include "neva/app_runtime/browser/app_runtime_browser_switches.h"
#include "sql/statement.h"

namespace {
const char kDatabaseFileName[] = "URLDatabase.db";
}

namespace browser {

URLDatabase::URLDatabase(const std::string& table_name)
    : table_name_(table_name) {
  base::CommandLine* cmd_line = base::CommandLine::ForCurrentProcess();
  db_file_path_ =
      cmd_line->GetSwitchValuePath(kUserDataDir).AppendASCII(kDatabaseFileName);
  LOG(INFO) << __func__ << "db_file_path_ = " << db_file_path_.value();
  CHECK(!db_file_path_.empty()) << "db_file_path_ is Empty";
  CHECK(db_.Open(db_file_path_)) << "URLDatabase Open Operation Failed";
  CHECK(CreateTableIfNeeded()) << "Creation of Table Failed";
}

URLDatabase::~URLDatabase() = default;

bool URLDatabase::InsertURL(const std::string& url) {
  if (!db_.BeginTransaction()) {
    LOG(ERROR) << __func__ << "Failed to begin the transaction.";
    return false;
  }

  const std::string query =
      base::StringPrintf("INSERT INTO %s VALUES (?)", table_name_.c_str());

  sql::Statement statement(
      db_.GetCachedStatement(SQL_FROM_HERE, query.c_str()));
  statement.BindString(0, url);
  statement.Run();

  if (!db_.GetLastChangeCount()) {
    LOG(ERROR) << __func__ << "Insertion in DB Failed";
    db_.RollbackTransaction();
    return false;
  }

  db_.CommitTransaction();
  return true;
}

bool URLDatabase::DeleteURLs(const std::vector<std::string>& url_list) {
  VLOG(2) << __func__ << "Number of URLs to be deleted: " << url_list.size();

  if (!db_.BeginTransaction()) {
    LOG(ERROR) << __func__ << "Failed to begin the transaction.";
    return false;
  }

  const std::string query = base::StringPrintf(
      "DELETE FROM %s WHERE url LIKE ?", table_name_.c_str());
  for (const auto& url : url_list) {
    sql::Statement statement(
        db_.GetCachedStatement(SQL_FROM_HERE, query.c_str()));
    statement.BindString(0, url);
    statement.Run();

    if (!db_.GetLastChangeCount()) {
      LOG(ERROR) << __func__ << "Deletion from DB Failed";
      db_.RollbackTransaction();
      return false;
    }
  }

  db_.CommitTransaction();
  return true;
}

bool URLDatabase::ModifyURL(const std::string& old_url,
                            const std::string& new_url) {
  const std::string query = base::StringPrintf(
      "UPDATE  %s SET url = ? WHERE url LIKE ? ", table_name_.c_str());

  sql::Statement statement(
      db_.GetCachedStatement(SQL_FROM_HERE, query.c_str()));
  statement.BindString(0, new_url);
  statement.BindString(1, old_url);
  statement.Run();

  if (!db_.GetLastChangeCount()) {
    LOG(ERROR) << __func__ << "Modification of URL in DB Failed";
    return false;
  }

  return true;
}

bool URLDatabase::IsURLAvailable(const std::string& url) {
  int count = 0;
  const std::string query =
      "select COUNT(*) from " + table_name_ + " WHERE url LIKE ? ";
  sql::Statement response_urls(
      db_.GetCachedStatement(SQL_FROM_HERE, query.c_str()));
  response_urls.BindString(0, url);
  if (response_urls.Step())
    count = response_urls.ColumnInt(0);

  return count > 0 ? true : false;
}

bool URLDatabase::GetAllURLs(std::vector<std::string>& url_list) {
  const std::string query = "select url from " + table_name_;
  sql::Statement response_urls(db_.GetUniqueStatement(query.c_str()));
  while (response_urls.Step()) {
    url_list.push_back(response_urls.ColumnString(0));
  }

  return true;
}

bool URLDatabase::CreateTableIfNeeded() {
  if (db_.DoesTableExist(table_name_.c_str())) {
    LOG(INFO) << __func__ << "Table Already Exists";
    return true;
  }

  const std::string query = base::StringPrintf(
      "CREATE TABLE %s ( "
      "url TEXT PRIMARY KEY NOT NULL"
      ")",
      table_name_.c_str());

  if (!db_.Execute(query.c_str())) {
    LOG(ERROR) << __func__ << "Error Creating " << table_name_ << " Table";
    return false;
  }

  return true;
}

}  // namespace browser
