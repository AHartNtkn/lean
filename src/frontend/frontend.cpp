/*
Copyright (c) 2013 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Leonardo de Moura
*/
#include <atomic>
#include <unordered_map>
#include "frontend.h"
#include "environment.h"
#include "operator_info.h"

namespace lean {
/** \brief Implementation of the Lean frontend */
struct frontend::imp {
    // Remark: only named objects are stored in the dictionary.
    typedef std::unordered_map<name, operator_info, name_hash, name_eq> operator_table;
    std::atomic<unsigned> m_num_children;
    std::shared_ptr<imp>  m_parent;
    environment           m_env;
    operator_table        m_nud; // nud table for Pratt's parser
    operator_table        m_led; // led table for Pratt's parser
    operator_table        m_name_to_operator; // map internal names to operators (this is used for pretty printing)

    bool has_children() const { return m_num_children > 0; }
    void inc_children() { m_num_children++; }
    void dec_children() { m_num_children--; }

    bool has_parent() const { return m_parent != nullptr; }

    imp():
        m_num_children(0) {
    }

    explicit imp(std::shared_ptr<imp> const & parent):
        m_num_children(0),
        m_parent(parent),
        m_env(m_parent->m_env.mk_child()) {
        m_parent->inc_children();
    }

    ~imp() {
        if (m_parent)
            m_parent->dec_children();
    }
};

frontend::frontend():
    m_imp(new imp()) {
}

frontend::frontend(imp * new_ptr):
    m_imp(new_ptr) {
}

frontend::frontend(std::shared_ptr<imp> const & ptr):
    m_imp(ptr) {
}

frontend::~frontend() {
}

frontend frontend::mk_child() const {
    return frontend(new imp(m_imp));
}

bool frontend::has_children() const {
    return m_imp->has_children();
}

bool frontend::has_parent() const {
    return m_imp->has_parent();
}

frontend frontend::parent() const {
    lean_assert(has_parent());
    return frontend(m_imp->m_parent);
}

environment const & frontend::env() const { return m_imp->m_env; }

level frontend::add_uvar(name const & n, level const & l) { return m_imp->m_env.add_uvar(n, l); }
level frontend::add_uvar(name const & n) { return m_imp->m_env.add_uvar(n); }
level frontend::get_uvar(name const & n) const { return m_imp->m_env.get_uvar(n); }

}

