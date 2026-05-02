#ifndef INTERPRETER_H
#define INTERPRETER_H


#include <nob.h>

#define SMORTH_STACK_CAPACITY (1024*64)
#define SMORTH_DATA_CAPACITY (1024*64)

typedef enum TokenKind
{
    NUMBER,
    FWORD
}
TokenKind;

typedef struct
{
    TokenKind kind;
    union
    {
        int64_t number;
        String_View word;
    } as;
    
    String_View raw;
}
Token;

typedef struct
{
    Token *items;
    size_t count;
    size_t capacity;
} 
Tokens;


typedef struct
{
    bool imm;
    char *name;
    String_Builder source;
    void(*codeptr)(int64_t**);
}
Execution_Token;

typedef struct
{
    Execution_Token **items;
    size_t count;
    size_t capacity;
}
Word_Table;

typedef struct
{
    char *kind;
    size_t handle;
}
Control_Flow_Stack_Item;

typedef struct
{
    Word_Table word_table;

    int64_t stack[SMORTH_STACK_CAPACITY];
    int64_t *sp;
    int64_t *stack_base;
    int64_t *stack_limit;

    int64_t data[SMORTH_DATA_CAPACITY];
    int64_t *dp;

    const char *current_word;

    String_View ib;

    int64_t compiling;
    const char *word_name;
    String_Builder word_source;
    bool immediate;
    Control_Flow_Stack_Item cf_stack[1024];
    size_t cfi;
}
Program_State;


void interpret(Program_State *program_state);

Token next_token(String_View *source);
void smorth_stack_effect_guard(Program_State *ps, const char *word, int64_t pop_count, int64_t push_count);
int64_t smorth_stack_pop(Program_State *ps, const char *word);
void smorth_stack_push(Program_State *ps, int64_t value, const char *word);
void sb_insert_stack_effect_guard(String_Builder *sb, Program_State *ps, const char *word, int64_t pop_count, int64_t push_count);
void add_word(Program_State *ps, const char *name, String_Builder source);
void add_word_imm(Program_State *ps, const char *name, String_Builder source);
void add_word_effect(Program_State *ps, const char *name, String_Builder source, int64_t pop_count, int64_t push_count);
void add_word_imm_effect(Program_State *ps, const char *name, String_Builder source, int64_t pop_count, int64_t push_count);
Execution_Token *get_word(Word_Table *word_table, const char *name);
void call_word(void(*word)(int64_t**), Program_State *program_state);

#endif
