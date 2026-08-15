#include <stdbool.h>
#include <stdint.h>

typedef struct MahoAgentSecureKey {
  char *ptr;
  uintptr_t len;
  void (*free_fn)(char *, uintptr_t);
  char *base_url;
  char *model;
  void (*cstring_free_fn)(char *);
} MahoAgentSecureKey;

typedef struct MahoAgentToolResult {
  char *json_ptr;
  char *error_ptr;
  void (*free_fn)(char *);
} MahoAgentToolResult;

void maho_string_free(char *ptr);
void *mock_core(void);
void mock_pause_next_operation(void);
bool mock_wait_until_operation_paused(uint32_t timeout_ms);
void mock_resume_operation(void);
void mock_block_next_native_free(void);
bool mock_wait_until_native_free_blocked(uint32_t timeout_ms);
void mock_resume_native_free(void);
bool mock_wait_for_native_free_count(uint32_t count, uint32_t timeout_ms);
uint32_t mock_native_free_count(void);
uint32_t mock_free_before_operation_release_count(void);
uint32_t mock_operation_poison_observation_count(void);
uint32_t mock_distinct_session_count(void);
void mock_emit_artifact(void);
void mock_release_turn(void);
void mock_set_artifact_path(const char *path);
const char *mock_artifact_root(void);
bool mock_write_at(const char *relative, const char *contents);
