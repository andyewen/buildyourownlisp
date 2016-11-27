#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <editline/readline.h>
#include <editline/history.h>
#include "mpc.h"

#define LASSERT(args, cond, fmt, ...) \
  if (!(cond)) { \
    lval* err = lval_err(fmt, ##__VA_ARGS__); \
    lval_del(args); \
    return err; \
  }

#define LASSERT_ARG_COUNT(v, c, fn) \
  LASSERT(v, v->data.sexprs.count == c, \
          "\"%s\" expected %i arguments, got %i.", \
          fn, c, v->data.sexprs.count);

#define LASSERT_ARG_TYPE(v, arg_num, expect_type, fn) {\
  int arg_type = v->data.sexprs.cell[(arg_num)]->type; \
  LASSERT(v, arg_type == expect_type, \
          "\"%s\" expected \"%s\", got \"%s\" for arg %i.", \
          fn, ltype_name(expect_type), ltype_name(arg_type), arg_num) };

#define LASSERT_ARG_NOT_EMPTY_LIST(v, arg_num, fn) \
  LASSERT(v, v->data.sexprs.cell[arg_num]->data.sexprs.count > 0, \
          "\"%s\" was passed {}.", fn);

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
    char* sym;
    char* err;

    /* A function */
    struct {
      lbuiltin builtin;
      lenv* env;
      lval* formals;
      lval* body;
    } fn;

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
  v->data.fn.builtin = func;
  return v;
}

lenv* lenv_new(void);

lval* lval_lambda(lval* formals, lval* body) {
  lval* v = malloc(sizeof(lval));

  v->type = LVAL_FUN;
  v->data.fn.builtin = NULL;
  v->data.fn.env = lenv_new();
  v->data.fn.formals = formals;
  v->data.fn.body = body;
  return v;
}

lval* lval_sym(char* s) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->data.sym = malloc(strlen(s) + 1);
  strcpy(v->data.sym, s);
  return v;
}

lval* lval_err(char* fmt, ...) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_ERR;

  va_list va;
  va_start(va, fmt);

  v->data.err = malloc(512);
  vsnprintf(v->data.err, 511, fmt, va);
  v->data.err = realloc(v->data.err, strlen(v->data.err) + 1);
  
  va_end(va);
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

lenv* lenv_copy(lenv*);

lval* lval_copy(lval* v) {
  lval* x = malloc(sizeof(lval));
  x->type = v->type;

  switch (x->type) {
    case LVAL_FUN:
      if (v->data.fn.builtin) {
        x->data.fn.builtin = v->data.fn.builtin;
      } else {
        x->data.fn.builtin = NULL;
        x->data.fn.env = lenv_copy(v->data.fn.env);
        x->data.fn.formals = lval_copy(v->data.fn.formals);
        x->data.fn.body = lval_copy(v->data.fn.body);
      }
      break;
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

void lenv_del(lenv*);

void lval_del(lval* v) {
  switch (v->type) {
    case LVAL_NUM: break;

    case LVAL_ERR: free(v->data.err); break;
    case LVAL_SYM: free(v->data.sym); break;

    case LVAL_FUN:
      if (!v->data.fn.builtin) {
        lenv_del(v->data.fn.env);
        lval_del(v->data.fn.formals);
        lval_del(v->data.fn.body);
      }
      break;

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
  lenv* parent;
  int count;
  char** syms;
  lval** vals;
} lenv;

lenv* lenv_new(void) {
  lenv* e = malloc(sizeof(lenv));
  e->parent = NULL;
  e->count = 0;
  e->syms = NULL;
  e->vals = NULL;
  return e;
}

lenv* lenv_copy(lenv* e) {
  lenv* n = malloc(sizeof(lenv));
  n->parent = e->parent;
  n->count = e->count;
  n->syms = malloc(sizeof(char*) * n->count);
  n->vals = malloc(sizeof(lval*) * n->count);
  
  for (int i = 0; i < n->count; i++) {
    n->syms[i] = malloc(strlen(e->syms[i]) + 1);
    strcpy(n->syms[i], e->syms[i]);
    n->vals[i] = lval_copy(e->vals[i]);
  }
  
  return n;
}

lval* lenv_get(lenv* e, lval* k) {
  for (int i = 0; i < e->count; i++) {
    if (strcmp(e->syms[i], k->data.sym) == 0) {
      return lval_copy(e->vals[i]);
    }
  }
  if (e->parent) {
    return lenv_get(e->parent, k);
  } else {
    return lval_err("Symbol \"%s\" doesn't exist.", k->data.sym);
  }
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

void lenv_def(lenv* e, lval* k, lval* v) {
  while (e->parent) { e = e->parent; }
  lenv_put(e, k, v);
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

    case LVAL_FUN:
      if (v->data.fn.builtin) {
        printf("<builtin>");
      } else {
        printf("(\\ "); lval_print(v->data.fn.formals);
        putchar(' '); lval_print(v->data.fn.body); putchar(')');
      }
      break;

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
char* ltype_name(int t) {
  switch (t) {
    case LVAL_FUN: return "Function";
    case LVAL_NUM: return "Number";
    case LVAL_ERR: return "Error";
    case LVAL_SYM: return "Symbol";
    case LVAL_SEXPR: return "S-Expression";
    case LVAL_QEXPR: return "Q-Expression";
    default: return "Unknown";
  }
}

lval* builtin_var(lenv* e, lval* a, char* func) {
  LASSERT_ARG_TYPE(a, 0, LVAL_QEXPR, "def");

  lval* syms = a->data.sexprs.cell[0];
  for (int i = 0; i < syms->data.sexprs.count; i++) {
    LASSERT(a, syms->data.sexprs.cell[i]->type == LVAL_SYM,
            "Function \"%s\" cannot define non symbol.", func);
  }

  LASSERT(a, syms->data.sexprs.count == a->data.sexprs.count - 1,
          "Function \"%s\"'s lists of symbols and values lengths "
          "were different. %i symbols and %i values were passed.",
          func, syms->data.sexprs.count, a->data.sexprs.count - 1);

  for (int i = 0; i < syms->data.sexprs.count; i++) {
    if (strcmp(func, "def") == 0) {
      lenv_def(e, syms->data.sexprs.cell[i], a->data.sexprs.cell[i + 1]);
    }
    if (strcmp(func, "=") == 0) {
      lenv_put(e, syms->data.sexprs.cell[i], a->data.sexprs.cell[i + 1]);
    }
  }

  lval_del(a);
  return lval_sexpr();
}

lval* builtin_def(lenv* e, lval* a) { return builtin_var(e, a, "def"); }
lval* builtin_put(lenv* e, lval* a) { return builtin_var(e, a, "="); }

lval* builtin_lambda(lenv* e, lval* a) {
  LASSERT_ARG_COUNT(a, 2, "\\");
  LASSERT_ARG_TYPE(a, 0, LVAL_QEXPR, "\\");
  LASSERT_ARG_TYPE(a, 1, LVAL_QEXPR, "\\");

  for (int i = 0; i < a->data.sexprs.cell[0]->data.sexprs.count; i++) {
    int t = a->data.sexprs.cell[0]->data.sexprs.cell[i]->type;
    LASSERT(a, t == LVAL_SYM, "Cannot define non symbol.");
  }

  lval* formals = lval_pop(a, 0);
  lval* body = lval_pop(a, 0);
  lval_del(a);

  return lval_lambda(formals, body);
}

lval* builtin_op(lenv* e, lval* a, char* op) {
  /* Ensure all arguments are numbers. */
  for (int i = 0; i < a->data.sexprs.count; i++) {
    LASSERT_ARG_TYPE(a, i, LVAL_NUM, op);
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
  LASSERT_ARG_COUNT(a, 1, "head");
  LASSERT_ARG_TYPE(a, 0, LVAL_QEXPR, "head");
  LASSERT_ARG_NOT_EMPTY_LIST(a, 0, "head");

  lval* v = lval_take(a, 0);
  while (v->data.sexprs.count > 1) { lval_del(lval_pop(v, 1)); }
  return v;
}

lval* builtin_tail(lenv* e, lval* a) {
  /* Check error conditions. */
  LASSERT_ARG_COUNT(a, 1, "tail");
  LASSERT_ARG_TYPE(a, 0, LVAL_QEXPR, "tail");
  LASSERT_ARG_NOT_EMPTY_LIST(a, 0, "tail");

  lval* v = lval_take(a, 0);
  lval_del(lval_pop(v, 0));
  return v;
}

lval* builtin_list(lenv* e, lval* a) {
  a->type = LVAL_QEXPR;
  return a;
}

lval* builtin_cons(lenv* e, lval* a) {
  LASSERT_ARG_COUNT(a, 2, "cons");
  LASSERT_ARG_TYPE(a, 1, LVAL_QEXPR, "cons");

  lval* v = lval_add(lval_qexpr(), lval_pop(a, 0));
  lval* q = lval_take(a, 0);

  while (q->data.sexprs.count) {
    lval_add(v, lval_pop(q, 0));
  }

  lval_del(q);

  return v;
}

lval* builtin_len(lenv* e, lval* a) {
  LASSERT_ARG_COUNT(a, 1, "len");
  LASSERT_ARG_TYPE(a, 0, LVAL_QEXPR, "len");

  int n = a->data.sexprs.cell[0]->data.sexprs.count;
  lval_del(lval_pop(a, 0));

  a->type = LVAL_NUM;
  a->data.num = n;

  return a;
}

lval* builtin_init(lenv* e, lval* a) {
  LASSERT_ARG_COUNT(a, 1, "init");
  LASSERT_ARG_TYPE(a, 0, LVAL_QEXPR, "init");
  LASSERT_ARG_NOT_EMPTY_LIST(a, 0, "init");

  lval* v = lval_take(a, 0);
  lval_del(lval_pop(v, v->data.sexprs.count - 1));

  return v;
}

lval* lval_eval(lenv* e, lval* v);

lval* builtin_eval(lenv* e, lval* a) {
  LASSERT_ARG_COUNT(a, 1, "eval");
  LASSERT_ARG_TYPE(a, 0, LVAL_QEXPR, "eval");

  lval* x = lval_take(a, 0);
  x->type = LVAL_SEXPR;
  return lval_eval(e, x);
}

lval* builtin_join(lenv* e, lval* a) {
  for (int i = 0; i < a->data.sexprs.count; i++) {
    LASSERT_ARG_TYPE(a, i, LVAL_QEXPR, "join");
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
  lenv_add_builtin(e, "=", builtin_put);

  /* Lambda builtin. */
  lenv_add_builtin(e, "\\", builtin_lambda);

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

lval* lval_call(lenv* e, lval* f, lval* a) {
  if (f->data.fn.builtin) { return f->data.fn.builtin(e, a); }

  int given = a->data.sexprs.count;
  int total = f->data.fn.formals->data.sexprs.count;

  // /* Assign each argument to each formal in order. */
  // for (int = 0; i < a->count; i++) {
  //   lenv_put(f->data.fn.env, f->data.fn.formals->data.sexprs.cell[i],
  //            a->data.sexprs.cell[i]);
  // }
  
  while (a->data.sexprs.count) {
    if (f->data.fn.formals->data.sexprs.count == 0) {
      lval_del(a);
      return lval_err("Function passed too many arguments. "
                      "Expected %i, got %i.", total, given);
    }

    lval* sym = lval_pop(f->data.fn.formals, 0);
    /* Special case to deal with '&' */
    if (strcmp(sym->data.sym, "&") == 0) {
      /* Ensure '&' is followed by another symbol. */
      if (f->data.fn.formals->data.sexprs.count != 1) {
        lval_del(a);
        return lval_err("Function format invalid. "
                        "Symbol \"&\" must be followed by a single symbol.");
      }

      /* Next formal should be bound to remaining arguments. */
      lval* nsym = lval_pop(f->data.fn.formals, 0);
      lenv_put(f->data.fn.env, nsym, builtin_list(e, a));
      lval_del(sym);
      lval_del(nsym);
      break;
    }

    lval* val = lval_pop(a, 0);

    lenv_put(f->data.fn.env, sym, val);

    lval_del(sym);
    lval_del(val);
  }

  lval_del(a);

  /* If "&" remains in formal list, bind to empty list. */
  if (f->data.fn.formals->data.sexprs.count > 0 &&
      strcmp(f->data.fn.formals->data.sexprs.cell[0]->data.sym, "&") == 0) {
    
    /* Check to ensure that & is not passed invalidly. */
    if (f->data.fn.formals->data.sexprs.count != 2) {
      return lval_err("Function format invalid. "
                      "Symbol \"&\" not followed by a single symbol.");
    }

    /* Pop and delete "&" symbol. */
    lval_del(lval_pop(f->data.fn.formals, 0));

    /* Pop next symbol and create empty list. */
    lval* sym = lval_pop(f->data.fn.formals, 0);
    lval* val = lval_qexpr();

    /* Bind to environment and delete. */
    lenv_put(f->data.fn.env, sym, val);
    lval_del(sym);
    lval_del(val);
  }

  if (f->data.fn.formals->data.sexprs.count == 0) {
    /* Evaluate if all arguments are bound. */
    f->data.fn.env->parent = e;
    return builtin_eval(f->data.fn.env, lval_add(lval_sexpr(), 
                                         lval_copy(f->data.fn.body)));
  } else {
    /* Return partially evaluated function. */
    return lval_copy(f);
  }
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
  lval* result = lval_call(e, f, v);
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
