/*
Copyright (c) 2013 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#include "kernel/type_checker_justification.h"
#include "kernel/metavar.h"

namespace lean {
abstraction_expected_justification_cell::~abstraction_expected_justification_cell() {
}

format abstraction_expected_justification_cell::pp_header(formatter const & fmt, options const & opts, optional<metavar_env> const & menv) const {
    unsigned indent = get_pp_indent(opts);
    format expr_fmt = fmt(instantiate_metavars(menv, m_ctx), instantiate_metavars(menv, m_expr), false, opts);
    format r;
    r += format(get_abst_str());
    r += format(" expected at");
    r += nest(indent, compose(line(), expr_fmt));
    return r;
}

void abstraction_expected_justification_cell::get_children(buffer<justification_cell*> &) const {
}

optional<expr> abstraction_expected_justification_cell::get_main_expr() const {
    return some_expr(m_expr);
}

char const * function_expected_justification_cell::get_abst_str() const { return "Function"; }

char const * pair_expected_justification_cell::get_abst_str() const { return "Pair"; }

app_type_match_justification_cell::~app_type_match_justification_cell() {
}

format app_type_match_justification_cell::pp_header(formatter const & fmt, options const & opts, optional<metavar_env> const & menv) const {
    unsigned indent = get_pp_indent(opts);
    expr new_app      = instantiate_metavars(menv, m_app);
    expr new_arg      = instantiate_metavars(menv, m_arg);
    expr new_arg_ty   = instantiate_metavars(menv, m_arg_ty);
    expr new_expected = instantiate_metavars(menv, m_expected);
    context new_ctx      = instantiate_metavars(menv, m_ctx);
    format r;
    r += format("In the application");
    r += nest(indent, compose(line(), fmt(new_ctx, new_app, false, opts)));
    r += compose(line(), format("the type of"));
    r += nest(indent, compose(line(), fmt(new_ctx, new_arg, false, opts)));
    r += compose(line(), format("is"));
    r += nest(indent, compose(line(), fmt(new_ctx, new_arg_ty, false, opts)));
    r += compose(line(), format("but is expected to be"));
    r+= nest(indent, compose(line(), fmt(new_ctx, new_expected, false, opts)));
    return r;
}

void app_type_match_justification_cell::get_children(buffer<justification_cell*> &) const {
}

optional<expr> app_type_match_justification_cell::get_main_expr() const {
    return some_expr(m_app);
}

pair_type_match_justification_cell::~pair_type_match_justification_cell() {
}

format pair_type_match_justification_cell::pp_header(formatter const & fmt, options const & opts, optional<metavar_env> const & menv) const {
    unsigned indent = get_pp_indent(opts);
    format r;
    r += format("Type of ");
    if (m_first)
        r += format("1st");
    else
        r += format("2nd");
    r += format(" component must be convertible to the expected type in the pair");
    expr new_expr = instantiate_metavars(menv, m_expr);
    r += nest(indent, compose(line(), fmt(instantiate_metavars(menv, m_ctx), new_expr, false, opts)));
    return r;
}

void pair_type_match_justification_cell::get_children(buffer<justification_cell*> &) const {
}

optional<expr> pair_type_match_justification_cell::get_main_expr() const {
    return some_expr(m_expr);
}

type_expected_justification_cell::~type_expected_justification_cell() {
}

format type_expected_justification_cell::pp_header(formatter const & fmt, options const & opts, optional<metavar_env> const & menv) const {
    unsigned indent = get_pp_indent(opts);
    format expr_fmt = fmt(instantiate_metavars(menv, m_ctx), instantiate_metavars(menv, m_type), false, opts);
    format r;
    r += format("Type expected at");
    r += nest(indent, compose(line(), expr_fmt));
    return r;
}

void type_expected_justification_cell::get_children(buffer<justification_cell*> &) const {
}

optional<expr> type_expected_justification_cell::get_main_expr() const {
    return some_expr(m_type);
}

def_type_match_justification_cell::~def_type_match_justification_cell() {
}

format def_type_match_justification_cell::pp_header(formatter const &, options const &, optional<metavar_env> const &) const {
    format r;
    r += format("Type of definition '");
    r += format(get_name());
    r += format("' must be convertible to expected type.");
    return r;
}

void def_type_match_justification_cell::get_children(buffer<justification_cell*> &) const {
}

optional<expr> def_type_match_justification_cell::get_main_expr() const {
    return some_expr(m_value);
}

type_match_justification_cell::~type_match_justification_cell() {
}

format type_match_justification_cell::pp_header(formatter const &, options const &, optional<metavar_env> const &) const {
    return format("Type of expression must be convertible to expected type.");
}

void type_match_justification_cell::get_children(buffer<justification_cell*> &) const {
}

optional<expr> type_match_justification_cell::get_main_expr() const {
    return some_expr(m_value);
}
}
