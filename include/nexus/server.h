#ifndef __nexus_server_h__
#define __nexus_server_h__ 1
/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <proton/engine.h>

typedef struct nx_listener_t nx_listener_t;
typedef struct nx_connector_t nx_connector_t;
typedef struct nx_connection_t nx_connection_t;
typedef struct nx_user_fd_t nx_user_fd_t;

/**
 * Event type for the connection callback.
 *
 * @enum NX_CONN_EVENT_LISTENER_OPEN The connection just opened via a listener (inbound).
 * @enum NX_CONN_EVENT_CONNECTOR_OPEN The connection just opened via a connector (outbound).
 * @enum NX_CONN_EVENT_CLOSE The connection was closed at the transport level (not cleanly).
 * @enum NX_CONN_EVENT_PROCESS The connection requires processing.
 */
typedef enum {
    NX_CONN_EVENT_LISTENER_OPEN,
    NX_CONN_EVENT_CONNECTOR_OPEN,
    NX_CONN_EVENT_CLOSE,
    NX_CONN_EVENT_PROCESS
} nx_conn_event_t;


/**
 * Thread Start Handler
 *
 * Callback invoked when a new server thread is started.  The callback is
 * invoked on the newly created thread.
 *
 * This handler can be used to set processor affinity or other thread-specific
 * tuning values.
 *
 * @param context The handler context supplied in nx_server_initialize.
 * @param thread_id The integer thread identifier that uniquely identifies this thread.
 */
typedef void (*nx_thread_start_cb_t)(void* context, int thread_id);


/**
 * Signal Handler
 *
 * Callback for caught signals.  This handler will only be invoked for signal numbers
 * that were registered via nx_server_signal.  The handler is not invoked in the context
 * of the OS signal handler.  Rather, it is invoked on one of the worker threads in an
 * orderly sequence.
 *
 * @param context The handler context supplied in nx_server_initialize.
 * @param signum The signal number that was raised.
 */
typedef void (*nx_signal_handler_cb_t)(void* context, int signum);


/**
 * Connection Handler
 *
 * Callback invoked when processing is needed on a proton connection.  This callback
 * shall be invoked on one of the server's worker threads.  The server guarantees that
 * no two threads shall be allowed to process a single connection concurrently.
 * The implementation of this handler may assume that it has exclusive access to the
 * connection and it's subservient components.
 *
 * @param context The handler context supplied in nx_server_{connect,listen}.
 * @param event The event/reason for the invocation of the handler.
 * @param conn The connection that requires processing by the handler.
 * @return A value greater than zero if the handler did any proton processing for
 *         the connection.  If no work was done, zero is returned.
 */
typedef int (*nx_conn_handler_cb_t)(void* context, nx_conn_event_t event, nx_connection_t *conn);


/**
 * User_fd Handler
 *
 * Callback invoked when a user-managed file descriptor is available for reading or writing or there
 * was an error on the file descriptor.
 *
 * @param context The handler context supplied in the nx_user_fd call.
 * @param ufd The user_fd handle for the processable fd.
 */
typedef void (*nx_user_fd_handler_cb_t)(void* context, nx_user_fd_t *ufd);


/**
 * Server Initializer
 *
 * Initialize the server module and prepare it for operation.
 *
 * @param thread_count The number of worker threads (1 or more) that the server shall create
 */
void nx_server_initialize(int thread_count);


/**
 * Server Finalizer
 *
 * Finalize the server after it has stopped running.
 */
void nx_server_finalize(void);


/**
 * Set the connection handler callback for the server.  This callback is mandatory and must be set
 * prior to the invocation of nx_server_run.
 *
 * @param conn_hander The handler for processing connection-related events.
 */
void nx_server_set_conn_handler(nx_conn_handler_cb_t conn_handler);

/**
 * Set the user-fd handler callback for the server.  This handler is optional, but must be supplied
 * if the nx_server is used to manage the activation of user file descriptors.
 */
void nx_server_set_user_fd_handler(nx_user_fd_handler_cb_t ufd_handler);

/**
 * Set the signal handler for the server.  The signal handler is invoked cleanly on a worker thread
 * after the server process catches an operating-system signal.  The signal handler is optional and
 * need not be set.
 *
 * @param signal_handler The signal handler called when a registered signal is caught.
 * @param context Opaque context to be passed back in the callback function.
 */
void nx_server_set_signal_handler(nx_signal_handler_cb_t signal_handler, void *context);


/**
 * Set the thread-start handler.  This handler is called once on each worker thread at the time
 * the thread is started.  This may be used to set tuning settings like processor affinity, etc.
 *
 * @param start_handler The thread-start handler invoked per thread on thread startup.
 * @param context Opaque context to be passed back in the callback function.
 */
void nx_server_set_start_handler(nx_thread_start_cb_t start_handler, void *context);


/**
 * Server Run Entry Point
 *
 * Start the operation of the server, including launching all of the worker threads.
 * This function does not return until after the server has been stopped.  The thread
 * that calls nx_server_run is used as one of the worker threads.
 */
void nx_server_run(void);


/**
 * Stop the server
 *
 * Stop the server and join all of its worker threads.  This function may be called from any
 * thread.
 */
void nx_server_stop(void);


/**
 * Register a signal to be caught and handled by the signal handler.
 *
 * @param signum The signal number of a signal to be handled by the application.
 */
void nx_server_signal(int signum);


/**
 * Pause (quiesce) the server.  This call blocks until all of the worker threads (except
 * the one calling the this function) are finished processing and have been blocked.  When
 * this call returns, the calling thread is the only thread running in the process.
 */
void nx_server_pause(void);


/**
 * Resume normal operation of a paused server.  This call unblocks all of the worker threads
 * so they can resume normal connection processing.
 */
void nx_server_resume(void);


/**
 * Activate a connection for output.
 *
 * This function is used to request that the server activate the indicated connection.
 * It is assumed that the connection is one that the caller does not have permission to
 * access (i.e. it may be owned by another thread currently).  An activated connection
 * will, when writable, appear in the work list and be invoked for processing by a worker
 * thread.
 *
 * @param conn The connection over which the application wishes to send data
 */
void nx_server_activate(nx_connection_t *conn);


/**
 * Set the user context for a connection.
 *
 * @param conn Connection object supplied in NX_CONN_EVENT_{LISTENER,CONNETOR}_OPEN
 * @param context User context to be stored with the connection.
 */
void nx_connection_set_context(nx_connection_t *conn, void *context);


/**
 * Get the user context from a connection.
 *
 * @param conn Connection object supplied in NX_CONN_EVENT_{LISTENER,CONNETOR}_OPEN
 * @return The user context stored with the connection.
 */
void *nx_connection_get_context(nx_connection_t *conn);


/**
 * Get the proton-engine connection object.
 *
 * @param conn Connection object supplied in NX_CONN_EVENT_{LISTENER,CONNETOR}_OPEN
 * @return The proton connection object.
 */
pn_connection_t *nx_connection_pn(nx_connection_t *conn);


/**
 * Configuration block for a connector or a listener.
 */
typedef struct nx_server_config_t {
    /**
     * Host name or network address to bind to a listener or use in the connector.
     */
    char *host;

    /**
     * Port name or number to bind to a listener or use in the connector.
     */
    char *port;

    /**
     * Space-separated list of SASL mechanisms to be accepted for the connection.
     */
    char *sasl_mechanisms;

    /**
     * If appropriate for the mechanism, the username for authentication
     * (connector only)
     */
    char *sasl_username;

    /**
     * If appropriate for the mechanism, the password for authentication
     * (connector only)
     */
    char *sasl_password;

    /**
     * If appropriate for the mechanism, the minimum acceptable security strength factor
     */
    int sasl_minssf;

    /**
     * If appropriate for the mechanism, the maximum acceptable security strength factor
     */
    int sasl_maxssf;

    /**
     * SSL is enabled for this connection iff non-zero.
     */
    int ssl_enabled;

    /**
     * Connection will take on the role of SSL server iff non-zero.
     */
    int ssl_server;

    /**
     * Iff non-zero AND ssl_enabled is non-zero, this listener will detect the client's use
     * of SSL or non-SSL and conform to the client's protocol.
     * (listener only)
     */
    int ssl_allow_unsecured_client;

    /**
     * Path to the file containing the PEM-formatted public certificate for the local end
     * of the connection.
     */
    char *ssl_certificate_file;

    /**
     * Path to the file containing the PEM-formatted private key for the local end of the
     * connection.
     */
    char *ssl_private_key_file;

    /**
     * The password used to sign the private key, or NULL if the key is not protected.
     */
    char *ssl_password;

    /**
     * Path to the file containing the PEM-formatted set of certificates of trusted CAs.
     */
    char *ssl_trusted_certificate_db;

    /**
     * Iff non-zero, require that the peer's certificate be supplied and that it be authentic
     * according to the set of trusted CAs.
     */
    int ssl_require_peer_authentication;

    /**
     * Allow the connection to be redirected by the peer (via CLOSE->Redirect).  This is
     * meaningful for outgoing (connector) connections only.
     */
    int allow_redirect;
} nx_server_config_t;


/**
 * Create a listener for incoming connections.
 *
 * @param config Pointer to a configuration block for this listener.  This block will be
 *               referenced by the server, not copied.  The referenced record must remain
 *               in-scope for the life of the listener.
 * @param context User context passed back in the connection handler.
 * @return A pointer to the new listener, or NULL in case of failure.
 */
nx_listener_t *nx_server_listen(const nx_server_config_t *config, void *context);

/**
 * Free the resources associated with a listener.
 *
 * @param li A listener pointer returned by nx_listen.
 */
void nx_listener_free(nx_listener_t* li);

/**
 * Close a listener so it will accept no more connections.
 *
 * @param li A listener pointer returned by nx_listen.
 */
void nx_listener_close(nx_listener_t* li);

/**
 * Create a connector for an outgoing connection.
 *
 * @param config Pointer to a configuration block for this connector.  This block will be
 *               referenced by the server, not copied.  The referenced record must remain
 *               in-scope for the life of the connector..
 * @param context User context passed back in the connection handler.
 * @return A pointer to the new connector, or NULL in case of failure.
 */
nx_connector_t *nx_server_connect(const nx_server_config_t *config, void *context);

/**
 * Free the resources associated with a connector.
 *
 * @param ct A connector pointer returned by nx_connect.
 */
void nx_connector_free(nx_connector_t* ct);

/**
 * Create a tracker for a user-managed file descriptor.
 *
 * A user-fd is appropriate for use when the application opens and manages file descriptors
 * for purposes other than AMQP communication.  Registering a user fd with the nexus server
 * controls processing of the FD alongside the FDs used for messaging.
 *
 * @param fd The open file descriptor being managed by the application.
 * @param context User context passed back in the connection handler.
 * @return A pointer to the new user_fd.
 */
nx_user_fd_t *nx_user_fd(int fd, void *context);

/**
 * Free the resources for a user-managed FD tracker.
 *
 * @param ufd Structure pointer returned by nx_user_fd.
 */
void nx_user_fd_free(nx_user_fd_t *ufd);

/**
 * Activate a user-fd for read.
 *
 * Use this activation when the application has capacity to receive data from the user-fd.  This will
 * cause the callback set in nx_server_set_user_fd_handler to later be invoked when the
 * file descriptor has data to read.
 *
 * @param ufd Structure pointer returned by nx_user_fd.
 */
void nx_user_fd_activate_read(nx_user_fd_t *ufd);

/**
 * Activate a user-fd for write.
 *
 * Use this activation when the application has data to write via the user-fd.  This will
 * cause the callback set in nx_server_set_user_fd_handler to later be invoked when the
 * file descriptor is writable.
 *
 * @param ufd Structure pointer returned by nx_user_fd.
 */
void nx_user_fd_activate_write(nx_user_fd_t *ufd);

/**
 * Check readable status of a user-fd
 *
 * Note: It is possible that readable status is spurious (i.e. this function returns true
 *       but the file-descriptor is not readable and will block if not set to O_NONBLOCK).
 *       Code accordingly.
 *
 * @param ufd Structure pointer returned by nx_user_fd.
 * @return true iff the user file descriptor is readable.
 */
bool nx_user_fd_is_readable(nx_user_fd_t *ufd);

/**
 * Check writable status of a user-fd
 *
 * @param ufd Structure pointer returned by nx_user_fd.
 * @return true iff the user file descriptor is writable.
 */
bool nx_user_fd_is_writeable(nx_user_fd_t *ufd);

#endif
