#include <interpreter.h>
#include <codegen.h>
#include <errno.h>

static void smorth_die(const char *message)
{
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static void smorth_die_undefined_word(const char *name)
{
    fprintf(stderr, "undefined word: %s\n", name);
    exit(1);
}

static void *xmalloc(size_t size)
{
    void *ptr = malloc(size);
    if(ptr==NULL)
    {
        fprintf(stderr, "out of memory allocating %zu bytes\n", size);
        exit(1);
    }
    return ptr;
}

void interpret(Program_State *program_state)
{
    while (strcmp(program_state->ib.data, "")!=0&&program_state->ib.count!=0)
    {
        Token token = next_token(&program_state->ib);
        if (token.raw.count==0) continue;
        if (token.kind==NUMBER) 
        {
            if (program_state->compiling)
            {
                if (program_state->word_name==NULL) smorth_die("invalid word declaration");

                sb_insert_mov(&program_state->word_source, reg_make_ptr(get_register(1),0), get_register(0));
                sb_insert_movabs(&program_state->word_source, get_register(5), (void *)token.as.number);
                sb_insert_mov(&program_state->word_source, get_register(5), reg_make_ptr(get_register(0),0));
                sb_insert_addimm(&program_state->word_source, reg_make_ptr(get_register(1), 0), 0x8);                
            }
            else
            {
                *program_state->sp=token.as.number;
                program_state->sp++;
                program_state->current_word=token.raw.data;
            }
        }
        else if (token.kind==FWORD)
        {
            if(program_state->compiling)
            {
                if (strcmp(token.as.word.data, "immediate")==0) program_state->immediate=true;
                else
                {
                    Execution_Token *word = get_word(&program_state->word_table, token.as.word.data);
                    if(word==NULL) smorth_die_undefined_word(token.as.word.data);
                    if(word->imm) call_word(word->codeptr, program_state);
                    else sb_insert_call(&program_state->word_source, word->codeptr);
                }
            }
            else
            {
                if (strcmp(token.as.word.data, "immediate")==0)
                {
                    if(program_state->word_table.count==0) smorth_die("immediate requires a previous word");
                    program_state->word_table.items[program_state->word_table.count-1]->imm=true;
                }
                else
                {
                    Execution_Token *word = get_word(&program_state->word_table, token.as.word.data);
                    if(word==NULL) smorth_die_undefined_word(token.as.word.data);
                    program_state->current_word=word->name;
                    call_word(word->codeptr, program_state);
                    if (program_state->sp<program_state->stack) smorth_die("stack underflow");
                    {
                        String_Builder word_ret = {0};
                        sb_append_cstr(&word_ret, word->name);
                        sb_append_cstr(&word_ret, " - ret");
                        sb_append_null(&word_ret);
                        program_state->current_word=word_ret.items;
                    }
                }
            }
        }
        
    }
}


typedef enum
{
    NUMBER_PARSE_NOT_NUMBER,
    NUMBER_PARSE_OK,
    NUMBER_PARSE_INVALID
}
Number_Parse_Result;

static Number_Parse_Result parse_i64(const char *raw, int64_t *out)
{
    size_t i = 0;
    if (raw[i]=='-' || raw[i]=='+') i++;
    if (!isdigit((unsigned char)raw[i])) return NUMBER_PARSE_NOT_NUMBER;
    while (isdigit((unsigned char)raw[i])) i++;
    while (isspace((unsigned char)raw[i])) i++;
    if(raw[i]!='\0') return NUMBER_PARSE_NOT_NUMBER;

    errno = 0;
    char *end = NULL;
    long long number = strtoll(raw, &end, 10);
    if(errno==ERANGE || end==raw) return NUMBER_PARSE_INVALID;
    *out = (int64_t)number;
    return NUMBER_PARSE_OK;
}

Token next_token(String_View *source)
{
    *source = sv_trim_left(*source);
    if(strcmp(source->data, "")==0||source->count==0) return (Token){0};

    String_Builder rawsb = {0};
    while (source->count>0 && !isspace(source->data[0])) sb_append(&rawsb, *sv_chop_left(source, 1).data);
    sb_append_null(&rawsb);
    String_View raw = sb_to_sv(rawsb);
    int64_t number = 0;
    Number_Parse_Result number_parse = parse_i64(raw.data, &number);
    if (number_parse==NUMBER_PARSE_OK)
    {
        return (Token){.kind=NUMBER, .raw=raw, .as.number=number};
    }
    if(number_parse==NUMBER_PARSE_INVALID)
    {
        fprintf(stderr, "invalid number: %s\n", raw.data);
        exit(1);
    }
    else return (Token){.kind=FWORD, .raw=raw, .as.word=raw};
}


void *exallocsb(String_Builder *sb);
void exfreesb(void *ptr, size_t len);

void add_word_impl(Word_Table *word_table, const char *name, String_Builder source, bool immediate)
{
    String_Builder tmp = {0};
    sb_insert_word_prologue(&tmp);
    sb_append_buf(&tmp, source.items, source.count);
    source = tmp;
    
    Execution_Token *word = xmalloc(sizeof(Execution_Token));
        word->imm=immediate;
        if(name)
        {
            size_t name_len = strlen(name);
            word->name = xmalloc(name_len+1);
            memcpy(word->name, name, name_len+1);
        } else word->name=NULL;
        word->source = source;
        word->codeptr = exallocsb(&source);
    da_append(word_table, word);
}
void add_word(Word_Table *word_table, const char *name, String_Builder source) { add_word_impl(word_table, name, source, false); }
void add_word_imm(Word_Table *word_table, const char *name, String_Builder source) { add_word_impl(word_table, name, source, true); }

Execution_Token *get_word(Word_Table *word_table, const char *name)
{
    if(!name) return NULL;
    for(size_t i=word_table->count; i>0; --i) if(word_table->items[i-1]->name) if(strcmp(word_table->items[i-1]->name, name)==0) return word_table->items[i-1];
    return NULL;
}

void call_word(void(*word)(int64_t**), Program_State *program_state)
{
    if(word==NULL) smorth_die("cannot call null word");
    word(&program_state->sp);
}


#ifdef __WIN32
    #include <windows.h>
#else
    #include <sys/mman.h>
#endif

void *exallocsb(String_Builder *sb)
{
#ifdef _WIN32
    if(sb->count==0) smorth_die("cannot allocate empty executable buffer");
    void *ptr = VirtualAlloc(NULL, sb->count, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if(ptr==NULL) smorth_die("VirtualAlloc executable buffer failed");
    memcpy(ptr, sb->items, sb->count);
    return ptr;
#else
    if(sb->count==0) smorth_die("cannot allocate empty executable buffer");
    void *ptr = mmap(NULL, sb->count, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(ptr==MAP_FAILED)
    {
        perror("mmap executable buffer");
        exit(1);
    }
    memcpy(ptr, sb->items, sb->count);
    __builtin___clear_cache(ptr, (char *)ptr + sb->count);
    if(mprotect(ptr, sb->count, PROT_READ | PROT_EXEC)!=0)
    {
        perror("mprotect executable buffer");
        munmap(ptr, sb->count);
        exit(1);
    }
    return ptr;
#endif
}

// len only needed on unix
void exfreesb(void *ptr, size_t len)
{
#ifdef _WIN32
    (void)len;
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, len);
#endif
    return;
}
