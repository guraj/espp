#pragma once

#include <optional>
#include <vector>

#include "logger.hpp"
#include "socket_common.hpp"
#include "task.hpp"

// TODO: should this class _contain_ a socket or just create sockets within each
//       call?

namespace espp {
  /**
   *   @brief Class for managing sending and receiving data using UDP/IP. Can be
   *          used to create client or server sockets.
   *
   *   See
   *   https://github.com/espressif/esp-idf/tree/master/examples/protocols/sockets/udp_multicast
   *   for more information on udp multicast sockets.
   *
   * \section udp_ex1 UDP Client Example
   * \snippet socket_example.cpp UDP Client example
   * \section udp_ex2 UDP Server Example
   * \snippet socket_example.cpp UDP Server example
   *
   * \section udp_ex3 UDP Client Response Example
   * \snippet socket_example.cpp UDP Client Response example
   * \section udp_ex4 UDP Server Response Example
   * \snippet socket_example.cpp UDP Server Response example
   *
   * \section udp_ex5 UDP Multicast Client Example
   * \snippet socket_example.cpp UDP Multicast Client example
   * \section udp_ex6 UDP Multicast Server Example
   * \snippet socket_example.cpp UDP Multicast Server example
   *
   */
  class UdpSocket {
  public:

    /**
     * @brief Callback function to be called with data received from a remote when calling recvfrom.
     * @param std::vector<uint8_t>& data
     * @param Socket::Info& Sender information (address, port)
     * @return std::optional<std::vector<uint8_t>> optional data to return to sender.
     */
    typedef std::function<std::optional<std::vector<uint8_t>>(std::vector<uint8_t>&, const Socket::Info&)> receive_callback_fn;

    /**
     * @brief Callback function to be called with data returned from remote after calling sendto.
     * @param std::vector<uint8_t>& data
     */
    typedef std::function<void(std::vector<uint8_t>&)> response_callback_fn;

    struct Config {
      Logger::Verbosity log_level{Logger::Verbosity::WARN}; /**< Verbosity level for the UDP socket logger. */
    };

    struct ReceiveConfig {
      size_t port; /**< Port number to bind to / receive from. */
      size_t buffer_size; /**< Max size of data we can receive at one time. */
      bool is_multicast_endpoint{false}; /**< Whether this should be a multicast endpoint. */
      std::string multicast_group{""}; /**< If this is a multicast endpoint, this is the group it belongs to. */
      receive_callback_fn on_receive_callback{nullptr}; /**< Function containing business logic to handle data received. */
    };

    struct SendConfig {
      std::string ip_address; /**< Address to send data to. */
      size_t port; /**< Port number to send data to.*/
      bool is_multicast_endpoint{false}; /**< Whether this should be a multicast endpoint. */
      bool wait_for_response{false}; /**< Whether to wait for a response from the remote or not. */
      size_t response_size{0}; /**< If waiting for a response, this is the maximum size response we will receive. */
      response_callback_fn on_response_callback{nullptr}; /**< If waiting for a response, this is an optional handler which is provided the response data. */
      std::chrono::duration<float> response_timeout{0.5f}; /**< If waiting for a response, this is the maximum timeout to wait. */
    };

    UdpSocket(const Config& config) : logger_({.tag="UdpSocket", .level=config.log_level}) {
      init();
    }

    /**
     * @brief Tear down any resources associted with the socket.
     */
    ~UdpSocket() {
      // clean up socket resources here
      cleanup();
    }

    /**
     * @brief Send data to the endpoint specified by the send_config.
     *        Can be configured to multicast (within send_config) and can be
     *        configured to block waiting for a response from the remote.
     *
     *          NOTE: in the case of multicast, it will block only until the
     *                first response.
     *
     *        If response is requested, a callback can be provided in
     *        send_config which will be provided the response data for
     *        processing.
     * @param data vector of bytes to send to the remote endpoint.
     * @param send_config SendConfig struct indicating where to send and whether
     *        to wait for a response.
     * @return true if the data was sent, false otherwise.
     */
    bool send(const std::vector<uint8_t>& data, const SendConfig& send_config) {
      if (!Socket::is_valid(socket_)) {
        logger_.error("Socket invalid, cannot send");
        return false;
      }
      if (send_config.is_multicast_endpoint) {
        // configure it for multicast
        if (!Socket::make_multicast(socket_)) {
          logger_.error("Cannot make multicast: {} - '{}'", errno, strerror(errno));
          return false;
        }
      }
      // set the receive timeout
      if (!Socket::set_receive_timeout(socket_, send_config.response_timeout)) {
        logger_.error("Could not set receive timeout to {}: {} - '{}'",
                      send_config.response_timeout.count(), errno, strerror(errno));
        return false;
      }
      // sendto
      Socket::Info server_info;
      server_info.init_ipv4(send_config.ip_address, send_config.port);
      auto server_address = server_info.ipv4_ptr();
      logger_.info("Client sending {} bytes to {}:{}", data.size(),
                   send_config.ip_address, send_config.port);
      int num_bytes_sent = sendto(socket_, data.data(), data.size(), 0, (struct sockaddr *)server_address, sizeof(*server_address));
      if (num_bytes_sent < 0) {
        logger_.error("Error occurred during sending: {} - '{}'", errno, strerror(errno));
        return false;
      }
      logger_.debug("Client sent {} bytes", num_bytes_sent);
      // we don't need to wait for a response and the socket is good;
      if (!send_config.wait_for_response) {
        return true;
      }
      if (send_config.response_size == 0) {
        logger_.warn("Response requested, but response_size=0, not waiting for response!");
        // NOTE: we did send successfully, so we return true and warn about
        // misconfiguration
        return true;
      }
      std::vector<uint8_t> received_data;
      logger_.info("Client waiting for response");
      if (!receive(send_config.response_size, received_data, server_info)) {
        logger_.warn("Client could not get response");
        return false;
      }
      logger_.info("Client got {} bytes of response", received_data.size());
      if (send_config.on_response_callback) {
        logger_.debug("Client calling response callback");
        send_config.on_response_callback(received_data);
      }
      return true;
    }

    /**
     * @brief Call recvfrom on the socket, assuming it has already been
     *        configured appropriately.
     *
     * @param max_num_bytes Maximum number of bytes to receive.
     * @param data Vector of bytes of received data.
     * @param remote_info Socket::Info containing the sender's information. This
     *        will be populated with the information about the sender.
     * @return true if successfully received, false otherwise.
     */
    bool receive(size_t max_num_bytes, std::vector<uint8_t>& data, Socket::Info& remote_info) {
      if (!Socket::is_valid(socket_)) {
        logger_.error("Socket invalid, cannot receive.");
        return false;
      }
      // recvfrom
      auto remote_address = remote_info.ipv4_ptr();
      socklen_t socklen = sizeof(*remote_address);
      // put it on the heap so that our stack usage doesn't change depending on
      // max_num_bytes
      std::unique_ptr<uint8_t[]> receive_buffer(new uint8_t[max_num_bytes]());
      // now actually receive
      logger_.info("Receiving up to {} bytes", max_num_bytes);
      int num_bytes_received = recvfrom(socket_, receive_buffer.get(),
                                        max_num_bytes, 0,
                                        (struct sockaddr *)remote_address, &socklen);
      // if we didn't receive anything return false and don't do anything else
      if (num_bytes_received < 0) {
        logger_.error("Receive failed: {} - '{}'", errno, strerror(errno));
        return false;
      }
      // we received data, so call the callback function if one was provided.
      data.assign(receive_buffer.get(), receive_buffer.get() + num_bytes_received);
      remote_info.update();
      logger_.debug("Received {} bytes from {}", num_bytes_received, remote_info.to_string());
      return true;
    }

    /**
     * @brief Configure a server socket and start a thread to continuously
     *        receive and handle data coming in on that socket.
     *
     * @param task_config Task::Config struct for configuring the receive task.
     * @param receive_config ReceiveConfig struct with socket and callback info.
     * @return true if the socket was created and task was started, false otherwise.
     */
    bool start_receiving(Task::Config& task_config, const ReceiveConfig& receive_config) {
      if (task_ && task_->is_started()) {
        logger_.error("Server is alrady receiving");
        return false;
      }
      if (!Socket::is_valid(socket_)) {
        logger_.error("Socket invalid, cannot start receiving.");
        return false;
      }
      server_receive_callback_ = receive_config.on_receive_callback;
      // bind
      struct sockaddr_in server_addr;
      // configure the server socket accordingly - assume IPV4 and bind to the
      // any address "0.0.0.0"
      server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
      server_addr.sin_family = address_family_;
      server_addr.sin_port = htons(receive_config.port);
      int err = bind(socket_, (struct sockaddr *)&server_addr, sizeof(server_addr));
      if (err < 0) {
        logger_.error("Unable to bind: {} - '{}'", errno, strerror(errno));
        return false;
      }
      if (receive_config.is_multicast_endpoint) {
        // enable multicast
        if (!Socket::make_multicast(socket_)) {
          logger_.error("Unable to make bound socket multicast: {} - '{}'", errno, strerror(errno));
          return false;
        }
        // add multicast group
        if (!Socket::add_multicast_group(socket_, receive_config.multicast_group)) {
          logger_.error("Unable to add multicast group to bound socket: {} - '{}'", errno, strerror(errno));
          return false;
        }
      }
      // set the callback function
      using namespace std::placeholders;
      task_config.callback = std::bind(&UdpSocket::server_task_function, this,
                                       receive_config.buffer_size, _1, _2);
      // start the thread
      task_ = Task::make_unique(task_config);
      task_->start();
      return true;
    }

  protected:

    /**
     * @brief Create the UDP socket and enable reuse.
     * @return true if the socket was initialized properly, false otherwise
     */
    bool init() {
      // actually make the socket
      socket_ = socket(address_family_, SOCK_DGRAM, ip_protocol_);
      if (!Socket::is_valid(socket_)) {
        logger_.error("Cannot create socket: {} - '{}'", errno, strerror(errno));
        return false;
      }
      if (!Socket::enable_reuse(socket_)) {
        logger_.error("Cannot enable reuse: {} - '{}'", errno, strerror(errno));
        return false;
      }
      return true;
    }

    /**
     *  @brief If the socket was created, we shut it down and close it here.
     */
    void cleanup() {
      if (Socket::is_valid(socket_)) {
        shutdown(socket_, 0);
        close(socket_);
        socket_ = -1;
        logger_.info("Closed socket");
      }
    }

    /**
     * @brief Function run in the task_ when start_receiving is called.
     *        Continuously receive data on the socket, pass the received data to
     *        the registered callback function (registered in start_receiving in
     *        the ReceiveConfig struct), and optionally respond to the sender if
     *        the registered callback returns data.
     * @param buffer_size [description]
     * @param m [description]
     * @param cv [description]
     */
    void server_task_function(size_t buffer_size, std::mutex& m, std::condition_variable& cv) {
      // receive data
      std::vector<uint8_t> received_data;
      Socket::Info sender_info;
      if (!receive(buffer_size, received_data, sender_info)) {
        // if we failed to receive, then likely we should delay a little bit
        using namespace std::chrono_literals;
        std::unique_lock<std::mutex> lk(m);
        cv.wait_for(lk, 1ms);
        return;
      }
      if (!server_receive_callback_) {
        logger_.error("Server receive callback is invalid");
        return;
      }
      // callback
      auto maybe_response = server_receive_callback_(received_data, sender_info);
      // send if callback returned data
      if (!maybe_response.has_value()) {
        return;
      }
      auto response = maybe_response.value();
      // sendto
      logger_.info("Server responding to {} with message of length {}", sender_info.to_string(), response.size());
      auto sender_address = sender_info.ipv4_ptr();
      int num_bytes_sent = sendto(socket_, response.data(), response.size(), 0,
                                  (struct sockaddr*)sender_address, sizeof(*sender_address));
      if (num_bytes_sent < 0) {
        logger_.error("Error occurred responding: {} - '{}'", errno, strerror(errno));
      }
      logger_.info("Server responded with {} bytes", num_bytes_sent);
    }

    static constexpr int address_family_{AF_INET};
    static constexpr int ip_protocol_{IPPROTO_IP};

    int socket_;
    std::unique_ptr<Task> task_;
    receive_callback_fn server_receive_callback_;
    Logger logger_;
  };
}