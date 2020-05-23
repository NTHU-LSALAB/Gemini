/**
 * Copyright 2020 Hung-Hsin Chen, LSA Lab, National Tsing Hua University
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <glib.h>

#include <string>
#include <vector>

class ConfigFile {
 public:
  ConfigFile(const char *filename);
  ~ConfigFile();
  int getInteger(const gchar *group, const gchar *key, int fallback);
  double getDouble(const gchar *group, const gchar *key, double fallback);
  std::string getString(const gchar *group, const gchar *key, std::string fallback);
  // Parse byte size strings which may end with SI or IEC suffixes like M, GB, MiB, Gi
  size_t getSize(const gchar *group, const gchar *key, size_t fallback);
  // Get all groups in config file
  std::vector<std::string> getGroups();

 private:
  GKeyFile *keyfile_;
  std::vector<std::string> keyfile_groups_;
  template <typename T>
  T getField(const char *group, const char *key,
             T (*getter)(GKeyFile *, const gchar *, const gchar *, GError **), T fallback);
};
