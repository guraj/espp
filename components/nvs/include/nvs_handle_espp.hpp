#pragma once

#include <string>

#include "nvs.h"
#include "nvs.hpp"
#include "nvs_flash.h"
#include "nvs_handle.hpp"

#include "base_component.hpp"

namespace espp {
/**
 * @brief Class to manage NVS handles.
 * @details This class provides an interface for managing specific ESP NVS namespaces,
 * enabling operations like reading, writing, and committing key-value pairs. It
 * encapsulates all direct interactions with the NVS to ensure proper error handling
 * and namespace management.
 *
 * @section nvshandle_ex1 NVSHandle Example
 * @snippet nvs_example.cpp nvshandle example
 */
class NVSHandle : public BaseComponent {
public:
  /// @brief Construct a new NVSHandle object
  /// @param[in] ns_name Namespace for NVS
  /// @param[out] ec Saves a std::error_code representing success or failure
  /// @details Create an NVSHandle object for the key-value pairs in the ns_name namespace
  explicit NVSHandle(const char *ns_name, std::error_code &ec)
      : BaseComponent("NVSHandle", espp::Logger::Verbosity::WARN) {
    if (strlen(ns_name) > 15) {
      logger_.error("Namespace too long, must be <= 15 characters: {}", ns_name);
      ec = make_error_code(NvsErrc::Namespace_Length_Too_Long);
      return;
    }

    esp_err_t err;
    handle = nvs::open_nvs_handle(ns_name, NVS_READWRITE, &err);
    if (err != ESP_OK) {
      logger_.error("Error {} opening NVS handle for namespace '{}'!", esp_err_to_name(err),
                    ns_name);
      ec = make_error_code(NvsErrc::Open_NVS_Handle_Failed);
    }
  }

  /// @brief Reads a variable from the NVS
  /// @param[in] key NVS Key of the variable to read
  /// @param[in] value Variable to read
  /// @param[out] ec Saves a std::error_code representing success or failure
  /// @details Reads the value of key into value, if key exists
  template <typename T> void get(const char *key, T &value, std::error_code &ec) {
    check_key_length(key, ec);
    if (ec)
      return;
    esp_err_t err;
    T readvalue;
    err = handle->get_item(key, readvalue);
    switch (err) {
    case ESP_OK:
      value = readvalue;
      break;
    case ESP_ERR_NVS_NOT_FOUND:
      ec = make_error_code(NvsErrc::Key_Not_Found);
      logger_.error("The value is not initialized in NVS, key = '{}'", key);
      break;
    default:
      logger_.error("Error {} reading!", esp_err_to_name(err));
      ec = make_error_code(NvsErrc::Read_NVS_Failed);
    }
    return;
  }

  /// @brief Reads a variable from the NVS
  /// @param[in] key NVS Key of the variable to read
  /// @param[in] value Variable to read
  /// @param[out] ec Saves a std::error_code representing success or failure
  template <typename T> void get(std::string_view key, T &value, std::error_code &ec) {
    get(key.data(), value, ec);
  }

  /// @brief Reads a bool from the NVS
  /// @param[in] key NVS Key of the bool to read
  /// @param[in] value bool to read
  /// @param[out] ec Saves a std::error_code representing success or failure
  /// @details Read the key/variable pair
  void get(const char *key, bool &value, std::error_code &ec) {
    uint8_t u8 = static_cast<uint8_t>(value);
    get<uint8_t>(key, u8, ec);
    if (!ec)
      value = static_cast<bool>(u8);
  }

  /// @brief Reads a bool from the NVS
  /// @param[in] key NVS Key of the bool to read
  /// @param[in] value bool to read
  /// @param[out] ec Saves a std::error_code representing success or failure
  void get(std::string_view key, bool &value, std::error_code &ec) { get(key.data(), value, ec); }

  /// @brief Reads a string from the NVS
  /// @param[in] key NVS Key of the string to read
  /// @param[in] value string to read
  /// @param[out] ec Saves a std::error_code representing success or failure
  void get(std::string_view key, std::string &value, std::error_code &ec) {
    get(key.data(), value, ec);
  }

  /// @brief Reads a string from the NVS
  /// @param[in] key NVS Key of the string to read
  /// @param[in] value string to read
  /// @param[out] ec Saves a std::error_code representing success or failure
  void get(const char *key, std::string &value, std::error_code &ec) {
    check_key_length(key, ec);
    if (ec)
      return;
    esp_err_t err;
    std::size_t len = 0;
    err = handle->get_item_size(nvs::ItemType::SZ, key, len);
    if (err != ESP_OK) {
      logger_.error("Error {} reading!", esp_err_to_name(err));
      ec = make_error_code(NvsErrc::Read_NVS_Failed);
      return;
    }
    value.resize(len);
    err = handle->get_string(key, value.data(), len);
    if (err != ESP_OK) {
      ec = make_error_code(NvsErrc::Read_NVS_Failed);
      logger_.error("Error {} reading from NVS!", esp_err_to_name(err));
      return;
    }
    return;
  }

  /// @brief Save a variable in the NVS
  /// @param[in] key NVS Key of the variable to read
  /// @param[in] value Variable to read
  /// @param[out] ec Saves a std::error_code representing success or failure
  /// @details Saves the key/variable pair without committing the NVS.
  template <typename T> void set(const char *key, T value, std::error_code &ec) {
    check_key_length(key, ec);
    if (ec)
      return;
    esp_err_t err;
    err = handle->set_item(key, value);
    if (err != ESP_OK) {
      ec = make_error_code(NvsErrc::Write_NVS_Failed);
      logger_.error("Error {} writing to NVS!", esp_err_to_name(err));
    }
    return;
  }

  /// @brief Save a variable in the NVS
  /// @param[in] key NVS Key of the variable to save
  /// @param[in] value Variable to save
  /// @param[out] ec Saves a std::error_code representing success or failure
  template <typename T> void set(std::string_view key, T value, std::error_code &ec) {
    set(key.data(), value, ec);
  }

  /// @brief Set a bool in the NVS
  /// @param[in] key NVS Key of the bool to set
  /// @param[in] value bool to set
  /// @param[out] ec Saves a std::error_code representing success or failure
  void set(std::string_view key, bool value, std::error_code &ec) { set(key.data(), value, ec); }

  /// @brief Set a bool in the NVS
  /// @param[in] key NVS Key of the bool to set
  /// @param[in] value bool to set
  /// @param[out] ec Saves a std::error_code representing success or failure
  void set(const char *key, bool value, std::error_code &ec) {
    uint8_t u8 = static_cast<uint8_t>(value);
    set<uint8_t>(key, u8, ec);
  }

  /// @brief Set a string in the NVS
  /// @param[in] key NVS Key of the string to set
  /// @param[in] value string to set
  /// @param[out] ec Saves a std::error_code representing success or failure
  void set(std::string_view key, const std::string &value, std::error_code &ec) {
    set(key.data(), value, ec);
  }

  /// @brief Set a string in the NVS
  /// @param[in] key NVS Key of the string to set
  /// @param[in] value string to set
  /// @param[out] ec Saves a std::error_code representing success or failure
  void set(const char *key, const std::string &value, std::error_code &ec) {
    check_key_length(key, ec);
    if (ec)
      return;
    esp_err_t err;
    err = handle->set_string(key, value.data());
    if (err != ESP_OK) {
      ec = make_error_code(NvsErrc::Write_NVS_Failed);
      logger_.error("Error {} writing to NVS!", esp_err_to_name(err));
      return;
    }
    return;
  }

  /// @brief Commit changes
  /// @param[out] ec Saves a std::error_code representing success or failure
  /// @details Commits changes to the NVS
  void commit(std::error_code &ec) {
    esp_err_t err = handle->commit();
    if (err != ESP_OK) {
      logger_.error("Error {} committing to NVS!", esp_err_to_name(err));
      ec = make_error_code(NvsErrc::Commit_NVS_Failed);
    }
    return;
  }

protected:
  std::unique_ptr<nvs::NVSHandle> handle;

  void check_key_length(const char *key, std::error_code &ec) {
    if (strlen(key) > 15) {
      logger_.error("Key too long, must be <= 15 characters: {}", key);
      ec = make_error_code(NvsErrc::Key_Length_Too_Long);
      return;
    }
  }

  /**
   * @brief overload of std::make_error_code used by custom error codes.
   */
  std::error_code make_error_code(NvsErrc e) { return {static_cast<int>(e), theNvsErrCategory}; }

}; // Class NVSHandle
} // namespace espp
