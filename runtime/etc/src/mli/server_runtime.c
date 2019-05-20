/*
 * Copyright 2004-2019 Cray Inc.
 * Other additional copyright holders may be indicated within.
 *
 * The entirety of this work is licensed under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.
 *
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

#ifndef CHPL_RUNTIME_ETC_SRC_MLI_SERVER_RUNTIME_C_
#define CHPL_RUNTIME_ETC_SRC_MLI_SERVER_RUNTIME_C_

#include "mli/common_code.c"
#include <time.h>

//
// The definition of this is generated by the compiler.
//
int64_t chpl_mli_sdispatch(int64_t id);

//
// Contains sockets.
//
struct chpl_mli_context chpl_server;

void chpl_mli_server_init(struct chpl_mli_context* server) {
  if (server->context) { return; }

  server->context = zmq_ctx_new();
  server->setup_sock = zmq_socket(server->context, ZMQ_PUSH);
  server->main    = zmq_socket(server->context, ZMQ_REP);
  server->arg     = zmq_socket(server->context, ZMQ_REP);
  server->res     = zmq_socket(server->context, ZMQ_REQ);

  return;
}

void chpl_mli_server_deinit(struct chpl_mli_context* server) {
  if (!server->context) { return; }

  // TODO: Set socket LINGER to 0 to prevent blocking.
  zmq_ctx_destroy(server->context);
  server->context = NULL;

  return;
}

static
void chpl_mli_push_connection(char* connection) {
  int len = strlen(connection) + 1;
  chpl_mli_debugf("Pushing expected size %d\n", len);
  chpl_mli_push(chpl_server.setup_sock, &len, sizeof(len), 0);
  chpl_mli_debugf("Pushing string itself: %s\n", connection);
  chpl_mli_push(chpl_server.setup_sock, (void*)connection, len, 0);
}

void chpl_mli_terminate(enum chpl_mli_errors e) {
  const char* errstr = chpl_mli_errstr(e);
  chpl_mli_debugf("Terminated abruptly with error: %s\n", errstr);
  mli_terminate();
}

void chpl_mli_smain(char* setup_conn) {
  int64_t id = -1;
  int64_t ack = 0;
  int execute = 1;
  int err = 0;
  clock_t before = clock();
  double seconds = 0;

  const char* ip = "localhost";

  chpl_mli_server_init(&chpl_server);

  chpl_mli_debugf("%s\n", "Starting server for multi-locale library!");

  chpl_mli_connect(chpl_server.setup_sock, setup_conn);

  chpl_mli_bind(chpl_server.main);
  char* main_conn = chpl_mli_connection_info(chpl_server.main);
  chpl_mli_debugf("Main port on: %s\n", main_conn);

  chpl_mli_bind(chpl_server.arg);
  char* arg_conn = chpl_mli_connection_info(chpl_server.arg);
  chpl_mli_debugf("Arg port on: %s\n", arg_conn);

  chpl_mli_bind(chpl_server.res);
  char* res_conn = chpl_mli_connection_info(chpl_server.res);
  chpl_mli_debugf("Res port on: %s\n", res_conn);

  // Send main, arg, res connection info to the client
  chpl_mli_debugf("%s\n", "Sending connection information to the client");
  chpl_mli_push_connection(main_conn);
  chpl_mli_push_connection(arg_conn);
  chpl_mli_push_connection(res_conn);

  chpl_mli_debugf("%s\n", "Clean up obtained connection strings");
  mli_free(main_conn);
  mli_free(arg_conn);
  mli_free(res_conn);

  while (execute) {

    chpl_mli_debugf("%s\n", "Listening...");
    
    // Every transaction starts by reading an int64 off the wire.
    err = chpl_mli_pull(
              chpl_server.main,
              &id,
              sizeof(id),
              0);

    // TODO: Handle socket errors on inbound read.
    if (err < 0) {
      chpl_mli_debugf("Socket error on read: %d\n", err);
      ack = CHPL_MLI_CODE_ESOCKET;
    }

    if (id < 0) {
      chpl_mli_debugf("Client sent code: %s\n", chpl_mli_errstr(id));
      ack = CHPL_MLI_CODE_SHUTDOWN;
      execute = 0;
    } else {
      chpl_mli_debugf("Received request for ID: %lld\n", id);
      ack = CHPL_MLI_CODE_NONE;
    }
 
    chpl_mli_debugf("Responding with code: %s\n", chpl_mli_errstr(0));
    err = chpl_mli_push(chpl_server.main, &ack, sizeof(ack), 0);

    if (err < 0) { chpl_mli_debugf("Socket error on write: %d\n", err); }

    // TODO: This ack value is currently just overwritten...
    if (execute && id > 0) { ack = chpl_mli_sdispatch(id); }
  }

  chpl_mli_debugf("Shutdown, code: %s\n", chpl_mli_errstr(id));

  seconds = (double) (clock() - before) / (double) CLOCKS_PER_SEC;

  chpl_mli_close(chpl_server.setup_sock);
  chpl_mli_close(chpl_server.main);
  chpl_mli_close(chpl_server.arg);
  chpl_mli_close(chpl_server.res);

  chpl_mli_server_deinit(&chpl_server);

  chpl_mli_debugf("Total time elapsed: %gs\n", seconds);

  return;
}

#endif
