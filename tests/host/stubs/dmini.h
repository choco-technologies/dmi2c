#ifndef TEST_DMINI_H
#define TEST_DMINI_H
typedef struct test_ini *dmini_context_t;
int dmini_section_count(dmini_context_t ini);
const char *dmini_section_name(dmini_context_t ini, int index);
int dmini_has_key(dmini_context_t ini, const char *section, const char *key);
const char *dmini_get_string(dmini_context_t ini, const char *section, const char *key, const char *fallback);
#endif
