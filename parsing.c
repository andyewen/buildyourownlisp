#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <editline/readline.h>
#include <editline/history.h>
#include "mpc.h"

#define LASSERT(args, cond, err) \
  if (!(cond)) { lval_del(args); return lval_err(err); }

struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;

/* Lisp value definitions. */
enum { LVAL_ERR, LVAL_NUM, LVAL_SYM, LVAL_FUN, LVAL_SEXPR, LVAL_QEXPR };

typedef lval*(*lbuiltin)(lenv*, lval*);

/* lval type definition. */
typedef struct lval {
  int type;
  union {
    /* Values */
    long num;
    lbuiltin fun;
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

lval* lval_fun(lbuiltin func) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_FUN;
  v->data.fun = func;
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

lval* lval_qexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_QEXPR;
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

lval* lval_copy(lval* v) {
  lval* x = malloc(sizeof(lval));
  x->type = v->type;

  switch (x->type) {
    case LVAL_FUN: x->data.fun = v->data.fun; break;
    case LVAL_NUM: x->data.num = v->data.num; break;

    case LVAL_ERR:
      x->data.err = malloc(strlen(v->data.err) + 1);
      strcpy(x->data.err, v->data.err);
      break;

    case LVAL_SYM:
      x->data.sym = malloc(strlen(v->data.sym) + 1);
      strcpy(x->data.sym, v->data.sym);
      break;

    case LVAL_QEXPR:
    case LVAL_SEXPR:
      x->data.sexprs.count = v->data.sexprs.count;
      x->data.sexprs.cell = malloc(sizeof(lval*) * x->data.sexprs.count);
      for (int i = 0; i < x->data.sexprs.count; i++) {
        x->data.sexprs.cell[i] = lval_copy(v->data.sexprs.cell[i]);
      }
      break;
  }

  return x;
}

void lval_del(lval* v) {
  switch (v->type) {
    case LVAL_NUM: break;

    case LVAL_ERR: free(v->data.err); break;
    case LVAL_SYM: free(v->data.sym); break;

    case LVAL_FUN: break;

    case LVAL_QEXPR:
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

lval* lval_pop(lval* v, int i) {
  /* Find the item at "i" */
  lval* x = v->data.sexprs.cell[i];

  /* Shift memory after the item at "i" over the top. */
  memmove(&v->data.sexprs.cell[i], &v->data.sexprs.cell[i + 1],
          sizeof(lval*) * (v->data.sexprs.count - i - 1));

  /* Decrease count of items in the list. */
  v->data.sexprs.count--;

  /* Reallocate the memory used. */
  v->data.sexprs.cell = realloc(v->data.sexprs.cell,
                                sizeof(lval*) * v->data.sexprs.count);
  return x;
}

lval* lval_take(lval* v, int i) {
  lval* x = lval_pop(v, i);
  lval_del(v);
  return x;
}

lval* lval_join(lval* x, lval* y) {
  /* For each cell in \"y\" add it to \"x\". */
  while (y->data.sexprs.count) {
    x = lval_add(x, lval_pop(y, 0));
  }

  lval_del(y);
  return x;
}


/* lenv type definition */
typedef struct lenv {
  int count;
  char** syms;
  lval** vals;
} lenv;

lenv* lenv_new(void) {
  lenv* e = malloc(sizeof(lenv));
  e->count = 0;
  e->syms = NULL;
  e->vals = NULL;
  return e;
}

void lenv_del(lenv* e) {
  for (int i = 0; i < e->count; i++) {
    free(e->syms[i]);
    lval_del(e->vals[i]);
  }

  free(e->syms);
  free(e->vals);
  free(e);
}

lval* lenv_get(lenv* e, lval* k) {
  for (int i = 0; i < e->count; i++) {
    if (strcmp(e->syms[i], k->data.sym) == 0) {
      return lval_copy(e->vals[i]);
    }
  }
  return lval_err("Symbol doesn't exist.");
}

void lenv_put(lenv* e, lval* k, lval* v) {
  for (int i = 0; i < e->count; i++) {
    if (strcmp(e->syms[i], k->data.sym) == 0) {
      lval_del(e->vals[i]);
      e->vals[i] = lval_copy(v);
      return;
    }
  }

  e->count++;
  e->vals = realloc(e->vals, sizeof(lval*) * e->count);
  e->syms = realloc(e->syms, sizeof(char*) * e->count);

  e->vals[e->count - 1] = lval_copy(v);
  e->syms[e->count - 1] = malloc(strlen(k->data.sym) + 1);
  strcpy(e->syms[e->count - 1], k->data.sym);
}

void lenv_add_builtin(lenv* e, char* name, lbuiltin func) {
  lval* k = lval_sym(name);
  lval* v = lval_fun(func);

  lenv_put(e, k, v);

  lval_del(k);
  lval_del(v);
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
    case LVAL_FUN: printf("<function>"); break;
    case LVAL_ERR: printf("Error: %s", v->data.err); break;
    case LVAL_SYM: printf("%s", v->data.sym); break;
    case LVAL_SEXPR: lval_expr_print(v, '(', ')'); break;
    case LVAL_QEXPR: lval_expr_print(v, '{', '}'); break;
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

  if (strstr(t->tag, "qexpr")) { x = lval_qexpr(); }

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
lval* builtin_def(lenv* e, lval* a) {
  LASSERT(a, a->data.sexprs.cell[0]->type == LVAL_QEXPR,
          "Function \"def\" was passed incorrect type.");

  lval* syms = a->data.sexprs.cell[0];
  for (int i = 0; i < syms->data.sexprs.count; i++) {
    LASSERT(a, syms->data.sexprs.cell[i]->type == LVAL_SYM,
            "Function \"def\" cannot define non symbol.");
  }

  LASSERT(a, syms->data.sexprs.count == a->data.sexprs.count - 1,
          "Function \"def\"'s lists of symbols and values lengths "
          "were different!");

  for (int i = 0; i < syms->data.sexprs.count; i++) {
    lenv_put(e, syms->data.sexprs.cell[i], a->data.sexprs.cell[i + 1]);
  }

  lval_del(a);
  return lval_sexpr();
}

lval* builtin_op(lenv* e, lval* a, char* op) {
  /* Ensure all arguments are numbers. */
  for (int i = 0; i < a->data.sexprs.count; i++) {
    if (a->data.sexprs.cell[i]->type != LVAL_NUM) {
      lval_del(a);
      return lval_err("Cannot operate on non-number!");
    }
  }

  /* Pop the first element. */
  lval* x = lval_pop(a, 0);

  /* If no arguments and sub then perform unary negations. */
  if (a->data.sexprs.count == 0 &&
      strcmp(op, "-") == 0) {
    x->data.num = -x->data.num;
  }

  /* While there are still elements remaining. */
  while (a->data.sexprs.count > 0) {
    /* Pop the next element. */
    lval* y = lval_pop(a, 0);

    /* Basic math operators. */
    if (strcmp(op, "+") == 0) { x->data.num += y->data.num; }
    if (strcmp(op, "-") == 0) { x->data.num -= y->data.num; }
    if (strcmp(op, "*") == 0) { x->data.num *= y->data.num; }
    if (strcmp(op, "/") == 0) {
      if (y->data.num == 0) {
        lval_del(x);
        lval_del(y);
        x = lval_err("Division by zero!");
        break;
      }
      x->data.num /= y->data.num;
    }

    /* Extra math operators. */
    if (strcmp(op, "%") == 0) { x->data.num %= y->data.num; }
    if (strcmp(op, "^") == 0) { x->data.num = (long)pow(x->data.num, y->data.num); }

    lval_del(y);
  }

  lval_del(a);
  return x;
}

lval* builtin_add(lenv* e, lval* a) { return builtin_op(e, a, "+"); }
lval* builtin_sub(lenv* e, lval* a) { return builtin_op(e, a, "-"); }
lval* builtin_mul(lenv* e, lval* a) { return builtin_op(e, a, "*"); }
lval* builtin_div(lenv* e, lval* a) { return builtin_op(e, a, "/"); }
lval* builtin_mod(lenv* e, lval* a) { return builtin_op(e, a, "%"); }
lval* builtin_pow(lenv* e, lval* a) { return builtin_op(e, a, "^"); }

lval* builtin_head(lenv* e, lval* a) {
  /* Check error conditions. */
  LASSERT(a, a->data.sexprs.count == 1,
          "Function \"head\" was passed too many arguments!");
  LASSERT(a, a->data.sexprs.cell[0]->type == LVAL_QEXPR,
          "Function \"head\" was passed incorrect type!");
  LASSERT(a, a->data.sexprs.cell[0]->data.sexprs.count > 0,
          "Function \"head\" was passed {}!");

  lval* v = lval_take(a, 0);
  while (v->data.sexprs.count > 1) { lval_del(lval_pop(v, 1)); }
  return v;
}

lval* builtin_tail(lenv* e, lval* a) {
  /* Check error conditions. */
  LASSERT(a, a->data.sexprs.count == 1,
          "Function \"tail\" was passed too many arguments!");
  LASSERT(a, a->data.sexprs.cell[0]->type == LVAL_QEXPR,
          "Function \"tail\" was passed incorrect type!");
  LASSERT(a, a->data.sexprs.cell[0]->data.sexprs.count > 0,
          "Function \"tail\" was passed {}!");

  lval* v = lval_take(a, 0);
  lval_del(lval_pop(v, 0));
  return v;
}

lval* builtin_list(lenv* e, lval* a) {
  a->type = LVAL_QEXPR;
  return a;
}

lval* builtin_cons(lenv* e, lval* a) {
  LASSERT(a, a->data.sexprs.count == 2,
          "Function \"cons\" was passed the wrong number of arguments!");
  LASSERT(a, a->data.sexprs.cell[1]->type == LVAL_QEXPR,
          "Second argument to \"cons\" was not a q-expression!");

  lval* v = lval_add(lval_qexpr(), lval_pop(a, 0));
  lval* q = lval_take(a, 0);

  while (q->data.sexprs.count) {
    lval_add(v, lval_pop(q, 0));
  }

  lval_del(q);

  return v;
}

lval* builtin_len(lenv* e, lval* a) {
  LASSERT(a, a->data.sexprs.count == 1,
          "Function \"len\" was passed too many arguments!");
  LASSERT(a, a->data.sexprs.cell[0]->type == LVAL_QEXPR,
          "Function \"len\" passed incorrect type!");

  int n = a->data.sexprs.cell[0]->data.sexprs.count;
  lval_del(lval_pop(a, 0));

  a->type = LVAL_NUM;
  a->data.num = n;

  return a;
}

lval* builtin_init(lenv* e, lval* a) {
  LASSERT(a, a->data.sexprs.count == 1,
          "Function \"init\" was passed too many arguments!");
  LASSERT(a, a->data.sexprs.cell[0]->type == LVAL_QEXPR,
          "Function \"init\" passed incorrect type!");
  LASSERT(a, a->data.sexprs.cell[0]->data.sexprs.count > 0,
          "Function \"init\" was passed {}!");

  lval* v = lval_take(a, 0);
  lval_del(lval_pop(v, v->data.sexprs.count - 1));

  return v;
}

lval* lval_eval(lenv* e, lval* v);

lval* builtin_eval(lenv* e, lval* a) {
  LASSERT(a, a->data.sexprs.count == 1,
          "Function \"eval\" was passed too many arguments!");
  LASSERT(a, a->data.sexprs.cell[0]->type == LVAL_QEXPR,
          "Function \"eval\" passed incorrect type!");

  lval* x = lval_take(a, 0);
  x->type = LVAL_SEXPR;
  return lval_eval(e, x);
}

lval* builtin_join(lenv* e, lval* a) {
  for (int i = 0; i < a->data.sexprs.count; i++) {
    LASSERT(a, a->data.sexprs.cell[i]->type == LVAL_QEXPR,
            "Function \"join\" passed incorrect type.");
  }

  lval* x = lval_pop(a, 0);

  while (a->data.sexprs.count) {
    x = lval_join(x, lval_pop(a, 0));
  }

  lval_del(a);
  return x;
}


void lenv_add_builtins(lenv* e) {
  /* Special def function. */
  lenv_add_builtin(e, "def", builtin_def);

  /* List functions */
  lenv_add_builtin(e, "list", builtin_list);
  lenv_add_builtin(e, "head", builtin_head);
  lenv_add_builtin(e, "tail", builtin_tail);
  lenv_add_builtin(e, "eval", builtin_eval);
  lenv_add_builtin(e, "join", builtin_join);
  lenv_add_builtin(e, "cons", builtin_cons);
  lenv_add_builtin(e, "len",  builtin_len);
  lenv_add_builtin(e, "init", builtin_init);

  /* Aritmetic functions. */
  lenv_add_builtin(e, "+", builtin_add);
  lenv_add_builtin(e, "-", builtin_sub);
  lenv_add_builtin(e, "*", builtin_mul);
  lenv_add_builtin(e, "/", builtin_div);
  lenv_add_builtin(e, "%", builtin_mod);
  lenv_add_builtin(e, "^", builtin_pow);
}


lval* lval_eval_sexpr(lenv* e, lval* v) {
  /* Evaulate children. */
  for (int i = 0; i < v->data.sexprs.count; i++) {
    v->data.sexprs.cell[i] = lval_eval(e, v->data.sexprs.cell[i]);
  }

  /* Check for errors. */
  for (int i = 0; i < v->data.sexprs.count; i++) {
    if (v->data.sexprs.cell[i]->type == LVAL_ERR) { return lval_take(v, i); }
  }

  /* Empty expression */
  if (v->data.sexprs.count == 0) { return v; }

  /* Single expression */
  if (v->data.sexprs.count == 1) { return lval_take(v, 0); }

  /* Ensure first element is a function. */
  lval* f = lval_pop(v, 0);
  if (f->type != LVAL_FUN) {
    lval_del(f);
    lval_del(v);
    return lval_err("S-expression doesn't begin with a function!");
  }

  /* Call builtin with operator */
  lval* result = f->data.fun(e, v);
  lval_del(f);
  return result;
}

lval* lval_eval(lenv* e, lval* v) {
  /* If symbol return associated value */
  if (v->type == LVAL_SYM) {
    lval* x = lenv_get(e, v);
    lval_del(v);
    return x;
  }

  /* Evaluate s-expressions. */
  if (v->type == LVAL_SEXPR) { return lval_eval_sexpr(e, v); }

  /* All other lval types remain the same. */
  return v;
}


/* Main application. */
int main(int argc, char** argv) {
  // Create some parsers.
  mpc_parser_t* Number = mpc_new("number");
  mpc_parser_t* Symbol = mpc_new("symbol");
  mpc_parser_t* Sexpr = mpc_new("sexpr");
  mpc_parser_t* Qexpr = mpc_new("qexpr");
  mpc_parser_t* Expr = mpc_new("expr");
  mpc_parser_t* Lispy = mpc_new("lispy");

  // Define the language.
  mpca_lang(MPCA_LANG_DEFAULT,
    "                                                       \
      number    : /-?[0-9]+(\\.[0-9]+)?/  ;                 \
      symbol    : /[a-zA-Z0-9_+\\-*\\/\\^%\\\\=<>!&]+/ ;    \
      sexpr     : '(' <expr>* ')' ;                         \
      qexpr     : '{' <expr>* '}' ;                         \
      expr      : <number> | <symbol> | <sexpr> | <qexpr> ; \
      lispy     : /^/ <expr>* /$/ ;                         \
    ",
    Number, Symbol, Sexpr, Qexpr, Expr, Lispy);

  puts("Lispy version 0.0.1");
  puts("Press ^C to exit.");

  lenv* e = lenv_new();
  lenv_add_builtins(e);

  while(1) {
    char* input = readline("lispy> ");

    add_history(input);

    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Lispy, &r)) {
      /* lval result = eval(r.output); */
      /* lval_println(result); */
      lval* x = lval_eval(e, lval_read(r.output));
      lval_println(x);
      lval_del(x);
      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }

    free(input);
  }

  lenv_del(e);

  mpc_cleanup(4, Number, Symbol, Sexpr, Qexpr, Expr, Lispy);
  return 0;
}
