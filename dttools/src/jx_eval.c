/*
Copyright (C) 2016- The University of Notre Dame
This software is distributed under the GNU General Public License.
See the file COPYING for details.
*/

#include "jx_eval.h"
#include "debug.h"
#include "jx_function.h"
#include "jx_print.h"

#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

// FAILOP(jx_operator *op, struct jx *left, struct jx *right, const char *message)
// left, right, and message are evaluated exactly once
#define FAILOP(op, left, right, message) do { \
	assert(op); \
	assert(message); \
	struct jx *t = jx_operator(op->type, left, right); \
	char *s = jx_print_string(t); \
	struct jx *e = jx_error(jx_format( \
		"on line %d, %s: %s", \
		op->line, \
		s, \
		message \
	)); \
	jx_delete(t); \
	free(s); \
	return e; \
} while (false)

// FAILARR(struct jx *array, const char *message)
#define FAILARR(array, message) do { \
	assert(array); \
	assert(message); \
	return jx_error(jx_format( \
		"array reference on line %d: %s", \
		array->line, \
		message \
	)); \
} while (false)

static struct jx *jx_check_errors(struct jx *j);

static struct jx *jx_eval_null(struct jx_operator *op, struct jx *left, struct jx *right) {
	assert(op);
	switch(op->type) {
		case JX_OP_EQ:
			return jx_boolean(1);
		case JX_OP_NE:
			return jx_boolean(0);
		default: FAILOP(op, jx_null(), jx_null(), "unsupported operator on null");
	}
}

static struct jx *jx_eval_boolean(struct jx_operator *op, struct jx *left, struct jx *right) {
	int a = left ? left->u.boolean_value : 0;
	int b = right ? right->u.boolean_value : 0;

	assert(op);
	switch(op->type) {
		case JX_OP_EQ:
			return jx_boolean(a==b);
		case JX_OP_NE:
			return jx_boolean(a!=b);
		case JX_OP_AND:
			return jx_boolean(a&&b);
		case JX_OP_OR:
			return jx_boolean(a||b);
		case JX_OP_NOT:
			return jx_boolean(!b);
		default: FAILOP(op, jx_copy(left), jx_copy(right), "unsupported operator on boolean");
	}
}

static struct jx *jx_eval_integer(struct jx_operator *op, struct jx *left, struct jx *right) {
	jx_int_t a = left ? left->u.integer_value : 0;
	jx_int_t b = right ? right->u.integer_value : 0;

	assert(op);
	switch(op->type) {
		case JX_OP_EQ:
			return jx_boolean(a==b);
		case JX_OP_NE:
			return jx_boolean(a!=b);
		case JX_OP_LT:
			return jx_boolean(a<b);
		case JX_OP_LE:
			return jx_boolean(a<=b);
		case JX_OP_GT:
			return jx_boolean(a>b);
		case JX_OP_GE:
			return jx_boolean(a>=b);
		case JX_OP_ADD:
			return jx_integer(a+b);
		case JX_OP_SUB:
			return jx_integer(a-b);
		case JX_OP_MUL:
			return jx_integer(a*b);
		case JX_OP_DIV:
			if(b==0) FAILOP(op, jx_copy(left), jx_copy(right), "division by zero");
			return jx_integer(a/b);
		case JX_OP_MOD:
			if(b==0) FAILOP(op, jx_copy(left), jx_copy(right), "division by zero");
			return jx_integer(a%b);
		default: FAILOP(op, jx_copy(left), jx_copy(right), "unsupported operator on integer");
	}
}

static struct jx *jx_eval_double(struct jx_operator *op, struct jx *left, struct jx *right) {
	double a = left ? left->u.double_value : 0;
	double b = right ? right->u.double_value : 0;

	assert(op);
	switch(op->type) {
		case JX_OP_EQ:
			return jx_boolean(a==b);
		case JX_OP_NE:
			return jx_boolean(a!=b);
		case JX_OP_LT:
			return jx_boolean(a<b);
		case JX_OP_LE:
			return jx_boolean(a<=b);
		case JX_OP_GT:
			return jx_boolean(a>b);
		case JX_OP_GE:
			return jx_boolean(a>=b);
		case JX_OP_ADD:
			return jx_double(a+b);
		case JX_OP_SUB:
			return jx_double(a-b);
		case JX_OP_MUL:
			return jx_double(a*b);
		case JX_OP_DIV:
			if(b==0) FAILOP(op, jx_copy(left), jx_copy(right), "division by zero");
			return jx_double(a/b);
		case JX_OP_MOD:
			if(b==0) FAILOP(op, jx_copy(left), jx_copy(right), "division by zero");
			return jx_double((jx_int_t)a%(jx_int_t)b);
		default: FAILOP(op, jx_copy(left), jx_copy(right), "unsupported operator on double");
	}
}

static struct jx *jx_eval_string(struct jx_operator *op, struct jx *left, struct jx *right) {
	const char *a = left ? left->u.string_value : "";
	const char *b = right ? right->u.string_value : "";

	assert(op);
	switch(op->type) {
		case JX_OP_EQ:
			return jx_boolean(0==strcmp(a,b));
		case JX_OP_NE:
			return jx_boolean(0!=strcmp(a,b));
		case JX_OP_LT:
			return jx_boolean(strcmp(a,b)<0);
		case JX_OP_LE:
			return jx_boolean(strcmp(a,b)<=0);
		case JX_OP_GT:
			return jx_boolean(strcmp(a,b)>0);
		case JX_OP_GE:
			return jx_boolean(strcmp(a,b)>=0);
		case JX_OP_ADD:
			return jx_format("%s%s",a,b);
		default: FAILOP(op, jx_copy(left), jx_copy(right), "unsupported operator on string");
	}
}

static struct jx *jx_eval_array(struct jx_operator *op, struct jx *left, struct jx *right) {
	assert(op);
	if (!(left && right)) FAILOP(op, jx_copy(left), jx_copy(right), "missing arguments to array operator");

	switch(op->type) {
		case JX_OP_EQ:
			return jx_boolean(jx_equals(left, right));
		case JX_OP_NE:
			return jx_boolean(!jx_equals(left, right));
		case JX_OP_ADD:
			return jx_check_errors(jx_array_concat(jx_copy(left), jx_copy(right), NULL));
		default: FAILOP(op, jx_copy(left), jx_copy(right), "unsupported operator on array");
	}
}

static struct jx *jx_eval_call(struct jx *func, struct jx *args, struct jx *ctx) {
	assert(func);
	assert(args);
	assert(jx_istype(args, JX_ARRAY));
	assert(jx_istype(func, JX_SYMBOL)); //TODO

	if (!strcmp(func->u.symbol_name, "range")) {
		return jx_function_range(jx_eval(args, ctx));
	} else if (!strcmp(func->u.symbol_name, "format")) {
		return jx_function_format(jx_eval(args, ctx));
	} else if (!strcmp(func->u.symbol_name, "join")) {
		return jx_function_join(jx_eval(args, ctx));
	} else if (!strcmp(func->u.symbol_name, "ceil")) {
		return jx_function_ceil(jx_eval(args, ctx));
	} else if (!strcmp(func->u.symbol_name, "floor")) {
		return jx_function_floor(jx_eval(args, ctx));
	} else if (!strcmp(func->u.symbol_name, "basename")) {
		return jx_function_basename(jx_eval(args, ctx));
	} else if (!strcmp(func->u.symbol_name, "dirname")) {
		return jx_function_dirname(jx_eval(args, ctx));
	} else if (!strcmp(func->u.symbol_name, "listdir")) {
		return jx_function_listdir(jx_eval(args, ctx));
	} else if (!strcmp(func->u.symbol_name, "escape")) {
		return jx_function_escape(jx_eval(args, ctx));
	} else if (!strcmp(func->u.symbol_name, "template")) {
		return jx_function_template(jx_eval(args, ctx), ctx);
	} else if (!strcmp(func->u.symbol_name, "len")) {
		return jx_function_len(jx_eval(args, ctx));
	} else if (!strcmp(func->u.symbol_name, "fetch")) {
		return jx_function_fetch(jx_eval(args, ctx));
	} else if (!strcmp(func->u.symbol_name, "schema")) {
		return jx_function_schema(jx_eval(args, ctx));
	} else if (!strcmp(func->u.symbol_name, "like")) {
		return jx_function_like(jx_eval(args, ctx));

	/* select() and project() differ from the other functions in
	 * that they need deferred eval for specific arguments.
	 * We therefore just give them a copy of the args, and let
	 * them do the eval themselves.
	 *
	 * When adding new functions, you probably don't want to do this
	 */
	} else if (!strcmp(func->u.symbol_name, "select")) {
		return jx_function_select(jx_copy(args), ctx);
	} else if (!strcmp(func->u.symbol_name, "project")) {
		return jx_function_project(jx_copy(args), ctx);
	} else {
		return jx_error(jx_format(
			"on line %d, unknown function: %s",
			func->line,
			func->u.symbol_name
		));
	}
}

static struct jx *jx_eval_slice(struct jx *array, struct jx *slice) {
	assert(array);
	assert(slice);
	assert(slice->type == JX_OPERATOR);
	assert(slice->u.oper.type == JX_OP_SLICE);
	struct jx *left = slice->u.oper.left;
	struct jx *right = slice->u.oper.right;

	if (array->type != JX_ARRAY) {
		return jx_error(jx_format(
			"on line %d, only arrays support slicing",
			right->line
		));
	}
	if (left && left->type != JX_INTEGER) FAILOP((&slice->u.oper), jx_copy(left), jx_copy(right),
		"slice indices must be integers");
	if (right && right->type != JX_INTEGER) FAILOP((&slice->u.oper), jx_copy(left), jx_copy(right),
		"slice indices must be integers");

	struct jx *result = jx_array(NULL);
	int len = jx_array_length(array);

	// this is all SUPER inefficient
	jx_int_t start = left ? left->u.integer_value : 0;
	jx_int_t end = right ? right->u.integer_value : len;
	if (start < 0) start += len;
	if (end < 0) end += len;

	for (jx_int_t i = start; i < end; ++i) {
		struct jx *j = jx_array_index(array, i);
		if (j) jx_array_append(result, jx_copy(j));
	}

	return result;
}

/*
Handle a lookup operator, which has two valid cases:
1 - left is an object, right is a string, return the named item in the object.
2 - left is an array, right is an integer, return the nth item in the array.
*/

static struct jx * jx_eval_lookup( struct jx *left, struct jx *right )
{
	assert(right);
	if(left->type==JX_OBJECT && right->type==JX_STRING) {
		struct jx *r = jx_lookup(left,right->u.string_value);
		if(r) {
			return jx_copy(r);
		} else {
			return jx_error(jx_format(
				"lookup on line %d, key not found",
				right->line
			));
		}
	} else if(left->type==JX_ARRAY && right->type==JX_INTEGER) {
		struct jx_item *item = left->u.items;
		int count = right->u.integer_value;

		if (count < 0) {
			count += jx_array_length(left);
			if (count < 0) FAILARR(right, "index out of range");
		}

		while (count > 0) {
			if (!item) FAILARR(right, "index out of range");
			item = item->next;
			count--;
		}

		if (item) {
			return jx_copy(item->value);
		} else {
			FAILARR(right, "index out of range");
		}
	} else {
		char *s = jx_print_string(right);
		struct jx *err = jx_error(jx_format(
			"on line %d, %s: invalid type for lookup",
			right->line,
			s
		));
		free(s);
		return jx_error(err);

	}
}

/*
Type conversion rules:
Generally, operators are not meant to be applied to unequal types.
NULL is the result of an operator on two incompatible expressions.
Exception: integers are promoted to doubles as needed.
Exception: string+x or x+string for atomic types results in converting x to string and concatenating.
Exception: When x and y are incompatible types, x==y returns FALSE and x!=y returns TRUE.
Exception: The lookup operation can be "object[string]" or "array[integer]"
*/

static struct jx * jx_eval_operator( struct jx_operator *o, struct jx *context )
{
	if(!o) return 0;

	struct jx *left = NULL;
	struct jx *right = NULL;
	struct jx *result = NULL;

	if (o->type == JX_OP_CALL) return jx_eval_call(o->left, o->right, context);

	left = jx_eval(o->left,context);
	if (jx_istype(left, JX_ERROR)) {
		result = left;
		left = NULL;
		goto DONE;
	}
	right = jx_eval(o->right,context);
	if (jx_istype(right, JX_ERROR)) {
		result = right;
		right = NULL;
		goto DONE;
	}

	if (o->type == JX_OP_SLICE) return jx_operator(JX_OP_SLICE, left, right);

	if((left && right) && (left->type!=right->type) ) {
		if( left->type==JX_INTEGER && right->type==JX_DOUBLE) {
			struct jx *n = jx_double(left->u.integer_value);
			jx_delete(left);
			left = n;
		} else if( left->type==JX_DOUBLE && right->type==JX_INTEGER) {
			struct jx *n = jx_double(right->u.integer_value);
			jx_delete(right);
			right = n;
		} else if(o->type==JX_OP_EQ) {
			jx_delete(left);
			jx_delete(right);
			return jx_boolean(0);
		} else if(o->type==JX_OP_NE) {
			jx_delete(left);
			jx_delete(right);
			return jx_boolean(1);
		} else if(o->type==JX_OP_LOOKUP) {
			struct jx *r;
			if (right->type == JX_OPERATOR && right->u.oper.type == JX_OP_SLICE) {
				r = jx_eval_slice(left, right);
			} else {
				r = jx_eval_lookup(left, right);
			}
			jx_delete(left);
			jx_delete(right);
			return r;
		} else if(o->type==JX_OP_ADD && jx_istype(left,JX_STRING) && jx_isatomic(right) ) {

			char *str = jx_print_string(right);
			jx_delete(right);
			right = jx_string(str);
			free(str);
			/* fall through */

		} else if(o->type==JX_OP_ADD && jx_istype(right,JX_STRING) && jx_isatomic(left) ) {
			char *str = jx_print_string(left);
			jx_delete(left);
			left = jx_string(str);
			free(str);
			/* fall through */
 			
		} else {
			FAILOP(o, left, right, "mismatched types for operator");
		}
	}

	switch(right->type) {
		case JX_NULL:
			result = jx_eval_null(o, left, right);
			break;
		case JX_BOOLEAN:
			result = jx_eval_boolean(o, left, right);
			break;
		case JX_INTEGER:
			result = jx_eval_integer(o, left, right);
			break;
		case JX_DOUBLE:
			result = jx_eval_double(o, left, right);
			break;
		case JX_STRING:
			result = jx_eval_string(o, left, right);
			break;
		case JX_ARRAY:
			result = jx_eval_array(o, left, right);
			break;
		default: FAILOP(o, left, right, "rvalue does not support operators");
	}

DONE:
	jx_delete(left);
	jx_delete(right);

	return result;
}

static struct jx_item *jx_eval_comprehension(struct jx *body, struct jx_comprehension *comp, struct jx *context) {
	assert(body);
	assert(comp);

	struct jx *list = jx_eval(comp->elements, context);
	if (jx_istype(list, JX_ERROR)) return jx_item(list, NULL);
	if (!jx_istype(list, JX_ARRAY)) {
		return jx_item(jx_error(jx_format(
			"on line %d: list comprehension takes an array",
			comp->line
		)), NULL);
	}

	struct jx_item *result = NULL;
	struct jx_item *tail = NULL;

	struct jx *j = NULL;
	void *i = NULL;
	while ((j = jx_iterate_array(list, &i))) {
		struct jx *ctx = jx_copy(context);
		jx_insert(ctx, jx_string(comp->variable), jx_copy(j));
		if (comp->condition) {
			struct jx *cond = jx_eval(comp->condition, ctx);
			if (jx_istype(cond, JX_ERROR)) {
				jx_delete(ctx);
				jx_delete(list);
				jx_item_delete(result);
				return jx_item(cond, NULL);
			}
			if (!jx_istype(cond, JX_BOOLEAN)) {
				jx_delete(ctx);
				jx_delete(list);
				jx_item_delete(result);
				char *s = jx_print_string(cond);
				struct jx *err = jx_error(jx_format(
					"on line %d, %s: list comprehension condition takes a boolean",
					cond->line,
					s
				));
				free(s);
				return jx_item(err, NULL);
			}
			int ok = cond->u.boolean_value;
			jx_delete(cond);
			if (!ok) {
				jx_delete(ctx);
				continue;
			}
		}

		if (comp->next) {
			struct jx_item *val = jx_eval_comprehension(body, comp->next, ctx);
			jx_delete(ctx);
			if (result) {
				tail->next = val;
			} else {
				result = tail = val;
			}
			// this is going to go over the list LOTS of times
			// in the various recursive calls
			while (tail && tail->next) tail = tail->next;

		} else {
			struct jx *val = jx_eval(body, ctx);
			jx_delete(ctx);
			if (!val) {
				jx_delete(list);
				jx_item_delete(result);
				return NULL;
			}
			if (result) {
				tail->next = jx_item(val, NULL);
				tail = tail->next;
			} else {
				result = tail = jx_item(val, NULL);
			}
		}
	}

	jx_delete(list);
	return result;
}

static struct jx_pair *jx_eval_pair(struct jx_pair *pair, struct jx *context) {
	if (!pair) return 0;

	return jx_pair(
		jx_eval(pair->key, context),
		jx_eval(pair->value, context),
		jx_eval_pair(pair->next, context));
}

static struct jx_item *jx_eval_item(struct jx_item *item, struct jx *context) {
	if (!item) return NULL;

	if (item->comp) {
		struct jx_item *result = jx_eval_comprehension(item->value, item->comp, context);
		if (result) {
			struct jx_item *i = result;
			while (i->next) i = i->next;
			i->next = jx_eval_item(item->next, context);
			return result;
		} else {
			return jx_eval_item(item->next, context);
		}
	} else {
		return jx_item(jx_eval(item->value, context),
			jx_eval_item(item->next, context));
	}
}

static struct jx *jx_check_errors(struct jx *j)
{
	struct jx *err = NULL;
	switch (j->type) {
		case JX_ARRAY:
			for(struct jx_item *i = j->u.items; i; i = i->next) {
				if(jx_istype(i->value, JX_ERROR)) {
					err = jx_copy(i->value);
					jx_delete(j);
					return err;
				}
			}
			return j;
		case JX_OBJECT:
			for(struct jx_pair *p = j->u.pairs; p; p = p->next) {
				if (jx_istype(p->key, JX_ERROR)) err = jx_copy(p->key);
				if (!err && jx_istype(p->value, JX_ERROR)) err = jx_copy(p->value);
				if (err) {
					jx_delete(j);
					return err;
				}
			}
			return j;
		default:
			return j;
	}
}

struct jx * jx_eval( struct jx *j, struct jx *context )
{
	struct jx *result = NULL;
	if (!j) return NULL;

	if (context && !jx_istype(context, JX_OBJECT)) {
		return jx_error(jx_string("context must be an object"));
	}

	switch(j->type) {
		case JX_SYMBOL: {
			struct jx *t = jx_lookup(context, j->u.symbol_name);
			if (t) {
				result = jx_eval(t,context);
				break;
			} else {
				return jx_error(jx_format(
					"on line %d, %s: undefined symbol",
					j->line,
					j->u.symbol_name
				));
			}
		}
		case JX_DOUBLE:
		case JX_BOOLEAN:
		case JX_INTEGER:
		case JX_STRING:
		case JX_ERROR:
		case JX_NULL:
			result = jx_copy(j);
			break;
		case JX_ARRAY:
			result = jx_check_errors(jx_array(jx_eval_item(j->u.items, context)));
			break;
		case JX_OBJECT:
			result = jx_check_errors(jx_object(jx_eval_pair(j->u.pairs, context)));
			break;
		case JX_OPERATOR:
			result = jx_eval_operator(&j->u.oper, context);
			break;
	}

	return result;
}

struct jx * jx_eval_with_defines( struct jx *j, struct jx *context )
{
	// Find the define clause in j, if it exists.
	struct jx *defines = jx_lookup(j,"define");
	if(!defines) defines = jx_object(0);
	if(!context) context = jx_object(0);

	// Merge the context and defines into mcontext.
	struct jx *mcontext = jx_merge(defines,context,0);

	// Now use that to evaluate j.
	struct jx * result = jx_eval(j,mcontext);

	jx_delete(mcontext);
	return result;
}

/*vim: set noexpandtab tabstop=4: */
