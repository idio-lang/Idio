/*
 * Copyright (c) 2015, 2017, 2018, 2020, 2021 Ian Fitchet <idf(at)idio-lang.org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License.  You
 * may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/*
 * expander.c
 *
 */

#include "idio.h"

/*
 * Expanders (aka templates) live in their own little world...
 */

IDIO idio_expander_module = idio_S_nil;
IDIO idio_operator_module = idio_S_nil;

static IDIO idio_expander_list = idio_S_nil;
static IDIO idio_expander_list_src = idio_S_nil;
IDIO idio_expander_thread = idio_S_nil;

static IDIO idio_infix_operator_list = idio_S_nil;
static IDIO idio_infix_operator_group = idio_S_nil;
static IDIO idio_postfix_operator_list = idio_S_nil;
static IDIO idio_postfix_operator_group = idio_S_nil;

static IDIO idio_initial_expander (IDIO x, IDIO e);

static IDIO idio_evaluator_extend (IDIO name, IDIO primdata, IDIO module, const char *cpp__FILE__, int cpp__LINE__)
{
    IDIO_ASSERT (name);
    IDIO_ASSERT (primdata);
    IDIO_ASSERT (module);
    IDIO_TYPE_ASSERT (symbol, name);
    IDIO_TYPE_ASSERT (primitive, primdata);
    IDIO_TYPE_ASSERT (module, module);

    IDIO si = idio_module_find_symbol (name, module);
    if (idio_S_false != si) {
	IDIO fvi = IDIO_PAIR_HTT (si);
	IDIO pd = idio_vm_values_ref (IDIO_FIXNUM_VAL (fvi));

	if (IDIO_PRIMITIVE_F (primdata) != IDIO_PRIMITIVE_F (pd)) {
	    /*
	     * Tricky to generate a test case for this as it requires
	     * that we really do redefine a primitive.
	     *
	     * It should catch any developer foobars, though.
	     */

	    idio_meaning_error_static_redefine (name, IDIO_C_FUNC_LOCATION (), "evaluator value change", name, pd, primdata);

	    return idio_S_notreached;
	}

	return fvi;
    }

    idio_ai_t mci = idio_vm_constants_lookup_or_extend (name);
    IDIO fmci = idio_fixnum (mci);

    idio_ai_t gvi = idio_vm_extend_values ();
    IDIO fgvi = idio_fixnum (gvi);

    idio_module_set_vci (module, fmci, fmci);
    idio_module_set_vvi (module, fmci, fgvi);
    idio_module_set_symbol (name, IDIO_LIST5 (idio_S_predef, fmci, fgvi, module, idio_string_C ("idio_evaluator_extend")), module);

    /*
     * idio_module_set_symbol_value() is a bit sniffy about setting
     * predefs -- rightly so -- so go under the hood!
     */
    idio_vm_values_set (gvi, primdata);

    return fgvi;
}

IDIO idio_add_evaluation_primitive (idio_primitive_desc_t *d, IDIO module, const char *cpp__FILE__, int cpp__LINE__)
{
    IDIO_C_ASSERT (d);
    IDIO_ASSERT (module);
    IDIO_TYPE_ASSERT (module, module);

    IDIO primdata = idio_primitive_data (d);
    IDIO sym = idio_symbols_C_intern (d->name);
    return idio_evaluator_extend (sym, primdata, module, cpp__FILE__, cpp__LINE__);
}

void idio_add_expander_primitive (idio_primitive_desc_t *d, IDIO cs, const char *cpp__FILE__, int cpp__LINE__)
{
    IDIO_C_ASSERT (d);
    IDIO_ASSERT (cs);
    IDIO_TYPE_ASSERT (array, cs);

    idio_add_primitive (d, cs, cpp__FILE__, cpp__LINE__);
    IDIO primdata = idio_primitive_data (d);
    idio_install_expander_source (idio_symbols_C_intern (d->name), primdata, primdata);
}

void idio_add_infix_operator_primitive (idio_primitive_desc_t *d, int pri, const char *cpp__FILE__, int cpp__LINE__)
{
    idio_add_evaluation_primitive (d, idio_operator_module, cpp__FILE__, cpp__LINE__);
    IDIO primdata = idio_primitive_data (d);
    idio_install_infix_operator (idio_symbols_C_intern (d->name), primdata, pri);
}

void idio_add_postfix_operator_primitive (idio_primitive_desc_t *d, int pri, const char *cpp__FILE__, int cpp__LINE__)
{
    idio_add_evaluation_primitive (d, idio_operator_module, cpp__FILE__, cpp__LINE__);
    IDIO primdata = idio_primitive_data (d);
    idio_install_postfix_operator (idio_symbols_C_intern (d->name), primdata, pri);
}

IDIO idio_evaluate_expander_source (IDIO x, IDIO e)
{
    IDIO_ASSERT (x);
    IDIO_ASSERT (e);

    /* idio_debug ("evaluate-expander-source: in: x=%s", x); */
    /* idio_debug (" e=%s\n", e); */

    IDIO cthr = idio_thread_current_thread ();
    IDIO ethr = idio_expander_thread;
    /* ethr = cthr; */

    idio_thread_set_current_thread (ethr);

    idio_ai_t pc0 = IDIO_THREAD_PC (ethr);
    idio_vm_default_pc (ethr);

    idio_initial_expander (x, e);
    IDIO r = idio_vm_run_C (ethr, IDIO_THREAD_PC (ethr));

    idio_ai_t pc = IDIO_THREAD_PC (ethr);
    if (pc == (idio_vm_FINISH_pc + 1)) {
	IDIO_THREAD_PC (ethr) = pc0;
    }
    idio_thread_set_current_thread (cthr);

    /* idio_debug ("evaluate-expander-source: out: %s\n", r); */

    /* if (idio_S_nil == r) { */
    /* 	fprintf (stderr, "evaluate-expander: bad expansion?\n"); */
    /* } */

    return r;
}

/*
 * Poor man's let:
 *
 * 1. (let bindings body)
 * 2. (let name bindings body)
 *
 * =>
 *
 * 1. (apply (function (map ph bindings) body) (map pht bindings))
 * 2. (apply (letrec ((name (function (map ph bindings) body))) (map pht bindings)))
 */

IDIO_DEFINE_PRIMITIVE1_DS ("let", let, (IDIO e), "e", "\
poor man's let	\n\
")
{
    IDIO_ASSERT (e);

    IDIO_USER_TYPE_ASSERT (list, e);

    /*
     * e should be (let bindings body)
     */
    size_t nargs = idio_list_length (e);

    if (nargs < 3) {
	/*
	 * Test Case: expander-errors/let-1-arg.idio
	 *
	 * let 1
	 */
	idio_meaning_error_static_arity (e, IDIO_C_FUNC_LOCATION (), "(let bindings body)", e);

	return idio_S_notreached;
    }

    IDIO src = e;
    e = IDIO_PAIR_T (e);
    /*
     * e is now (bindings body)
     */

    IDIO bindings = IDIO_PAIR_H (e);
    IDIO vars = idio_S_nil;
    IDIO vals = idio_S_nil;
    IDIO name = idio_S_nil;
    if (idio_isa_symbol (bindings)) {
	name = bindings;
	e = IDIO_PAIR_T (e);
	bindings = IDIO_PAIR_H (e);
    }

    if (! idio_isa_pair (bindings)) {
	/*
	 * Test Case: expander-errors/let-invalid-bindings.idio
	 *
	 * let 1 2
	 */
	idio_meaning_error_param_type (src, IDIO_C_FUNC_LOCATION (), "bindings: pair", bindings);

	return idio_S_notreached;
    }

    while (idio_S_nil != bindings) {
	IDIO binding = IDIO_PAIR_H (bindings);

	IDIO value_expr = idio_S_undef;

	if (idio_isa_pair (binding)) {
	    vars = idio_pair (IDIO_PAIR_H (binding), vars);
	    if (idio_isa_pair (IDIO_PAIR_T (binding))) {
		value_expr = IDIO_PAIR_HT (binding);
	    }
	    vals = idio_pair (value_expr, vals);
	} else if (idio_isa_symbol (binding)) {
	    vars = idio_pair (binding, vars);
	    vals = idio_pair (value_expr, vals);
	} else {
	    /*
	     * Test Case: expander-errors/let-invalid-binding.idio
	     *
	     * let (1) 2
	     */
	    idio_meaning_error_param_type (src, IDIO_C_FUNC_LOCATION (), "binding: pair/symbol", binding);

	    return idio_S_notreached;
	}

	bindings = IDIO_PAIR_T (bindings);
    }

    e = IDIO_PAIR_T (e);
    /*
     * e is currently a list, either (body) or (body ...)
     *
     * body could be a single expression in which case we want the ph
     * of e (otherwise we will attempt to apply the result of body) or
     * multiple expressions in which case we want to prefix e with
     * begin
     *
     * it could be nil too...
     */

    if (idio_S_nil == IDIO_PAIR_T (e)) {
	e = IDIO_PAIR_H (e);
    } else {
	IDIO e2 = idio_list_append2 (IDIO_LIST1 (idio_S_begin), e);
	e = e2;
    }

    IDIO fn;

    if (idio_S_nil == name) {
	/*
	 * (let bindings body)
	 *
	 * This expression can be transformed into the implied
	 * execution of an anonymous function.  Which means we only
	 * need to supposrt the execution of functions to create local
	 * variables.
	 *
	 * The function is {body} with arguments that are the ph's of
	 * bindings and then the application of that function passes
	 * the pt's of bindings.
	 *
	 * (let ((a1 v1) (a2 v2)) ...)
	 *
	 * becomes
	 *
	 * ((function (a1 a2) ...) v1 v2)
	 */
	fn = IDIO_LIST3 (idio_S_function, idio_list_reverse (vars), e);

	IDIO appl = idio_list_append2 (IDIO_LIST1 (fn), idio_list_reverse (vals));
	idio_meaning_copy_src_properties (src, appl);

	return appl;
    } else {
	/*
	 * (let name bindings body)
	 *
	 * where {body} is massaged into a function called {name}
	 * whose arguments are the ph's of {bindings} and the function
	 * is initially called with the pt's of {bindings}
	 *
	 * The {body} can call {name}.  It is clearly(?) a {letrec}
	 * construct and is used to quickly define and invoke loops.
	 *
	 * (let loop ((a1 v1) (a2 v2)) ...)
	 *
	 * becomes
	 *
	 * (letrec ((loop (function (a1 a2) ...)))
	 *   (loop v1 v2))
	 *
	 * Those Schemers, eh?
	 */
	fn = IDIO_LIST3 (idio_S_function, idio_list_reverse (vars), e);

	IDIO appl = idio_list_append2 (IDIO_LIST1 (name), idio_list_reverse (vals));

	IDIO letrec = IDIO_LIST3 (idio_S_letrec,
				  IDIO_LIST1 (IDIO_LIST2 (name, fn)),
				  appl);
	idio_meaning_copy_src_properties (src, letrec);

	return letrec;
    }
}

/*
 * Poor man's let*:
 *
 * (let bindings body)
 *
 * =>
 *
 * (apply (function (map ph bindings) body) (map pt bindings))
 */

IDIO_DEFINE_PRIMITIVE1_DS ("let*", lets, (IDIO e), "e", "\
poor man's let*	\n\
")
{
    IDIO_ASSERT (e);

    IDIO_USER_TYPE_ASSERT (list, e);

    /*
     * e should be (let* bindings body)
     */
    size_t nargs = idio_list_length (e);

    if (nargs < 3) {
	/*
	 * Test Case: expander-errors/let*-1-arg.idio
	 *
	 * let* 1
	 */
	idio_meaning_error_static_arity (e, IDIO_C_FUNC_LOCATION (), "(let* bindings body)", e);

	return idio_S_notreached;
    }

    /* idio_debug ("let*: in %s\n", e); */

    IDIO src = e;
    e = IDIO_PAIR_T (e);
    /*
     * e is now (bindings body)
     */

    IDIO bindings = IDIO_PAIR_H (e);

    if (! idio_isa_pair (bindings)) {
	/*
	 * Test Case: expander-errors/let*-invalid-bindings.idio
	 *
	 * let 1 2
	 */
	idio_meaning_error_param_type (src, IDIO_C_FUNC_LOCATION (), "bindings: pair", bindings);

	return idio_S_notreached;
    }

    /*
     * NB reverse {bindings} so that when we walk over it below we
     * will create a nested set of {let}s in the right order
     *
     * Therefore {let} will do the validation of each {binding}.
     */
    bindings = idio_list_reverse (bindings);

    e = IDIO_PAIR_T (e);
    /*
     * e is currently a list, either (body) or (body ...)
     *
     * body could be a single expression in which case we want the
     * head of e (otherwise we will attempt to apply the result of
     * body) or multiple expressions in which case we want to prefix e
     * with begin
     *
     * it could be nil too...
     */

    if (idio_S_nil == IDIO_PAIR_T (e)) {
	e = IDIO_PAIR_H (e);
    } else {
	IDIO e2 = idio_list_append2 (IDIO_LIST1 (idio_S_begin), e);
	e = e2;
    }

    IDIO lets = e;
    while (idio_S_nil != bindings) {
	IDIO binding = IDIO_PAIR_H (bindings);

	lets = IDIO_LIST3 (idio_S_let,
			   IDIO_LIST1 (binding),
			   lets);

	bindings = IDIO_PAIR_T (bindings);
    }

    /* idio_debug ("let*: out %s\n", lets); */

    return lets;
}

/*
 * Poor man's letrec:
 *
 * (letrec bindings body)
 *
 * =>
 *
 * (apply (function (map ph bindings) body) (map pt bindings))
 */

IDIO_DEFINE_PRIMITIVE1_DS ("letrec", letrec, (IDIO e), "e", "\
poor man's letrec				\n\
")
{
    IDIO_ASSERT (e);

    IDIO_USER_TYPE_ASSERT (list, e);

    /*
     * e should be (letrec bindings body)
     */
    size_t nargs = idio_list_length (e);

    if (nargs < 3) {
	/*
	 * Test Case: expander-errors/letrec-1-arg.idio
	 *
	 * letrec 1
	 */
	idio_meaning_error_static_arity (e, IDIO_C_FUNC_LOCATION (), "(letrec bindings body)", e);

	return idio_S_notreached;
    }

    IDIO src = e;
    e = IDIO_PAIR_T (e);
    /*
     * e is now (bindings body)
     */

    IDIO bindings = IDIO_PAIR_H (e);
    IDIO vars = idio_S_nil;
    IDIO tmps = idio_S_nil;
    IDIO vals = idio_S_nil;

    if (! idio_isa_pair (bindings)) {
	/*
	 * Test Case: expander-errors/letrec-invalid-bindings.idio
	 *
	 * letrec 1 2
	 */
	idio_meaning_error_param_type (src, IDIO_C_FUNC_LOCATION (), "bindings: pair", bindings);

	return idio_S_notreached;
    }

    while (idio_S_nil != bindings) {
	IDIO binding = IDIO_PAIR_H (bindings);

	IDIO value_expr = idio_S_undef;

	if (idio_isa_pair (binding)) {
	    vars = idio_pair (IDIO_PAIR_H (binding), vars);
	    tmps = idio_pair (idio_gensym (NULL), tmps);
	    if (idio_isa_pair (IDIO_PAIR_T (binding))) {
		value_expr = IDIO_PAIR_HT (binding);
	    }
	    vals = idio_pair (value_expr, vals);
	} else if (idio_isa_symbol (binding)) {
	    vars = idio_pair (binding, vars);
	    tmps = idio_pair (idio_gensym (NULL), tmps);
	    vals = idio_pair (value_expr, vals);
	} else {
	    /*
	     * Test Case: expander-errors/letrec-invalid-binding.idio
	     *
	     * let (1) 2
	     */
	    idio_meaning_error_param_type (src, IDIO_C_FUNC_LOCATION (), "binding: pair/symbol", binding);

	    return idio_S_notreached;
	}

	bindings = IDIO_PAIR_T (bindings);
    }

    e = IDIO_PAIR_T (e);
    /*
     * e is now (body)
     */

    vars = idio_list_reverse (vars);
    tmps = idio_list_reverse (tmps);
    vals = idio_list_reverse (vals);

    IDIO ri = idio_S_nil;	/* init vars to #f */
    IDIO rt = idio_S_nil;	/* set tmps (in context of vars) */
    IDIO rs = idio_S_nil;	/* set vars */
    IDIO ns = vars;
    IDIO ts = tmps;
    IDIO vs = vals;
    while (idio_S_nil != ns) {
	ri = idio_pair (IDIO_LIST2 (IDIO_PAIR_H (ns), idio_S_undef), ri);
	rt = idio_pair (IDIO_LIST2 (IDIO_PAIR_H (ts), IDIO_PAIR_H (vs)), rt);
	rs = idio_pair (IDIO_LIST3 (idio_S_set, IDIO_PAIR_H (ns), IDIO_PAIR_H (ts)), rs);

	ns = IDIO_PAIR_T (ns);
	ts = IDIO_PAIR_T (ts);
	vs = IDIO_PAIR_T (vs);
    }
    ri = idio_list_reverse (ri);
    rt = idio_list_reverse (rt);
    rs = idio_list_reverse (rs);
    IDIO r = idio_list_append2 (idio_list_reverse (rs), e);

    IDIO rs_body = idio_list_append2 (IDIO_LIST1 (idio_S_begin),
				      idio_list_append2 (rs, e));

    IDIO let_rt= IDIO_LIST3 (idio_S_let,
			     rt,
			     rs_body);

    r = IDIO_LIST3 (idio_S_let,
		    ri,
		    let_rt);

    return r;
}

IDIO idio_expanderp (IDIO name)
{
    IDIO_ASSERT (name);

    if (! idio_isa_symbol (name)) {
      return idio_S_false;
    }

    IDIO expander_list = idio_module_symbol_value (idio_expander_list, idio_expander_module, idio_S_nil);

    IDIO assq = idio_list_assq (name, expander_list);

    if (idio_S_false != assq) {
	IDIO v = IDIO_PAIR_T (assq);
	if (idio_isa_pair (v)) {

	    IDIO lv = idio_module_current_symbol_value_recurse (name, idio_S_nil);

	    if (idio_isa_primitive (lv) ||
		idio_isa_closure (lv)) {
		IDIO_PAIR_T (assq) = lv;
	    } else {
		/*
		 * idio_module_current_symbol_value_recurse()
		 * nominally returns #unspec but when the template was
		 * defined we extended the VM's values which means
		 * we'll get back the default value, #undef
		 */
		if (idio_S_undef == lv) {
		    idio_debug ("WARNING: using %s in the same file it is defined in may not have the desired effects\n", name);
		} else {
		    idio_debug ("expander?: %s not an expander?\n", name);
		    fprintf (stderr, "name ISA %s\n", idio_type2string (name));
		    idio_debug ("lv=%s\n", lv);
		}
	    }
	} else {
	    /* fprintf (stderr, "expander?: isa %s\n", idio_type2string (v));  */
	}
    }

    return assq;
}

IDIO_DEFINE_PRIMITIVE1_DS ("expander?", expanderp, (IDIO o), "o", "\
is `o` an expander				\n\
						\n\
:param o: value to test				\n\
:return: an entry from the expanders table if	\n\
`o` is an expander or #f			\n\
")
{
    IDIO_ASSERT (o);

    return idio_expanderp (o);
}

static IDIO idio_application_expander (IDIO x, IDIO e)
{
    IDIO_ASSERT (x);
    IDIO_ASSERT (e);

    /*
     * (application-expander x e)
     * =>
     * (map* (function (y) (e y e)) x)
     *
     * map* is:
     */

    /* idio_debug ("application-expander: in x=%s", x); */
    /* idio_debug (" e=%s\n", e); */

    IDIO r = idio_S_nil;

    IDIO xh = IDIO_PAIR_H (x);
    if (idio_S_nil == xh) {
	return idio_S_nil;
    } else if (idio_isa_pair (xh)) {
	IDIO mph = idio_list_map_ph (x);
	IDIO mpt = idio_list_map_pt (x);

	if (idio_S_false == e) {
	    r = idio_pair (mph,
			   idio_application_expander (mpt, e));
	} else {
	    r = idio_pair (idio_initial_expander (mph, e),
			   idio_application_expander (mpt, e));
	}
    } else {
	if (idio_S_false == e) {
	    r = idio_pair (x, r);
	} else {
	    r = idio_pair (idio_initial_expander (x, e), r);
	}
    }

    /* idio_debug ("application-expander: r=%s\n", r); */

    return r;
}

static IDIO idio_initial_expander (IDIO x, IDIO e)
{
    IDIO_ASSERT (x);
    IDIO_ASSERT (e);

    /* idio_debug ("initial-expander: x=%s", x); */
    /* idio_debug (" e=%s\n", e); */

    if (! idio_isa_pair (x)) {
	return x;
    }

    IDIO xh = IDIO_PAIR_H (x);

    if (! idio_isa_symbol (xh)) {
	return idio_application_expander (x, e);
    } else {
	IDIO expander = idio_expanderp (xh);
	if (idio_S_false != expander) {
	    /*
	     * apply the macro!
	     *
	     * ((pt (assq functor *expander-list*)) x e)
	     */
	    return idio_apply (IDIO_PAIR_T (expander), IDIO_LIST3 (x, e, idio_S_nil));
	} else {
	    return idio_application_expander (x, e);
	}
    }
}

void idio_install_expander (IDIO id, IDIO proc)
{
    IDIO_ASSERT (id);
    IDIO_ASSERT (proc);

    IDIO_TYPE_ASSERT (symbol, id);

    IDIO el = idio_module_symbol_value (idio_expander_list, idio_expander_module, idio_S_nil);
    IDIO old = idio_list_assq (id, el);

    if (idio_S_false == old) {
	idio_module_set_symbol_value (idio_expander_list,
				      idio_pair (idio_pair (id, proc),
						 el),
				      idio_expander_module);
    } else {
	IDIO_PAIR_T (old) = proc;
    }
}

void idio_install_expander_source (IDIO id, IDIO proc, IDIO code)
{
    /* idio_debug ("install-expander-source: %s\n", id); */

    idio_install_expander (id, proc);

    IDIO els = idio_module_symbol_value (idio_expander_list_src, idio_expander_module, idio_S_nil);
    IDIO old = idio_list_assq (id, els);
    if (idio_S_false == old) {
	idio_module_set_symbol_value (idio_expander_list_src,
				      idio_pair (idio_pair (id, code),
						 els),
				      idio_expander_module);
    } else {
	IDIO_PAIR_T (old) = proc;
    }
}

IDIO idio_evaluate_expander_code (IDIO m, IDIO cs)
{
    IDIO_ASSERT (m);
    IDIO_ASSERT (cs);
    IDIO_TYPE_ASSERT (array, cs);

    /* idio_debug ("evaluate-expander-code: %s\n", m); */

    IDIO cthr = idio_thread_current_thread ();
    IDIO ethr = idio_expander_thread;

    idio_thread_set_current_thread (ethr);

    idio_ai_t pc0 = IDIO_THREAD_PC (ethr);
    idio_vm_default_pc (ethr);

    idio_ai_t cg_pc = idio_codegen (ethr, m, cs);
    IDIO r = idio_vm_run_C (ethr, cg_pc);

    idio_ai_t pc = IDIO_THREAD_PC (ethr);
    if (pc == (idio_vm_FINISH_pc + 1)) {
	IDIO_THREAD_PC (ethr) = pc0;
    }
    idio_thread_set_current_thread (cthr);

    /* idio_debug ("evaluate-expander-code: out: %s\n", r); */

    return r;
}

/* static void idio_push_expander (IDIO id, IDIO proc) */
/* { */
/*     idio_module_set_symbol_value (idio_expander_list, */
/* 				  idio_pair (idio_pair (id, proc), */
/* 					     idio_module_symbol_value (idio_expander_list, idio_expander_module)), */
/* 				  idio_expander_module); */
/* } */

/* static void idio_delete_expander (IDIO id) */
/* { */
/*     IDIO el = idio_module_symbol_value (idio_expander_list, idio_expander_module); */
/*     IDIO prv = idio_S_false; */

/*     for (;;) { */
/* 	if (idio_S_nil == el) { */
/* 	    return; */
/* 	} else if (idio_eqp (IDIO_PAIR_HH (el)), id)) { */
/* 	    if (idio_S_false == prv) { */
/* 		idio_module_set_symbol_value (idio_expander_list, */
/* 					      IDIO_PAIR_T (el), */
/* 					      idio_expander_module); */
/* 		return; */
/* 	    } else { */
/* 		IDIO_PAIR_T (prv) = IDIO_PAIR_T (el); */
/* 		return; */
/* 	    } */
/* 	} */
/* 	prv = el; */
/* 	el = IDIO_PAIR_T (el); */
/*     } */

/*     IDIO_C_ASSERT (0); */
/*     return; */
/* } */

IDIO idio_template_expand (IDIO e)
{
    IDIO_ASSERT (e);

    IDIO r = idio_evaluate_expander_source (e, idio_S_false);

    return r;
}

IDIO idio_template_expands (IDIO e)
{
    IDIO_ASSERT (e);

    for (;;) {
	IDIO new = idio_template_expand (e);

	if (idio_equalp (new, e)) {
	    return new;
	} else {
	    e = new;
	}
    }
}

void idio_install_operator (IDIO id, IDIO proc, int pri, IDIO ol_sym, IDIO og_sym)
{
    IDIO_ASSERT (id);
    IDIO_ASSERT (proc);
    IDIO_TYPE_ASSERT (symbol, id);
    IDIO_ASSERT (ol_sym);
    IDIO_ASSERT (og_sym);

    /* idio_debug ("op install %s", id); */
    /* idio_debug (" as %s\n", proc); */

    if (idio_S_undef == proc) {
	IDIO_C_ASSERT (0);
    }

    idio_module_set_symbol_value (id, proc, idio_operator_module);

    IDIO ol = idio_module_symbol_value (ol_sym, idio_operator_module, idio_S_nil);
    IDIO op = idio_list_assq (id, ol);

    if (idio_S_false == op) {
	idio_module_set_symbol_value (ol_sym,
				      idio_pair (idio_pair (id, proc),
						 ol),
				      idio_operator_module);
    } else {
	IDIO_PAIR_T (op) = proc;
    }

    IDIO og = idio_module_symbol_value (og_sym, idio_operator_module, idio_S_nil);

    IDIO fpri = idio_fixnum (pri);
    IDIO grp = idio_list_assq (fpri, og);

    if (idio_S_false == grp) {
	grp = IDIO_LIST1 (idio_pair (id, proc));

	if (idio_S_nil == og) {
	    idio_module_set_symbol_value (og_sym,
					  idio_pair (idio_pair (fpri, grp),
						     og),
					  idio_operator_module);
	} else {
	    IDIO c = og;
	    IDIO p = idio_S_nil;
	    while (idio_S_nil != c) {
		IDIO cpri = IDIO_PAIR_HH (c);
		if (IDIO_FIXNUM_VAL (cpri) < pri) {
		    if (idio_S_nil == p) {
			idio_module_set_symbol_value (og_sym,
						      idio_pair (idio_pair (fpri, grp),
								 c),
						      idio_operator_module);
		    } else {
			IDIO_PAIR_T (p) = idio_pair (idio_pair (fpri, grp),
						     c);
		    }
		    break;
		}
		p = c;
		c = IDIO_PAIR_T (c);
	    }
	    if (idio_S_nil == c) {
		IDIO_PAIR_T (p) = idio_pair (idio_pair (fpri, grp),
					     c);
	    }
	}
    } else {
	IDIO procs = IDIO_PAIR_T (grp);
	IDIO old = idio_list_assq (id, procs);

	if (idio_S_false == old) {
	    IDIO_PAIR_T (grp) = idio_pair (idio_pair (id, proc), procs);
	} else {
	    IDIO_PAIR_T (old) = proc;
	}
    }
}

void idio_copy_operator (IDIO new_id, IDIO fpri, IDIO old_id, IDIO ol_sym, IDIO og_sym)
{
    IDIO_ASSERT (new_id);
    IDIO_ASSERT (fpri);
    IDIO_ASSERT (old_id);

    IDIO_TYPE_ASSERT (symbol, new_id);
    IDIO_TYPE_ASSERT (symbol, old_id);

    IDIO ol = idio_module_symbol_value (ol_sym, idio_operator_module, idio_S_nil);

    IDIO new = idio_list_assq (new_id, ol);

    if (idio_S_false != new) {
	idio_meaning_evaluation_error (new_id, IDIO_C_FUNC_LOCATION (), "operator already defined", new_id);

	/* notreached */
	return;
    }

    IDIO old = idio_list_assq (old_id, ol);

    if (idio_S_false == old) {
	idio_meaning_evaluation_error (old_id, IDIO_C_FUNC_LOCATION (), "operator not defined", old_id);

	/* notreached */
	return;
    } else {
	idio_module_set_symbol_value (ol_sym,
				      idio_pair (idio_pair (new_id, IDIO_PAIR_T (old)),
						 ol),
				      idio_operator_module);

	intptr_t new_pri = IDIO_FIXNUM_VAL (fpri);
	IDIO og = idio_module_symbol_value (og_sym, idio_operator_module, idio_S_nil);
	IDIO grp = idio_list_assq (fpri, og);

	if (idio_S_false == grp) {
	    grp = IDIO_LIST1 (idio_pair (new_id, IDIO_PAIR_T (old)));

	    IDIO c = og;
	    IDIO p = idio_S_nil;
	    while (idio_S_nil != c) {
		IDIO cpri = IDIO_PAIR_HH (c);
		if (IDIO_FIXNUM_VAL (cpri) < new_pri) {
		    if (idio_S_nil == p) {
			idio_module_set_symbol_value (og_sym,
						      idio_pair (idio_pair (fpri, grp),
								 c),
						      idio_operator_module);
		    } else {
			IDIO_PAIR_T (p) = idio_pair (idio_pair (fpri, grp),
						     c);
		    }
		    break;
		}
		p = c;
		c = IDIO_PAIR_T (c);
	    }
	    if (idio_S_nil == c) {
		idio_module_set_symbol_value (og_sym,
					      idio_list_append2 (og,
								 IDIO_LIST1 (idio_pair (fpri, grp))),
					      idio_operator_module);
	    }
	} else {
	    IDIO procs = IDIO_PAIR_T (grp);
	    IDIO_PAIR_T (grp) = idio_pair (idio_pair (new_id, IDIO_PAIR_T (old)), procs);
	}
    }
}

static IDIO idio_evaluate_operator (IDIO n, IDIO e, IDIO b, IDIO a)
{
    IDIO_ASSERT (n);
    IDIO_ASSERT (e);
    IDIO_ASSERT (b);
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (pair, e);

    /* fprintf (stderr, "evaluate-operator:");   */
    /* idio_debug (" %s", b);   */
    /* idio_debug (" %s", n);   */
    /* idio_debug (" %s\n", a);   */

    IDIO func = IDIO_PAIR_T (e);
    if (! (idio_isa_closure (func) ||
	   idio_isa_primitive (func))) {
	/*
	 * Can we write a test case for this?  Is it possible to have
	 * created an operator whose functional part is not a
	 * function?
	 *
	 * Probably just a developer catch.
	 */
	idio_error_C ("operator: invalid code", IDIO_LIST2 (n, e), IDIO_C_FUNC_LOCATION ());

	return idio_S_notreached;
    }
    IDIO cthr = idio_thread_current_thread ();
    IDIO ethr = idio_expander_thread;
    /* ethr = cthr; */

    idio_thread_set_current_thread (ethr);

    idio_ai_t pc0 = IDIO_THREAD_PC (ethr);
    idio_vm_default_pc (ethr);

    idio_apply (func, IDIO_LIST3 (n, b, IDIO_LIST1 (a)));
#ifdef IDIO_VM_PROF
    struct timespec prim_t0;
    struct rusage prim_ru0;
    idio_vm_func_start (func, &prim_t0, &prim_ru0);
#endif
    IDIO r = idio_vm_run_C (ethr, IDIO_THREAD_PC (ethr));
#ifdef IDIO_VM_PROF
    struct timespec prim_te;
    struct rusage prim_rue;
    idio_vm_func_stop (func, &prim_te, &prim_rue);
    idio_vm_prim_time (func, &prim_t0, &prim_te, &prim_ru0, &prim_rue);
#endif

    idio_ai_t pc = IDIO_THREAD_PC (ethr);
    if (pc == (idio_vm_FINISH_pc + 1)) {
	IDIO_THREAD_PC (ethr) = pc0;
    }
    idio_thread_set_current_thread (cthr);

    /* idio_debug ("evaluate-operator: out: %s\n", r);      */

    return r;
}

void idio_install_infix_operator (IDIO id, IDIO proc, int pri)
{
    IDIO_ASSERT (id);
    IDIO_ASSERT (proc);
    IDIO_TYPE_ASSERT (symbol, id);

    /* idio_debug ("op install %s", id); */
    /* idio_debug (" as %s\n", proc); */

    idio_install_operator (id, proc, pri, idio_infix_operator_list, idio_infix_operator_group);
}

void idio_copy_infix_operator (IDIO new_id, IDIO fpri, IDIO old_id)
{
    IDIO_ASSERT (new_id);
    IDIO_ASSERT (fpri);
    IDIO_ASSERT (old_id);

    IDIO_TYPE_ASSERT (symbol, new_id);
    IDIO_TYPE_ASSERT (symbol, old_id);

    idio_copy_operator (new_id, fpri, old_id, idio_infix_operator_list, idio_infix_operator_group);
}

IDIO idio_evaluate_infix_operator_code (IDIO m, IDIO cs)
{
    IDIO_ASSERT (m);
    IDIO_ASSERT (cs);
    IDIO_TYPE_ASSERT (array, cs);

    /* idio_debug ("evaluate-infix-operator-code: %s\n", m);    */

    return idio_evaluate_expander_code (m, cs);
}

static IDIO idio_evaluate_infix_operator (IDIO n, IDIO e, IDIO b, IDIO a)
{
    IDIO_ASSERT (n);
    IDIO_ASSERT (e);
    IDIO_ASSERT (b);
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (pair, e);

    /* fprintf (stderr, "evaluate-infix-operator:");   */
    /* idio_debug (" %s", b);   */
    /* idio_debug (" %s", n);   */
    /* idio_debug (" %s\n", a);   */

    return idio_evaluate_operator (n, e, b, a);
}

IDIO idio_common_operatorp (IDIO name, IDIO ol_sym)
{
    IDIO_ASSERT (name);

    if (! idio_isa_symbol (name)) {
      return idio_S_false;
    }

    IDIO ol = idio_module_symbol_value (ol_sym, idio_operator_module, idio_S_nil);

    IDIO assq = idio_list_assq (name, ol);

    if (idio_S_false != assq) {
	IDIO v = IDIO_PAIR_T (assq);
	if (idio_isa_pair (v)) {
	    IDIO lv = idio_module_current_symbol_value_recurse (name, idio_S_nil);
	    if (idio_isa_primitive (lv) ||
		idio_isa_closure (lv)) {
		IDIO_PAIR_T (assq) = lv;
	    }
	} else {
	    /* fprintf (stderr, "operator?: isa %s\n", idio_type2string (v)); */
	}
    }

    return assq;
}

IDIO idio_infix_operatorp (IDIO name)
{
    IDIO_ASSERT (name);

    return idio_common_operatorp (name, idio_infix_operator_list);
}

IDIO_DEFINE_PRIMITIVE1_DS ("infix-operator?", infix_operatorp, (IDIO o), "o", "\
test if `o` is a infix operator		\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is a infix operator, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    return idio_infix_operatorp (o);
}

void idio_install_postfix_operator (IDIO id, IDIO proc, int pri)
{
    IDIO_ASSERT (id);
    IDIO_ASSERT (proc);
    IDIO_TYPE_ASSERT (symbol, id);

    /* idio_debug ("op install %s", id); */
    /* idio_debug (" as %s\n", proc); */

    idio_install_operator (id, proc, pri, idio_postfix_operator_list, idio_postfix_operator_group);
}

void idio_copy_postfix_operator (IDIO new_id, IDIO fpri, IDIO old_id)
{
    IDIO_ASSERT (new_id);
    IDIO_ASSERT (fpri);
    IDIO_ASSERT (old_id);

    IDIO_TYPE_ASSERT (symbol, new_id);
    IDIO_TYPE_ASSERT (symbol, old_id);

    idio_copy_operator (new_id, fpri, old_id, idio_postfix_operator_list, idio_postfix_operator_group);
}

IDIO idio_evaluate_postfix_operator_code (IDIO m, IDIO cs)
{
    IDIO_ASSERT (m);
    IDIO_ASSERT (cs);
    IDIO_TYPE_ASSERT (array, cs);

    /* idio_debug ("evaluate-postfix-operator-code: %s\n", m);    */

    return idio_evaluate_expander_code (m, cs);
}

static IDIO idio_evaluate_postfix_operator (IDIO n, IDIO e, IDIO b, IDIO a)
{
    IDIO_ASSERT (n);
    IDIO_ASSERT (e);
    IDIO_ASSERT (b);
    IDIO_ASSERT (a);
    IDIO_TYPE_ASSERT (pair, e);

    /* idio_debug ("evaluate-postfix-operator: in n %s", n);  */
    /* idio_debug (" b %s", b);  */
    /* idio_debug (" a %s\n", a);  */

    return idio_evaluate_operator (n, e, b, a);
}

IDIO idio_postfix_operatorp (IDIO name)
{
    IDIO_ASSERT (name);

    return idio_common_operatorp (name, idio_postfix_operator_list);
}

IDIO_DEFINE_PRIMITIVE1_DS ("postfix-operator?", postfix_operatorp, (IDIO o), "o", "\
test if `o` is a postfix operator		\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is a postfix operator, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    return idio_postfix_operatorp (o);
}

IDIO idio_operatorp (IDIO name)
{
    IDIO_ASSERT (name);

    if (! idio_isa_symbol (name)) {
      return idio_S_false;
    }

    IDIO assq = idio_infix_operatorp (name);
    if (idio_S_false == assq) {
	assq = idio_postfix_operatorp (name);
    }

    return assq;
}

IDIO_DEFINE_PRIMITIVE1_DS ("operator?", operatorp, (IDIO o), "o", "\
test if `o` is a operator		\n\
						\n\
:param o: object to test			\n\
						\n\
:return: #t if `o` is a operator, #f otherwise	\n\
")
{
    IDIO_ASSERT (o);

    return idio_operatorp (o);
}

IDIO idio_common_operator_expand (IDIO e, int depth, IDIO og)
{
    IDIO_ASSERT (e);
    IDIO_ASSERT (og);

    /* idio_debug ("operator-expand:   %s\n", e); */

    if (idio_isa_pair (e)) {
	while (idio_S_nil != og) {
	    IDIO ogp = IDIO_PAIR_H (og);
	    IDIO ops = IDIO_PAIR_T (ogp);

	    IDIO b = IDIO_LIST1 (IDIO_PAIR_H (e));
	    IDIO a = IDIO_PAIR_T (e);
	    while (idio_S_nil != a) {
		IDIO s = IDIO_PAIR_H (a);

		if (idio_isa_pair (s)) {
		    if (idio_S_escape == IDIO_PAIR_H (s)) {
			/* s = IDIO_PAIR_HT (s); */
		    }
		} else {
		    IDIO opex = idio_list_assq (s, ops);

		    if (idio_S_false != opex) {
			b = idio_evaluate_operator (s, opex, b, IDIO_PAIR_T (a));
			return idio_common_operator_expand (b, depth + 1, og);
			break;
		    }
		}
		b = idio_list_append2 (b, IDIO_LIST1 (s));
		a = IDIO_PAIR_T (a);
	    }

	    e = b;
	    og = IDIO_PAIR_T (og);
	}
    }

    return e;
}

IDIO idio_infix_operator_expand (IDIO e, int depth)
{
    IDIO_ASSERT (e);

    /* idio_debug ("infix-operator-expand:   %s\n", e); */

    IDIO og = idio_module_symbol_value (idio_infix_operator_group, idio_operator_module, idio_S_nil);
    return idio_common_operator_expand (e, depth, og);
}

IDIO_DEFINE_PRIMITIVE1_DS ("infix-operator-expand", infix_operator_expand, (IDIO l), "l", "XXX")
{
    IDIO_ASSERT (l);

    IDIO_USER_TYPE_ASSERT (list, l);

    return idio_infix_operator_expand (l, 0);
}

IDIO idio_postfix_operator_expand (IDIO e, int depth)
{
    IDIO_ASSERT (e);

    /* idio_debug ("postfix-operator-expand:   %s\n", e); */

    IDIO og = idio_module_symbol_value (idio_postfix_operator_group, idio_operator_module, idio_S_nil);
    return idio_common_operator_expand (e, depth, og);
}

IDIO_DEFINE_PRIMITIVE1_DS ("postfix-operator-expand", postfix_operator_expand, (IDIO l), "l", "XXX")
{
    IDIO_ASSERT (l);

    IDIO_USER_TYPE_ASSERT (list, l);

    return idio_postfix_operator_expand (l, 0);
}

IDIO idio_operator_expand (IDIO e, int depth)
{
    IDIO_ASSERT (e);

    /* idio_debug ("operator-expand:   %s\n", e); */

    IDIO r = idio_infix_operator_expand (e, depth);
    r = idio_postfix_operator_expand (r, depth);

    return r;
}

IDIO_DEFINE_PRIMITIVE1_DS ("operator-expand", operator_expand, (IDIO l), "l", "XXX")
{
    IDIO_ASSERT (l);

    IDIO_USER_TYPE_ASSERT (list, l);

    IDIO r = idio_operator_expand (l, 0);

    return r;
}

/*
 * Test Case: expander-errors/infix-too-many-before.idio
 *
 * a b := 1
 *
 * Note that we won't have a lexical object to use.
 */

/*
 * Test Case: expander-errors/infix-too-few-after.idio
 *
 * (a := )
 *
 * NB Need to apply it to force the end of list otherwise you'll get
 * EOF
 *
 * Note that we won't have a lexical object to use.
 */
#define IDIO_DEFINE_ASSIGNMENT_INFIX_OPERATOR(iname,cname)		\
    IDIO_DEFINE_INFIX_OPERATOR (iname, cname, (IDIO op, IDIO before, IDIO args)) \
    {									\
	IDIO_ASSERT (op);						\
	IDIO_ASSERT (before);						\
	IDIO_ASSERT (args);						\
									\
	if (idio_S_nil != IDIO_PAIR_T (before)) {			\
	    idio_meaning_error_static_arity (before, IDIO_C_FUNC_LOCATION (), "too many args before " #iname, IDIO_LIST2 (before, args)); \
	    return idio_S_notreached;					\
	}								\
    									\
	if (idio_S_nil != args) {					\
	    IDIO after = IDIO_PAIR_H (args);				\
	    if (idio_S_nil == after) {					\
		idio_meaning_error_static_arity (before, IDIO_C_FUNC_LOCATION (), "too few args after " #iname, args); \
		return idio_S_notreached;				\
	    }								\
	    if (idio_S_nil == IDIO_PAIR_T (after)) {			\
		after = IDIO_PAIR_H (after);				\
	    } else {							\
		IDIO r_a = idio_operator_expand (after, 0);		\
		after = r_a;						\
	    }								\
	    return IDIO_LIST3 (op, IDIO_PAIR_H (before), after);	\
	}								\
									\
	return idio_S_unspec;						\
    }

IDIO_DEFINE_ASSIGNMENT_INFIX_OPERATOR ("=", set);
IDIO_DEFINE_ASSIGNMENT_INFIX_OPERATOR (":=", colon_eq);
IDIO_DEFINE_ASSIGNMENT_INFIX_OPERATOR (":+", colon_plus);
IDIO_DEFINE_ASSIGNMENT_INFIX_OPERATOR (":*", colon_star);
IDIO_DEFINE_ASSIGNMENT_INFIX_OPERATOR (":~", colon_tilde);
IDIO_DEFINE_ASSIGNMENT_INFIX_OPERATOR (":$", colon_dollar);

void idio_expander_add_primitives ()
{
    IDIO_ADD_EXPANDER (let);
    IDIO_ADD_EXPANDER (lets);
    IDIO_ADD_EXPANDER (letrec);

    IDIO_ADD_PRIMITIVE (expanderp);
    IDIO_ADD_PRIMITIVE (infix_operatorp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_operator_module, infix_operator_expand);
    IDIO_ADD_PRIMITIVE (postfix_operatorp);
    IDIO_EXPORT_MODULE_PRIMITIVE (idio_operator_module, postfix_operator_expand);
    IDIO_ADD_PRIMITIVE (operatorp);
    IDIO_ADD_PRIMITIVE (operator_expand);

    IDIO_ADD_INFIX_OPERATOR (set, 1000);
    IDIO_ADD_INFIX_OPERATOR (colon_eq, 1000);
    IDIO_ADD_INFIX_OPERATOR (colon_plus, 1000);
    IDIO_ADD_INFIX_OPERATOR (colon_star, 1000);
    IDIO_ADD_INFIX_OPERATOR (colon_tilde, 1000);
    IDIO_ADD_INFIX_OPERATOR (colon_dollar, 1000);
}

void idio_init_expander ()
{
    idio_module_table_register (idio_expander_add_primitives, NULL);

    idio_expander_module = idio_module (idio_symbols_C_intern ("expander"));

    idio_expander_list = idio_symbols_C_intern ("*expander-list*");
    idio_module_set_symbol_value (idio_expander_list, idio_S_nil, idio_expander_module);

    idio_expander_list_src = idio_symbols_C_intern ("*expander-list-src*");
    idio_module_set_symbol_value (idio_expander_list_src, idio_S_nil, idio_expander_module);

    idio_operator_module = idio_module (idio_symbols_C_intern ("operator"));

    idio_infix_operator_list = idio_symbols_C_intern ("*infix-operator-list*");
    idio_module_set_symbol_value (idio_infix_operator_list, idio_S_nil, idio_operator_module);

    idio_infix_operator_group = idio_symbols_C_intern ("*infix-operator-group*");
    idio_module_set_symbol_value (idio_infix_operator_group, idio_S_nil, idio_operator_module);

    idio_postfix_operator_list = idio_symbols_C_intern ("*postfix-operator-list*");
    idio_module_set_symbol_value (idio_postfix_operator_list, idio_S_nil, idio_operator_module);

    idio_postfix_operator_group = idio_symbols_C_intern ("*postfix-operator-group*");
    idio_module_set_symbol_value (idio_postfix_operator_group, idio_S_nil, idio_operator_module);
}

