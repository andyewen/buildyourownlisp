#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <editline/readline.h>
#include <editline/history.h>
#include "mpc.h"

long eval_op(char* op, long x, long y) {
  if (strcmp(op, "+") == 0 || strcmp(op, "add") == 0) {
    return x + y;
  }
  if (strcmp(op, "-") == 0 || strcmp(op, "sub") == 0) {
    return x - y;
  }
  if (strcmp(op, "*") == 0 || strcmp(op, "mul") == 0) {
    return x * y;
  }
  if (strcmp(op, "/") == 0 || strcmp(op, "div") == 0) {
    return x / y;
  }
  if (strcmp(op, "%") == 0 || strcmp(op, "mod") == 0) {
    return x % y;
  }
  if (strcmp(op, "^") == 0 || strcmp(op, "pow") == 0) {
    return (long)pow(x, y);
  }

  if (strcmp(op, "max") == 0) { return x >= y ? x : y; }
  if (strcmp(op, "min") == 0) { return x < y ? x : y; }

  return 0;
}

long eval_unary_op(char* op, long x) {
  if (strcmp(op, "-") == 0 || strcmp(op, "sub") == 0) {
    return -x;
  }
  return x;
}

long eval(mpc_ast_t* t) {
  // If node is a number just return it's value.
  if (strstr(t->tag, "number")) {
    return atoi(t->contents);
  }

  char* op = t->children[1]->contents;

  long x = eval(t->children[2]);

  int i = 3;
  while (strstr(t->children[i]->tag, "expr")) {
    x = eval_op(op, x, eval(t->children[i]));
    i++;
  }
  
  if (i == 3) {
    // Use unary eval.
    x = eval_unary_op(op, x);
  }

  return x;
}

int main(int argc, char** argv) {
  // Create some parsers.
  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Operator = mpc_new("operator");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lispy = mpc_new("lispy");

  // Define the language.
  mpca_lang(MPCA_LANG_DEFAULT,
    "                                                     \
      number    : /-?[0-9]+(\\.[0-9]+)?/  ;               \
      operator  : '+' | '-' | '/' | '*' | '%' | '^' |     \
                  \"add\" | \"sub\" | \"mul\" | \"div\" | \
                  \"mod\" | \"pow\" | \"max\" | \"min\";  \
      expr      : <number> | '(' <operator> <expr>+ ')' ; \
      lispy     : /^/ <operator> <expr>+ /$/ ;            \
    ",
    Number, Operator, Expr, Lispy);

  puts("Lispy version 0.0.1");
  puts("Press ^C to exit.");

  while(1) {
    char* input = readline("lispy> ");

    add_history(input);

    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Lispy, &r)) {
      long result = eval(r.output);
      printf("%li\n", result);
      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }

    free(input);
  }

  mpc_cleanup(4, Number, Operator, Expr, Lispy);
  return 0;
}
