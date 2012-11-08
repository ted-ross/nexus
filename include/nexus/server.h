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
 * Connection Handler
 *
 * Callback invoked when processing is needed on a proton connection.  This callback
 * shall be invoked on one of the server's worker threads.  The server guarantees that
 * no two threads shall be allowed to process a single connection concurrently.
 * The implementation of this handler may assume that it has exclusive access to the
 * connection and it's subservient components.
 *
 * @param context The handler context supplied in nx_server_initialize.
 * @param event The event/reason for the invocation of the handler.
 * @param conn The connection that requires processing by the handler.
 * @return A value greater than zero if the handler did any proton processing for
 *         the connection.  If no work was done, zero is returned.
 */
typedef int (*nx_conn_handler_cb_t)(void* context, nx_conn_event_t event, pn_connection_t *conn);


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
 * Server Initializer
 *
 * Initialize the server module and prepare it for operation.
 *
 * @param thread_count The number of worker threads (1 or more) that the server shall create
 * @param conn_hander The handler for processing an operational connection
 * @param close_handler The handler for a connection who's transport has closed
 * @param start_handler The thread-start handler invoked per thread on thread startup
 * @param handler_context Opaque context to be passed back in the callback functions
 */
void nx_server_initialize(int                     thread_count,
                          nx_conn_handler_cb_t    conn_handler,
                          nx_signal_handler_cb_t  signal_handler,
                          nx_thread_start_cb_t    start_handler,
                          void                   *handler_context);


/**
 * Server Finalizer
 *
 * Finalize the server after it has stopped running.
 */
void nx_server_finalize(void);


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
 * This function is used to request that the server activate the connection associated
 * with the supplied link.  It is assumed that the link is associated with a connection
 * that the caller does not have permission to access (i.e. it may be owned by another
 * thread currently).  An activated connection will, when writable, appear in the work
 * list and be invoked for processing by a worker thread.
 *
 * @param link The link over which the application wishes to send data
 */
void nx_server_activate(pn_link_t *link);


typedef struct nx_server_listener_t nx_server_listener_t;
typedef struct nx_server_connector_t nx_server_connector_t;

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
} nx_server_config_t;


/**
 * Create a listener for incoming connections.
 *
 * @param config Pointer to a configuration block for this listener.  This block will be
 *               referenced by the server, not copied.  The referenced record must remain
 *               in-scope for the life of the listener.
 * @param context Opaque user context accessible from the connection context.
 * @return A pointer to the new listener, or NULL in case of failure.
 */
nx_server_listener_t *nx_server_listener(nx_server_config_t *config, void *context);

/**
 * Free the resources associated with a listener.
 *
 * @param li A listener pointer returned by nx_server_listener.
 */
void nx_server_listener_free(nx_server_listener_t* li);

/**
 * Close a listener so it will accept no more connections.
 *
 * @param li A listener pointer returned by nx_server_listener.
 */
void nx_server_listener_close(nx_server_listener_t* li);

/**
 * Create a connector for an outgoing connection.
 *
 * @param config Pointer to a configuration block for this connector.  This block will be
 *               referenced by the server, not copied.  The referenced record must remain
 *               in-scope for the life of the connector..
 * @param context Opaque user context accessible from the connection context.
 * @return A pointer to the new connector, or NULL in case of failure.
 */
nx_server_connector_t *nx_server_connector(nx_server_config_t *config, void *context);

/**
 * Free the resources associated with a connector.
 *
 * @param li A connector pointer returned by nx_server_connector.
 */
void nx_server_connector_free(nx_server_connector_t* ct);

/**
 * Close a connector.
 *
 * @param li A connector pointer returned by nx_server_connector.
 */
void nx_server_connector_close(nx_server_connector_t* ct);

#endif
