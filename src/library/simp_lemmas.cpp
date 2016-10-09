/*
Copyright (c) 2016 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#include <vector>
#include <algorithm>
#include "kernel/error_msgs.h"
#include "kernel/instantiate.h"
#include "kernel/for_each_fn.h"
#include "kernel/find_fn.h"
#include "library/simp_lemmas.h"
#include "library/constants.h"
#include "library/trace.h"
#include "library/util.h"
#include "library/reducible.h"
#include "library/attribute_manager.h"
#include "library/relation_manager.h"

namespace lean {
LEAN_THREAD_VALUE(bool, g_throw_ex, false);

struct simp_lemma_cell {
    simp_lemma_kind     m_kind;
    name                m_id;
    levels              m_umetas;
    list<expr>          m_emetas;
    list<bool>          m_instances;

    expr                m_lhs;
    expr                m_rhs;
    unsigned            m_priority;
    MK_LEAN_RC(); // Declare m_rc counter
    void dealloc();
    simp_lemma_cell():m_kind(simp_lemma_kind::Simp) {}
    simp_lemma_cell(simp_lemma_kind k,
                    name const & id, levels const & umetas, list<expr> const & emetas,
                    list<bool> const & instances, expr const & lhs, expr const & rhs,
                    unsigned priority):
        m_kind(k), m_id(id), m_umetas(umetas), m_emetas(emetas), m_instances(instances),
        m_lhs(lhs), m_rhs(rhs), m_priority(priority), m_rc(0) {}
};

struct simp_lemma_with_proof_cell : public simp_lemma_cell {
    expr m_proof;
    simp_lemma_with_proof_cell(simp_lemma_kind k,
                               name const & id, levels const & umetas, list<expr> const & emetas,
                               list<bool> const & instances, expr const & lhs, expr const & rhs,
                               expr const & proof, unsigned priority):
        simp_lemma_cell(k, id, umetas, emetas, instances, lhs, rhs, priority),
        m_proof(proof) {
    }
};

struct regular_simp_lemma_cell : public simp_lemma_with_proof_cell {
    bool m_is_permutation;
    regular_simp_lemma_cell(name const & id, levels const & umetas, list<expr> const & emetas,
                            list<bool> const & instances, expr const & lhs, expr const & rhs,
                            expr const & proof, bool is_perm, unsigned priority):
        simp_lemma_with_proof_cell(simp_lemma_kind::Simp, id, umetas, emetas, instances, lhs, rhs, proof, priority),
        m_is_permutation(is_perm) {
    }
};

struct congr_lemma_cell : public simp_lemma_with_proof_cell {
    list<expr> m_congr_hyps;
    congr_lemma_cell(name const & id, levels const & umetas, list<expr> const & emetas,
                     list<bool> const & instances, expr const & lhs, expr const & rhs,
                     expr const & proof, list<expr> const & congr_hyps, unsigned priority):
        simp_lemma_with_proof_cell(simp_lemma_kind::Congr, id, umetas, emetas, instances, lhs, rhs, proof, priority),
        m_congr_hyps(congr_hyps) {
    }
};

void simp_lemma_cell::dealloc() {
    switch (m_kind) {
    case simp_lemma_kind::Simp:
        delete static_cast<simp_lemma_with_proof_cell*>(this);
        break;
    case simp_lemma_kind::Congr:
        delete static_cast<congr_lemma_cell*>(this);
        break;
    case simp_lemma_kind::Refl:
        delete this;
        break;
    }
}

static simp_lemma_cell * g_dummy = nullptr;

simp_lemma::simp_lemma():simp_lemma(g_dummy) {
}

simp_lemma::simp_lemma(simp_lemma_cell * ptr):m_ptr(ptr) {
    if (m_ptr) m_ptr->inc_ref();
}

simp_lemma_cell * simp_lemma::steal_ptr() {
    simp_lemma_cell * r = m_ptr; m_ptr = nullptr; return r;
}

simp_lemma::simp_lemma(simp_lemma const & s):m_ptr(s.m_ptr) {
    if (m_ptr) m_ptr->inc_ref();
}

simp_lemma::simp_lemma(simp_lemma && s):m_ptr(s.m_ptr) {
    s.m_ptr = nullptr;
}

simp_lemma::~simp_lemma() {
    if (m_ptr) m_ptr->dec_ref();
}

simp_lemma & simp_lemma::operator=(simp_lemma const & s) {
    LEAN_COPY_REF(s);
}

simp_lemma & simp_lemma::operator=(simp_lemma && s) {
    LEAN_MOVE_REF(s);
}

simp_lemma_kind simp_lemma::kind() const {
    return m_ptr->m_kind;
}

name const & simp_lemma::get_id() const {
    return m_ptr->m_id;
}

unsigned simp_lemma::get_num_umeta() const {
    return length(m_ptr->m_umetas);
}

unsigned simp_lemma::get_num_emeta() const {
    return length(m_ptr->m_emetas);
}

list<expr> const & simp_lemma::get_emetas() const {
    return m_ptr->m_emetas;
}

list<bool> const & simp_lemma::get_instances() const {
    return m_ptr->m_instances;
}

unsigned simp_lemma::get_priority() const {
    return m_ptr->m_priority;
}

expr const & simp_lemma::get_lhs() const {
    return m_ptr->m_lhs;
}

expr const & simp_lemma::get_rhs() const {
    return m_ptr->m_rhs;
}

bool simp_lemma::is_permutation() const {
    return
        kind() == simp_lemma_kind::Simp &&
        static_cast<regular_simp_lemma_cell const *>(m_ptr)->m_is_permutation;
}

expr const & simp_lemma::get_proof() const {
    lean_assert(kind() == simp_lemma_kind::Congr || kind() == simp_lemma_kind::Simp);
    return static_cast<simp_lemma_with_proof_cell const *>(m_ptr)->m_proof;
}

list<expr> const & simp_lemma::get_congr_hyps() const {
    lean_assert(kind() == simp_lemma_kind::Congr);
    return static_cast<congr_lemma_cell const *>(m_ptr)->m_congr_hyps;
}

format simp_lemma::pp(formatter const & fmt) const {
    format r;
    r += format("[") + format(get_id()) + format("]") + space();
    r += format("#") + format(get_num_emeta());
    if (get_priority() != LEAN_DEFAULT_PRIORITY)
        r += space() + paren(format(get_priority()));
    if (is_refl())
        r += space() + format("defeq");
    if (is_permutation())
        r += space() + format("perm");
    if (kind() == simp_lemma_kind::Congr) {
        format r1;
        for (expr const & h : get_congr_hyps()) {
            r1 += space() + paren(fmt(mlocal_type(h)));
        }
        r += group(r1);
    }
    format r1 = comma() + space() + fmt(get_lhs());
    r1       += space() + format("↦") + pp_indent_expr(fmt, get_rhs());
    r += group(r1);
    return r;
}

bool operator==(simp_lemma const & r1, simp_lemma const & r2) {
    if (r1.kind() != r2.kind() || r1.get_lhs() != r2.get_lhs() || r1.get_rhs() != r2.get_rhs())
        return false;
    if (r1.kind() == simp_lemma_kind::Congr &&
        r1.get_congr_hyps() != r2.get_congr_hyps())
        return false;
    return true;
}

simp_lemma mk_simp_lemma(name const & id, levels const & umetas, list<expr> const & emetas,
                         list<bool> const & instances, expr const & lhs, expr const & rhs,
                         expr const & proof, bool is_perm, unsigned priority) {
    return simp_lemma(new regular_simp_lemma_cell(id, umetas, emetas, instances, lhs, rhs, proof, is_perm, priority));
}

simp_lemma mk_rfl_lemma(name const & id, levels const & umetas, list<expr> const & emetas,
                        list<bool> const & instances, expr const & lhs, expr const & rhs,
                        unsigned priority) {
    return simp_lemma(new simp_lemma_cell(simp_lemma_kind::Refl, id, umetas, emetas,
                                          instances, lhs, rhs, priority));
}

simp_lemma mk_congr_lemma(name const & id, levels const & umetas, list<expr> const & emetas,
                          list<bool> const & instances, expr const & lhs, expr const & rhs,
                          expr const & proof, list<expr> const & congr_hyps, unsigned priority) {
    return simp_lemma(new congr_lemma_cell(id, umetas, emetas, instances, lhs, rhs, proof, congr_hyps, priority));
}

simp_lemmas_for::simp_lemmas_for():
    m_eqv(get_eq_name()) {}

simp_lemmas_for::simp_lemmas_for(name const & eqv):
    m_eqv(eqv) {}

void simp_lemmas_for::insert(simp_lemma const & r) {
    if (r.is_congr())
        m_congr_set.insert(r.get_lhs(), r);
    else
        m_simp_set.insert(r.get_lhs(), r);
}

void simp_lemmas_for::erase(simp_lemma const & r) {
    if (r.is_congr())
        m_congr_set.erase(r.get_lhs(), r);
    else
        m_simp_set.erase(r.get_lhs(), r);
}

list<simp_lemma> const * simp_lemmas_for::find(head_index const & h) const {
    return m_simp_set.find(h);
}

void simp_lemmas_for::for_each(std::function<void(simp_lemma const &)> const & fn) const {
    m_simp_set.for_each_entry([&](head_index const &, simp_lemma const & r) { fn(r); });
}

list<simp_lemma> const * simp_lemmas_for::find_congr(head_index const & h) const {
    return m_congr_set.find(h);
}

void simp_lemmas_for::for_each_congr(std::function<void(simp_lemma const &)> const & fn) const {
    m_congr_set.for_each_entry([&](head_index const &, simp_lemma const & r) { fn(r); });
}

static void erase_core(simp_lemma_set & S, name_set const & ids) {
    // This method is not very smart and doesn't use any indexing or caching.
    // So, it may be a bottleneck in the future
    buffer<simp_lemma> to_delete;
    S.for_each_entry([&](head_index const &, simp_lemma const & r) {
            if (ids.contains(r.get_id())) {
                to_delete.push_back(r);
            }
        });
    for (simp_lemma const & r : to_delete) {
        S.erase(r.get_lhs(), r);
    }
}

void simp_lemmas_for::erase(name_set const & ids) {
    erase_core(m_simp_set, ids);
    erase_core(m_congr_set, ids);
}

void simp_lemmas_for::erase(buffer<name> const & ids) {
    erase(to_name_set(ids));
}

void simp_lemmas::insert(name const & eqv, simp_lemma const & r) {
    simp_lemmas_for s(eqv);
    if (auto const * curr = m_sets.find(eqv)) {
        s = *curr;
    }
    s.insert(r);
    m_sets.insert(eqv, s);
}

void simp_lemmas::erase(name const & eqv, simp_lemma const & r) {
    if (auto const * curr = m_sets.find(eqv)) {
        simp_lemmas_for s = *curr;
        s.erase(r);
        if (s.empty())
            m_sets.erase(eqv);
        else
            m_sets.insert(eqv, s);
    }
}

void simp_lemmas::get_relations(buffer<name> & rs) const {
    m_sets.for_each([&](name const & r, simp_lemmas_for const &) {
            rs.push_back(r);
        });
}

void simp_lemmas::erase(name_set const & ids) {
    name_map<simp_lemmas_for> new_sets;
    m_sets.for_each([&](name const & n, simp_lemmas_for const & s) {
            simp_lemmas_for new_s = s;
            new_s.erase(ids);
            new_sets.insert(n, new_s);
        });
    m_sets = new_sets;
}

void simp_lemmas::erase(buffer<name> const & ids) {
    erase(to_name_set(ids));
}

simp_lemmas_for const * simp_lemmas::find(name const & eqv) const {
    return m_sets.find(eqv);
}

list<simp_lemma> const * simp_lemmas::find(name const & eqv, head_index const & h) const {
    if (auto const * s = m_sets.find(eqv))
        return s->find(h);
    return nullptr;
}

list<simp_lemma> const * simp_lemmas::find_congr(name const & eqv, head_index const & h) const {
    if (auto const * s = m_sets.find(eqv))
        return s->find_congr(h);
    return nullptr;
}

void simp_lemmas::for_each(std::function<void(name const &, simp_lemma const &)> const & fn) const {
    m_sets.for_each([&](name const & eqv, simp_lemmas_for const & s) {
            s.for_each([&](simp_lemma const & r) {
                    fn(eqv, r);
                });
        });
}

void simp_lemmas::for_each_congr(std::function<void(name const &, simp_lemma const &)> const & fn) const {
    m_sets.for_each([&](name const & eqv, simp_lemmas_for const & s) {
            s.for_each_congr([&](simp_lemma const & r) {
                    fn(eqv, r);
                });
        });
}

format simp_lemmas::pp(formatter const & fmt, format const & header, bool simp, bool congr) const {
    format r;
    if (simp) {
        name prev_eqv;
        for_each([&](name const & eqv, simp_lemma const & rw) {
                if (prev_eqv != eqv) {
                    r += format("simplification rules for ") + format(eqv);
                    r += header;
                    r += line();
                    prev_eqv = eqv;
                }
                r += rw.pp(fmt) + line();
            });
    }

    if (congr) {
        name prev_eqv;
        for_each_congr([&](name const & eqv, simp_lemma const & cr) {
                if (prev_eqv != eqv) {
                    r += format("congruencec rules for ") + format(eqv) + line();
                    prev_eqv = eqv;
                }
                r += cr.pp(fmt) + line();
            });
    }
    return r;
}

format simp_lemmas::pp_simp(formatter const & fmt, format const & header) const {
    return pp(fmt, header, true, false);
}

format simp_lemmas::pp_simp(formatter const & fmt) const {
    return pp(fmt, format(), true, false);
}

format simp_lemmas::pp_congr(formatter const & fmt) const {
    return pp(fmt, format(), false, true);
}

format simp_lemmas::pp(formatter const & fmt) const {
    return pp(fmt, format(), true, true);
}

bool is_simp_relation(environment const & env, name const & n) {
    return is_trans_relation(env, n) && is_refl_relation(env, n);
}

bool is_simp_relation(environment const & env, expr const & e, expr & rel, expr & lhs, expr & rhs) {
    buffer<expr> args;
    rel = get_app_args(e, args);
    if (!is_constant(rel) || !is_simp_relation(env, const_name(rel)))
        return false;
    relation_info const * rel_info = get_relation_info(env, const_name(rel));
    if (!rel_info || rel_info->get_lhs_pos() >= args.size() || rel_info->get_rhs_pos() >= args.size())
        return false;
    lhs = args[rel_info->get_lhs_pos()];
    rhs = args[rel_info->get_rhs_pos()];
    return true;
}

bool is_simp_relation(environment const & env, expr const & e, expr & lhs, expr & rhs) {
    expr rel;
    return is_simp_relation(env, e, rel, lhs, rhs);
}

static bool is_ceqv(type_context & ctx, expr e);

/** \brief Auxiliary functional object for creating "conditional equations" */
class to_ceqvs_fn {
    environment const & m_env;
    type_context &      m_ctx;

    static list<expr_pair> mk_singleton(expr const & e, expr const & H) {
        return list<expr_pair>(mk_pair(e, H));
    }

    bool is_type(expr const & e) {
        return is_sort(m_ctx.whnf(m_ctx.infer(e)));
    }

    bool is_relation(expr const & e) {
        if (!is_app(e)) return false;
        expr const & fn = get_app_fn(e);
        return is_constant(fn) && is_simp_relation(m_env, const_name(fn));
    }

    list<expr_pair> lift(expr const & local, list<expr_pair> const & l) {
        lean_assert(is_local(local));
        return map(l, [&](expr_pair const & e_H) {
                return mk_pair(m_ctx.mk_pi({local}, e_H.first), m_ctx.mk_lambda({local}, e_H.second));
            });
    }

    bool is_prop(expr const & e) {
        return m_ctx.is_prop(e);
    }

    // If restricted is true, we don't use (e <-> true) rewrite
    list<expr_pair> apply(expr const & e, expr const & H, bool restricted) {
        expr c, Hdec, A, arg1, arg2;
        if (is_relation(e)) {
            return mk_singleton(e, H);
        } else if (is_not(e, arg1)) {
            expr new_e = mk_iff(arg1, mk_false());
            expr new_H = mk_app(mk_constant(get_iff_false_intro_name()), arg1, H);
            return mk_singleton(new_e, new_H);
        } else if (is_and(e, arg1, arg2)) {
            // TODO(Leo): we can extend this trick to any type that has only one constructor
            expr H1 = mk_app(mk_constant(get_and_elim_left_name()), arg1, arg2, H);
            expr H2 = mk_app(mk_constant(get_and_elim_right_name()), arg1, arg2, H);
            auto r1 = apply(arg1, H1, restricted);
            auto r2 = apply(arg2, H2, restricted);
            return append(r1, r2);
        } else if (is_pi(e)) {
            type_context::tmp_locals locals(m_ctx);
            expr local = locals.push_local_from_binding(e);
            expr new_e = instantiate(binding_body(e), local);
            expr new_H = mk_app(H, local);
            auto r = apply(new_e, new_H, restricted);
            unsigned len = length(r);
            if (len == 0) {
                return r;
            } else if (len == 1 && head(r).first == new_e && head(r).second == new_H) {
                return mk_singleton(e, H);
            } else {
                return lift(local, r);
            }
        } else if (is_ite(e, c, Hdec, A, arg1, arg2) && is_prop(e)) {
            expr not_c = mk_app(mk_constant(get_not_name()), c);
            type_context::tmp_locals locals(m_ctx);
            expr Hc    = locals.push_local(name(), c);
            expr Hnc   = locals.push_local(name(), not_c);
            expr H1    = mk_app({mk_constant(get_implies_of_if_pos_name()), c, arg1, arg2, Hdec, e, Hc});
            expr H2    = mk_app({mk_constant(get_implies_of_if_neg_name()), c, arg1, arg2, Hdec, e, Hnc});
            auto r1    = lift(Hc, apply(arg1, H1, restricted));
            auto r2    = lift(Hnc, apply(arg2, H2, restricted));
            return append(r1, r2);
        } else if (!restricted) {
            expr new_e = m_ctx.whnf(e);
            if (new_e != e) {
                if (auto r = apply(new_e, H, true))
                    return r;
            }
            if (is_prop(e)) {
                expr new_e = mk_iff(e, mk_true());
                expr new_H = mk_app(mk_constant(get_iff_true_intro_name()), e, H);
                return mk_singleton(new_e, new_H);
            } else {
                return list<expr_pair>();
            }
        } else {
            return list<expr_pair>();
        }
    }

public:
    to_ceqvs_fn(type_context & ctx):m_env(ctx.env()), m_ctx(ctx) {}

    list<expr_pair> operator()(expr const & e, expr const & H) {
        bool restricted = false;
        list<expr_pair> lst = apply(e, H, restricted);
        return filter(lst, [&](expr_pair const & p) { return is_ceqv(m_ctx, p.first); });
    }
};

static list<expr_pair> to_ceqvs(type_context & ctx, expr const & e, expr const & H) {
    return to_ceqvs_fn(ctx)(e, H);
}

static bool is_ceqv(type_context & ctx, expr e) {
    if (has_expr_metavar(e))
        return false;
    name_set to_find;
    // Define a procedure for removing arguments from to_find.
    auto visitor_fn = [&](expr const & e, unsigned) {
        if (is_local(e)) {
            to_find.erase(mlocal_name(e));
            return false;
        } else if (is_metavar(e)) {
            return false;
        } else {
            return true;
        }
    };
    environment const & env = ctx.env();
    buffer<expr> hypotheses; // arguments that are propositions
    type_context::tmp_locals locals(ctx);
    while (is_pi(e)) {
        if (!to_find.empty()) {
            // Support for dependent types.
            // We may find the instantiation for the previous arguments
            // by matching the type.
            for_each(binding_domain(e), visitor_fn);
        }
        expr local = locals.push_local(name(), binding_domain(e));
        if (binding_info(e).is_inst_implicit()) {
            // If the argument can be instantiated by type class resolution, then
            // we don't need to find it in the lhs
        } else if (ctx.is_prop(binding_domain(e))) {
            // If the argument is a proposition, we store it in hypotheses.
            // We check whether the lhs occurs in hypotheses or not.
            hypotheses.push_back(binding_domain(e));
        } else {
            to_find.insert(mlocal_name(local));
        }
        e = instantiate(binding_body(e), local);
    }
    expr lhs, rhs;
    if (!is_simp_relation(env, e, lhs, rhs))
        return false;
    // traverse lhs, and remove found variables from to_find
    for_each(lhs, visitor_fn);
    if (!to_find.empty())
        return false;
    // basic looping ceq detection: the left-hand-side should not occur in the right-hand-side,
    // nor it should occur in any of the hypothesis
    if (occurs(lhs, rhs))
        return false;
    if (std::any_of(hypotheses.begin(), hypotheses.end(), [&](expr const & h) { return occurs(lhs, h); }))
        return false;
    return true;
}

static bool is_permutation(expr const & lhs, expr const & rhs, unsigned offset, buffer<optional<unsigned>> & p) {
    if (lhs.kind() != rhs.kind())
        return false;
    switch (lhs.kind()) {
    case expr_kind::Constant: case expr_kind::Sort:
    case expr_kind::Meta: case expr_kind::Local:
        return lhs == rhs;
    case expr_kind::Var:
        if (var_idx(lhs) < offset) {
            return lhs == rhs; // locally bound variable
        } else if (var_idx(lhs) - offset < p.size()) {
            if (p[var_idx(lhs) - offset]) {
                return *(p[var_idx(lhs) - offset]) == var_idx(rhs) - offset;
            } else {
                p[var_idx(lhs) - offset] = var_idx(rhs) - offset;
                return true;
            }
        } else {
            return lhs == rhs; // free variable
        }
    case expr_kind::Lambda: case expr_kind::Pi:
        return
            is_permutation(binding_domain(lhs), binding_domain(rhs), offset, p) &&
            is_permutation(binding_body(lhs), binding_body(rhs), offset+1, p);
    case expr_kind::Let:
        // Let-expressions must be unfolded before invoking this method
        lean_unreachable();
    case expr_kind::App:
        return
            is_permutation(app_fn(lhs), app_fn(rhs), offset, p) &&
            is_permutation(app_arg(lhs), app_arg(rhs), offset, p);
    case expr_kind::Macro:
        if (macro_def(lhs) != macro_def(rhs) ||
            macro_num_args(lhs) != macro_num_args(rhs))
            return false;
        for (unsigned i = 0; i < macro_num_args(lhs); i++) {
            if (!is_permutation(macro_arg(lhs, i), macro_arg(rhs, i), offset, p))
                return false;
        }
        return true;
    }
    lean_unreachable();
}

static bool is_permutation_ceqv(environment const & env, expr e) {
    unsigned num_args = 0;
    while (is_pi(e)) {
        e = binding_body(e);
        num_args++;
    }
    expr lhs, rhs;
    if (is_simp_relation(env, e, lhs, rhs)) {
        buffer<optional<unsigned>> permutation;
        permutation.resize(num_args);
        return is_permutation(lhs, rhs, 0, permutation);
    } else {
        return false;
    }
}

/* Getters/checkers */
static void report_failure(sstream const & strm) {
    if (g_throw_ex){
        throw exception(strm);
    } else {
        lean_trace(name({"simp_lemmas", "failure"}), tout() << strm.str() << "\n";);
    }
}

static simp_lemmas add_core(type_context & ctx, simp_lemmas const & s, name const & id, levels const & univ_metas,
                            expr const & e, expr const & h, unsigned priority) {
    lean_assert(ctx.in_tmp_mode());
    list<expr_pair> ceqvs   = to_ceqvs(ctx, e, h);
    if (is_nil(ceqvs)) {
        report_failure(sstream() << "invalid [simp] lemma '" << id << "' : " << e);
        return s;
    }
    environment const & env = ctx.env();
    simp_lemmas new_s = s;
    for (expr_pair const & p : ceqvs) {
        /* We only clear the eassignment since we want to reuse the temporary universe metavariables associated
           with the declaration. */
        ctx.clear_tmp_eassignment();
        expr rule  = ctx.whnf(p.first);
        expr proof = ctx.whnf(p.second);
        bool is_perm = is_permutation_ceqv(env, rule);
        buffer<expr> emetas;
        buffer<bool> instances;
        while (is_pi(rule)) {
            expr mvar = ctx.mk_tmp_mvar(binding_domain(rule));
            emetas.push_back(mvar);
            instances.push_back(binding_info(rule).is_inst_implicit());
            rule = ctx.whnf(instantiate(binding_body(rule), mvar));
            proof = mk_app(proof, mvar);
        }
        expr rel, lhs, rhs;
        if (is_simp_relation(env, rule, rel, lhs, rhs) && is_constant(rel)) {
            new_s.insert(const_name(rel), mk_simp_lemma(id, univ_metas, reverse_to_list(emetas),
                                                        reverse_to_list(instances), lhs, rhs,
                                                        proof, is_perm, priority));
        }
    }
    return new_s;
}

static bool is_rfl_lemma(environment const & env, name const & cname) {
    declaration const & d = env.get(cname);
    if (!d.is_definition()) return false;
    expr type = d.get_type();
    expr pf   = d.get_value();
    while (is_pi(type)) {
        if (!is_lambda(pf)) return false;
        pf   = binding_body(pf);
        type = binding_body(type);
    }
    expr lhs, rhs;
    if (!is_eq(type, lhs, rhs)) return false;
    if (!is_app_of(pf, get_eq_refl_name(), 2) && !is_app_of(pf, get_rfl_name(), 2)) return false;
    return lhs != rhs;
}

static simp_lemmas add_core(type_context & ctx, simp_lemmas const & s, name const & cname, unsigned priority) {
    environment const & env = ctx.env();
    type_context::tmp_mode_scope scope(ctx);
    declaration const & d = env.get(cname);
    buffer<level> us;
    unsigned num_univs = d.get_num_univ_params();
    for (unsigned i = 0; i < num_univs; i++) {
        us.push_back(ctx.mk_tmp_univ_mvar());
    }
    levels ls = to_list(us);
    expr type = instantiate_type_univ_params(d, ls);
    if (is_rfl_lemma(env, cname)) {
        buffer<expr> emetas;
        buffer<bool> instances;
        while (is_pi(type)) {
            expr mvar = ctx.mk_tmp_mvar(binding_domain(type));
            emetas.push_back(mvar);
            instances.push_back(binding_info(type).is_inst_implicit());
            type = instantiate(binding_body(type), mvar);
        }
        expr lhs, rhs;
        lean_verify(is_eq(type, lhs, rhs));
        simp_lemmas new_s = s;
        new_s.insert(get_eq_name(), mk_rfl_lemma(cname, ls, to_list(emetas), to_list(instances),
                                                 lhs, rhs, priority));
        return new_s;
    } else {
        expr proof = mk_constant(cname, ls);
        return add_core(ctx, s, cname, ls, type, proof, priority);
    }
}

/* Return true iff lhs is of the form (B (x : ?m1), ?m2) or (B (x : ?m1), ?m2 x),
   where B is lambda or Pi */
static bool is_valid_congr_rule_binding_lhs(expr const & lhs, name_set & found_mvars) {
    lean_assert(is_binding(lhs));
    expr const & d = binding_domain(lhs);
    expr const & b = binding_body(lhs);
    if (!is_metavar(d))
        return false;
    if (is_metavar(b) && b != d) {
        found_mvars.insert(mlocal_name(b));
        found_mvars.insert(mlocal_name(d));
        return true;
    }
    if (is_app(b) && is_metavar(app_fn(b)) && is_var(app_arg(b), 0) && app_fn(b) != d) {
        found_mvars.insert(mlocal_name(app_fn(b)));
        found_mvars.insert(mlocal_name(d));
        return true;
    }
    return false;
}

/* Return true iff all metavariables in e are in found_mvars */
static bool only_found_mvars(expr const & e, name_set const & found_mvars) {
    return !find(e, [&](expr const & m, unsigned) {
            return is_metavar(m) && !found_mvars.contains(mlocal_name(m));
        });
}

/* Check whether rhs is of the form (mvar l_1 ... l_n) where mvar is a metavariable,
   and l_i's are local constants, and mvar does not occur in found_mvars.
   If it is return true and update found_mvars */
static bool is_valid_congr_hyp_rhs(expr const & rhs, name_set & found_mvars) {
    buffer<expr> rhs_args;
    expr const & rhs_fn = get_app_args(rhs, rhs_args);
    if (!is_metavar(rhs_fn) || found_mvars.contains(mlocal_name(rhs_fn)))
        return false;
    for (expr const & arg : rhs_args)
        if (!is_local(arg))
            return false;
    found_mvars.insert(mlocal_name(rhs_fn));
    return true;
}

static simp_lemmas add_congr_core(type_context & ctx, simp_lemmas const & s, name const & n, unsigned prio) {
    type_context::tmp_mode_scope scope(ctx);
    declaration const & d = ctx.env().get(n);
    buffer<level> us;
    unsigned num_univs = d.get_num_univ_params();
    for (unsigned i = 0; i < num_univs; i++) {
        us.push_back(ctx.mk_tmp_univ_mvar());
    }
    levels ls = to_list(us);
    expr rule    = ctx.whnf(instantiate_type_univ_params(d, ls));
    expr proof   = mk_constant(n, ls);

    buffer<expr> emetas;
    buffer<bool> instances, explicits;

    while (is_pi(rule)) {
        expr mvar = ctx.mk_tmp_mvar(binding_domain(rule));
        emetas.push_back(mvar);
        explicits.push_back(is_explicit(binding_info(rule)));
        instances.push_back(binding_info(rule).is_inst_implicit());
        rule  = ctx.whnf(instantiate(binding_body(rule), mvar));
        proof = mk_app(proof, mvar);
    }
    expr rel, lhs, rhs;
    if (!is_simp_relation(ctx.env(), rule, rel, lhs, rhs) || !is_constant(rel)) {
        report_failure(sstream() << "invalid [congr] lemma, '" << n
                       << "' resulting type is not of the form t ~ s, where '~' is a transitive and reflexive relation");
    }
    name_set found_mvars;
    buffer<expr> lhs_args, rhs_args;
    expr const & lhs_fn = get_app_args(lhs, lhs_args);
    expr const & rhs_fn = get_app_args(rhs, rhs_args);
    if (is_constant(lhs_fn)) {
        if (!is_constant(rhs_fn) || const_name(lhs_fn) != const_name(rhs_fn) || lhs_args.size() != rhs_args.size()) {
            report_failure(sstream() << "invalid [congr] lemma, '" << n
                           << "' resulting type is not of the form (" << const_name(lhs_fn) << "  ...) "
                           << "~ (" << const_name(lhs_fn) << " ...), where ~ is '" << const_name(rel) << "'");
        }
        for (expr const & lhs_arg : lhs_args) {
            if (is_sort(lhs_arg))
                continue;
            if (!is_metavar(lhs_arg) || found_mvars.contains(mlocal_name(lhs_arg))) {
                report_failure(sstream() << "invalid [congr] lemma, '" << n
                               << "' the left-hand-side of the congruence resulting type must be of the form ("
                               << const_name(lhs_fn) << " x_1 ... x_n), where each x_i is a distinct variable or a sort");
            }
            found_mvars.insert(mlocal_name(lhs_arg));
        }
    } else if (is_binding(lhs)) {
        if (lhs.kind() != rhs.kind()) {
            report_failure(sstream() << "invalid [congr] lemma, '" << n
                           << "' kinds of the left-hand-side and right-hand-side of "
                           << "the congruence resulting type do not match");
        }
        if (!is_valid_congr_rule_binding_lhs(lhs, found_mvars)) {
            report_failure(sstream() << "invalid [congr] lemma, '" << n
                           << "' left-hand-side of the congruence resulting type must "
                           << "be of the form (fun/Pi (x : A), B x)");
        }
    } else {
        report_failure(sstream() << "invalid [congr] lemma, '" << n
                       << "' left-hand-side is not an application nor a binding");
    }

    buffer<expr> congr_hyps;
    lean_assert(emetas.size() == explicits.size());
    for (unsigned i = 0; i < emetas.size(); i++) {
        expr const & mvar = emetas[i];
        if (explicits[i] && !found_mvars.contains(mlocal_name(mvar))) {
            expr type = mlocal_type(mvar);
            type_context::tmp_locals locals(ctx);
            while (is_pi(type)) {
                expr local = locals.push_local_from_binding(type);
                type = instantiate(binding_body(type), local);
            }
            expr h_rel, h_lhs, h_rhs;
            if (!is_simp_relation(ctx.env(), type, h_rel, h_lhs, h_rhs) || !is_constant(h_rel))
                continue;
            unsigned j = 0;
            for (expr const & local : locals.as_buffer()) {
                j++;
                if (!only_found_mvars(mlocal_type(local), found_mvars)) {
                    report_failure(sstream() << "invalid [congr] lemma, '" << n
                                   << "' argument #" << j << " of parameter #" << (i+1) << " contains "
                                   << "unresolved parameters");
                }
            }
            if (!only_found_mvars(h_lhs, found_mvars)) {
                report_failure(sstream() << "invalid [congr] lemma, '" << n
                               << "' argument #" << (i+1) << " is not a valid hypothesis, the left-hand-side contains "
                               << "unresolved parameters");
            }
            if (!is_valid_congr_hyp_rhs(h_rhs, found_mvars)) {
                report_failure(sstream() << "invalid [congr] lemma, '" << n
                               << "' argument #" << (i+1) << " is not a valid hypothesis, the right-hand-side must be "
                               << "of the form (m l_1 ... l_n) where m is parameter that was not "
                               << "'assigned/resolved' yet and l_i's are locals");
            }
            found_mvars.insert(mlocal_name(mvar));
            congr_hyps.push_back(mvar);
        }
    }
    simp_lemmas new_s = s;
    new_s.insert(const_name(rel), mk_congr_lemma(n, ls, reverse_to_list(emetas),
                                                 reverse_to_list(instances), lhs, rhs, proof, to_list(congr_hyps), prio));
    return new_s;
}

simp_lemmas add(type_context & ctx, simp_lemmas const & s, name const & id, unsigned priority) {
    return add_core(ctx, s, id, priority);
}

simp_lemmas add(type_context & ctx, simp_lemmas const & s, name const & id, expr const & e, expr const & h, unsigned priority) {
    type_context::tmp_mode_scope scope(ctx);
    return add_core(ctx, s, id, list<level>(), e, h, priority);
}

simp_lemmas add_congr(type_context & ctx, simp_lemmas const & s, name const & id, unsigned priority) {
    return add_congr_core(ctx, s, id, priority);
}

simp_lemmas join(simp_lemmas const & s1, simp_lemmas const & s2) {
    if (s1.empty()) return s2;
    if (s2.empty()) return s1;
    simp_lemmas new_s1 = s1;

    buffer<pair<name const &, simp_lemma>> slemmas;
    s2.for_each([&](name const & eqv, simp_lemma const & r) {
            slemmas.push_back({eqv, r});
        });
    for (unsigned i = slemmas.size() - 1; i + 1 > 0; --i)
        new_s1.insert(slemmas[i].first, slemmas[i].second);

    buffer<pair<name const &, simp_lemma>> clemmas;
    s2.for_each_congr([&](name const & eqv, simp_lemma const & r) {
            clemmas.push_back({eqv, r});
        });
    for (unsigned i = clemmas.size() - 1; i + 1 > 0; --i)
        new_s1.insert(clemmas[i].first, clemmas[i].second);

    return new_s1;
}

static void on_add_simp_lemma(environment const & env, name const & c, bool) {
    type_context ctx(env);
    simp_lemmas s;
    flet<bool> set_ex(g_throw_ex, true);
    add_core(ctx, s, c, LEAN_DEFAULT_PRIORITY);
}

static void on_add_congr_lemma(environment const & env, name const & c, bool) {
    type_context ctx(env);
    simp_lemmas s;
    flet<bool> set_ex(g_throw_ex, true);
    add_congr_core(ctx, s, c, LEAN_DEFAULT_PRIORITY);
}

static simp_lemmas get_simp_lemmas_from_attribute(type_context & ctx, name const & attr_name, simp_lemmas result) {
    auto const & attr = get_attribute(ctx.env(), attr_name);
    buffer<name> simp_lemmas;
    attr.get_instances(ctx.env(), simp_lemmas);
    unsigned i = simp_lemmas.size();
    while (i > 0) {
        i--;
        name const & id = simp_lemmas[i];
        result = add_core(ctx, result, id, attr.get_prio(ctx.env(), id));
    }
    return result;
}

static simp_lemmas get_congr_lemmas_from_attribute(type_context & ctx, name const & attr_name, simp_lemmas result) {
    auto const & attr = get_attribute(ctx.env(), attr_name);
    buffer<name> congr_lemmas;
    attr.get_instances(ctx.env(), congr_lemmas);
    unsigned i = congr_lemmas.size();
    while (i > 0) {
        i--;
        name const & id = congr_lemmas[i];
        result = add_congr_core(ctx, result, id, attr.get_prio(ctx.env(), id));
    }
    return result;
}

struct simp_lemmas_config {
    std::vector<name> m_simp_attrs;
    std::vector<name> m_congr_attrs;
};

static std::vector<simp_lemmas_config> * g_simp_lemmas_configs = nullptr;
static name_map<unsigned> * g_name2simp_token = nullptr;
static simp_lemmas_token    g_default_token;

simp_lemmas_token register_simp_attribute(name const & user_name, std::initializer_list<name> const & simp_attrs, std::initializer_list<name> const & congr_attrs) {
    simp_lemmas_config cfg;
    for (name const & attr_name : simp_attrs) {
        cfg.m_simp_attrs.push_back(attr_name);
        if (!is_system_attribute(attr_name)) {
            register_system_attribute(basic_attribute::with_check(attr_name, "simplification lemma", on_add_simp_lemma));
        }
    }
    for (name const & attr_name : congr_attrs) {
        cfg.m_congr_attrs.push_back(attr_name);
        if (!is_system_attribute(attr_name)) {
            register_system_attribute(basic_attribute::with_check(attr_name, "congruence lemma", on_add_congr_lemma));
        }
    }
    simp_lemmas_token tk = g_simp_lemmas_configs->size();
    g_simp_lemmas_configs->push_back(cfg);
    g_name2simp_token->insert(user_name, tk);
    return tk;
}

static simp_lemmas_config const & get_simp_lemmas_config(simp_lemmas_token tk) {
    lean_assert(tk < g_simp_lemmas_configs->size());
    return (*g_simp_lemmas_configs)[tk];
}

/* This is the cache for internally used simp_lemma collections */
class simp_lemmas_cache {
    struct entry {
        environment           m_env;
        std::vector<unsigned> m_fingerprints;
        unsigned              m_reducibility_fingerprint;
        optional<simp_lemmas> m_lemmas;
        entry(environment const & env):
            m_env(env), m_reducibility_fingerprint(0) {}
    };
    std::vector<entry>        m_entries[4];

public:
    void expand(environment const & env, transparency_mode m, unsigned new_sz) {
        unsigned midx = static_cast<unsigned>(m);
        for (unsigned tk = m_entries[midx].size(); tk < new_sz; tk++) {
            auto & cfg   = get_simp_lemmas_config(tk);
            m_entries[midx].emplace_back(env);
            auto & entry = m_entries[midx].back();
            entry.m_fingerprints.resize(cfg.m_simp_attrs.size() + cfg.m_congr_attrs.size());
        }
    }

    simp_lemmas mk_lemmas(environment const & env, transparency_mode m, entry & C, simp_lemmas_token tk) {
        lean_trace("simp_lemmas_cache", tout() << "make simp lemmas [" << tk << "]\n";);
        type_context ctx(env, m);
        C.m_env = env;
        auto & cfg = get_simp_lemmas_config(tk);
        simp_lemmas lemmas;
        unsigned i = 0;
        for (name const & attr_name : cfg.m_simp_attrs) {
            lemmas = get_simp_lemmas_from_attribute(ctx, attr_name, lemmas);
            C.m_fingerprints[i] = get_attribute_fingerprint(env, attr_name);
            i++;
        }
        for (name const & attr_name : cfg.m_congr_attrs) {
            lemmas = get_congr_lemmas_from_attribute(ctx, attr_name, lemmas);
            C.m_fingerprints[i] = get_attribute_fingerprint(env, attr_name);
            i++;
        }
        C.m_lemmas = lemmas;
        C.m_reducibility_fingerprint = get_reducibility_fingerprint(env);
        return lemmas;
    }

    simp_lemmas lemmas_of(entry const & C, simp_lemmas_token tk) {
        lean_trace("simp_lemmas_cache", tout() << "reusing cached simp lemmas [" << tk << "]\n";);
        return *C.m_lemmas;
    }

    bool is_compatible(entry const & C, environment const & env, simp_lemmas_token tk) {
        if (!env.is_descendant(C.m_env))
            return false;
        if (get_reducibility_fingerprint(env) != C.m_reducibility_fingerprint)
            return false;
        auto & cfg = get_simp_lemmas_config(tk);
        unsigned i = 0;
        for (name const & attr_name : cfg.m_simp_attrs) {
            if (get_attribute_fingerprint(env, attr_name) != C.m_fingerprints[i])
                return false;
            i++;
        }
        for (name const & attr_name : cfg.m_congr_attrs) {
            if (get_attribute_fingerprint(env, attr_name) != C.m_fingerprints[i])
                return false;
            i++;
        }
        return true;
    }

    simp_lemmas get(environment const & env, transparency_mode m, simp_lemmas_token tk) {
        lean_assert(tk < g_simp_lemmas_configs->size());
        unsigned midx = static_cast<unsigned>(m);
        if (tk >= m_entries[midx].size()) expand(env, m, tk+1);
        lean_assert(tk < m_entries[midx].size());
        entry & C = m_entries[midx][tk];
        if (!C.m_lemmas) return mk_lemmas(env, m, C, tk);
        if (is_eqp(env, C.m_env)) return lemmas_of(C, tk);
        if (!is_compatible(C, env, tk)) {
            lean_trace("simp_lemmas_cache", tout() << "creating new cache\n";);
            return mk_lemmas(env, m, C, tk);
        }
        return lemmas_of(C, tk);
    }
};

MK_THREAD_LOCAL_GET_DEF(simp_lemmas_cache, get_cache);

simp_lemmas get_simp_lemmas(environment const & env, transparency_mode m, simp_lemmas_token tk) {
    return get_cache().get(env, m, tk);
}

simp_lemmas get_default_simp_lemmas(environment const & env, transparency_mode m) {
    return get_simp_lemmas(env, m, g_default_token);
}

simp_lemmas get_simp_lemmas(environment const & env, transparency_mode m, name const & tk_name) {
    if (simp_lemmas_token const * tk = g_name2simp_token->find(tk_name))
        return get_simp_lemmas(env, m, *tk);
    else
        throw exception(sstream() << "unknown simp_lemmas collection '" << tk_name << "'");
}

static bool instantiate_emetas(type_context & ctx, list<expr> const & _emetas, list<bool> const & _instances) {
    buffer<expr> emetas;
    buffer<bool> instances;
    to_buffer(_emetas, emetas);
    to_buffer(_instances, instances);

    lean_assert(emetas.size() == instances.size());
    for (unsigned i = 0; i < emetas.size(); ++i) {
        expr m = emetas[i];
        unsigned mvar_idx = emetas.size() - 1 - i;
        expr m_type = ctx.instantiate_mvars(ctx.infer(m));
        // TODO(Leo, Daniel): do we need the following assertion?
        // lean_assert(!has_expr_metavar(m_type));
        if (ctx.get_tmp_mvar_assignment(mvar_idx)) continue;
        if (instances[i]) {
            if (auto v = ctx.mk_class_instance(m_type)) {
                if (!ctx.is_def_eq(m, *v)) {
                    lean_trace(name({"simp_lemma", "failure"}),
                               tout() << "unable to assign instance for: " << m_type << "\n";);
                    return false;
                } else {
                    lean_assert(ctx.get_tmp_mvar_assignment(mvar_idx));
                    continue;
                }
            } else {
                lean_trace(name({"simp_lemma", "failure"}),
                           tout() << "unable to synthesize instance for: " << m_type << "\n";);
                return false;
            }
        } else {
            lean_trace(name({"simp_lemma", "failure"}),
                       tout() << "failed to assign: " << m << " : " << m_type << "\n";);
            return false;
        }
    }
    return true;
}

expr refl_lemma_rewrite(type_context & ctx, expr const & e, simp_lemma const & sl) {
    lean_assert(sl.is_refl());
    type_context::tmp_mode_scope scope(ctx, sl.get_num_umeta(), sl.get_num_emeta());
    if (!ctx.is_def_eq(e, sl.get_lhs())) return e;

    lean_trace("simp_lemma",
               expr new_lhs = ctx.instantiate_mvars(sl.get_lhs());
               expr new_rhs = ctx.instantiate_mvars(sl.get_rhs());
               tout() << "(" << sl.get_id() << ") "
               << "[" << new_lhs << " --> " << new_rhs << "]\n";);

    if (!instantiate_emetas(ctx, sl.get_emetas(), sl.get_instances())) return e;

    for (unsigned i = 0; i < sl.get_num_umeta(); i++) {
        if (!ctx.get_tmp_uvar_assignment(i)) return e;
    }

    return ctx.instantiate_mvars(sl.get_rhs());
}

void initialize_simp_lemmas() {
    g_dummy               = new simp_lemma_cell();
    g_simp_lemmas_configs = new std::vector<simp_lemmas_config>();
    g_name2simp_token     = new name_map<unsigned>();
    g_default_token       = register_simp_attribute("default", {"simp", "wrapper_eq"}, {"congr"});
    register_trace_class("simp_lemmas");
    register_trace_class("simp_lemmas_cache");
    register_trace_class(name{"simp_lemmas", "failure"});
}

void finalize_simp_lemmas() {
    delete g_simp_lemmas_configs;
    delete g_name2simp_token;
    delete g_dummy;
}
}