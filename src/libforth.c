#include <libforth.h>
#include <codegen.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>


void populate_core_words(Program_State *ps);
void populate_tool_words(Program_State *ps);

static void smorth_lib_die(const char *message)
{
    fprintf(stderr, "%s\n", message);
    exit(1);
}

static size_t cf_stack_capacity(Program_State *ps)
{
    return sizeof(ps->cf_stack)/sizeof(ps->cf_stack[0]);
}

static void push_cf(Program_State *ps, const char *kind, size_t handle)
{
    if(ps->cfi>=cf_stack_capacity(ps)) smorth_lib_die("control-flow stack overflow");
    ps->cf_stack[ps->cfi++] = (Control_Flow_Stack_Item){.kind=(char *)kind, .handle=handle};
}

static Control_Flow_Stack_Item pop_cf(Program_State *ps)
{
    if(ps->cfi==0) smorth_lib_die("control-flow stack underflow");
    return ps->cf_stack[--ps->cfi];
}

void populate_builtin_words(Program_State *ps)
{
    populate_core_words(ps);
    populate_tool_words(ps);
}

void load_library(const char *lib_path, Program_State *ps)
{
    String_Builder lib_code = {0};
    if(!read_entire_file(lib_path, &lib_code))
    {
        fprintf(stderr, "failed to load library: %s\n", lib_path);
        exit(1);
    }
    for(size_t i=0;i<lib_code.count;i++) if(isspace(lib_code.items[i])) lib_code.items[i]=' ';
    sb_append_null(&lib_code);

    ps->ib=sb_to_sv(lib_code);
    interpret(ps);
    return;
}

// core words
// --------------------------------------------------------------------------------------
void dot_impl(int64_t num)
{
    printf("%" PRId64 " ", num);
    return;
}

void colon_impl(Program_State *ps)
{
    if(ps->compiling) smorth_lib_die("nested word declarations are not supported");
    Token token = next_token(&ps->ib);
    if(token.raw.count==0 || token.kind!=FWORD) smorth_lib_die("expected word name after :");
    ps->word_name=token.raw.data;
    push_cf(ps, "colon-sys", get_jmp_marker(&ps->word_source));
    ps->compiling=true;
}

void semicolon_impl(Program_State *ps)
{
    if(!ps->compiling) smorth_lib_die("; outside word declaration");
    if(strcmp(pop_cf(ps).kind, "colon-sys")!=0) smorth_lib_die("invalid control-flow stack for ;");
    sb_insert_ret(&ps->word_source);
    if(ps->immediate) add_word_imm(ps, ps->word_name, ps->word_source);
    else add_word(ps, ps->word_name, ps->word_source);
    if (ps->word_name==NULL) smorth_stack_push(ps, (int64_t)&ps->word_table.items[ps->word_table.count-1], ";");
    ps->word_name=NULL;
    ps->word_source.count=0;
    ps->compiling=false;
    ps->immediate=false;
}

void begin_impl(Program_State *ps)
{
    push_cf(ps, "dist", get_jmp_marker(&ps->word_source));
}

void while_impl(Program_State *ps)
{
    Control_Flow_Stack_Item item = pop_cf(ps);
    if (strcmp(item.kind, "dist")==0)
    {
        sb_insert_stack_effect_guard(&ps->word_source, ps, "while", 1, 0);
        sb_insert_subimm(&ps->word_source, reg_make_ptr(get_register(1),0), 0x8);
        sb_insert_mov(&ps->word_source, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&ps->word_source, reg_make_ptr(get_register(0),0), get_register(0));
        sb_insert_cmpimm(&ps->word_source, get_register(0), 0);
        push_cf(ps, "orig", sb_start_jcc(&ps->word_source, EQ));
        push_cf(ps, item.kind, item.handle);
    }
    else smorth_lib_die("invalid control-flow stack for while");
}

void repeat_impl(Program_State *ps)
{
    Control_Flow_Stack_Item item = pop_cf(ps);
    if (strcmp(item.kind, "dist")==0)
    {
        sb_insert_jmp(&ps->word_source, item.handle);
        item = pop_cf(ps);
        if (strcmp(item.kind, "orig")==0) sb_end_jmp(&ps->word_source, item.handle);
        else smorth_lib_die("invalid control-flow stack for repeat");
    }
    else smorth_lib_die("invalid control-flow stack for repeat");
}

void until_impl(Program_State *ps)
{
    Control_Flow_Stack_Item item = pop_cf(ps);
    if (strcmp(item.kind, "dist")==0)
    {
        sb_insert_stack_effect_guard(&ps->word_source, ps, "until", 1, 0);
        sb_insert_subimm(&ps->word_source, reg_make_ptr(get_register(1),0), 0x8);
        sb_insert_mov(&ps->word_source, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&ps->word_source, reg_make_ptr(get_register(0),0), get_register(0));
        sb_insert_cmpimm(&ps->word_source, get_register(0), 0);
        sb_insert_jcc(&ps->word_source, item.handle, EQ);
    }
    else smorth_lib_die("invalid control-flow stack for until");
}

void do_init_impl(Program_State *ps)
{
    sb_insert_stack_effect_guard(&ps->word_source, ps, "do", 2, 0);
    sb_insert_mov(&ps->word_source, reg_make_ptr(get_register(1),0), get_register(0));
    sb_insert_mov(&ps->word_source, reg_make_ptr(get_register(0),-8), get_register(5));
    sb_insert_mov(&ps->word_source, reg_make_ptr(get_register(0),-16), get_register(6));
    sb_insert_subimm(&ps->word_source, reg_make_ptr(get_register(1),0), 0x10);

    sb_insert_current_address(&ps->word_source, get_register(0));
    size_t diff = get_jmp_marker(&ps->word_source);
    sb_insert_addimm(&ps->word_source, get_register(0), 0);
    push_cf(ps, "do-sys", ps->word_source.count);
    sb_insert_addimm(&ps->word_source, get_register(0), get_jmp_marker(&ps->word_source)-diff);
    sb_insert_push(&ps->word_source, get_register(0));

    sb_insert_push(&ps->word_source, get_register(5));
    sb_insert_push(&ps->word_source, get_register(6));
    sb_insert_push(&ps->word_source, REG_RBP);
    sb_insert_set_frame_pointer(&ps->word_source);
}

void plus_loop_impl(Program_State *ps)
{
    Control_Flow_Stack_Item item = pop_cf(ps);
    if (strcmp(item.kind, "dist")==0)
    {
        sb_insert_stack_effect_guard(&ps->word_source, ps, "+loop", 1, 0);
        sb_insert_subimm(&ps->word_source, reg_make_ptr(get_register(1),0), 0x8);
        sb_insert_mov(&ps->word_source, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&ps->word_source, reg_make_ptr(get_register(0),0), get_register(5));
        sb_insert_add(&ps->word_source, get_register(5), reg_make_ptr(REG_RBP,LOOP_INDEX_OFFSET));
        sb_insert_mov(&ps->word_source, reg_make_ptr(REG_RBP,LOOP_LIMIT_OFFSET), get_register(5));
        sb_insert_mov(&ps->word_source, reg_make_ptr(REG_RBP,LOOP_INDEX_OFFSET), get_register(6));
        sb_insert_cmp(&ps->word_source, get_register(5), get_register(6));
        sb_insert_jcc(&ps->word_source, item.handle, NE);
        sb_insert_pop(&ps->word_source, REG_RBP);
        sb_insert_pop(&ps->word_source, get_register(0));
        sb_insert_pop(&ps->word_source, get_register(0));
        sb_insert_pop(&ps->word_source, get_register(0));
    }
    else smorth_lib_die("invalid control-flow stack for +loop");

     item = pop_cf(ps);
     if(strcmp(item.kind, "do-sys")==0) sb_end_jmp(&ps->word_source, item.handle);
     else smorth_lib_die("invalid control-flow stack for +loop");
}

void if_impl(Program_State *ps)
{
    sb_insert_stack_effect_guard(&ps->word_source, ps, "if", 1, 0);
    sb_insert_subimm(&ps->word_source, reg_make_ptr(get_register(1), 0), 8);
    sb_insert_mov(&ps->word_source, reg_make_ptr(get_register(1), 0), get_register(0));
    sb_insert_mov(&ps->word_source, reg_make_ptr(get_register(0), 0), get_register(0));
    sb_insert_cmpimm(&ps->word_source, get_register(0), 0);
    push_cf(ps, "orig", sb_start_jcc(&ps->word_source, EQ));
}

void else_impl(Program_State *ps)
{
    Control_Flow_Stack_Item item = pop_cf(ps);
    if(strcmp(item.kind, "orig")==0)
    {
        push_cf(ps, "orig", sb_start_jmp(&ps->word_source));
        sb_end_jmp(&ps->word_source, item.handle);
    }
    else smorth_lib_die("invalid control-flow stack for else");
}

void then_impl(Program_State *ps)
{
    Control_Flow_Stack_Item item = pop_cf(ps);
    if(strcmp(item.kind, "orig")==0) sb_end_jmp(&ps->word_source, item.handle);
    else smorth_lib_die("invalid control-flow stack for then");
}

void parse_impl(Program_State *ps)
{
    char delim = (char)smorth_stack_pop(ps, "parse");

    ps->ib = sv_trim_left(ps->ib);
    if(ps->ib.count==0) smorth_lib_die("parse reached end of input");

    String_Builder ccc = {0};
    while (ps->ib.count>0 && ps->ib.data[0]!=delim) sb_append(&ccc, *sv_chop_left(&ps->ib, 1).data);
    if (ps->ib.count>0) 
    {
        sb_append_null(&ccc);
        sv_chop_left(&ps->ib, 1);
    }
    
    smorth_stack_push(ps, (uint64_t)ccc.items, "parse");
    smorth_stack_push(ps, ccc.count, "parse");

    return;
}

void find_impl(Program_State *ps)
{
    const char *name = (const char *)smorth_stack_pop(ps, "find");

    Execution_Token *word = get_word(&ps->word_table, name);
    if(word!=NULL)
    {
        smorth_stack_push(ps, (uint64_t)word, "find");
        smorth_stack_push(ps, (word->imm) ? 1:-1, "find");
    }
    else
    {
        smorth_stack_push(ps, (uint64_t)name, "find");
        smorth_stack_push(ps, 0, "find");
    }

    return;
}

void compile_comma_impl(Program_State *ps)
{
    Execution_Token *word = (Execution_Token *)smorth_stack_pop(ps, "compile,");
    if(word==NULL) smorth_lib_die("compile, requires a word");
    sb_insert_call(&ps->word_source, word->codeptr);
}

void aligned_impl(Program_State *ps) 
{
    int64_t *addr = (int64_t *)smorth_stack_pop(ps, "aligned");
    uint64_t phase = (uint64_t)ps->dp%8;
    if(phase!=0) addr = (int64_t *)((char *)addr+(8-phase));
    smorth_stack_push(ps, (int64_t)addr, "aligned");
}

void execute_impl(Program_State *ps)
{
    Execution_Token *word = (Execution_Token *)smorth_stack_pop(ps, "execute");
    if(word==NULL) smorth_lib_die("execute requires a word");
    call_word(word->codeptr, ps);
}

void literal_impl(Program_State *ps)
{
    uint64_t x = smorth_stack_pop(ps, "literal");

    sb_insert_stack_effect_guard(&ps->word_source, ps, "literal", 0, 1);
    sb_insert_mov(&ps->word_source, reg_make_ptr(get_register(1),0), get_register(0));
    sb_insert_movabs(&ps->word_source, get_register(5), (void *)x);
    sb_insert_mov(&ps->word_source, get_register(5), reg_make_ptr(get_register(0),0));
    sb_insert_addimm(&ps->word_source, reg_make_ptr(get_register(1), 0), 0x8);    
}

void recurse_impl(Program_State *ps)
{
    Control_Flow_Stack_Item *item=NULL;
    for(size_t i=ps->cfi; i>0;--i) if(strcmp("colon-sys", ps->cf_stack[i-1].kind)==0) {item=&ps->cf_stack[i-1]; break;}
    if(item) sb_insert_rel_call(&ps->word_source, item->handle);
    else smorth_lib_die("recurse outside word declaration");
}

void type_impl(Program_State *ps)
{
    uint64_t len = smorth_stack_pop(ps, "type");
    char *buf = (char *)smorth_stack_pop(ps, "type");

    if(len>0)
    {
        String_Builder slice = {0};
        sb_append_buf(&slice, buf, len);
        sb_append_null(&slice);
        printf("%s", slice.items);
    }
}

void populate_core_words(Program_State *ps)
{
    String_Builder src = {0};

    src.count = 0;
        sb_insert_subimm(&src, reg_make_ptr(get_register(1), 0), 0x10);
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),8), get_register(5));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),0), get_register(6));
        sb_insert_mov(&src, get_register(6), reg_make_ptr(get_register(5),0));
        sb_insert_ret(&src);
    add_word_effect(ps, "!", src, 2, 0);

    src.count = 0;
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-8), get_register(5));
        sb_insert_movabs(&src, get_register(6), &ps->dp);
        sb_insert_addimm(&src, reg_make_ptr(get_register(6), 0), 0x8);
        sb_insert_mov(&src, reg_make_ptr(get_register(6),0), get_register(6));
        sb_insert_mov(&src, get_register(5), reg_make_ptr(get_register(6),-8));
        sb_insert_subimm(&src, reg_make_ptr(get_register(1),0), 0x8);
        sb_insert_ret(&src);
    add_word_effect(ps, ",", src, 1, 0);

    src.count = 0;
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-8), get_register(5));
        sb_insert_mov(&src, reg_make_ptr(get_register(5),0), get_register(5));
        sb_insert_mov(&src, get_register(5), reg_make_ptr(get_register(0),-8));
        sb_insert_ret(&src);
    add_word_effect(ps, "@", src, 1, 1);

    src.count = 0;
        sb_insert_subimm(&src, reg_make_ptr(get_register(1), 0), 0x8);
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),0), get_register(5));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-8), get_register(6));
        sb_insert_add(&src, get_register(5), get_register(6));
        sb_insert_mov(&src, get_register(6), reg_make_ptr(get_register(0),-8));
        sb_insert_ret(&src);
    add_word_effect(ps, "+", src, 2, 1);

    src.count = 0;
        sb_insert_subimm(&src, reg_make_ptr(get_register(1), 0), 0x10);
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),0), get_register(5));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),8), get_register(6));
        sb_insert_add(&src, get_register(5), reg_make_ptr(get_register(6),0));
        sb_insert_ret(&src);
    add_word_effect(ps, "+!", src, 2, 0);

    src.count = 0;
        sb_insert_subimm(&src, reg_make_ptr(get_register(1), 0), 0x8);
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),0), get_register(5));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-8), get_register(6));
        sb_insert_sub(&src, get_register(5), get_register(6));
        sb_insert_mov(&src, get_register(6), reg_make_ptr(get_register(0),-8));
        sb_insert_ret(&src);
    add_word_effect(ps, "-", src, 2, 1);

    src.count = 0;
        sb_insert_subimm(&src, reg_make_ptr(get_register(1), 0), 0x8);
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),0), get_register(5));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-8), get_register(6));
        sb_insert_imul(&src, get_register(5), get_register(6));
        sb_insert_mov(&src, get_register(6), reg_make_ptr(get_register(0),-8));
        sb_insert_ret(&src);
    add_word_effect(ps, "*", src, 2, 1);

    src.count = 0;
        sb_insert_subimm(&src, reg_make_ptr(get_register(1), 0), 0x8);
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),0), get_register(5));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-8), get_register(6));
        sb_insert_idiv(&src, get_register(5), get_register(6));
        sb_insert_mov(&src, get_register(6), reg_make_ptr(get_register(0),-8));
        sb_insert_ret(&src);
    add_word_effect(ps, "/", src, 2, 1);
    
    src.count = 0;
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-8), get_register(5));
        sb_insert_mov(&src, get_register(5), reg_make_ptr(get_register(0),0));
        sb_insert_addimm(&src, reg_make_ptr(get_register(1), 0), 0x8);
        sb_insert_ret(&src);
    add_word_effect(ps, "dup", src, 1, 2);

    src.count = 0;
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-0x8), get_register(5));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-0x10), get_register(6));
        sb_insert_mov(&src, get_register(6), reg_make_ptr(get_register(0),0));
        sb_insert_mov(&src, get_register(5), reg_make_ptr(get_register(0),8));
        sb_insert_addimm(&src, reg_make_ptr(get_register(1), 0), 0x10);
        sb_insert_ret(&src);
    add_word_effect(ps, "2dup", src, 2, 4);

    src.count = 0;
        sb_insert_subimm(&src, reg_make_ptr(get_register(1), 0), 0x8);
        sb_insert_ret(&src);
    add_word_effect(ps, "drop", src, 1, 0);

    src.count = 0;
        sb_insert_subimm(&src, reg_make_ptr(get_register(1), 0), 0x10);
        sb_insert_ret(&src);
    add_word_effect(ps, "2drop", src, 2, 0);

    src.count = 0;
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-16), get_register(5));
        sb_insert_mov(&src, get_register(5), reg_make_ptr(get_register(0),0));
        sb_insert_addimm(&src, reg_make_ptr(get_register(1), 0), 0x8);
        sb_insert_ret(&src);
    add_word_effect(ps, "over", src, 2, 3);

    src.count = 0;
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-0x18), get_register(5));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-0x20), get_register(6));
        sb_insert_mov(&src, get_register(6), reg_make_ptr(get_register(0),0));
        sb_insert_mov(&src, get_register(5), reg_make_ptr(get_register(0),8));
        sb_insert_addimm(&src, reg_make_ptr(get_register(1), 0), 0x10);
        sb_insert_ret(&src);
    add_word_effect(ps, "2over", src, 4, 6);
        
    src.count = 0;
        sb_insert_subimm(&src, reg_make_ptr(get_register(1), 0), 0x8);
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),0), get_register(5));
        sb_insert_mov(&src, get_register(5), reg_make_ptr(get_register(0),-8));
        sb_insert_ret(&src);
    add_word_effect(ps, "nip", src, 2, 1);

    src.count = 0;
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-8), get_register(5));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-16), get_register(6));
        sb_insert_mov(&src, get_register(5), reg_make_ptr(get_register(0),-16));
        sb_insert_mov(&src, get_register(6), reg_make_ptr(get_register(0),-8));
        sb_insert_ret(&src);
    add_word_effect(ps, "swap", src, 2, 2);

    src.count = 0;
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-0x08), get_register(5));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-0x18), get_register(6));
        sb_insert_mov(&src, get_register(5), reg_make_ptr(get_register(0),-0x18));
        sb_insert_mov(&src, get_register(6), reg_make_ptr(get_register(0),-0x08));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-0x10), get_register(5));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-0x20), get_register(6));
        sb_insert_mov(&src, get_register(5), reg_make_ptr(get_register(0),-0x20));
        sb_insert_mov(&src, get_register(6), reg_make_ptr(get_register(0),-0x10));
        sb_insert_ret(&src);
    add_word_effect(ps, "2swap", src, 4, 4);

    src.count = 0;
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-8), get_register(5));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-24), get_register(6));
        sb_insert_mov(&src, get_register(6), reg_make_ptr(get_register(0),-8));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-16), get_register(6));
        sb_insert_mov(&src, get_register(5), reg_make_ptr(get_register(0),-16));
        sb_insert_mov(&src, get_register(6), reg_make_ptr(get_register(0),-24));
        sb_insert_ret(&src);
    add_word_effect(ps, "rot", src, 3, 3);

    src.count = 0;
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-8), get_register(5));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-16), get_register(6));
        sb_insert_mov(&src, get_register(5), reg_make_ptr(get_register(0),-16));
        sb_insert_mov(&src, get_register(6), reg_make_ptr(get_register(0),-8));
        sb_insert_mov(&src, get_register(5), reg_make_ptr(get_register(0),0));
        sb_insert_addimm(&src, reg_make_ptr(get_register(1), 0), 0x8);
        sb_insert_ret(&src);
    add_word_effect(ps, "tuck", src, 2, 3);

    src.count = 0;
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(5));
        sb_insert_movabs(&src, get_register(6), ps->stack);
        sb_insert_sub(&src, get_register(6), get_register(5));
        sb_insert_idivabs(&src, get_register(5), sizeof(int64_t));
        sb_insert_subimm(&src, get_register(5), 1);
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, get_register(5), reg_make_ptr(get_register(0),0));
        sb_insert_addimm(&src, reg_make_ptr(get_register(1), 0), 0x8);
        sb_insert_ret(&src);
    add_word_effect(ps, "depth", src, 0, 1);

    src.count = 0;
    {
        String_Builder param_code = {0};
            sb_insert_subimm(&param_code, reg_make_ptr(get_register(1), 0), 0x8);
            sb_insert_mov(&param_code, reg_make_ptr(get_register(1), 0), get_register(1));
            sb_insert_mov(&param_code, reg_make_ptr(get_register(1), 0), get_register(1));
            sb_insert_C_call(&src, dot_impl, &param_code);
        sb_free(param_code);
        sb_insert_ret(&src);
    }
    add_word_effect(ps, ".", src, 1, 0);

    src.count = 0;
    {
        String_Builder param_code = {0};
            sb_insert_movabs(&param_code, get_register(1), ps);
            sb_insert_C_call(&src, colon_impl, &param_code);
        sb_free(param_code);
        sb_insert_ret(&src);
    }
    add_word(ps, ":", src);

    src.count = 0;
    {
        String_Builder param_code = {0};
            sb_insert_movabs(&param_code, get_register(1), ps);
            sb_insert_C_call(&src, semicolon_impl, &param_code);
        sb_free(param_code);
        sb_insert_ret(&src);
    }
    add_word_imm(ps, ";", src);

    src.count = 0;
    {
        String_Builder param_code = {0};
            sb_insert_movabs(&param_code, get_register(1), ps);
            sb_insert_C_call(&src, begin_impl, &param_code);
        sb_free(param_code);
        sb_insert_ret(&src);
    }
    add_word_imm(ps, "begin", src);

    src.count = 0;
    {
        String_Builder param_code = {0};
            sb_insert_movabs(&param_code, get_register(1), ps);
            sb_insert_C_call(&src, while_impl, &param_code);
        sb_free(param_code);
        sb_insert_ret(&src);
    }
    add_word_imm(ps, "while", src);

    src.count = 0;
    {
        String_Builder param_code = {0};
            sb_insert_movabs(&param_code, get_register(1), ps);
            sb_insert_C_call(&src, repeat_impl, &param_code);
        sb_free(param_code);
        sb_insert_ret(&src);
    }
    add_word_imm(ps, "repeat", src);

    src.count = 0;
    {
        String_Builder param_code = {0};
            sb_insert_movabs(&param_code, get_register(1), ps);
            sb_insert_C_call(&src, until_impl, &param_code);
        sb_free(param_code);
        sb_insert_ret(&src);
    }
    add_word_imm(ps, "until", src);

    src.count = 0;
    {
        String_Builder param_code = {0};
            sb_insert_movabs(&param_code, get_register(1), ps);
            sb_insert_C_call(&src, do_init_impl, &param_code);
        sb_free(param_code);
        sb_insert_ret(&src);
    }
    add_word_imm(ps, "(do)", src);

    src.count = 0;
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(REG_RBP,LOOP_INDEX_OFFSET), get_register(5));
        sb_insert_mov(&src, get_register(5), reg_make_ptr(get_register(0),0));
        sb_insert_addimm(&src, reg_make_ptr(get_register(1), 0), 0x8);
        sb_insert_ret(&src);
    add_word_effect(ps, "i", src, 0, 1);

    src.count = 0;
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(REG_RBP,LOOP_OUTER_INDEX_OFFSET), get_register(5));
        sb_insert_mov(&src, get_register(5), reg_make_ptr(get_register(0),0));
        sb_insert_addimm(&src, reg_make_ptr(get_register(1), 0), 0x8);
        sb_insert_ret(&src);
    add_word_effect(ps, "j", src, 0, 1);

    src.count = 0;
        sb_insert_pop(&src, get_register(0));
        sb_insert_pop(&src, REG_RBP);
        sb_insert_pop(&src, get_register(0));
        sb_insert_pop(&src, get_register(0));
        sb_insert_ret(&src);
    add_word(ps, "leave", src);

    src.count = 0;
        sb_insert_pop(&src, get_register(5));
        sb_insert_pop(&src, REG_RBP);
        sb_insert_pop(&src, get_register(0));
        sb_insert_pop(&src, get_register(0));
        sb_insert_pop(&src, get_register(0));
        sb_insert_push(&src, get_register(5));
        sb_insert_ret(&src);
    add_word(ps, "unloop", src);

    src.count = 0;
        sb_insert_pop(&src, get_register(0));
        sb_insert_ret(&src);
    add_word(ps, "exit", src);

    src.count = 0;
    {
        String_Builder param_code = {0};
            sb_insert_movabs(&param_code, get_register(1), ps);
            sb_insert_C_call(&src, plus_loop_impl, &param_code);
        sb_free(param_code);
        sb_insert_ret(&src);
    }
    add_word_imm(ps, "+loop", src);

    src.count = 0;
    {
        String_Builder param_code = {0};
            sb_insert_movabs(&param_code, get_register(1), ps);
            sb_insert_C_call(&src, if_impl, &param_code);
        sb_free(param_code);
        sb_insert_ret(&src);
    }
    add_word_imm(ps, "if", src);

    src.count = 0;
    {
        String_Builder param_code = {0};
            sb_insert_movabs(&param_code, get_register(1), ps);
            sb_insert_C_call(&src, else_impl, &param_code);
        sb_free(param_code);
        sb_insert_ret(&src);
    }
    add_word_imm(ps, "else", src);

    src.count = 0;
    {
        String_Builder param_code = {0};
            sb_insert_movabs(&param_code, get_register(1), ps);
            sb_insert_C_call(&src, then_impl, &param_code);
        sb_free(param_code);
        sb_insert_ret(&src);
    }
    add_word_imm(ps, "then", src);

    src.count = 0;
    {
        String_Builder param_code = {0};
            sb_insert_movabs(&param_code, get_register(1), ps);
            sb_insert_C_call(&src, parse_impl, &param_code);
        sb_free(param_code);
        sb_insert_ret(&src);
    }
    add_word_effect(ps, "parse", src, 1, 2);

    src.count = 0;
    {
        String_Builder param_code = {0};
            sb_insert_movabs(&param_code, get_register(1), ps);
            sb_insert_C_call(&src, find_impl, &param_code);
        sb_free(param_code);
        sb_insert_ret(&src);
    }
    add_word_effect(ps, "find", src, 1, 2);

    src.count = 0;
    {
        String_Builder param_code = {0};
            sb_insert_movabs(&param_code, get_register(1), ps);
            sb_insert_C_call(&src, compile_comma_impl, &param_code);
        sb_free(param_code);
        sb_insert_ret(&src);
    }
    add_word_effect(ps, "compile,", src, 1, 0);

    src.count = 0;
        sb_insert_pop(&src, get_register(5));
        sb_insert_subimm(&src, reg_make_ptr(get_register(1), 0), 0x8);
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_push(&src, reg_make_ptr(get_register(0),0));
        sb_insert_push(&src, get_register(5));
        sb_insert_ret(&src);
    add_word_effect(ps, ">r", src, 1, 0);

    src.count = 0;
        sb_insert_pop(&src, get_register(5));
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_pop(&src, reg_make_ptr(get_register(0),0));
        sb_insert_addimm(&src, reg_make_ptr(get_register(1), 0), 0x8);
        sb_insert_push(&src, get_register(5));
        sb_insert_ret(&src);
    add_word_effect(ps, "r>", src, 0, 1);
    
    src.count=0;
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-8), get_register(5));
        sb_insert_get_flagimm(&src, get_register(5), 0, LT);
        sb_insert_mov(&src, get_register(5), reg_make_ptr(get_register(0),-8));
        sb_insert_ret(&src);
    add_word_effect(ps, "0<", src, 1, 1);

    src.count=0;
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-8), get_register(5));
        sb_insert_get_flagimm(&src, get_register(5), 0, EQ);
        sb_insert_mov(&src, get_register(5), reg_make_ptr(get_register(0),-8));
        sb_insert_ret(&src);
    add_word_effect(ps, "0=", src, 1, 1);

    src.count=0;
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-8), get_register(5));
        sb_insert_get_flagimm(&src, get_register(5), 0, NE);
        sb_insert_mov(&src, get_register(5), reg_make_ptr(get_register(0),-8));
        sb_insert_ret(&src);
    add_word_effect(ps, "0<>", src, 1, 1);

    src.count=0;
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-8), get_register(5));
        sb_insert_get_flagimm(&src, get_register(5), 0, GT);
        sb_insert_mov(&src, get_register(5), reg_make_ptr(get_register(0),-8));
        sb_insert_ret(&src);
    add_word_effect(ps, "0>", src, 1, 1);

    src.count=0;
        sb_insert_subimm(&src, reg_make_ptr(get_register(1), 0), 0x8);
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),0), get_register(5));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-8), get_register(6));
        sb_insert_get_flag(&src, get_register(5), get_register(6), LT);
        sb_insert_mov(&src, get_register(6), reg_make_ptr(get_register(0),-8));
        sb_insert_ret(&src);
    add_word_effect(ps, "<", src, 2, 1);

    src.count=0;
        sb_insert_subimm(&src, reg_make_ptr(get_register(1), 0), 0x8);
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),0), get_register(5));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-8), get_register(6));
        sb_insert_get_flag(&src, get_register(5), get_register(6), EQ);
        sb_insert_mov(&src, get_register(6), reg_make_ptr(get_register(0),-8));
        sb_insert_ret(&src);
    add_word_effect(ps, "=", src, 2, 1);

    src.count=0;
        sb_insert_subimm(&src, reg_make_ptr(get_register(1), 0), 0x8);
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),0), get_register(5));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-8), get_register(6));
        sb_insert_get_flag(&src, get_register(5), get_register(6), NE);
        sb_insert_mov(&src, get_register(6), reg_make_ptr(get_register(0),-8));
        sb_insert_ret(&src);
    add_word_effect(ps, "<>", src, 2, 1);

    src.count=0;
        sb_insert_subimm(&src, reg_make_ptr(get_register(1), 0), 0x8);
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),0), get_register(5));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),-8), get_register(6));
        sb_insert_get_flag(&src, get_register(5), get_register(6), GT);
        sb_insert_mov(&src, get_register(6), reg_make_ptr(get_register(0),-8));
        sb_insert_ret(&src);
    add_word_effect(ps, ">", src, 2, 1);

    src.count=0;
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_inc(&src, reg_make_ptr(get_register(0),-8));
        sb_insert_ret(&src);
    add_word_effect(ps, "1+", src, 1, 1);

    src.count=0;
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_dec(&src, reg_make_ptr(get_register(0),-8));
        sb_insert_ret(&src);
    add_word_effect(ps, "1-", src, 1, 1);

    src.count = 0;
    {
        String_Builder param_code = {0};
            sb_insert_movabs(&param_code, get_register(1), ps);
            sb_insert_C_call(&src, aligned_impl, &param_code);
        sb_free(param_code);
        sb_insert_ret(&src);
    }
    add_word_effect(ps, "aligned", src, 1, 1);

    src.count=0;
        sb_insert_subimm(&src, reg_make_ptr(get_register(1), 0), 0x8);
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_mov(&src, reg_make_ptr(get_register(0),0), get_register(5));
        sb_insert_movabs(&src, get_register(6), &ps->dp);
        sb_insert_add(&src, get_register(5), reg_make_ptr(get_register(6),0));
        sb_insert_ret(&src);
    add_word_effect(ps, "allot", src, 1, 0);

    src.count = 0;
    {
        String_Builder param_code = {0};
            sb_insert_movabs(&param_code, get_register(1), ps);
            sb_insert_C_call(&src, execute_impl, &param_code);
        sb_free(param_code);
        sb_insert_ret(&src);
    }
    add_word_effect(ps, "execute", src, 1, 0);

    src.count=0;
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_movabs(&src, get_register(5), &ps->dp);
        sb_insert_mov(&src, reg_make_ptr(get_register(5),0), get_register(5));
        sb_insert_mov(&src, get_register(5), reg_make_ptr(get_register(0),0));
        sb_insert_addimm(&src, reg_make_ptr(get_register(1), 0), 0x8);
        sb_insert_ret(&src);
    add_word_effect(ps, "here", src, 0, 1);

    src.count=0;
    {
        String_Builder param_code = {0};
            sb_insert_movabs(&param_code, get_register(1), ps);
            sb_insert_C_call(&src, literal_impl, &param_code);
        sb_free(param_code);
        sb_insert_ret(&src);
    }
    add_word_imm_effect(ps, "literal", src, 1, 0);

    src.count=0;
        sb_insert_mov(&src, reg_make_ptr(get_register(1),0), get_register(0));
        sb_insert_movabs(&src, get_register(5), &ps->compiling);
        sb_insert_mov(&src, get_register(5), reg_make_ptr(get_register(0),0));
        sb_insert_addimm(&src, reg_make_ptr(get_register(1), 0), 0x8);
        sb_insert_ret(&src);
    add_word_effect(ps, "state", src, 0, 1);

    src.count=0;
    {
        String_Builder param_code = {0};
            sb_insert_movabs(&param_code, get_register(1), ps);
            sb_insert_C_call(&src, recurse_impl, &param_code);
        sb_free(param_code);
        sb_insert_ret(&src);
    }
    add_word_imm(ps, "recurse", src);

    src.count=0;
    {
        String_Builder param_code = {0};
            sb_insert_movabs(&param_code, get_register(1), ps);
            sb_insert_C_call(&src, type_impl, &param_code);
        sb_free(param_code);
        sb_insert_ret(&src);
    }
    add_word_effect(ps, "type", src, 2, 0);

    sb_free(src);
}
// --------------------------------------------------------------------------------------


// tool words
// --------------------------------------------------------------------------------------
void dot_s_impl(Program_State *ps)
{
    printf("\n<%td> [ ", ps->sp-ps->stack);
    for(int64_t *i=ps->sp-1; i>=ps->stack; --i) printf("%" PRId64 " ", *i);
    printf("]\n");
}

void populate_tool_words(Program_State *ps)
{
    String_Builder src = {0};
    
    src.count = 0;
    {
        String_Builder param_code = {0};
            sb_insert_movabs(&param_code, get_register(1), 0);
            sb_insert_C_call(&src, exit, &param_code);
        sb_insert_ret(&src);
    }
    add_word(ps, "bye", src);

    src.count = 0;
    {
        String_Builder param_code = {0};
            sb_insert_movabs(&param_code, get_register(1), ps);
            sb_insert_C_call(&src, dot_s_impl, &param_code);
        sb_insert_ret(&src);
    }
    add_word(ps, ".s", src);

    sb_free(src);
}
