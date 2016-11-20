#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <editline/readline.h>
#include <editline/history.h>
#include "mpc.h"

/* Lisp value definitions. */
enum { LVAL_ERR, LVAL_NUM, LVAL_SYM, LVAL_SEXPR };

/* lval type definition. */
typedef struct lval {
  int type;
  union {
    /* Values */
    long num;
    char* sym;
    char* err;
    /* List of more sexprs. */
    struct {
      int count;
      struct lval** cell;
    } sexprs;
  } data;
} lval;

/* lval constructors and destructor */
lval* lval_num(long x) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->data.num = x;
  return v;
}

lval* lval_sym(char* s) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->data.sym = malloc(strlen(s) + 1);
  strcpy(v->data.sym, s);
  return v;
}

lval* lval_err(char* m) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_ERR;
  v->data.err = malloc(strlen(m) + 1);
  strcpy(v->data.err, m);
  return v;
}

lval* lval_sexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SEXPR;
  v->data.sexprs.count = 0;
  v->data.sexprs.cell = NULL;
  return v;
}

lval* lval_add(lval* v, lval* x) {
  v->data.sexprs.count++;
  v->data.sexprs.cell = realloc(v->data.sexprs.cell,
                                sizeof(lval*) * v->data.sexprs.count);
  v->data.sexprs.cell[v->data.sexprs.count - 1] = x;
  return v;
}

void lval_del(lval* v) {
  switch (v->type) {
    case LVAL_NUM: break;

    case LVAL_ERR: free(v->data.err); break;
    case LVAL_SYM: free(v->data.sym); break;

    case LVAL_SEXPR:
      /* Recursively delete sexprs with this one. */
      for (int i = 0; i < v->data.sexprs.count; i++) {
        lval_del(v->data.sexprs.cell[i]);
      }
      /* Free pointer list. */
      free(v->data.sexprs.cell);
      break;
  }
  /* Free the lval struct itself. */
  free(v);
}


/* Output */
void lval_print(lval* v);

void lval_expr_print(lval* v, char open, char close) {
  putchar(open);
  for (int i = 0; i < v->data.sexprs.count; i++) {
    lval_print(v->data.sexprs.cell[i]);
    if (i != (v->data.sexprs.count - 1)) {
      putchar(' ');
    }
  }
  putchar(close);
}

void lval_print(lval* v) {
  switch (v->type) {
    case LVAL_NUM: printf("%li", v->data.num); break;
    case LVAL_ERR: printf("Error: %s", v->data.err); break;
    case LVAL_SYM: printf("%s", v->data.sym); break;
    case LVAL_SEXPR: lval_expr_print(v, '(', ')'); break;
  }
}

void lval_println(lval* v) { lval_print(v); putchar('\n'); }


lval* lval_read_num(mpc_ast_t* t) {
  errno = 0;
  long x = strtol(t->contents, NULL, 10);
  return errno != ERANGE ?
    lval_num(x) : lval_err("Invalid number.");
}

lval* lval_read(mpc_ast_t* t) {
  if (strstr(t->tag, "number")) { return lval_read_num(t); }
  if (strstr(t->tag, "symbol")) { return lval_sym(t->contents); }

  lval* x = NULL;
  if (strcmp(t->tag, ">") == 0) { x = lval_sexpr(); }
  if (strstr(t->tag, "sexpr"))  { x = lval_sexpr(); }

  for (int i = 0; i < t->children_num; i++) {
    if (strcmp(t->children[i]->contents, "(") == 0) { continue; }
    if (strcmp(t->children[i]->contents, ")") == 0) { continue; }
    if (strcmp(t->children[i]->contents, "{") == 0) { continue; }
    if (strcmp(t->children[i]->contents, "}") == 0) { continue; }
    if (strcmp(t->children[i]->tag, "regex") == 0)  { continue; }
    x = lval_add(x, lval_read(t->children[i]));
  }
  return x;
}


/* Evaluation logic. */
/*lval eval_op(char* op, lval x, lval y) {
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
*/

/* Main application. */
int main(int argc, char** argv) {
  // Create some parsers.
  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Symbol = mpc_new("symbol");
  mpc_parser_t* Sexpr = mpc_new("sexpr");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lispy = mpc_new("lispy");

  // Define the language.
  mpca_lang(MPCA_LANG_DEFAULT,
    "                                                     \
      number    : /-?[0-9]+(\\.[0-9]+)?/  ;               \
      symbol    : '+' | '-' | '/' | '*' | '%' | '^' |     \
                  \"add\" | \"sub\" | \"mul\" | \"div\" | \
                  \"mod\" | \"pow\" | \"max\" | \"min\";  \
      sexpr     : '(' <expr>* ')' ;                       \
      expr      : <number> | <symbol> | <sexpr> ;         \
      lispy     : /^/ <expr>* /$/ ;                       \
    ",
    Number, Symbol, Sexpr, Expr, Lispy);

  puts("Lispy version 0.0.1");
  puts("Press ^C to exit.");

  while(1) {
    char* input = readline("lispy> ");

    add_history(input);

    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Lispy, &r)) {
      /* lval result = eval(r.output); */
      /* lval_println(result); */
      lval* x = lval_read(r.output);
      lval_println(x);
      lval_del(x);
      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }

    free(input);
  }

  mpc_cleanup(4, Number, Symbol, Sexpr, Expr, Lispy);
  return 0;
}
