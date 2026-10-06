#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Version 1: opaque handles, fixed-width scalars, UTF-8 bytes; no C++ ABI exposed.
   Handle lifetime is the owning runtime context. Handles must not cross contexts. */
typedef uint64_t JMHandle;
enum JMRuntimeType {
    JM_RT_ANY = 0,
    JM_RT_INT = 1,
    JM_RT_FLOAT = 2,
    JM_RT_BOOL = 3,
    JM_RT_STRING = 4,
    JM_RT_VOID = 5,
    JM_RT_LIST = 6
};
enum JMRuntimeOperation {
    JM_RT_CONCAT = 1,
    JM_RT_EQUAL,
    JM_RT_COMPARE,
    JM_RT_LENGTH,
    JM_RT_INDEX,
    JM_RT_SUBSTRING,
    JM_RT_CONTAINS,
    JM_RT_STARTS_WITH,
    JM_RT_ENDS_WITH,
    JM_RT_FIND,
    JM_RT_REPLACE,
    JM_RT_SPLIT,
    JM_RT_TRIM,
    JM_RT_UPPER,
    JM_RT_LOWER,
    JM_RT_LIST_CREATE,
    JM_RT_LIST_GET,
    JM_RT_LIST_SET,
    JM_RT_LIST_PUSH,
    JM_RT_LIST_POP,
    JM_RT_LIST_CLEAR,
    JM_RT_LIST_REVERSE,
    JM_RT_LIST_SORT,
    JM_RT_LIST_CONTAINS,
    JM_RT_LIST_INDEX_OF,
    JM_RT_LIST_INSERT,
    JM_RT_LIST_REMOVE_AT,
    JM_RT_TO_STRING,
    JM_RT_PARSE_INT,
    JM_RT_PARSE_FLOAT,
    JM_RT_PRINT,
    JM_RT_PRINTLN,
    JM_RT_LIST_CLONE,
    JM_RT_CODEPOINT_LENGTH,
    JM_RT_TRUNC,
    JM_RT_TAN,
    JM_RT_ASIN,
    JM_RT_ACOS,
    JM_RT_ATAN,
    JM_RT_ATAN2,
    JM_RT_LOG,
    JM_RT_LOG10,
    JM_RT_EXP,
    JM_RT_LERP,
    JM_RT_DEG_TO_RAD,
    JM_RT_RAD_TO_DEG,
    JM_RT_SIGN,
    JM_RT_FRACT,
    JM_RT_SMOOTHSTEP,
    JM_RT_SEED,
    JM_RT_RANDOM,
    JM_RT_RANDOM_INT,
    JM_RT_RANDOM_FLOAT,
    JM_RT_TIME_NOW,
    JM_RT_TIME_ELAPSED,
    JM_RT_LIST_EQUAL
};
void *jm_runtime_create_context(void);
void jm_runtime_destroy_context(void *context);
void *jm_runtime_activate(void *context);
void jm_runtime_root(uint64_t slot, JMHandle value);
void jm_runtime_retain(JMHandle value);
void jm_runtime_release(JMHandle value);
void jm_runtime_collect(void);
uint64_t jm_runtime_live_objects(void);
void jm_runtime_set_aot(uint64_t enabled);
JMHandle jm_string_create(const char *bytes, uint64_t length);
uint64_t jm_runtime_call(uint64_t operation, uint64_t a, uint64_t b, uint64_t c);
uint64_t jm_list_element_type(JMHandle list);
const char *jm_string_bytes(JMHandle string, uint64_t *length);
#ifdef __cplusplus
}
#endif
