#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <editline/readline.h>
#include <editline/history.h>
#include "mpc.h"

/* Lisp value definitions. */
enum { LVAL_NUM, LVAL_ERR };
enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM };

typedef struct {
  int type;
  union {
    long num;
    int err;
  } data;
} lval;

lval lval_num(long x) {
  lval v;
  v.type = LVAL_NUM;
  v.data.num = x;
  return v;
}

lval lval_err(int x) {
  lval v;
  v.type = LVAL_ERR;
  v.data.err = x;
  return v;
}

void lval_print(lval v) {
  switch (v.type) {
    case LVAL_NUM:
      printf("%li", v.data.num);
      break;

    case LVAL_ERR:
      if (v.data.err == LERR_DIV_ZERO) {
        printf("Error: Division by zero.");
      }
      if (v.data.err == LERR_BAD_OP) {
        printf("Error: Invalid operator.");
      }
      if (v.data.err == LERR_BAD_NUM) {
        printf("Error: Invalid number.");
      }
      break;
  }
}

void lval_println(lval v) { lval_print(v); putchar('\n'); }

/* Evaluation logic. */
lval eval_op(char* op, lval x, lval y) {
  // Return any errors that occurred.
  if (x.type == LVAL_ERR) { return x; }
  if (y.type == LVAL_ERR) { return y; }

  if (strcmp(op, "+") == 0 || strcmp(op, "add") == 0) {
    return lval_num(x.data.num + y.data.num);
  }
  if (strcmp(op, "-") == 0 || strcmp(op, "sub") == 0) {
    return lval_num(x.data.num - y.data.num);
  }
  if (strcmp(op, "*") == 0 || strcmp(op, "mul") == 0) {
    return lval_num(x.data.num * y.data.num);
  }
  if (strcmp(op, "/") == 0 || strcmp(op, "div") == 0) {
    return y.data.num == 0
      ? lval_err(LERR_DIV_ZERO)
      : lval_num(x.data.num / y.data.num);
  }
  if (strcmp(op, "%") == 0 || strcmp(op, "mod") == 0) {
    return lval_num(x.data.num % y.data.num);
  }
  if (strcmp(op, "^") == 0 || strcmp(op, "pow") == 0) {
    return lval_num((long)pow(x.data.num, y.data.num));
  }

  if (strcmp(op, "max") == 0) {
    return x.data.num >= y.data.num ? x : y;
  }
  if (strcmp(op, "min") == 0) {
    return x.data.num < y.data.num ? x : y;
  }

  return lval_err(LERR_BAD_OP);
}

lval eval_unary_op(char* op, lval x) {
  if (strcmp(op, "-") == 0 || strcmp(op, "sub") == 0) {
    return lval_num(-x.data.num);
  }
  return lval_err(LERR_BAD_OP);
}

lval eval(mpc_ast_t* t) {
  // If node is a number just return it's value.
  if (strstr(t->tag, "number")) {
    errno = 0;
    long x = strtol(t->contents, NULL, 10);
    return errno != ERANGE ? lval_num(x) : lval_err(LERR_BAD_NUM);
  }

  char* op = t->children[1]->contents;

  lval x = eval(t->children[2]);

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

/* Main application. */
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
      lval result = eval(r.output);
      lval_println(result);
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
