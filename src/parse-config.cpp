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

#include "parse-config.h"

#include <regex>
#include <stdexcept>

using std::string;
using std::vector;

ConfigFile::ConfigFile(const char *filename) {
  GError *err = nullptr;

  keyfile_ = g_key_file_new();
  if (!g_key_file_load_from_file(keyfile_, filename, G_KEY_FILE_NONE, &err)) {
    char msg[256];
    snprintf(msg, sizeof(msg), "%s (file: %s)", err->message, filename);
    throw std::runtime_error(msg);
  }

  gsize n_group;
  gchar **groups = g_key_file_get_groups(keyfile_, &n_group);
  for (gsize i = 0; i < n_group; i++) {
    keyfile_groups_.push_back(groups[i]);
  }
  g_strfreev(groups);
}

ConfigFile::~ConfigFile() { g_key_file_free(keyfile_); }

template <typename T>
T ConfigFile::getField(const char *group, const char *key,
                       T (*getter)(GKeyFile *, const gchar *, const gchar *, GError **),
                       T fallback) {
  GError *err = nullptr;
  T value = getter(keyfile_, group, key, &err);
  if (err != nullptr) {
    if (strcmp("g-key-file-error-quark", g_quark_to_string(err->domain)) == 0 &&
        err->code == G_KEY_FILE_ERROR_KEY_NOT_FOUND) {
      // not defined in config file
      value = fallback;
    } else {
      throw std::runtime_error(err->message);
    }
    g_error_free(err);
  }
  return value;
}

int ConfigFile::getInteger(const char *group, const char *key, int fallback) {
  return getField<int>(group, key, g_key_file_get_integer, fallback);
}

double ConfigFile::getDouble(const char *group, const char *key, double fallback) {
  return getField<double>(group, key, g_key_file_get_double, fallback);
}

string ConfigFile::getString(const char *group, const char *key, string fallback) {
  char *tmp = new char[fallback.length() + 1];
  strncpy(tmp, fallback.c_str(), fallback.length());
  string result(getField<char *>(group, key, g_key_file_get_string, tmp));
  delete[] tmp;
  return result;
}

size_t ConfigFile::getSize(const gchar *group, const gchar *key, size_t fallback) {
  gchar *value_str = g_key_file_get_string(keyfile_, group, key, nullptr);
  if (value_str == nullptr) {
    // not defined in config file
    return fallback;
  }

  // parse suffix like M, KB, GiB
  std::regex reg("(\\d+)\\s*(([K,M,G,T]i?)?B?)");
  std::cmatch cm;
  const char *l = value_str, *r = value_str + strlen(value_str);
  if (std::regex_match(l, r, cm, reg)) {
    // cm[0]: whole string
    // cm[1]: number
    // cm[2]: suffix w/ B (e.g. MiB)
    // cm[3]: suffix w/o B (e.g. Mi)
    size_t value = stoul(cm[1].str(), nullptr, 10);
    size_t base = 1000, scale = 1;
    if (cm[3].str().length() > 1) base = 1UL << 10;
    if (cm[3].str().length() > 0) {
      switch (cm[3].str()[0]) {
        case 'T':
          scale *= base;
        case 'G':
          scale *= base;
        case 'M':
          scale *= base;
        case 'K':
          scale *= base;
      }
    }
    delete[] value_str;
    return value * scale;
  } else {
    // wrong format
    char msg[256];
    snprintf(msg, sizeof(msg),
             "Key file contains key \"%s\" in group \"%s\" which has a value "
             "\"%s\" that cannot be interpreted.",
             group, key, value_str);
    delete[] value_str;
    throw std::runtime_error(msg);
  }
}

vector<string> ConfigFile::getGroups() { return keyfile_groups_; }
