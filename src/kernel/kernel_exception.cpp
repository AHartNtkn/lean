/*
Copyright (c) 2013 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#include <vector>
#include "kernel/kernel_exception.h"

namespace lean {
format kernel_exception::pp(formatter const &, options const &) const {
    return format(what());
}

format unknown_name_exception::pp(formatter const &, options const &) const {
    return format{format(what()), format(" '"), format(get_name()), format("'")};
}

format already_declared_exception::pp(formatter const &, options const &) const {
    return format{format("invalid object declaration, environment already has an object named '"), format(get_name()), format("'")};
}

format has_no_type_exception::pp(formatter const &, options const &) const {
    return format{format("object '"), format(const_name(m_const)), format("' has no type associated with it")};
}

format app_type_mismatch_exception::pp(formatter const & fmt, options const & opts) const {
    unsigned indent     = get_pp_indent(opts);
    context const & ctx = get_context();
    expr const & app    = get_application();
    expr const & arg    = get_arg();
    expr const & arg_ty = get_arg_type();
    expr const & exp_ty = get_expected_type();

    format app_fmt           = fmt(ctx, app, false, opts);
    format arg_fmt           = fmt(ctx, arg, false, opts);
    format arg_type_fmt      = fmt(ctx, arg_ty, false, opts);
    format expected_type_fmt = fmt(ctx, exp_ty, false, opts);
    format r;
    r += format("Type mismatch at application");
    r += nest(indent, compose(line(), app_fmt));
    r += compose(line(), format("Argument"));
    r += nest(indent, compose(line(), arg_fmt));
    r += compose(line(), format("is of type"));
    r += nest(indent, compose(line(), arg_type_fmt));
    r += compose(line(), format("but is expected to be of type"));
    r += nest(indent, compose(line(), expected_type_fmt));
    return r;
}

format pair_type_mismatch_exception::pp(formatter const & fmt, options const & opts) const {
    unsigned indent     = get_pp_indent(opts);
    context const & ctx = get_context();
    expr const & pair   = get_pair();
    format pair_fmt     = fmt(ctx, pair, false, opts);
    format r = format("type mismatch in the ");
    if (m_first)
        r += format("1st");
    else
        r += format("2nd");
    r += format(" argument of the pair");
    r += nest(indent, compose(line(), pair_fmt));
    r += compose(line(), format("Pair type:"));
    r += nest(indent, compose(line(), fmt(ctx, m_sig_type, false, opts)));
    r += line();
    r += format("Argument type:");
    r += nest(indent, compose(line(), fmt(ctx, m_arg_type, false, opts)));
    return r;
}

format abstraction_expected_exception::pp(formatter const & fmt, options const & opts) const {
    unsigned indent = get_pp_indent(opts);
    format expr_fmt = fmt(get_context(), get_expr(), false, opts);
    format r;
    r += format(what());
    r += format(" at");
    r += nest(indent, compose(line(), expr_fmt));
    return r;
}

format type_expected_exception::pp(formatter const & fmt, options const & opts) const {
    unsigned indent = get_pp_indent(opts);
    format expr_fmt = fmt(get_context(), get_expr(), false, opts);
    format r;
    r += format("type expected, got");
    r += nest(indent, compose(line(), expr_fmt));
    return r;
}

format def_type_mismatch_exception::pp(formatter const & fmt, options const & opts) const {
    unsigned indent     = get_pp_indent(opts);
    context const & ctx = get_context();
    format r;
    r += format("type mismatch at definition '");
    r += format(get_name());
    r += format("', expected type");
    r += nest(indent, compose(line(), fmt(ctx, get_type(), false, opts)));
    r += compose(line(), format("Given type:"));
    r += nest(indent, compose(line(), fmt(ctx, get_value_type(), false, opts)));
    return r;
}
}
