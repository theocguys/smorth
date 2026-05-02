#define NOB_IMPLEMENTATION
#include <nob.h>
#undef NOB_IMPLEMENTATION
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <interpreter.h>
#include <libforth.h>


int main(int argc, char **argv)
{   
    Program_State program_state = {0};
    program_state.stack_base = program_state.stack;
    program_state.stack_limit = program_state.stack + SMORTH_STACK_CAPACITY;
    const char *stack_limit_cells = getenv("SMORTH_STACK_LIMIT_CELLS");
    if(stack_limit_cells!=NULL && stack_limit_cells[0]!='\0')
    {
        errno = 0;
        char *end = NULL;
        long limit = strtol(stack_limit_cells, &end, 10);
        if(errno!=0 || end==stack_limit_cells || *end!='\0' || limit<=0 || limit>SMORTH_STACK_CAPACITY)
        {
            fprintf(stderr, "invalid SMORTH_STACK_LIMIT_CELLS: %s\n", stack_limit_cells);
            return 1;
        }
        program_state.stack_limit = program_state.stack + limit;
    }
    program_state.sp = program_state.stack;
    program_state.dp = program_state.data;
    
    populate_builtin_words(&program_state);
    load_library("lib/core.fth", &program_state);

    bool st=false;
    shift(argv, argc);
    while (argc)
    {
        char *arg = shift(argv, argc);
        if (strcmp(arg, "-st")==0) st=true;
        else
        {
            printf("unknown flag(%s)\n", arg);
            return 1;
        }
    }

    printf("SMORTH v0.5\nby (re)tur(n) 0;\nthe bye word can be used at any time to quit\n");

    String_Builder line = {0};
    sb_append_null(&line);
    while (true)
    {
        line.count=0;
        char c;
        while ((c = getchar())!='\n') sb_append(&line, c);
        sb_append_null(&line);
        program_state.ib = sb_to_sv(line);
        
        interpret(&program_state);
        if(st)
        {
            program_state.ib = sv_from_cstr(".s");
            interpret(&program_state);
        }
        printf("ok\n");
    }
    
    return 0;
}
