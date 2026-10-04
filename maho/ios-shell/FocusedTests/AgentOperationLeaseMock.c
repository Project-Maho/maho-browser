#include "AgentOperationLeaseMock.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct MahoAgentSession {
  uint64_t magic;
  uint32_t generation;
  bool freed;
  void (*artifact_cb)(void *, const char *);
  void *artifact_user_data;
  void (*session_release_cb)(void *);
  void *session_release_user_data;
  void (*turn_release_cb)(void *);
  void *turn_release_user_data;
  char artifact_root[4096];
  char artifact_path[4096];
} MahoAgentSession;
typedef struct MahoCore { int marker; } MahoCore;
typedef int32_t (*PermissionCb)(void *, const char *, const char *);
typedef MahoAgentSecureKey (*StorageCb)(void *, const char *);
typedef MahoAgentToolResult (*BrowserCb)(void *, const char *, const char *);
typedef void (*ArtifactCb)(void *, const char *);
typedef void (*ReleaseCb)(void *);

static const uint64_t LIVE_MAGIC = UINT64_C(0x4d41484f4c495645);
static const uint64_t POISON_MAGIC = UINT64_C(0xdeadf00dbaadbeef);
static MahoCore core;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condition = PTHREAD_COND_INITIALIZER;
static bool pause_next_operation;
static bool operation_paused;
static bool resume_operation;
static MahoAgentSession *active_operation_session;
static bool block_next_native_free;
static bool native_free_blocked;
static bool resume_native_free;
static uint32_t native_free_count;
static uint32_t free_before_operation_release_count;
static uint32_t operation_poison_observation_count;
static uint32_t distinct_session_count;
static uint32_t next_generation;
static MahoAgentSession *latest_session;

static const char *artifact_json =
    "{\"artifact_id\":\"artifact-23\",\"session_id\":\"session-23\","
    "\"display_name\":\"report.txt\",\"mime_type\":\"text/plain\","
    "\"size_bytes\":21,\"storage_rel_path\":\"report.txt\",\"created_at_ms\":42}";

static bool wait_for_predicate(bool (*predicate)(void *), void *data, uint32_t timeout_ms) {
  struct timespec deadline;
  clock_gettime(CLOCK_REALTIME, &deadline);
  deadline.tv_sec += timeout_ms / 1000;
  deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
  if (deadline.tv_nsec >= 1000000000L) {
    deadline.tv_sec += 1;
    deadline.tv_nsec -= 1000000000L;
  }
  pthread_mutex_lock(&lock);
  while (!predicate(data)) {
    if (pthread_cond_timedwait(&condition, &lock, &deadline) != 0) {
      pthread_mutex_unlock(&lock);
      return false;
    }
  }
  pthread_mutex_unlock(&lock);
  return true;
}

static bool paused_predicate(void *unused) {
  (void)unused;
  return operation_paused;
}

static bool free_blocked_predicate(void *unused) {
  (void)unused;
  return native_free_blocked;
}

static bool free_count_predicate(void *data) {
  return native_free_count >= *(uint32_t *)data;
}

static bool validate_session_locked(MahoAgentSession *session) {
  bool valid = session && session->magic == LIVE_MAGIC && !session->freed;
  if (!valid) operation_poison_observation_count += 1;
  return valid;
}

static bool run_operation_pause(MahoAgentSession *session) {
  pthread_mutex_lock(&lock);
  bool valid = validate_session_locked(session);
  active_operation_session = session;
  if (pause_next_operation) {
    pause_next_operation = false;
    operation_paused = true;
    pthread_cond_broadcast(&condition);
    while (!resume_operation) pthread_cond_wait(&condition, &lock);
    resume_operation = false;
    operation_paused = false;
  }
  valid = validate_session_locked(session) && valid;
  active_operation_session = NULL;
  pthread_cond_broadcast(&condition);
  pthread_mutex_unlock(&lock);
  return valid;
}

void *mock_core(void) { return &core; }

void mock_pause_next_operation(void) {
  pthread_mutex_lock(&lock);
  pause_next_operation = true;
  pthread_mutex_unlock(&lock);
}

bool mock_wait_until_operation_paused(uint32_t timeout_ms) {
  return wait_for_predicate(paused_predicate, NULL, timeout_ms);
}

void mock_resume_operation(void) {
  pthread_mutex_lock(&lock);
  resume_operation = true;
  pthread_cond_broadcast(&condition);
  pthread_mutex_unlock(&lock);
}

void mock_block_next_native_free(void) {
  pthread_mutex_lock(&lock);
  block_next_native_free = true;
  pthread_mutex_unlock(&lock);
}

bool mock_wait_until_native_free_blocked(uint32_t timeout_ms) {
  return wait_for_predicate(free_blocked_predicate, NULL, timeout_ms);
}

void mock_resume_native_free(void) {
  pthread_mutex_lock(&lock);
  resume_native_free = true;
  pthread_cond_broadcast(&condition);
  pthread_mutex_unlock(&lock);
}

bool mock_wait_for_native_free_count(uint32_t count, uint32_t timeout_ms) {
  return wait_for_predicate(free_count_predicate, &count, timeout_ms);
}

uint32_t mock_native_free_count(void) {
  pthread_mutex_lock(&lock);
  uint32_t result = native_free_count;
  pthread_mutex_unlock(&lock);
  return result;
}

uint32_t mock_free_before_operation_release_count(void) {
  pthread_mutex_lock(&lock);
  uint32_t result = free_before_operation_release_count;
  pthread_mutex_unlock(&lock);
  return result;
}

uint32_t mock_operation_poison_observation_count(void) {
  pthread_mutex_lock(&lock);
  uint32_t result = operation_poison_observation_count;
  pthread_mutex_unlock(&lock);
  return result;
}

uint32_t mock_distinct_session_count(void) {
  pthread_mutex_lock(&lock);
  uint32_t result = distinct_session_count;
  pthread_mutex_unlock(&lock);
  return result;
}

MahoAgentSession *maho_agent_create_session_leased(
    MahoCore *ignored_core, const char *session_id, const char *workspace_root,
    bool insecure, PermissionCb permission_cb, void *permission_data,
    StorageCb storage_cb, void *storage_data, BrowserCb browser_cb,
    void *browser_data, const char *space_id, ReleaseCb release_cb,
    void *release_data) {
  (void)ignored_core; (void)session_id; (void)workspace_root; (void)insecure;
  (void)permission_cb; (void)permission_data; (void)storage_cb; (void)storage_data;
  (void)browser_cb; (void)browser_data; (void)space_id;
  MahoAgentSession *session = calloc(1, sizeof(*session));
  if (!session) return NULL;
  session->magic = LIVE_MAGIC;
  snprintf(session->artifact_path, sizeof(session->artifact_path), "report.txt");
  pthread_mutex_lock(&lock);
  session->generation = ++next_generation;
  session->session_release_cb = release_cb;
  session->session_release_user_data = release_data;
  latest_session = session;
  distinct_session_count += 1;
  pthread_mutex_unlock(&lock);
  return session;
}

void maho_agent_session_free(MahoAgentSession *session) {
  ReleaseCb release_cb = NULL;
  void *release_data = NULL;
  pthread_mutex_lock(&lock);
  if (active_operation_session == session) free_before_operation_release_count += 1;
  if (block_next_native_free) {
    block_next_native_free = false;
    native_free_blocked = true;
    pthread_cond_broadcast(&condition);
    while (!resume_native_free) pthread_cond_wait(&condition, &lock);
    resume_native_free = false;
    native_free_blocked = false;
  }
  if (!session || session->magic != LIVE_MAGIC || session->freed) {
    operation_poison_observation_count += 1;
    pthread_mutex_unlock(&lock);
    return;
  }
  session->freed = true;
  session->magic = POISON_MAGIC;
  native_free_count += 1;
  release_cb = session->session_release_cb;
  release_data = session->session_release_user_data;
  session->session_release_cb = NULL;
  session->session_release_user_data = NULL;
  pthread_cond_broadcast(&condition);
  pthread_mutex_unlock(&lock);
  if (release_cb) release_cb(release_data);
}

bool maho_agent_send_message_leased(
    MahoAgentSession *session, const char *message, void *token_cb,
    void *thinking_cb, void *tool_call_cb, void *tool_result_cb,
    void *complete_cb, void *error_cb, void *callback_data,
    ReleaseCb release_cb, void *release_data) {
  (void)message; (void)token_cb; (void)thinking_cb; (void)tool_call_cb;
  (void)tool_result_cb; (void)complete_cb; (void)error_cb; (void)callback_data;
  if (!run_operation_pause(session)) return false;
  pthread_mutex_lock(&lock);
  session->turn_release_cb = release_cb;
  session->turn_release_user_data = release_data;
  pthread_mutex_unlock(&lock);
  return true;
}

bool maho_agent_cancel(MahoAgentSession *session) {
  return run_operation_pause(session);
}

char *maho_agent_list_tools(MahoAgentSession *session) {
  return run_operation_pause(session) ? strdup("[]") : NULL;
}

void maho_agent_set_artifact_root(MahoAgentSession *session, const char *path) {
  pthread_mutex_lock(&lock);
  if (validate_session_locked(session)) {
    snprintf(session->artifact_root, sizeof(session->artifact_root), "%s", path ? path : "");
    mkdir(session->artifact_root, 0700);
  }
  pthread_mutex_unlock(&lock);
}

void maho_agent_set_artifact_created_callback(
    MahoAgentSession *session, ArtifactCb cb, void *user_data) {
  pthread_mutex_lock(&lock);
  if (validate_session_locked(session)) {
    session->artifact_cb = cb;
    session->artifact_user_data = user_data;
  }
  pthread_mutex_unlock(&lock);
}

char *maho_agent_list_artifacts(MahoAgentSession *session) {
  if (!run_operation_pause(session)) return NULL;
  return strdup("[{\"artifact_id\":\"artifact-23\",\"session_id\":\"session-23\",\"display_name\":\"report.txt\",\"mime_type\":\"text/plain\",\"size_bytes\":21,\"storage_rel_path\":\"report.txt\",\"created_at_ms\":42}]");
}

char *maho_agent_artifact_path(MahoAgentSession *session, const char *artifact_id) {
  if (!run_operation_pause(session)) return NULL;
  return artifact_id && strcmp(artifact_id, "artifact-23") == 0
      ? strdup(session->artifact_path) : NULL;
}

void maho_string_free(char *ptr) { free(ptr); }

void mock_emit_artifact(void) {
  ArtifactCb callback = NULL;
  void *user_data = NULL;
  pthread_mutex_lock(&lock);
  if (latest_session && validate_session_locked(latest_session)) {
    callback = latest_session->artifact_cb;
    user_data = latest_session->artifact_user_data;
  }
  pthread_mutex_unlock(&lock);
  if (callback) callback(user_data, artifact_json);
}

void mock_release_turn(void) {
  ReleaseCb release_cb = NULL;
  void *release_data = NULL;
  pthread_mutex_lock(&lock);
  if (latest_session) {
    release_cb = latest_session->turn_release_cb;
    release_data = latest_session->turn_release_user_data;
    latest_session->turn_release_cb = NULL;
    latest_session->turn_release_user_data = NULL;
  }
  pthread_mutex_unlock(&lock);
  if (release_cb) release_cb(release_data);
}

void mock_set_artifact_path(const char *path) {
  pthread_mutex_lock(&lock);
  if (latest_session && validate_session_locked(latest_session)) {
    snprintf(latest_session->artifact_path, sizeof(latest_session->artifact_path), "%s", path ? path : "");
  }
  pthread_mutex_unlock(&lock);
}

const char *mock_artifact_root(void) {
  pthread_mutex_lock(&lock);
  const char *result = latest_session ? latest_session->artifact_root : "";
  pthread_mutex_unlock(&lock);
  return result;
}

bool mock_write_at(const char *relative, const char *contents) {
  char root[4096];
  pthread_mutex_lock(&lock);
  if (!latest_session || !validate_session_locked(latest_session)) {
    pthread_mutex_unlock(&lock);
    return false;
  }
  snprintf(root, sizeof(root), "%s", latest_session->artifact_root);
  pthread_mutex_unlock(&lock);
  char path[8192];
  snprintf(path, sizeof(path), "%s/%s", root, relative);
  FILE *file = fopen(path, "wb");
  if (!file) return false;
  size_t length = strlen(contents);
  bool ok = fwrite(contents, 1, length, file) == length;
  return fclose(file) == 0 && ok;
}
