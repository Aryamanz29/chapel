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

#ifndef CHPL_RUNTIME_ETC_SRC_MLI_CLIENT_RUNTIME_C_
#define CHPL_RUNTIME_ETC_SRC_MLI_CLIENT_RUNTIME_C_

#include "mli/common_code.c"
#include <sys/types.h>
#include <unistd.h>

// We'll declare this rather than include "chpllaunch.h" directly.
int chpl_launcher_main(int argc, char* argv[]);

struct chpl_mli_context chpl_client;

static
void chpl_mli_client_init(struct chpl_mli_context* client) {
  if (client->context) { return; }

  client->context = zmq_ctx_new();
  client->setup_sock = zmq_socket(client->context, ZMQ_PULL);
  client->main    = zmq_socket(client->context, ZMQ_REQ);
  client->arg     = zmq_socket(client->context, ZMQ_REQ);
  client->res     = zmq_socket(client->context, ZMQ_REP);

  return;
}

void chpl_mli_client_deinit(struct chpl_mli_context* client) {
  if (!client->context) { return; }

  // TODO: Set linger on the sockets to zero to prevent blocking.
  zmq_ctx_destroy(client->context);
  client->context = NULL;

  return;
}

static
char * chpl_mli_pull_connection() {
  int len;
  chpl_mli_debugf("Getting %s\n", "expected size");
  chpl_mli_pull(chpl_client.setup_sock, &len, sizeof(len), 0);
  chpl_mli_debugf("Expected size is %d\n", len);
  char* conn = mli_malloc(len);
  chpl_mli_debugf("Getting %s\n", "string itself");
  chpl_mli_pull(chpl_client.setup_sock, (void*)conn, len, 0);
  conn[len] = '\0';
  chpl_mli_debugf("String itself is %s\n", conn);
  return conn;
}

void chpl_mli_terminate(enum chpl_mli_errors e) {
  const char* errstr = chpl_mli_errstr(e);
  chpl_mli_debugf("Terminated abruptly with error: %s\n", errstr);
  mli_terminate();
}

//
// Many of the launchers call `chpl_launch_using_exec`, so we make sure to
// fork before calling `chpl_launcher_main` to avoid overwriting the client
// process with the launcher's.
//
int chpl_mli_client_launch(int argc, char** argv) {
  pid_t pid = fork();

  if (pid) {
    // TODO: Should parent wait here, or in `chpl_library_finalize`?
    if (pid == -1) { return - 1; }
  } else {
    chpl_launcher_main(argc, argv);
  }

  return 0;
}

void chpl_library_init(int argc, char** argv) {
  static int initialized = 0;

  // Just hardcode these values for now.
  const char* ip = "localhost";

  if (initialized) { return; }
  initialized = 1;

  // Set up the clientside ZMQ sockets.
  chpl_mli_client_init(&chpl_client);

  chpl_mli_bind(chpl_client.setup_sock);

  char* setup_sock_conn = chpl_mli_connection_info(chpl_client.setup_sock);
  chpl_mli_debugf("Setup socket used: %s\n", setup_sock_conn);

  // Send the setup socket as the last argument when launching the server.
  int argc_plus_sock = argc + 2;
  char** argv_plus_sock = mli_malloc(argc_plus_sock * sizeof(char*));
  for (int i = 0; i < argc; i++) {
    chpl_mli_debugf("passing along arg %d: %s\n", i, argv[i]);
    argv_plus_sock[i] = argv[i];
  }
  argv_plus_sock[argc] = "--chpl-mli-socket-loc";
  argv_plus_sock[argc + 1] = setup_sock_conn;
  chpl_mli_debugf("spawning server with %d args\n", argc_plus_sock);
  chpl_mli_client_launch(argc_plus_sock, argv_plus_sock);
  chpl_mli_debugf("clean up extended %s\n", "argv");
  mli_free(argv_plus_sock);

  chpl_mli_debugf("cleaning up connection %s\n", "info");
  mli_free(setup_sock_conn);

  char* main_conn = chpl_mli_pull_connection();
  chpl_mli_debugf("connection info for main %s\n", main_conn);
  char* arg_conn = chpl_mli_pull_connection();
  chpl_mli_debugf("connection info for arg %s\n", arg_conn);
  char* res_conn = chpl_mli_pull_connection();
  chpl_mli_debugf("connection info for res %s\n", res_conn);

  chpl_mli_connect(chpl_client.main, main_conn);
  chpl_mli_connect(chpl_client.arg, arg_conn);
  chpl_mli_connect(chpl_client.res, res_conn);

  chpl_mli_debugf("clean up %s\n", "client port strings");
  mli_free(main_conn);
  mli_free(arg_conn);
  mli_free(res_conn);

  return;
}

void chpl_library_finalize(void) {
  static int finalized = 0;

  if (finalized) { return; }
  finalized = 1;

  {
    int64_t shutdown = CHPL_MLI_CODE_SHUTDOWN;
    chpl_mli_push(chpl_client.main, &shutdown, sizeof(shutdown), 0);
    chpl_mli_pull(chpl_client.main, &shutdown, sizeof(shutdown), 0);

    // Can server ever respond with a different error?
    if (shutdown != CHPL_MLI_CODE_SHUTDOWN) { ;;; }
  }

  // TODO: It would be a good idea to set LINGER to 0 as well.
  // TODO: Maybe move the close connections to deinit?
  chpl_mli_close(chpl_client.setup_sock);
  chpl_mli_close(chpl_client.main);
  chpl_mli_close(chpl_client.arg);
  chpl_mli_close(chpl_client.res);

  chpl_mli_client_deinit(&chpl_client);

  return;
}

#endif

