#include <iostream>
// structure.cpp - см. structure.hpp. 1:1 порт structure.py.
#include <cstdint>  // БАГ-ФИКС: MinGW/Windows не тянет int64_t транзитивно через другие заголовки, как это молча делает libstdc++ на Linux - см. ошибку сборки Windows-раннера в этой сессии.
#include "structure.hpp"
#include "emit.hpp"
#include "process_jar.hpp"
#include "javatypes.hpp"
#include "stackvm.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <regex>

namespace nd {

// ==================== negate / sentinel ====================

ExprPtr negate(const ExprPtr& cond) {
    if (!cond) return cond;
    if (cond->kind == ExprKind::Const) {
        auto* c = static_cast<Const*>(cond.get());
        if (c->literal == "true") return std::make_shared<Const>("false", "boolean");
        if (c->literal == "false") return std::make_shared<Const>("true", "boolean");
    }
    if (cond->kind == ExprKind::UnOp) {
        auto* u = static_cast<UnOp*>(cond.get());
        if (u->op == "!") return u->expr;
    }
    if (cond->kind == ExprKind::BinOp) {
        auto* b = static_cast<BinOp*>(cond.get());
        static const std::map<std::string, std::string> flip = {
            {"==", "!="}, {"!=", "=="}, {"<", ">="}, {">=", "<"}, {">", "<="}, {"<=", ">"},
        };
        auto it = flip.find(b->op);
        if (it != flip.end()) return std::make_shared<BinOp>(it->second, b->left, b->right, "boolean");
        if (b->op == "&&") {
            return std::make_shared<BinOp>("||", negate(b->left), negate(b->right), "boolean");
        }
        if (b->op == "||") {
            return std::make_shared<BinOp>("&&", negate(b->left), negate(b->right), "boolean");
        }
    }
    return std::make_shared<UnOp>("!", cond, "boolean");
}

namespace {

bool is_sentinel(const ExprPtr& e) {
    return e && e->kind == ExprKind::Local && static_cast<Local*>(e.get())->name == CAUGHT_SENTINEL;
}

// ---------------- rename_local (см. _rename_local в оригинале) ----------------
// ВАЖНО: воспроизводит РОВНО те ограничения, что есть в оригинале (не
// заходит внутрь ForStmt.update - он Stmt, а не Expr, и в Python-версии
// стоит явный гейт `hasattr(v, "prec")`, отсеивающий не-Expr объекты; не
// заходит в BlockStmt.stmts - в generic-цикле оригинала проверяется имя
// атрибута "body", а у BlockStmt атрибут называется "stmts"; не заходит в
// TryStmt.finally_body - тоже не входит в список атрибутов оригинала для
// ИМЕННО этой функции). См. HANDOFF_38.

void rename_walk_expr(const ExprPtr& e, const std::string& old_name, const std::string& new_name) {
    if (!e) return;
    if (e->kind == ExprKind::Local) {
        auto* l = static_cast<Local*>(e.get());
        if (l->name == old_name) l->name = new_name;
        return;
    }
    switch (e->kind) {
        case ExprKind::FieldAccess:
            rename_walk_expr(static_cast<FieldAccess*>(e.get())->target, old_name, new_name);
            break;
        case ExprKind::ArrayAccess: {
            auto* a = static_cast<ArrayAccess*>(e.get());
            rename_walk_expr(a->array, old_name, new_name);
            rename_walk_expr(a->index, old_name, new_name);
            break;
        }
        case ExprKind::MethodCall: {
            auto* m = static_cast<MethodCall*>(e.get());
            rename_walk_expr(m->target, old_name, new_name);
            for (auto& a : m->args) rename_walk_expr(a, old_name, new_name);
            break;
        }
        case ExprKind::NewObject:
            for (auto& a : static_cast<NewObject*>(e.get())->args) rename_walk_expr(a, old_name, new_name);
            break;
        case ExprKind::NewArray:
            for (auto& d : static_cast<NewArray*>(e.get())->dims) rename_walk_expr(d, old_name, new_name);
            break;
        case ExprKind::Cast:
            rename_walk_expr(static_cast<Cast*>(e.get())->expr, old_name, new_name);
            break;
        case ExprKind::InstanceOf:
            rename_walk_expr(static_cast<InstanceOf*>(e.get())->expr, old_name, new_name);
            break;
        case ExprKind::BinOp: {
            auto* b = static_cast<BinOp*>(e.get());
            rename_walk_expr(b->left, old_name, new_name);
            rename_walk_expr(b->right, old_name, new_name);
            break;
        }
        case ExprKind::UnOp:
            rename_walk_expr(static_cast<UnOp*>(e.get())->expr, old_name, new_name);
            break;
        case ExprKind::Ternary: {
            auto* t = static_cast<Ternary*>(e.get());
            rename_walk_expr(t->cond, old_name, new_name);
            rename_walk_expr(t->tval, old_name, new_name);
            rename_walk_expr(t->fval, old_name, new_name);
            break;
        }
        case ExprKind::Assign: {
            auto* a = static_cast<Assign*>(e.get());
            rename_walk_expr(a->target, old_name, new_name);
            rename_walk_expr(a->value, old_name, new_name);
            break;
        }
        default:
            break;  // Const/This/Raw/ClassLiteral/Lambda - нет обходимых детей (см. пояснение выше)
    }
}

void rename_walk_stmt(const StmtPtr& s, const std::string& old_name, const std::string& new_name) {
    if (!s) return;
    switch (s->kind) {
        case StmtKind::ExprStmt:
            rename_walk_expr(static_cast<ExprStmtNode*>(s.get())->expr, old_name, new_name);
            break;
        case StmtKind::ThrowStmt:
            rename_walk_expr(static_cast<ThrowStmt*>(s.get())->expr, old_name, new_name);
            break;
        case StmtKind::SyncStmt:
            rename_walk_expr(static_cast<SyncStmt*>(s.get())->expr, old_name, new_name);
            break;
        case StmtKind::ReturnStmt:
            rename_walk_expr(static_cast<ReturnStmt*>(s.get())->expr, old_name, new_name);
            break;
        case StmtKind::IfStmt:
            rename_walk_expr(static_cast<IfStmt*>(s.get())->cond, old_name, new_name);
            break;
        case StmtKind::WhileStmt:
            rename_walk_expr(static_cast<WhileStmt*>(s.get())->cond, old_name, new_name);
            break;
        case StmtKind::DoWhileStmt:
            rename_walk_expr(static_cast<DoWhileStmt*>(s.get())->cond, old_name, new_name);
            break;
        case StmtKind::ForStmt:
            rename_walk_expr(static_cast<ForStmt*>(s.get())->init, old_name, new_name);
            rename_walk_expr(static_cast<ForStmt*>(s.get())->cond, old_name, new_name);
            // update - НЕ трогаем (см. комментарий выше)
            break;
        case StmtKind::LocalDecl:
            rename_walk_expr(static_cast<LocalDecl*>(s.get())->init, old_name, new_name);
            break;
        case StmtKind::SwitchStmt:
            rename_walk_expr(static_cast<SwitchStmt*>(s.get())->selector, old_name, new_name);
            break;
        default:
            break;
    }
    switch (s->kind) {
        case StmtKind::IfStmt: {
            auto* i = static_cast<IfStmt*>(s.get());
            for (auto& sub : i->then_body) rename_walk_stmt(sub, old_name, new_name);
            if (i->else_body.has_value()) {
                for (auto& sub : *i->else_body) rename_walk_stmt(sub, old_name, new_name);
            }
            break;
        }
        case StmtKind::WhileStmt:
            for (auto& sub : static_cast<WhileStmt*>(s.get())->body) rename_walk_stmt(sub, old_name, new_name);
            break;
        case StmtKind::DoWhileStmt:
            for (auto& sub : static_cast<DoWhileStmt*>(s.get())->body) rename_walk_stmt(sub, old_name, new_name);
            break;
        case StmtKind::ForStmt:
            for (auto& sub : static_cast<ForStmt*>(s.get())->body) rename_walk_stmt(sub, old_name, new_name);
            break;
        case StmtKind::TryStmt:
            for (auto& sub : static_cast<TryStmt*>(s.get())->body) rename_walk_stmt(sub, old_name, new_name);
            break;
        case StmtKind::SyncStmt:
            for (auto& sub : static_cast<SyncStmt*>(s.get())->body) rename_walk_stmt(sub, old_name, new_name);
            break;
        default:
            break;
    }
    if (s->kind == StmtKind::SwitchStmt) {
        for (auto& c : static_cast<SwitchStmt*>(s.get())->cases) {
            for (auto& sub : c.body) rename_walk_stmt(sub, old_name, new_name);
        }
    }
    if (s->kind == StmtKind::TryStmt) {
        for (auto& c : static_cast<TryStmt*>(s.get())->catches) {
            for (auto& sub : c.body) rename_walk_stmt(sub, old_name, new_name);
        }
    }
}

void rename_local(std::vector<StmtPtr>& stmts, const std::string& old_name, const std::string& new_name) {
    for (auto& s : stmts) rename_walk_stmt(s, old_name, new_name);
}

void rename_sentinel(std::vector<StmtPtr>& stmts, const std::string& new_name) {
    rename_local(stmts, CAUGHT_SENTINEL, new_name);
}

// ---------------- contains_local_ref (полностью общий обход Expr+Stmt) ----------------
// Собственный, БОЛЕЕ ШИРОКИЙ список атрибутов (включает "finally_body",
// заходит в ForStmt.update ЧЕРЕЗ вложенный ExprStmtNode.expr, т.к. не имеет
// гейта "только Expr") - сознательно ОТДЕЛЬНАЯ функция от rename_walk_*,
// не общий код - см. пояснение в HANDOFF_38.

bool contains_local_ref_expr(const ExprPtr& e, const std::string& name);
bool contains_local_ref_stmt(const StmtPtr& s, const std::string& name);

bool contains_local_ref_expr(const ExprPtr& e, const std::string& name) {
    if (!e) return false;
    if (e->kind == ExprKind::Local) return static_cast<Local*>(e.get())->name == name;
    switch (e->kind) {
        case ExprKind::FieldAccess:
            return contains_local_ref_expr(static_cast<FieldAccess*>(e.get())->target, name);
        case ExprKind::ArrayAccess: {
            auto* a = static_cast<ArrayAccess*>(e.get());
            return contains_local_ref_expr(a->array, name) || contains_local_ref_expr(a->index, name);
        }
        case ExprKind::MethodCall: {
            auto* m = static_cast<MethodCall*>(e.get());
            if (contains_local_ref_expr(m->target, name)) return true;
            for (auto& a : m->args) if (contains_local_ref_expr(a, name)) return true;
            return false;
        }
        case ExprKind::NewObject:
            for (auto& a : static_cast<NewObject*>(e.get())->args) if (contains_local_ref_expr(a, name)) return true;
            return false;
        case ExprKind::NewArray: {
            auto* n = static_cast<NewArray*>(e.get());
            for (auto& d : n->dims) if (contains_local_ref_expr(d, name)) return true;
            return false;
        }
        case ExprKind::Cast:
            return contains_local_ref_expr(static_cast<Cast*>(e.get())->expr, name);
        case ExprKind::InstanceOf:
            return contains_local_ref_expr(static_cast<InstanceOf*>(e.get())->expr, name);
        case ExprKind::BinOp: {
            auto* b = static_cast<BinOp*>(e.get());
            return contains_local_ref_expr(b->left, name) || contains_local_ref_expr(b->right, name);
        }
        case ExprKind::UnOp:
            return contains_local_ref_expr(static_cast<UnOp*>(e.get())->expr, name);
        case ExprKind::Ternary: {
            auto* t = static_cast<Ternary*>(e.get());
            return contains_local_ref_expr(t->cond, name) || contains_local_ref_expr(t->tval, name) ||
                   contains_local_ref_expr(t->fval, name);
        }
        case ExprKind::Assign: {
            auto* a = static_cast<Assign*>(e.get());
            return contains_local_ref_expr(a->target, name) || contains_local_ref_expr(a->value, name);
        }
        default:
            return false;
    }
}

bool contains_local_ref_stmt(const StmtPtr& s, const std::string& name) {
    if (!s) return false;
    switch (s->kind) {
        case StmtKind::ExprStmt:
            if (contains_local_ref_expr(static_cast<ExprStmtNode*>(s.get())->expr, name)) return true;
            break;
        case StmtKind::ThrowStmt:
            if (contains_local_ref_expr(static_cast<ThrowStmt*>(s.get())->expr, name)) return true;
            break;
        case StmtKind::SyncStmt:
            if (contains_local_ref_expr(static_cast<SyncStmt*>(s.get())->expr, name)) return true;
            break;
        case StmtKind::ReturnStmt:
            if (contains_local_ref_expr(static_cast<ReturnStmt*>(s.get())->expr, name)) return true;
            break;
        case StmtKind::IfStmt:
            if (contains_local_ref_expr(static_cast<IfStmt*>(s.get())->cond, name)) return true;
            break;
        case StmtKind::WhileStmt:
            if (contains_local_ref_expr(static_cast<WhileStmt*>(s.get())->cond, name)) return true;
            break;
        case StmtKind::DoWhileStmt:
            if (contains_local_ref_expr(static_cast<DoWhileStmt*>(s.get())->cond, name)) return true;
            break;
        case StmtKind::ForStmt: {
            auto* f = static_cast<ForStmt*>(s.get());
            if (contains_local_ref_expr(f->init, name)) return true;
            if (contains_local_ref_expr(f->cond, name)) return true;
            if (f->update && contains_local_ref_stmt(f->update, name)) return true;  // через ExprStmtNode.expr
            break;
        }
        case StmtKind::LocalDecl:
            if (contains_local_ref_expr(static_cast<LocalDecl*>(s.get())->init, name)) return true;
            break;
        case StmtKind::SwitchStmt:
            if (contains_local_ref_expr(static_cast<SwitchStmt*>(s.get())->selector, name)) return true;
            break;
        default:
            break;
    }
    switch (s->kind) {
        case StmtKind::IfStmt: {
            auto* i = static_cast<IfStmt*>(s.get());
            for (auto& sub : i->then_body) if (contains_local_ref_stmt(sub, name)) return true;
            if (i->else_body.has_value()) {
                for (auto& sub : *i->else_body) if (contains_local_ref_stmt(sub, name)) return true;
            }
            break;
        }
        case StmtKind::WhileStmt:
            for (auto& sub : static_cast<WhileStmt*>(s.get())->body) if (contains_local_ref_stmt(sub, name)) return true;
            break;
        case StmtKind::DoWhileStmt:
            for (auto& sub : static_cast<DoWhileStmt*>(s.get())->body) if (contains_local_ref_stmt(sub, name)) return true;
            break;
        case StmtKind::ForStmt:
            for (auto& sub : static_cast<ForStmt*>(s.get())->body) if (contains_local_ref_stmt(sub, name)) return true;
            break;
        case StmtKind::TryStmt:
            for (auto& sub : static_cast<TryStmt*>(s.get())->body) if (contains_local_ref_stmt(sub, name)) return true;
            break;
        case StmtKind::SyncStmt:
            for (auto& sub : static_cast<SyncStmt*>(s.get())->body) if (contains_local_ref_stmt(sub, name)) return true;
            break;
        case StmtKind::BlockStmt:
            // BlockStmt.stmts - НЕ в списке атрибутов _contains_local_ref
            // оригинала (там тоже "body", а не "stmts") - не заходим,
            // соответствует оригиналу.
            break;
        default:
            break;
    }
    if (s->kind == StmtKind::SwitchStmt) {
        for (auto& c : static_cast<SwitchStmt*>(s.get())->cases) {
            for (auto& sub : c.body) if (contains_local_ref_stmt(sub, name)) return true;
        }
    }
    if (s->kind == StmtKind::TryStmt) {
        auto* t = static_cast<TryStmt*>(s.get());
        for (auto& c : t->catches) {
            for (auto& sub : c.body) if (contains_local_ref_stmt(sub, name)) return true;
        }
        if (t->finally_body.has_value()) {
            for (auto& sub : *t->finally_body) if (contains_local_ref_stmt(sub, name)) return true;
        }
    }
    return false;
}

[[maybe_unused]] bool contains_local_ref_list(const std::vector<StmtPtr>& stmts, const std::string& name) {
    for (auto& s : stmts) if (contains_local_ref_stmt(s, name)) return true;
    return false;
}

// ---------------- substitute_temp (только внутри Expr-дерева) ----------------

bool substitute_temp(const ExprPtr& node, const std::string& name, const ExprPtr& replacement) {
    if (!node) return false;
    auto try_field = [&](ExprPtr& field) -> bool {
        if (!field) return false;
        if (field->kind == ExprKind::Local && static_cast<Local*>(field.get())->name == name) {
            field = replacement;
            return true;
        }
        return substitute_temp(field, name, replacement);
    };
    switch (node->kind) {
        case ExprKind::FieldAccess:
            return try_field(static_cast<FieldAccess*>(node.get())->target);
        case ExprKind::ArrayAccess: {
            auto* a = static_cast<ArrayAccess*>(node.get());
            if (try_field(a->array)) return true;
            return try_field(a->index);
        }
        case ExprKind::MethodCall: {
            auto* m = static_cast<MethodCall*>(node.get());
            if (try_field(m->target)) return true;
            for (auto& arg : m->args) if (try_field(arg)) return true;
            return false;
        }
        case ExprKind::NewObject: {
            auto* n = static_cast<NewObject*>(node.get());
            for (auto& arg : n->args) if (try_field(arg)) return true;
            return false;
        }
        case ExprKind::NewArray: {
            auto* n = static_cast<NewArray*>(node.get());
            for (auto& d : n->dims) if (try_field(d)) return true;
            return false;
        }
        case ExprKind::Cast:
            return try_field(static_cast<Cast*>(node.get())->expr);
        case ExprKind::InstanceOf:
            return try_field(static_cast<InstanceOf*>(node.get())->expr);
        case ExprKind::BinOp: {
            auto* b = static_cast<BinOp*>(node.get());
            if (try_field(b->left)) return true;
            return try_field(b->right);
        }
        case ExprKind::UnOp:
            return try_field(static_cast<UnOp*>(node.get())->expr);
        case ExprKind::Ternary: {
            auto* t = static_cast<Ternary*>(node.get());
            if (try_field(t->cond)) return true;
            if (try_field(t->tval)) return true;
            return try_field(t->fval);
        }
        case ExprKind::Assign: {
            auto* a = static_cast<Assign*>(node.get());
            if (try_field(a->target)) return true;
            return try_field(a->value);
        }
        default:
            return false;
    }
}

}  // namespace

// ==================== Structurer ====================

Structurer::Structurer(CFG& cfg, const std::map<int64_t, BlockResult>& block_results,
                        const std::vector<ExceptionEntry>& exceptions, MethodCtx& ctx)
    : cfg_(cfg), results_(block_results), exceptions_(exceptions), ctx_(ctx) {
    ipdom_ = cfg_.compute_postdominators();
    prepare_loops();
    prepare_try();
}

void Structurer::prepare_loops() {
    for (auto& [header, body, tails] : cfg_.natural_loops()) {
        (void)tails;
        std::set<int64_t> exits;
        for (int64_t b : body) {
            for (int64_t s : cfg_.blocks.at(b).succs) {
                if (!body.count(s)) exits.insert(s);
            }
        }
        std::optional<int64_t> exit_pc;
        if (exits.empty()) {
            exit_pc = std::nullopt;
        } else if (exits.size() == 1) {
            exit_pc = *exits.begin();
        } else {
            auto ip_it = ipdom_.find(header);
            std::optional<int64_t> ip = (ip_it != ipdom_.end()) ? ip_it->second : std::nullopt;
            if (ip.has_value() && exits.count(*ip)) {
                exit_pc = ip;
            } else {
                exit_pc = *exits.begin();  // std::set - уже отсортирован по возрастанию, begin() == min()
            }
        }
        loop_headers_[header] = {body, exit_pc};
    }
}

std::vector<Structurer::MergedExc> Structurer::merge_split_exception_ranges(const std::vector<ExceptionEntry>& exceptions) const {
    std::vector<std::pair<std::optional<std::string>, int64_t>> order;
    std::map<std::pair<std::optional<std::string>, int64_t>, std::vector<ExceptionEntry>> by_group;
    for (auto& e : exceptions) {
        auto key = std::make_pair(e.catch_type, static_cast<int64_t>(e.handler_pc));
        auto [it, inserted] = by_group.try_emplace(key);
        if (inserted) order.push_back(key);
        it->second.push_back(e);
    }
    std::vector<MergedExc> merged;
    for (auto& k : order) {
        auto es = by_group[k];
        std::stable_sort(es.begin(), es.end(), [](const ExceptionEntry& a, const ExceptionEntry& b) { return a.start_pc < b.start_pc; });
        int64_t min_start = es.front().start_pc;
        int64_t max_end = es.front().end_pc;
        for (auto& e : es) {
            min_start = std::min<int64_t>(min_start, e.start_pc);
            max_end = std::max<int64_t>(max_end, e.end_pc);
        }
        merged.push_back({min_start, max_end, k.first, k.second});
    }
    return merged;
}

void Structurer::prepare_try() {
    auto merged = merge_split_exception_ranges(exceptions_);
    std::vector<std::pair<int64_t, int64_t>> order;
    for (auto& e : merged) {
        auto key = std::make_pair(e.start_pc, e.end_pc);
        if (!try_by_key_.count(key)) order.push_back(key);
        try_by_key_[key].emplace_back(e.catch_type, e.handler_pc);
    }
    for (auto& key : order) {
        try_by_start_[key.first].push_back(key);
    }
}

// ---------------- entry point ----------------

std::vector<StmtPtr> Structurer::build(int64_t entry_pc) {
    auto stmts = region(entry_pc, {});
    recover_unconsumed_blocks(stmts, entry_pc);
    check_full_coverage(entry_pc, stmts);
    return stmts;
}

void Structurer::recover_unconsumed_blocks(std::vector<StmtPtr>& stmts, int64_t entry_pc) {
    std::set<int64_t> reachable;
    std::vector<int64_t> stack = {entry_pc};
    while (!stack.empty()) {
        int64_t b = stack.back();
        stack.pop_back();
        if (reachable.count(b) || !cfg_.blocks.count(b)) continue;
        reachable.insert(b);
        for (int64_t s : cfg_.blocks.at(b).succs) stack.push_back(s);
    }

    while (true) {
        std::set<int64_t> missing;
        for (int64_t pc : reachable) {
            if (!all_consumed_.count(pc)) missing.insert(pc);
        }
        if (missing.empty()) break;

        int64_t next_recover = -1;
        for (int64_t pc : missing) {
            const Block& b = cfg_.blocks.at(pc);
            bool has_real_stmts = false;
            if (results_.count(pc)) {
                for (auto& st : results_.at(pc).stmts) {
                    if (!st) continue;
                    if (st->kind == StmtKind::ReturnStmt && !static_cast<ReturnStmt*>(st.get())->expr) continue;
                    has_real_stmts = true;
                    break;
                }
            }
            if (has_real_stmts || (!b.instrs.empty() && b.instrs[0].mnemonic != "return" && b.instrs[0].mnemonic != "nop" && b.instrs[0].mnemonic != "goto" && b.instrs[0].mnemonic != "goto_w")) {
                next_recover = pc;
                break;
            }
        }

        if (next_recover == -1) {
            for (int64_t pc : missing) all_consumed_.insert(pc);
            break;
        }

        auto extra = region(next_recover, {});
        if (!extra.empty()) {
            stmts.insert(stmts.end(), extra.begin(), extra.end());
        }
        all_consumed_.insert(next_recover);
    }
}

void Structurer::check_full_coverage(int64_t entry_pc, std::vector<StmtPtr>& stmts) {
    std::set<int64_t> reachable;
    std::vector<int64_t> stack = {entry_pc};
    while (!stack.empty()) {
        int64_t b = stack.back();
        stack.pop_back();
        if (reachable.count(b) || !cfg_.blocks.count(b)) continue;
        reachable.insert(b);
        for (int64_t s : cfg_.blocks.at(b).succs) stack.push_back(s);
    }
    std::set<int64_t> missing;
    for (int64_t pc : reachable) {
        if (!all_consumed_.count(pc)) missing.insert(pc);
    }
    std::set<int64_t> real_missing;
    for (int64_t pc : missing) {
        const Block& b = cfg_.blocks.at(pc);
        bool empty_or_pure_jump = false;
        if (b.instrs.empty()) {
            empty_or_pure_jump = true;
        } else if (b.instrs.size() == 1) {
            const std::string& mn = b.instrs[0].mnemonic;
            if (mn == "goto" || mn == "goto_w" || mn == "nop" || mn == "return") {
                empty_or_pure_jump = true;
            }
        } else {
            bool all_nops_or_goto = true;
            for (const auto& ins : b.instrs) {
                if (ins.mnemonic != "nop" && ins.mnemonic != "goto" && ins.mnemonic != "goto_w") {
                    all_nops_or_goto = false;
                    break;
                }
            }
            if (all_nops_or_goto) empty_or_pure_jump = true;
        }
        bool has_real_stmts = false;
        if (results_.count(pc)) {
            for (auto& st : results_.at(pc).stmts) {
                if (!st) continue;
                if (st->kind == StmtKind::ReturnStmt && !static_cast<ReturnStmt*>(st.get())->expr) continue;
                has_real_stmts = true;
                break;
            }
        }
        if (!empty_or_pure_jump || has_real_stmts) {
            real_missing.insert(pc);
        }
    }
    if (!real_missing.empty()) {
        for (int64_t pc : real_missing) {
            if (results_.count(pc)) {
                for (auto& st : results_.at(pc).stmts) {
                    if (st) stmts.push_back(st);
                }
            }
            all_consumed_.insert(pc);
        }
    }
}

// ---------------- core linear region scanner ----------------

std::vector<StmtPtr> Structurer::region(std::optional<int64_t> pc_opt, const std::set<int64_t>& stop_addrs) {
    std::vector<StmtPtr> out;
    std::set<int64_t> seen_here;
    static thread_local int s_region_call_depth = 0;
    s_region_call_depth++;
    if (s_region_call_depth > 50) {
        s_region_call_depth--;
        if (pc_opt) return {std::make_shared<GotoStmt>("block_" + std::to_string(*pc_opt))};
        return {};
    }
    struct DepthGuard { ~DepthGuard() { s_region_call_depth--; } } dguard;
    while (true) {
        guard_ += 1;
        if (guard_ > 1500) break;
        if (!pc_opt.has_value() || !cfg_.blocks.count(*pc_opt) || stop_addrs.count(*pc_opt)) break;
        int64_t pc = *pc_opt;
        if (seen_here.count(pc)) {
            StmtPtr sp = try_resolve_special_target(pc);
            if (sp) {
                out.push_back(sp);
            } else {
                out.push_back(std::make_shared<GotoStmt>("block_" + std::to_string(pc)));
            }
            break;
        }
        seen_here.insert(pc);
        all_consumed_.insert(pc);

        if (try_by_start_.count(pc) && !consumed_try_.count(pc)) {
            auto [stmt, next_pc] = build_try(pc, stop_addrs);
            out.push_back(stmt);
            if (!next_pc.has_value()) break;
            pc_opt = next_pc;
            continue;
        }

        if (loop_headers_.count(pc) && !consumed_loop_.count(pc)) {
            auto [stmt, next_pc] = build_loop(pc, stop_addrs);
            out.push_back(stmt);
            if (!next_pc.has_value()) break;
            pc_opt = next_pc;
            continue;
        }

        const Block& block = cfg_.blocks.at(pc);
        auto rit = results_.find(pc);
        if (rit == results_.end()) {
            if (block.succs.size() == 1) {
                pc_opt = block.succs[0];
                continue;
            }
            break;
        }
        const BlockResult& res = rit->second;
        for (auto& s : res.stmts) out.push_back(s);

        if (res.term_kind == "return" || res.term_kind == "throw") break;

        if (res.term_kind == "if") {
            int64_t true_t = block.succs[0], false_t = block.succs[1];
            auto [stmt, next_pc] = build_if(pc, res.cond, true_t, false_t, stop_addrs);
            if (stmt) out.push_back(stmt);
            if (!next_pc.has_value()) break;
            pc_opt = next_pc;
            continue;
        }

        if (res.term_kind == "switch") {
            auto [stmt, next_pc] = build_switch(pc, res.cond, block, stop_addrs);
            out.push_back(stmt);
            if (!next_pc.has_value()) break;
            pc_opt = next_pc;
            continue;
        }

        const Instruction* last_ins = block.instrs.empty() ? nullptr : &block.instrs.back();
        if (last_ins && (last_ins->mnemonic == "goto" || last_ins->mnemonic == "goto_w")) {
            int64_t target = *last_ins->target;
            StmtPtr special_stmt;
            JumpKind jk = resolve_jump_stmt(target, stop_addrs, special_stmt);
            if (jk == JumpKind::NoStmt) {
                break;
            } else if (jk == JumpKind::ContinueLinearly) {
                pc_opt = target;
                continue;
            } else {
                out.push_back(special_stmt);
                break;
            }
        } else {
            if (!block.succs.empty()) {
                pc_opt = block.succs[0];
                continue;
            }
            break;
        }
    }
    return out;
}

// ---------------- jump resolution ----------------

Structurer::JumpKind Structurer::resolve_jump_stmt(int64_t target, const std::set<int64_t>& stop_addrs, StmtPtr& out_stmt) {
    for (auto it = loop_stack_.rbegin(); it != loop_stack_.rend(); ++it) {
        StackEntryPtr entry = *it;
        if (entry->header.has_value() && *entry->header == target) {
            if (entry == loop_stack_.back()) {
                out_stmt = std::make_shared<ContinueStmt>();
            } else {
                if (!entry->label.has_value()) entry->label = new_label();
                out_stmt = std::make_shared<ContinueStmt>(entry->label);
            }
            return JumpKind::Stmt;
        }
    }
    for (auto it = breakable_stack_.rbegin(); it != breakable_stack_.rend(); ++it) {
        StackEntryPtr entry = *it;
        if ((entry->exit.has_value() && *entry->exit == target) || entry->all_exits.count(target)) {
            if (entry == breakable_stack_.back()) {
                out_stmt = std::make_shared<BreakStmt>();
            } else {
                if (!entry->label.has_value()) entry->label = new_label();
                out_stmt = std::make_shared<BreakStmt>(entry->label);
            }
            return JumpKind::Stmt;
        }
    }
    if (stop_addrs.count(target)) return JumpKind::NoStmt;
    for (auto it = try_exit_stack_.rbegin(); it != try_exit_stack_.rend(); ++it) {
        if (*it == target) return JumpKind::NoStmt;
    }
    auto tit = cfg_.blocks.find(target);
    if (tit != cfg_.blocks.end()) {
        // v2.0.6: Если целевой блок состоит из чистого return (void return или возврат константы/локала),
        // заменяем нередуцируемый маркер на чистый оператор return
        if (tit->second.instrs.size() == 1 && tit->second.instrs[0].mnemonic == "return") {
            out_stmt = std::make_shared<ReturnStmt>(nullptr);
            return JumpKind::Stmt;
        }
        auto rit = results_.find(target);
        if (rit != results_.end()) {
            if (rit->second.stmts.size() == 1 && rit->second.stmts[0]->kind == StmtKind::ReturnStmt) {
                out_stmt = rit->second.stmts[0];
                return JumpKind::Stmt;
            }
            if (rit->second.stmts.empty() && rit->second.term_kind == "return") {
                out_stmt = std::make_shared<ReturnStmt>(nullptr);
                return JumpKind::Stmt;
            }
            // v2.0.6: Если целевой блок является чистым терминальным блоком (завершается return без последующих ветвлений),
            // подставляем его тело/возврат напрямую, исключая синтетический маркер goto
            if (tit->second.succs.empty() && rit->second.term_kind == "return" && !rit->second.stmts.empty() &&
                rit->second.stmts.back()->kind == StmtKind::ReturnStmt && rit->second.stmts.size() <= 3) {
                if (rit->second.stmts.size() == 1) {
                    out_stmt = rit->second.stmts[0];
                } else {
                    out_stmt = std::make_shared<BlockStmt>(rit->second.stmts);
                }
                return JumpKind::Stmt;
            }
        }
        if (tit->second.instrs.size() == 1 &&
            (tit->second.instrs[0].mnemonic == "goto" || tit->second.instrs[0].mnemonic == "goto_w") &&
            (!results_.count(target) || results_.at(target).stmts.empty())) {
            return JumpKind::ContinueLinearly;
        }
        if (!all_consumed_.count(target) && !stop_addrs.count(target)) {
            return JumpKind::ContinueLinearly;
        }
    }
    // HANDOFF: было throw DecompileAbort(...) здесь - ЛЮБОЙ нередуцируемый
    // goto откатывал ВЕСЬ метод целиком к сырому байткоду, даже если он
    // встречался в одном редком побочном ответвлении (напр. один case
    // switch'а или редкая ветка catch), а остальной метод прекрасно
    // структурировался. Полноценный node-splitting - отдельная большая
    // задача (см. HANDOFF_49), НЕ делаем это здесь наспех. Вместо этого -
    // локальная деградация: GotoStmt уже существовал в AST/emit.cpp
    // (печатается как некомпилируемый комментарий-маркер), но никогда не
    // конструировался - throw происходил раньше, чем до него доходило
    // дело. Теперь именно ЭТА ветка перехода превращается в маркер, а не
    // весь метод - при малом числе таких переходов итоговый метод почти
    // весь остаётся читаемым структурированным Java (маркер явно виден
    // и не выдаётся за компилируемый код), вместо полного отката.
    // Осторожность: не трогаем ContinueLinearly-путь и loop/break-резолвы
    // выше - только этот последний, ранее фатальный, случай.
    out_stmt = std::make_shared<GotoStmt>(std::to_string(target));
    return JumpKind::Stmt;
}

StmtPtr Structurer::try_resolve_special_target(int64_t target) {
    for (auto it = loop_stack_.rbegin(); it != loop_stack_.rend(); ++it) {
        StackEntryPtr entry = *it;
        if (entry->header.has_value() && *entry->header == target) {
            if (entry == loop_stack_.back()) return std::make_shared<ContinueStmt>();
            if (!entry->label.has_value()) entry->label = new_label();
            return std::make_shared<ContinueStmt>(entry->label);
        }
    }
    for (auto it = breakable_stack_.rbegin(); it != breakable_stack_.rend(); ++it) {
        StackEntryPtr entry = *it;
        if ((entry->exit.has_value() && *entry->exit == target) || entry->all_exits.count(target)) {
            if (entry == breakable_stack_.back()) return std::make_shared<BreakStmt>();
            if (!entry->label.has_value()) entry->label = new_label();
            return std::make_shared<BreakStmt>(entry->label);
        }
    }
    return nullptr;
}

std::string Structurer::new_label() {
    label_ctr_ += 1;
    return "loop" + std::to_string(label_ctr_);
}

// ---------------- is_terminating / find_forward_merge ----------------

bool Structurer::is_terminating(int64_t pc) {
    std::set<int64_t> seen;
    return is_terminating(pc, 0, seen);
}

bool Structurer::is_terminating(int64_t pc, int depth, std::set<int64_t>& seen) {
    auto cache_it = terminates_cache_.find(pc);
    if (cache_it != terminates_cache_.end()) return cache_it->second;
    if (depth > 60 || seen.count(pc) || !cfg_.blocks.count(pc)) {
        return false;
    }
    seen.insert(pc);
    auto res_it = results_.find(pc);
    if (res_it == results_.end()) {
        seen.erase(pc);
        return false;
    }
    const BlockResult& res = res_it->second;
    bool result = false;
    if (res.term_kind == "return" || res.term_kind == "throw") {
        result = true;
    } else if (res.term_kind == "if") {
        auto& succs = cfg_.blocks.at(pc).succs;
        if (succs.size() == 2) {
            result = is_terminating(succs[0], depth + 1, seen) && is_terminating(succs[1], depth + 1, seen);
        }
    } else if (res.term_kind == "switch" || loop_headers_.count(pc) || try_by_start_.count(pc)) {
        result = false;
    } else {
        const Block& block = cfg_.blocks.at(pc);
        const Instruction* last = block.instrs.empty() ? nullptr : &block.instrs.back();
        if (last && (last->mnemonic == "goto" || last->mnemonic == "goto_w")) {
            result = is_terminating(*last->target, depth + 1, seen);
        } else if (!block.succs.empty()) {
            result = is_terminating(block.succs[0], depth + 1, seen);
        } else {
            result = true;
        }
    }
    seen.erase(pc);
    terminates_cache_[pc] = result;
    return result;
}

std::optional<int64_t> Structurer::find_forward_merge(int64_t true_t, int64_t false_t, const std::set<int64_t>& stop_addrs,
                                                        bool exclude_starts) {
    auto reachable = [&](int64_t start) -> std::set<int64_t> {
        std::set<int64_t> seen;
        std::vector<int64_t> frontier = {start};
        size_t limit = 4000;
        while (!frontier.empty() && seen.size() < limit) {
            int64_t pc = frontier.back();
            frontier.pop_back();
            if (seen.count(pc) || !cfg_.blocks.count(pc)) continue;
            const Block& block = cfg_.blocks.at(pc);
            if (!block.handler_types.empty()) continue;
            seen.insert(pc);
            if (pc != start && stop_addrs.count(pc)) continue;
            for (int64_t s : block.succs) {
                if (s > pc) frontier.push_back(s);
            }
        }
        return seen;
    };
    std::set<int64_t> r1 = reachable(true_t);
    std::set<int64_t> r2 = reachable(false_t);
    std::set<int64_t> common;
    std::set_intersection(r1.begin(), r1.end(), r2.begin(), r2.end(), std::inserter(common, common.begin()));
    if (exclude_starts) {
        common.erase(true_t);
        common.erase(false_t);
    }
    if (common.empty()) return std::nullopt;
    return *common.begin();
}

// ---------------- if/else ----------------

std::pair<StmtPtr, std::optional<int64_t>> Structurer::build_if(int64_t pc, ExprPtr cond, int64_t true_t, int64_t false_t,
                                                                   const std::set<int64_t>& stop_addrs) {
    if_chain_depth_ += 1;
    // Ограничение глубины вложенности guard-цепочек для предотвращения анти-декомпиляторных ловушек (как в AegisArmorTrap)
    if (if_chain_depth_ > 45) {
        if_chain_depth_ -= 1;
        auto then_stmt = std::make_shared<GotoStmt>("block_" + std::to_string(true_t));
        auto else_stmt = std::make_shared<GotoStmt>("block_" + std::to_string(false_t));
        return {std::make_shared<IfStmt>(cond, std::vector<StmtPtr>{then_stmt}, std::vector<StmtPtr>{else_stmt}), std::nullopt};
    }

    // Short-circuit condition coalescing: combine chains of simple if-tests into && and ||
    bool progressed = true;
    while (progressed) {
        progressed = false;
        // 1. Logical AND (&&): if true_t is an if-block whose false-target is false_t (and preds == 1)
        while (results_.count(true_t) && cfg_.blocks.count(true_t) && !stop_addrs.count(true_t) &&
               !loop_headers_.count(true_t) && !try_by_start_.count(true_t)) {
            const BlockResult& t_res = results_.at(true_t);
            const Block& t_blk = cfg_.blocks.at(true_t);
            if (cond && t_res.cond && t_res.term_kind == "if" && t_res.stmts.empty() &&
                t_blk.succs.size() == 2 && t_blk.preds.size() == 1 && t_blk.handler_types.empty()) {
                if (t_blk.succs[1] == false_t && t_blk.succs[0] != false_t) {
                    cond = std::make_shared<BinOp>("&&", cond, t_res.cond, "boolean");
                    true_t = t_blk.succs[0];
                    progressed = true;
                    continue;
                } else if (t_blk.succs[0] == false_t && t_blk.succs[1] != false_t) {
                    cond = std::make_shared<BinOp>("&&", cond, negate(t_res.cond), "boolean");
                    true_t = t_blk.succs[1];
                    progressed = true;
                    continue;
                }
            }
            break;
        }
        // 2. Logical OR (||): if false_t is an if-block whose true-target is true_t (and preds == 1)
        while (results_.count(false_t) && cfg_.blocks.count(false_t) && !stop_addrs.count(false_t) &&
               !loop_headers_.count(false_t) && !try_by_start_.count(false_t)) {
            const BlockResult& f_res = results_.at(false_t);
            const Block& f_blk = cfg_.blocks.at(false_t);
            if (cond && f_res.cond && f_res.term_kind == "if" && f_res.stmts.empty() &&
                f_blk.succs.size() == 2 && f_blk.preds.size() == 1 && f_blk.handler_types.empty()) {
                if (f_blk.succs[0] == true_t && f_blk.succs[1] != true_t) {
                    cond = std::make_shared<BinOp>("||", cond, f_res.cond, "boolean");
                    false_t = f_blk.succs[1];
                    progressed = true;
                    continue;
                } else if (f_blk.succs[1] == true_t && f_blk.succs[0] != true_t) {
                    cond = std::make_shared<BinOp>("||", cond, negate(f_res.cond), "boolean");
                    false_t = f_blk.succs[0];
                    progressed = true;
                    continue;
                }
            }
            break;
        }
    }

    try {
        auto result = build_if_inner(pc, cond, true_t, false_t, stop_addrs);
        if_chain_depth_ -= 1;
        return result;
    } catch (...) {
        if_chain_depth_ -= 1;
        throw;
    }
}

std::pair<StmtPtr, std::optional<int64_t>> Structurer::build_if_inner(int64_t pc, ExprPtr cond, int64_t true_t, int64_t false_t,
                                                                         const std::set<int64_t>& stop_addrs) {
    bool was_right_after_try = last_try_merge_pc_.has_value() && *last_try_merge_pc_ == pc;
    last_try_merge_pc_ = std::nullopt;

    StmtPtr sp_true = try_resolve_special_target(true_t);
    StmtPtr sp_false = try_resolve_special_target(false_t);

    if (sp_true && sp_false) {
        auto merge_it = ipdom_.find(pc);
        std::optional<int64_t> merge = (merge_it != ipdom_.end()) ? merge_it->second : std::nullopt;
        auto stmt = std::make_shared<IfStmt>(cond, std::vector<StmtPtr>{sp_true}, std::optional<std::vector<StmtPtr>>(std::vector<StmtPtr>{sp_false}));
        return {stmt, (merge.has_value() && !stop_addrs.count(*merge)) ? merge : std::nullopt};
    }
    if (sp_true) {
        auto stmt = std::make_shared<IfStmt>(cond, std::vector<StmtPtr>{sp_true}, std::nullopt);
        return {stmt, false_t};
    }
    if (sp_false) {
        auto stmt = std::make_shared<IfStmt>(negate(cond), std::vector<StmtPtr>{sp_false}, std::nullopt);
        return {stmt, true_t};
    }

    auto merge_it = ipdom_.find(pc);
    std::optional<int64_t> merge = (merge_it != ipdom_.end()) ? merge_it->second : std::nullopt;
    if (!merge.has_value()) {
        bool t_term = is_terminating(true_t);
        bool f_term = is_terminating(false_t);
        if (t_term && !f_term) {
            auto raw_it = ipdom_.find(false_t);
            std::optional<int64_t> raw = (raw_it != ipdom_.end()) ? raw_it->second : std::nullopt;
            if (!raw.has_value()) raw = find_forward_merge(true_t, false_t, stop_addrs, true);
            merge = raw.has_value() ? raw : std::optional<int64_t>(false_t);
        } else if (f_term && !t_term) {
            auto raw_it = ipdom_.find(true_t);
            std::optional<int64_t> raw = (raw_it != ipdom_.end()) ? raw_it->second : std::nullopt;
            if (!raw.has_value()) raw = find_forward_merge(true_t, false_t, stop_addrs, true);
            merge = raw.has_value() ? raw : std::optional<int64_t>(true_t);
        }
        if (!merge.has_value()) {
            merge = find_forward_merge(true_t, false_t, stop_addrs, false);
        }
    }
    {
        auto fwd_merge = find_forward_merge(true_t, false_t, stop_addrs, true);
        if (fwd_merge.has_value()) {
            if (!is_terminating(*fwd_merge) || !try_exit_stack_.empty()) {
                if (!merge.has_value() || *fwd_merge < *merge) {
                    merge = fwd_merge;
                }
            }
        }
    }
    if (merge.has_value()) {
        for (auto& entry : loop_stack_) {
            if (entry->header.has_value() && *entry->header == *merge) {
                merge = std::nullopt;
                break;
            }
        }
    }
    std::set<int64_t> local_stop = stop_addrs;
    if (merge.has_value()) local_stop.insert(*merge);
    auto then_body = region(true_t, local_stop);
    std::vector<StmtPtr> else_body_vec;
    bool has_else = !((merge.has_value() && false_t == *merge) || stop_addrs.count(false_t));
    std::optional<std::vector<StmtPtr>> else_body;
    if (has_else) {
        else_body = region(false_t, local_stop);
    } else {
        else_body = std::nullopt;
    }
    if (then_body.size() == 1 && else_body.has_value() && else_body->size() == 1) {
        auto* ret_then = dynamic_cast<ReturnStmt*>(then_body[0].get());
        auto* ret_else = dynamic_cast<ReturnStmt*>((*else_body)[0].get());
        if (ret_then != nullptr && ret_then->expr != nullptr &&
            ret_else != nullptr && ret_else->expr != nullptr) {
            auto* const_then = dynamic_cast<Const*>(ret_then->expr.get());
            auto* const_else = dynamic_cast<Const*>(ret_else->expr.get());
            if (const_then != nullptr && const_else != nullptr &&
                const_then->type == "boolean" && const_else->type == "boolean") {
                if (const_then->value == "true" && const_else->value == "false") {
                    return {std::make_shared<ReturnStmt>(cond), merge};
                } else if (const_then->value == "false" && const_else->value == "true") {
                    return {std::make_shared<ReturnStmt>(negate(cond)), merge};
                }
            }
        }
    }
    auto stmt = std::make_shared<IfStmt>(cond, then_body, else_body);
    return {stmt, merge};
}

// ---------------- loops ----------------

std::pair<StmtPtr, std::optional<int64_t>> Structurer::build_loop(int64_t header_pc, const std::set<int64_t>& stop_addrs) {
    auto& [body_set, exit_pc] = loop_headers_.at(header_pc);
    (void)body_set;
    consumed_loop_.insert(header_pc);
    auto entry = std::make_shared<StackEntry>();
    entry->header = header_pc;
    entry->exit = exit_pc;
    entry->label = std::nullopt;
    for (int64_t b : body_set) {
        if (cfg_.blocks.count(b)) {
            for (int64_t s : cfg_.blocks.at(b).succs) {
                if (!body_set.count(s)) entry->all_exits.insert(s);
            }
        }
    }
    if (exit_pc.has_value()) entry->all_exits.insert(*exit_pc);
    loop_stack_.push_back(entry);
    breakable_stack_.push_back(entry);
    std::set<int64_t> local_stop = stop_addrs;
    if (exit_pc.has_value()) local_stop.insert(*exit_pc);
    auto body = region(header_pc, local_stop);
    loop_stack_.pop_back();
    breakable_stack_.pop_back();
    consumed_loop_.erase(header_pc);
    auto stmt = std::make_shared<WhileStmt>(std::make_shared<Const>("true", "boolean"), body, entry->label);
    return {stmt, exit_pc};
}

// ---------------- switch ----------------

std::pair<StmtPtr, std::optional<int64_t>> Structurer::build_switch(int64_t pc, ExprPtr selector, const Block& block,
                                                                       const std::set<int64_t>& stop_addrs) {
    if (selector && selector->kind == ExprKind::ArrayAccess) {
        auto* aa = static_cast<ArrayAccess*>(selector.get());
        if (aa->array && aa->array->kind == ExprKind::FieldAccess) {
            auto* fa = static_cast<FieldAccess*>(aa->array.get());
            if (fa->name.find("$SwitchMap$") != std::string::npos && aa->index && aa->index->kind == ExprKind::MethodCall) {
                auto* mc = static_cast<MethodCall*>(aa->index.get());
                if (mc->name == "ordinal" && mc->target) {
                    selector = mc->target;
                }
            }
        }
    }
    const Instruction& last_ins = block.instrs.back();
    const SwitchTargets& targets = *last_ins.targets;
    auto merge_it = ipdom_.find(pc);
    std::optional<int64_t> merge = (merge_it != ipdom_.end()) ? merge_it->second : std::nullopt;
    auto entry = std::make_shared<StackEntry>();
    entry->exit = merge;
    entry->label = std::nullopt;
    breakable_stack_.push_back(entry);

    std::vector<int64_t> label_order;
    std::map<int64_t, std::vector<std::string>> label_map;
    std::vector<std::pair<int64_t, int64_t>> sorted_vt;  // (value, target) отсортировано по value
    for (auto& [v, t] : targets) {
        if (v.has_value()) sorted_vt.emplace_back(*v, t);
    }
    std::sort(sorted_vt.begin(), sorted_vt.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (auto& [v, t] : sorted_vt) {
        if (!label_map.count(t)) label_order.push_back(t);
        label_map[t].push_back(std::to_string(v));
    }
    std::optional<int64_t> default_t;
    for (auto& [v, t] : targets) {
        if (!v.has_value()) { default_t = t; break; }
    }
    bool default_is_exit = false;
    if (default_t.has_value()) {
        if (merge.has_value() && *default_t == *merge) {
            default_is_exit = true;
        } else {
            // Check if any explicit case jumps to *default_t (indicates default_t is a break exit)
            std::set<int64_t> visited;
            std::vector<int64_t> q;
            for (auto& [v, t] : targets) {
                if (v.has_value()) q.push_back(t);
            }
            while (!q.empty()) {
                int64_t cur = q.back();
                q.pop_back();
                if (visited.count(cur) || !cfg_.blocks.count(cur)) continue;
                visited.insert(cur);
                const Block& b = cfg_.blocks.at(cur);
                if (!b.instrs.empty()) {
                    const auto& last = b.instrs.back();
                    if ((last.mnemonic == "goto" || last.mnemonic == "goto_w") && last.target.has_value()) {
                        if (*last.target == *default_t) {
                            default_is_exit = true;
                            break;
                        }
                    }
                }
                for (int64_t succ : b.succs) {
                    if (succ > pc && succ != *default_t && !visited.count(succ)) {
                        if (succ < *default_t) {
                            q.push_back(succ);
                        }
                    }
                }
            }
        }
        if (default_is_exit) {
            if (!merge.has_value() || *default_t < *merge) {
                merge = default_t;
                entry->exit = merge;
            }
            entry->all_exits.insert(*default_t);
        } else {
            if (!label_map.count(*default_t)) label_order.push_back(*default_t);
            label_map[*default_t].push_back("default");
        }
    }

    if (label_map.empty()) {
        breakable_stack_.pop_back();
        auto stmt = std::make_shared<SwitchStmt>(selector, std::vector<SwitchCase>{}, entry->label);
        return {stmt, merge};
    }

    std::vector<int64_t> case_addrs(label_order.begin(), label_order.end());
    std::sort(case_addrs.begin(), case_addrs.end());

    // Обнаружение целевого выхода (break) из ветвей switch, когда ipdom_ пуст (например из-за throw в default)
    std::map<int64_t, int> exit_candidates;
    for (int64_t c_addr : case_addrs) {
        int64_t cur = c_addr;
        std::set<int64_t> visited;
        while (cfg_.blocks.count(cur) && !visited.count(cur)) {
            visited.insert(cur);
            const Block& b = cfg_.blocks.at(cur);
            if (!b.instrs.empty() && (b.instrs.back().mnemonic == "goto" || b.instrs.back().mnemonic == "goto_w")) {
                if (b.instrs.back().target.has_value()) {
                    int64_t tgt = *b.instrs.back().target;
                    if (tgt > pc && !label_map.count(tgt)) {
                        exit_candidates[tgt]++;
                        break;
                    }
                }
            }
            if (b.succs.size() == 1) {
                int64_t s = b.succs[0];
                if (s > pc && !label_map.count(s)) {
                    exit_candidates[s]++;
                    break;
                }
                cur = s;
            } else {
                break;
            }
        }
    }
    if (!exit_candidates.empty()) {
        int64_t best_tgt = -1;
        int max_count = -1;
        for (auto& [tgt, count] : exit_candidates) {
            if (count > max_count || (count == max_count && tgt < best_tgt)) {
                max_count = count;
                best_tgt = tgt;
            }
        }
        if (best_tgt != -1 && (!merge.has_value() || best_tgt < *merge)) {
            merge = best_tgt;
            entry->exit = merge;
        }
        for (auto& [tgt, count] : exit_candidates) {
            entry->all_exits.insert(tgt);
        }
    }
    if (merge.has_value()) {
        entry->all_exits.insert(*merge);
    }

    std::set<int64_t> local_stop_base = stop_addrs;
    if (merge.has_value()) local_stop_base.insert(*merge);
    for (int64_t ae : entry->all_exits) local_stop_base.insert(ae);

    std::vector<SwitchCase> cases;
    for (size_t idx = 0; idx < case_addrs.size(); ++idx) {
        int64_t addr = case_addrs[idx];
        std::optional<int64_t> next_addr = (idx + 1 < case_addrs.size()) ? std::optional<int64_t>(case_addrs[idx + 1]) : std::nullopt;
        std::set<int64_t> case_stop = local_stop_base;
        if (next_addr.has_value()) case_stop.insert(*next_addr);
        auto body = region(addr, case_stop);
        std::vector<std::string> values;
        bool is_default = false;
        for (auto& v : label_map.at(addr)) {
            if (v == "default") is_default = true;
            else values.push_back(v);
        }
        cases.push_back({values, body, is_default});
    }

    breakable_stack_.pop_back();
    auto stmt = std::make_shared<SwitchStmt>(selector, cases, entry->label);
    return {stmt, merge};
}

// ---------------- try/catch ----------------

std::pair<StmtPtr, std::optional<int64_t>> Structurer::build_try(int64_t pc, const std::set<int64_t>& stop_addrs) {
    auto key = try_by_start_.at(pc)[0];
    int64_t start = key.first, end = key.second;
    auto& entries = try_by_key_.at(key);
    consumed_try_.insert(pc);

    std::set<int64_t> handler_pcs;
    for (auto& [ct, h] : entries) handler_pcs.insert(h);

    auto overall_it = ipdom_.find(start);
    std::optional<int64_t> overall_merge = (overall_it != ipdom_.end()) ? overall_it->second : std::nullopt;
    if (overall_merge.has_value() && (stop_addrs.count(*overall_merge) || handler_pcs.count(*overall_merge))) {
        overall_merge = std::nullopt;
    }

    if (cfg_.blocks.count(end) && !stop_addrs.count(end)) {
        const Block& end_block = cfg_.blocks.at(end);
        bool is_trampoline = end_block.instrs.size() == 1 &&
                              (end_block.instrs[0].mnemonic == "goto" || end_block.instrs[0].mnemonic == "goto_w");
        if (!is_trampoline && !handler_pcs.count(end)) {
            if (!overall_merge.has_value() || end < *overall_merge) overall_merge = end;
        } else if (is_trampoline && !end_block.succs.empty()) {
            int64_t target = end_block.succs[0];
            if (!stop_addrs.count(target) && !handler_pcs.count(target)) {
                if (!overall_merge.has_value() || target < *overall_merge) overall_merge = target;
            }
        }
    }

    if (!overall_merge.has_value()) {
        std::set<int64_t> candidate_merges;
        for (auto& [bpc, blk] : cfg_.blocks) {
            if (bpc >= start && bpc < end) {
                for (int64_t succ : blk.succs) {
                    if (succ >= end && !stop_addrs.count(succ) && !handler_pcs.count(succ) && cfg_.blocks.count(succ)) {
                        candidate_merges.insert(succ);
                    }
                }
            }
        }
        if (!candidate_merges.empty()) {
            overall_merge = *candidate_merges.begin();
        }
    }

    // v2.0.6: Если тело try завершается возвратом/throw и не имеет выходов,
    // ищем общий выход среди обработчиков catch
    if (!overall_merge.has_value()) {
        std::set<int64_t> catch_merges;
        for (int64_t hpc : handler_pcs) {
            std::set<int64_t> h_visited;
            std::vector<int64_t> h_stack = {hpc};
            while (!h_stack.empty()) {
                int64_t curr = h_stack.back();
                h_stack.pop_back();
                if (h_visited.count(curr) || !cfg_.blocks.count(curr)) continue;
                h_visited.insert(curr);
                const Block& b = cfg_.blocks.at(curr);
                for (int64_t s : b.succs) {
                    if (!handler_pcs.count(s) && !stop_addrs.count(s) && cfg_.blocks.count(s)) {
                        if (s >= end || s < start) {
                            catch_merges.insert(s);
                        } else if (!h_visited.count(s)) {
                            h_stack.push_back(s);
                        }
                    }
                }
            }
        }
        if (!catch_merges.empty()) {
            overall_merge = *catch_merges.begin();
        }
    }

    bool pushed_merge = false;
    if (overall_merge.has_value()) {
        try_exit_stack_.push_back(*overall_merge);
        pushed_merge = true;
    }
    struct TryExitGuard {
        std::vector<int64_t>& stack;
        bool pushed;
        ~TryExitGuard() { if (pushed) stack.pop_back(); }
    } exit_guard{try_exit_stack_, pushed_merge};

    std::set<int64_t> body_stop = stop_addrs;
    body_stop.insert(end);
    if (overall_merge.has_value()) body_stop.insert(*overall_merge);
    auto body = region(start, body_stop);

    std::vector<CatchClause> catches;
    std::set<int64_t> seen_handlers;
    for (auto& [catch_type, handler_pc] : entries) {
        if (seen_handlers.count(handler_pc)) continue;
        seen_handlers.insert(handler_pc);
        catch_var_ctr_ += 1;
        std::string disp_type = catch_type.has_value() ? ctx_.owner_display(*catch_type) : "Throwable";
        auto merge2_it = ipdom_.find(handler_pc);
        std::optional<int64_t> merge2;
        if (merge2_it != ipdom_.end()) {
            merge2 = merge2_it->second;
        } else {
            merge2 = std::nullopt;
        }
        std::set<int64_t> local_stop = stop_addrs;
        if (merge2.has_value()) local_stop.insert(*merge2);
        if (overall_merge.has_value()) local_stop.insert(*overall_merge);
        auto cbody = region(handler_pc, local_stop);
        std::string var_name = "e" + std::to_string(catch_var_ctr_);
        if (!cbody.empty() && cbody[0]->kind == StmtKind::LocalDecl && is_sentinel(static_cast<LocalDecl*>(cbody[0].get())->init)) {
            std::string old_name = static_cast<LocalDecl*>(cbody[0].get())->name;
            cbody.erase(cbody.begin());
            if (old_name.rfind("var", 0) == 0 || old_name.rfind("__", 0) == 0 || old_name == "obj" || old_name == "arg") {
                rename_local(cbody, old_name, var_name);
            } else {
                var_name = old_name;
                rename_sentinel(cbody, var_name);
            }
        } else if (!cbody.empty() && cbody[0]->kind == StmtKind::ExprStmt) {
            auto* es = static_cast<ExprStmtNode*>(cbody[0].get());
            if (es->expr && es->expr->kind == ExprKind::Assign) {
                auto* a = static_cast<Assign*>(es->expr.get());
                if (is_sentinel(a->value) && a->target && a->target->kind == ExprKind::Local) {
                    std::string old_name = static_cast<Local*>(a->target.get())->name;
                    cbody.erase(cbody.begin());
                    if (old_name.rfind("var", 0) == 0 || old_name.rfind("__", 0) == 0 || old_name == "obj" || old_name == "arg") {
                        rename_local(cbody, old_name, var_name);
                    } else {
                        var_name = old_name;
                        rename_sentinel(cbody, var_name);
                    }
                } else {
                    rename_sentinel(cbody, var_name);
                }
            } else {
                rename_sentinel(cbody, var_name);
            }
        } else {
            rename_sentinel(cbody, var_name);
        }
        catches.push_back({disp_type, var_name, cbody});
    }

    if (!overall_merge.has_value()) {
        std::set<int64_t> candidate_merges;
        for (auto& [bpc, blk] : cfg_.blocks) {
            if (bpc >= start && bpc < end && all_consumed_.count(bpc)) {
                for (int64_t succ : blk.succs) {
                    if (!all_consumed_.count(succ) && !stop_addrs.count(succ) && !handler_pcs.count(succ) && cfg_.blocks.count(succ)) {
                        candidate_merges.insert(succ);
                    }
                }
            }
        }
        if (!candidate_merges.empty()) {
            overall_merge = *candidate_merges.begin();
        }
    }
    if (!overall_merge.has_value()) {
        std::set<int64_t> catch_post_merges;
        for (auto& [bpc, blk] : cfg_.blocks) {
            if (all_consumed_.count(bpc)) {
                for (int64_t succ : blk.succs) {
                    if (!all_consumed_.count(succ) && !stop_addrs.count(succ) && !handler_pcs.count(succ) && cfg_.blocks.count(succ)) {
                        if (succ >= end || succ < start) {
                            catch_post_merges.insert(succ);
                        }
                    }
                }
            }
        }
        if (!catch_post_merges.empty()) {
            overall_merge = *catch_post_merges.begin();
        }
    }
    last_try_merge_pc_ = overall_merge;
    auto stmt = std::make_shared<TryStmt>(body, catches, std::nullopt);
    return {stmt, overall_merge};
}

// ==================== loop beautification / постобработка ====================

bool is_synth_temp(const std::string& name) {
    if (name.rfind("__", 0) == 0) return true;
    if (name.rfind("temp", 0) == 0 && name.size() > 4) {
        for (size_t i = 4; i < name.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(name[i]))) return false;
        }
        return true;
    }
    return false;
}

namespace {

std::optional<std::pair<ExprPtr, ExprPtr>> as_assign(const StmtPtr& stmt) {
    if (!stmt) return std::nullopt;
    if (stmt->kind == StmtKind::ExprStmt) {
        auto* es = static_cast<ExprStmtNode*>(stmt.get());
        if (es->expr && es->expr->kind == ExprKind::Assign) {
            auto* a = static_cast<Assign*>(es->expr.get());
            return std::make_pair(a->target, a->value);
        }
    } else if (stmt->kind == StmtKind::LocalDecl) {
        auto* ld = static_cast<LocalDecl*>(stmt.get());
        if (ld->init) {
            return std::make_pair(std::make_shared<Local>(ld->name, ld->type), ld->init);
        }
    }
    return std::nullopt;
}

std::optional<int> as_bool_const(const ExprPtr& v) {
    if (v && v->kind == ExprKind::Const) {
        auto* c = static_cast<Const*>(v.get());
        if ((c->type == "int" || c->type == "boolean")) {
            if (c->literal == "0" || c->literal == "false") return 0;
            if (c->literal == "1" || c->literal == "true") return 1;
        }
    }
    return std::nullopt;
}

bool same_target(const ExprPtr& a, const ExprPtr& b) {
    if (!a || !b) return false;
    if (a->kind == ExprKind::Local && b->kind == ExprKind::Local) {
        return static_cast<Local*>(a.get())->name == static_cast<Local*>(b.get())->name;
    }
    if (a->kind == ExprKind::FieldAccess && b->kind == ExprKind::FieldAccess) {
        auto* fa = static_cast<FieldAccess*>(a.get());
        auto* fb = static_cast<FieldAccess*>(b.get());
        return fa->name == fb->name && fa->is_static == fb->is_static && (fa->target == nullptr) == (fb->target == nullptr);
    }
    return false;
}

bool is_plain_break(const StmtPtr& x) {
    return x && x->kind == StmtKind::BreakStmt && !static_cast<BreakStmt*>(x.get())->label.has_value();
}
bool is_plain_continue(const StmtPtr& x) {
    return x && x->kind == StmtKind::ContinueStmt && !static_cast<ContinueStmt*>(x.get())->label.has_value();
}

bool looks_like_update(const StmtPtr& stmt) {
    if (!stmt || stmt->kind != StmtKind::ExprStmt) return false;
    ExprPtr e = static_cast<ExprStmtNode*>(stmt.get())->expr;
    if (!e) return false;
    if (e->kind == ExprKind::UnOp) {
        auto* u = static_cast<UnOp*>(e.get());
        if (u->op == "++" || u->op == "--") return true;
    }
    return e->kind == ExprKind::Assign;
}

static bool is_same_expr(const ExprPtr& a, const ExprPtr& b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    switch (a->kind) {
        case ExprKind::Local:
            return static_cast<Local*>(a.get())->name == static_cast<Local*>(b.get())->name;
        case ExprKind::Const:
            return static_cast<Const*>(a.get())->literal == static_cast<Const*>(b.get())->literal;
        case ExprKind::This:
            return true;
        case ExprKind::FieldAccess: {
            auto* fa1 = static_cast<FieldAccess*>(a.get());
            auto* fa2 = static_cast<FieldAccess*>(b.get());
            return fa1->name == fa2->name && fa1->owner == fa2->owner && is_same_expr(fa1->target, fa2->target);
        }
        case ExprKind::ArrayAccess: {
            auto* aa1 = static_cast<ArrayAccess*>(a.get());
            auto* aa2 = static_cast<ArrayAccess*>(b.get());
            return is_same_expr(aa1->array, aa2->array) && is_same_expr(aa1->index, aa2->index);
        }
        case ExprKind::Cast: {
            auto* c1 = static_cast<Cast*>(a.get());
            auto* c2 = static_cast<Cast*>(b.get());
            return c1->type == c2->type && is_same_expr(c1->expr, c2->expr);
        }
        case ExprKind::UnOp: {
            auto* u1 = static_cast<UnOp*>(a.get());
            auto* u2 = static_cast<UnOp*>(b.get());
            return u1->op == u2->op && u1->postfix == u2->postfix && is_same_expr(u1->expr, u2->expr);
        }
        case ExprKind::BinOp: {
            auto* b1 = static_cast<BinOp*>(a.get());
            auto* b2 = static_cast<BinOp*>(b.get());
            return b1->op == b2->op && is_same_expr(b1->left, b2->left) && is_same_expr(b1->right, b2->right);
        }
        case ExprKind::MethodCall: {
            auto* m1 = static_cast<MethodCall*>(a.get());
            auto* m2 = static_cast<MethodCall*>(b.get());
            if (m1->name != m2->name || m1->is_ctor != m2->is_ctor || m1->is_super != m2->is_super ||
                !is_same_expr(m1->target, m2->target) || m1->args.size() != m2->args.size()) return false;
            for (size_t k = 0; k < m1->args.size(); ++k) {
                if (!is_same_expr(m1->args[k], m2->args[k])) return false;
            }
            return true;
        }
        case ExprKind::NewObject: {
            auto* no1 = static_cast<NewObject*>(a.get());
            auto* no2 = static_cast<NewObject*>(b.get());
            if (no1->type != no2->type || no1->args.size() != no2->args.size()) return false;
            for (size_t k = 0; k < no1->args.size(); ++k) {
                if (!is_same_expr(no1->args[k], no2->args[k])) return false;
            }
            return true;
        }
        case ExprKind::NewArray: {
            auto* na1 = static_cast<NewArray*>(a.get());
            auto* na2 = static_cast<NewArray*>(b.get());
            if (na1->elem_type != na2->elem_type || na1->dims.size() != na2->dims.size() ||
                na1->initializer.has_value() != na2->initializer.has_value()) return false;
            for (size_t k = 0; k < na1->dims.size(); ++k) {
                if (!is_same_expr(na1->dims[k], na2->dims[k])) return false;
            }
            if (na1->initializer.has_value()) {
                if (na1->initializer->size() != na2->initializer->size()) return false;
                for (size_t k = 0; k < na1->initializer->size(); ++k) {
                    if (!is_same_expr((*na1->initializer)[k], (*na2->initializer)[k])) return false;
                }
            }
            return true;
        }
        case ExprKind::InstanceOf: {
            auto* io1 = static_cast<InstanceOf*>(a.get());
            auto* io2 = static_cast<InstanceOf*>(b.get());
            return io1->check_type == io2->check_type && io1->pattern_var == io2->pattern_var &&
                   is_same_expr(io1->expr, io2->expr);
        }
        case ExprKind::Ternary: {
            auto* t1 = static_cast<Ternary*>(a.get());
            auto* t2 = static_cast<Ternary*>(b.get());
            return is_same_expr(t1->cond, t2->cond) && is_same_expr(t1->tval, t2->tval) &&
                   is_same_expr(t1->fval, t2->fval);
        }
        case ExprKind::Assign: {
            auto* as1 = static_cast<Assign*>(a.get());
            auto* as2 = static_cast<Assign*>(b.get());
            return as1->op == as2->op && is_same_expr(as1->target, as2->target) &&
                   is_same_expr(as1->value, as2->value);
        }
        case ExprKind::Raw:
            return static_cast<Raw*>(a.get())->text == static_cast<Raw*>(b.get())->text;
        case ExprKind::ClassLiteral:
            return static_cast<ClassLiteral*>(a.get())->type_name == static_cast<ClassLiteral*>(b.get())->type_name;
        default:
            return false;
    }
}

static bool is_opposite_expr(const ExprPtr& a, const ExprPtr& b) {
    if (!a || !b) return false;
    if (a->kind == ExprKind::UnOp && static_cast<UnOp*>(a.get())->op == "!") {
        return is_same_expr(static_cast<UnOp*>(a.get())->expr, b);
    }
    if (b->kind == ExprKind::UnOp && static_cast<UnOp*>(b.get())->op == "!") {
        return is_same_expr(static_cast<UnOp*>(b.get())->expr, a);
    }
    if (a->kind == ExprKind::BinOp && b->kind == ExprKind::BinOp) {
        auto* ba = static_cast<BinOp*>(a.get());
        auto* bb = static_cast<BinOp*>(b.get());
        static const std::map<std::string, std::string> opp = {
            {"==", "!="}, {"!=", "=="},
            {"<", ">="}, {">=", "<"},
            {">", "<="}, {"<=", ">"}
        };
        auto it = opp.find(ba->op);
        if (it != opp.end() && it->second == bb->op) {
            return is_same_expr(ba->left, bb->left) && is_same_expr(ba->right, bb->right);
        }
    }
    return false;
}

static ExprPtr invert_condition(const ExprPtr& cond) {
    if (!cond) return nullptr;
    if (cond->kind == ExprKind::UnOp) {
        auto* u = static_cast<UnOp*>(cond.get());
        if (u->op == "!") return u->expr;
    }
    if (cond->kind == ExprKind::BinOp) {
        auto* b = static_cast<BinOp*>(cond.get());
        static const std::map<std::string, std::string> opp = {
            {"==", "!="}, {"!=", "=="},
            {"<", ">="}, {">=", "<"},
            {">", "<="}, {"<=", ">"}
        };
        auto it = opp.find(b->op);
        if (it != opp.end()) {
            return std::make_shared<BinOp>(it->second, b->left, b->right, "boolean");
        }
    }
    return std::make_shared<UnOp>("!", cond);
}

ExprPtr simplify_expr(ExprPtr e, int depth = 0) {
    static thread_local int s_simp_expr_depth = 0;
    s_simp_expr_depth++;
    struct SimpDepthGuard { ~SimpDepthGuard() { s_simp_expr_depth--; } } s_guard;
    if (!e || depth > 25 || s_simp_expr_depth > 25) return e;
    switch (e->kind) {
        case ExprKind::Const: {
            auto* c = static_cast<Const*>(e.get());
            if (c->literal == "2147483647") { c->literal = "Integer.MAX_VALUE"; c->type = "int"; }
            else if (c->literal == "-2147483648") { c->literal = "Integer.MIN_VALUE"; c->type = "int"; }
            else if (c->literal == "9223372036854775807L" || c->literal == "9223372036854775807") { c->literal = "Long.MAX_VALUE"; c->type = "long"; }
            else if (c->literal == "-9223372036854775808L" || c->literal == "-9223372036854775808") { c->literal = "Long.MIN_VALUE"; c->type = "long"; }
            else if (c->literal == "3.141592653589793" || c->literal == "3.141592653589793d") { c->literal = "Math.PI"; c->type = "double"; }
            else if (c->literal == "2.718281828459045" || c->literal == "2.718281828459045d") { c->literal = "Math.E"; c->type = "double"; }
            return e;
        }
        case ExprKind::FieldAccess: {
            auto* fa = static_cast<FieldAccess*>(e.get());
            if (fa->target) fa->target = simplify_expr(fa->target);
            break;
        }
        case ExprKind::ArrayAccess: {
            auto* aa = static_cast<ArrayAccess*>(e.get());
            if (aa->array) aa->array = simplify_expr(aa->array);
            if (aa->index) aa->index = simplify_expr(aa->index);
            ExprPtr arr = aa->array;
            if (arr && arr->kind == ExprKind::Cast) {
                arr = static_cast<Cast*>(arr.get())->expr;
            }
            if (arr && arr->kind == ExprKind::NewArray && aa->index && aa->index->kind == ExprKind::Const) {
                auto* na = static_cast<NewArray*>(arr.get());
                auto* idx_c = static_cast<Const*>(aa->index.get());
                if (na->initializer.has_value()) {
                    try {
                        long long idx = std::stoll(idx_c->literal);
                        if (idx >= 0 && static_cast<size_t>(idx) < na->initializer->size()) {
                            return (*na->initializer)[static_cast<size_t>(idx)];
                        }
                    } catch (...) {}
                }
            }
            break;
        }
        case ExprKind::MethodCall: {
            auto* mc = static_cast<MethodCall*>(e.get());
            if (mc->target) mc->target = simplify_expr(mc->target);
            for (auto& arg : mc->args) arg = simplify_expr(arg);

            // Inlining constant string methods used by obfuscators & standard code
            if (mc->target && mc->target->kind == ExprKind::Const) {
                auto* ct = static_cast<Const*>(mc->target.get());
                if (ct->type == "String" && (ct->raw.has_value() || (ct->literal.size() >= 2 && ct->literal.front() == '"' && ct->literal.back() == '"'))) {
                    std::string raw_str;
                    if (ct->raw.has_value()) {
                        raw_str = *ct->raw;
                    } else {
                        // Unescape literal
                        const std::string& lit = ct->literal;
                        for (size_t i = 1; i + 1 < lit.size(); ++i) {
                            if (lit[i] == '\\' && i + 2 < lit.size()) {
                                char next = lit[i + 1];
                                if (next == 'n') { raw_str += '\n'; i++; }
                                else if (next == 'r') { raw_str += '\r'; i++; }
                                else if (next == 't') { raw_str += '\t'; i++; }
                                else if (next == 'b') { raw_str += '\b'; i++; }
                                else if (next == 'f') { raw_str += '\f'; i++; }
                                else if (next == '\\') { raw_str += '\\'; i++; }
                                else if (next == '"') { raw_str += '"'; i++; }
                                else if (next == '\'') { raw_str += '\''; i++; }
                                else { raw_str += lit[i]; }
                            } else {
                                raw_str += lit[i];
                            }
                        }
                    }
                    if (mc->name == "intern" && mc->args.empty()) {
                        return mc->target;
                    }
                    if (mc->name == "length" && mc->args.empty()) {
                        return std::make_shared<Const>(std::to_string(raw_str.size()), "int");
                    }
                    if (mc->name == "isEmpty" && mc->args.empty()) {
                        return std::make_shared<Const>(raw_str.empty() ? "true" : "false", "boolean");
                    }
                    if (mc->name == "charAt" && mc->args.size() == 1 && mc->args[0]->kind == ExprKind::Const) {
                        try {
                            int idx = std::stoi(static_cast<Const*>(mc->args[0].get())->literal);
                            if (idx >= 0 && (size_t)idx < raw_str.size()) {
                                unsigned char uch = static_cast<unsigned char>(raw_str[idx]);
                                return std::make_shared<Const>(char_literal(uch), "char");
                            }
                        } catch (...) {}
                    }
                    if (mc->name == "substring" && mc->args.size() == 1 && mc->args[0]->kind == ExprKind::Const) {
                        try {
                            int idx = std::stoi(static_cast<Const*>(mc->args[0].get())->literal);
                            if (idx >= 0 && (size_t)idx <= raw_str.size()) {
                                std::string sub = raw_str.substr(idx);
                                return std::make_shared<Const>(java_string_literal(sub), "String", sub);
                            }
                        } catch (...) {}
                    }
                    if (mc->name == "substring" && mc->args.size() == 2 && mc->args[0]->kind == ExprKind::Const && mc->args[1]->kind == ExprKind::Const) {
                        try {
                            int start = std::stoi(static_cast<Const*>(mc->args[0].get())->literal);
                            int end = std::stoi(static_cast<Const*>(mc->args[1].get())->literal);
                            if (start >= 0 && end >= start && (size_t)end <= raw_str.size()) {
                                std::string sub = raw_str.substr(start, end - start);
                                return std::make_shared<Const>(java_string_literal(sub), "String", sub);
                            }
                        } catch (...) {}
                    }
                }
            }

            // Math constant folding (min, max, abs)
            if (mc->target == nullptr && (!mc->owner.has_value() || *mc->owner == "java/lang/Math" || *mc->owner == "Math" || *mc->owner == "java.lang.Math")) {
                if ((mc->name == "min" || mc->name == "max") && mc->args.size() == 2 &&
                    mc->args[0] && mc->args[0]->kind == ExprKind::Const &&
                    mc->args[1] && mc->args[1]->kind == ExprKind::Const) {
                    auto* c1 = static_cast<Const*>(mc->args[0].get());
                    auto* c2 = static_cast<Const*>(mc->args[1].get());
                    try {
                        std::string s1 = c1->literal;
                        std::string s2 = c2->literal;
                        bool is_long = (!s1.empty() && (s1.back() == 'L' || s1.back() == 'l')) ||
                                       (!s2.empty() && (s2.back() == 'L' || s2.back() == 'l')) ||
                                       c1->type == "long" || c2->type == "long";
                        if (!s1.empty() && (s1.back() == 'L' || s1.back() == 'l')) s1.pop_back();
                        if (!s2.empty() && (s2.back() == 'L' || s2.back() == 'l')) s2.pop_back();
                        long long v1 = (s1.rfind("0x", 0) == 0 || s1.rfind("0X", 0) == 0) ? std::stoll(s1, nullptr, 16) : std::stoll(s1, nullptr, 10);
                        long long v2 = (s2.rfind("0x", 0) == 0 || s2.rfind("0X", 0) == 0) ? std::stoll(s2, nullptr, 16) : std::stoll(s2, nullptr, 10);
                        long long res = (mc->name == "min") ? std::min(v1, v2) : std::max(v1, v2);
                        return std::make_shared<Const>(std::to_string(res) + (is_long ? "L" : ""), is_long ? "long" : "int");
                    } catch (...) {}
                }
                if (mc->name == "abs" && mc->args.size() == 1 &&
                    mc->args[0] && mc->args[0]->kind == ExprKind::Const) {
                    auto* c1 = static_cast<Const*>(mc->args[0].get());
                    try {
                        std::string s1 = c1->literal;
                        bool is_long = (!s1.empty() && (s1.back() == 'L' || s1.back() == 'l')) || c1->type == "long";
                        if (!s1.empty() && (s1.back() == 'L' || s1.back() == 'l')) s1.pop_back();
                        long long v1 = (s1.rfind("0x", 0) == 0 || s1.rfind("0X", 0) == 0) ? std::stoll(s1, nullptr, 16) : std::stoll(s1, nullptr, 10);
                        long long res = std::abs(v1);
                        return std::make_shared<Const>(std::to_string(res) + (is_long ? "L" : ""), is_long ? "long" : "int");
                    } catch (...) {}
                }
            }

            // String.valueOf constant folding
            if (mc->target == nullptr && (!mc->owner.has_value() || *mc->owner == "java/lang/String" || *mc->owner == "String" || *mc->owner == "java.lang.String")) {
                if (mc->name == "valueOf" && mc->args.size() == 1 && mc->args[0] && mc->args[0]->kind == ExprKind::Const) {
                    auto* c = static_cast<Const*>(mc->args[0].get());
                    if (c->type == "char" && c->literal.size() >= 3 && c->literal.front() == '\'' && c->literal.back() == '\'') {
                        std::string ch = c->literal.substr(1, c->literal.size() - 2);
                        return std::make_shared<Const>("\"" + ch + "\"", "String");
                    }
                    if (c->type == "int" || c->type == "boolean") {
                        return std::make_shared<Const>("\"" + c->literal + "\"", "String");
                    }
                }
            }

            // Unboxing / boxing deobfuscation (ObfUpd 11)
            static const std::set<std::string> unbox_method_names = {
                "booleanValue", "intValue", "longValue", "doubleValue",
                "floatValue", "byteValue", "shortValue", "charValue"
            };
            if (unbox_method_names.count(mc->name) && mc->args.empty() && mc->target) {
                if (mc->target->kind == ExprKind::MethodCall) {
                    auto* inner = static_cast<MethodCall*>(mc->target.get());
                    if (inner->name == "valueOf" && inner->args.size() == 1) {
                        return inner->args[0];
                    }
                } else if (mc->target->kind == ExprKind::NewObject) {
                    auto* no = static_cast<NewObject*>(mc->target.get());
                    if (no->args.size() == 1) {
                        return no->args[0];
                    }
                } else if (mc->target->kind == ExprKind::Cast) {
                    auto* c = static_cast<Cast*>(mc->target.get());
                    if (c->expr) return c->expr;
                }
            }

            // Reflection desugaring (ObfUpd 12): Class.forName(...).getMethod(...).invoke(...)
            if (mc->name == "invoke" && !mc->args.empty() && mc->target && mc->target->kind == ExprKind::MethodCall) {
                auto* get_m = static_cast<MethodCall*>(mc->target.get());
                if ((get_m->name == "getMethod" || get_m->name == "getDeclaredMethod") && !get_m->args.empty()) {
                    ExprPtr name_arg = get_m->args[0];
                    if (name_arg && name_arg->kind == ExprKind::Const) {
                        auto* cname = static_cast<Const*>(name_arg.get());
                        std::string method_name = cname->literal;
                        if (method_name.size() >= 2 && method_name.front() == '"' && method_name.back() == '"') {
                            method_name = method_name.substr(1, method_name.size() - 2);
                        }
                        if (!is_safe_local_name(method_name)) return e;
                        
                        std::string owner_class = "";
                        if (get_m->target) {
                            if (get_m->target->kind == ExprKind::ClassLiteral) {
                                owner_class = static_cast<ClassLiteral*>(get_m->target.get())->type_name;
                            } else if (get_m->target->kind == ExprKind::MethodCall) {
                                auto* for_name = static_cast<MethodCall*>(get_m->target.get());
                                if (for_name->name == "forName" && !for_name->args.empty() && for_name->args[0] && for_name->args[0]->kind == ExprKind::Const) {
                                    std::string c_lit = static_cast<Const*>(for_name->args[0].get())->literal;
                                    if (c_lit.size() >= 2 && c_lit.front() == '"' && c_lit.back() == '"') {
                                        owner_class = c_lit.substr(1, c_lit.size() - 2);
                                    }
                                }
                            }
                        }
                        
                        ExprPtr instance = mc->args[0];
                        bool is_static = false;
                        if (instance && instance->kind == ExprKind::Const && static_cast<Const*>(instance.get())->literal == "null") {
                            is_static = true;
                            instance = nullptr;
                        }
                        
                        std::vector<ExprPtr> call_args;
                        if (mc->args.size() > 1) {
                            ExprPtr second = mc->args[1];
                            if (second && second->kind == ExprKind::NewArray) {
                                auto* na = static_cast<NewArray*>(second.get());
                                if (na->initializer.has_value()) {
                                    for (const auto& a : *na->initializer) {
                                        if (a) call_args.push_back(a);
                                    }
                                }
                            } else {
                                for (size_t a_idx = 1; a_idx < mc->args.size(); ++a_idx) {
                                    if (mc->args[a_idx]) call_args.push_back(mc->args[a_idx]);
                                }
                            }
                        }
                        
                        std::optional<std::string> owner_opt = owner_class.empty() ? std::nullopt : std::optional<std::string>(owner_class);
                        return std::make_shared<MethodCall>(instance, method_name, call_args, "Object", is_static, owner_opt);
                    }
                }
            }
            
            // Reflection field access: getField("field").get(instance) / set(instance, val)
            if ((mc->name == "get" || mc->name == "set") && !mc->args.empty() && mc->target && mc->target->kind == ExprKind::MethodCall) {
                auto* get_f = static_cast<MethodCall*>(mc->target.get());
                if ((get_f->name == "getField" || get_f->name == "getDeclaredField") && !get_f->args.empty()) {
                    ExprPtr name_arg = get_f->args[0];
                    if (name_arg && name_arg->kind == ExprKind::Const) {
                        auto* cname = static_cast<Const*>(name_arg.get());
                        std::string field_name = cname->literal;
                        if (field_name.size() >= 2 && field_name.front() == '"' && field_name.back() == '"') {
                            field_name = field_name.substr(1, field_name.size() - 2);
                        }
                        if (!is_safe_local_name(field_name)) return e;
                        std::string owner_class = "";
                        if (get_f->target) {
                            if (get_f->target->kind == ExprKind::ClassLiteral) {
                                owner_class = static_cast<ClassLiteral*>(get_f->target.get())->type_name;
                            } else if (get_f->target->kind == ExprKind::MethodCall) {
                                auto* for_name = static_cast<MethodCall*>(get_f->target.get());
                                if (for_name->name == "forName" && !for_name->args.empty() && for_name->args[0] && for_name->args[0]->kind == ExprKind::Const) {
                                    std::string c_lit = static_cast<Const*>(for_name->args[0].get())->literal;
                                    if (c_lit.size() >= 2 && c_lit.front() == '"' && c_lit.back() == '"') {
                                        owner_class = c_lit.substr(1, c_lit.size() - 2);
                                    }
                                }
                            }
                        }
                        ExprPtr instance = mc->args[0];
                        bool is_static = false;
                        if (instance && instance->kind == ExprKind::Const && static_cast<Const*>(instance.get())->literal == "null") {
                            is_static = true;
                            instance = nullptr;
                        }
                        std::optional<std::string> owner_opt = owner_class.empty() ? std::nullopt : std::optional<std::string>(owner_class);
                        auto fa = std::make_shared<FieldAccess>(instance, field_name, "Object", is_static, owner_opt);
                        if (mc->name == "set" && mc->args.size() >= 2) {
                            return std::make_shared<Assign>(fa, mc->args[1], "=");
                        }
                        return fa;
                    }
                }
            }

            // StringBuilder / StringBuffer concatenation dechaining (ObfUpd 13)
            if (mc->name == "toString" && mc->args.empty() && mc->target) {
                std::vector<ExprPtr> append_args;
                ExprPtr curr = mc->target;
                bool is_sb_chain = false;
                while (curr && curr->kind == ExprKind::MethodCall) {
                    auto* app_mc = static_cast<MethodCall*>(curr.get());
                    if (app_mc->name == "append" && app_mc->args.size() == 1) {
                        append_args.push_back(app_mc->args[0]);
                        curr = app_mc->target;
                    } else {
                        break;
                    }
                }
                if (curr && curr->kind == ExprKind::NewObject) {
                    auto* no = static_cast<NewObject*>(curr.get());
                    if (no->type == "StringBuilder" || no->type == "StringBuffer" ||
                        no->type.find("StringBuilder") != std::string::npos ||
                        no->type.find("StringBuffer") != std::string::npos) {
                        is_sb_chain = true;
                        if (!no->args.empty() && no->args.size() == 1) {
                            if (no->args[0]->type == "String" || (no->args[0]->kind == ExprKind::Const && !static_cast<Const*>(no->args[0].get())->literal.empty() && static_cast<Const*>(no->args[0].get())->literal.front() == '"')) {
                                append_args.push_back(no->args[0]);
                            }
                        }
                    }
                }
                if (is_sb_chain && !append_args.empty()) {
                    std::reverse(append_args.begin(), append_args.end());
                    ExprPtr result = nullptr;
                    bool starts_with_str = (append_args[0]->type == "String" || 
                                           (append_args[0]->kind == ExprKind::Const && !static_cast<Const*>(append_args[0].get())->literal.empty() && static_cast<Const*>(append_args[0].get())->literal.front() == '"'));
                    if (!starts_with_str) {
                        result = std::make_shared<Const>("""", "String");
                        for (auto& a : append_args) {
                            result = std::make_shared<BinOp>("+", result, a, "String");
                        }
                    } else {
                        result = append_args[0];
                        for (size_t a_i = 1; a_i < append_args.size(); ++a_i) {
                            result = std::make_shared<BinOp>("+", result, append_args[a_i], "String");
                        }
                    }
                    return simplify_expr(result);
                }
            }
            break;
        }
        case ExprKind::NewObject: {
            auto* no = static_cast<NewObject*>(e.get());
            for (auto& arg : no->args) arg = simplify_expr(arg);
            break;
        }
        case ExprKind::NewArray: {
            auto* na = static_cast<NewArray*>(e.get());
            for (auto& d : na->dims) d = simplify_expr(d);
            if (na->initializer.has_value()) {
                for (auto& v : *na->initializer) v = simplify_expr(v);
            }
            break;
        }
        case ExprKind::Cast: {
            auto* c = static_cast<Cast*>(e.get());
            if (c->expr) c->expr = simplify_expr(c->expr);
            if (c->expr && c->expr->type == c->type) return c->expr;
            if (c->expr && c->expr->kind == ExprKind::Cast) {
                auto* inner_c = static_cast<Cast*>(c->expr.get());
                if (inner_c->type == c->type) return c->expr;
                if (c->type == "int" && (inner_c->type == "char" || inner_c->type == "byte" || inner_c->type == "short")) {
                    return c->expr;
                }
            }
            break;
        }
        case ExprKind::InstanceOf: {
            auto* io = static_cast<InstanceOf*>(e.get());
            if (io->expr) io->expr = simplify_expr(io->expr);
            break;
        }
        case ExprKind::BinOp: {
            auto* b = static_cast<BinOp*>(e.get());
            if (b->left) b->left = simplify_expr(b->left);
            if (b->right) b->right = simplify_expr(b->right);
            if (b->left && b->left->kind == ExprKind::Const && b->right && b->right->kind == ExprKind::Const) {
                auto* c1 = static_cast<Const*>(b->left.get());
                auto* c2 = static_cast<Const*>(b->right.get());
                bool eq = (c1->literal == c2->literal);
                if (b->op == "==") return std::make_shared<Const>(eq ? "true" : "false", "boolean");
                if (b->op == "!=") return std::make_shared<Const>(eq ? "false" : "true", "boolean");

                // Constant string folding: "hello " + "world" -> "hello world"
                if (b->op == "+" && c1->literal.size() >= 2 && c1->literal.front() == '"' && c1->literal.back() == '"' &&
                    c2->literal.size() >= 2 && c2->literal.front() == '"' && c2->literal.back() == '"') {
                    std::string s1 = c1->literal.substr(1, c1->literal.size() - 2);
                    std::string s2 = c2->literal.substr(1, c2->literal.size() - 2);
                    return std::make_shared<Const>("\"" + s1 + s2 + "\"", "String");
                }

                // Numeric constant comparisons and operations
                if (c1->literal != "true" && c1->literal != "false" && c1->literal != "null" &&
                    c2->literal != "true" && c2->literal != "false" && c2->literal != "null" &&
                    c1->literal.front() != '"' && c2->literal.front() != '"') {
                    try {
                        std::string s1 = c1->literal;
                        std::string s2 = c2->literal;
                        bool is_long = (!s1.empty() && (s1.back() == 'L' || s1.back() == 'l')) ||
                                       (!s2.empty() && (s2.back() == 'L' || s2.back() == 'l')) ||
                                       b->type == "long" || c1->type == "long" || c2->type == "long";
                        if (!s1.empty() && (s1.back() == 'L' || s1.back() == 'l')) s1.pop_back();
                        if (!s2.empty() && (s2.back() == 'L' || s2.back() == 'l')) s2.pop_back();
                        long long v1 = (s1.rfind("0x", 0) == 0 || s1.rfind("0X", 0) == 0) ? std::stoll(s1, nullptr, 16) : std::stoll(s1, nullptr, 10);
                        long long v2 = (s2.rfind("0x", 0) == 0 || s2.rfind("0X", 0) == 0) ? std::stoll(s2, nullptr, 16) : std::stoll(s2, nullptr, 10);

                        if (b->op == "<") return std::make_shared<Const>(v1 < v2 ? "true" : "false", "boolean");
                        if (b->op == "<=") return std::make_shared<Const>(v1 <= v2 ? "true" : "false", "boolean");
                        if (b->op == ">") return std::make_shared<Const>(v1 > v2 ? "true" : "false", "boolean");
                        if (b->op == ">=") return std::make_shared<Const>(v1 >= v2 ? "true" : "false", "boolean");

                        if (b->op == "+") {
                            long long res = is_long ? (v1 + v2) : (int32_t)((int32_t)v1 + (int32_t)v2);
                            return std::make_shared<Const>(std::to_string(res) + (is_long ? "L" : ""), is_long ? "long" : "int");
                        }
                        if (b->op == "-") {
                            long long res = is_long ? (v1 - v2) : (int32_t)((int32_t)v1 - (int32_t)v2);
                            return std::make_shared<Const>(std::to_string(res) + (is_long ? "L" : ""), is_long ? "long" : "int");
                        }
                        if (b->op == "*") {
                            long long res = is_long ? (v1 * v2) : (int32_t)((int32_t)v1 * (int32_t)v2);
                            return std::make_shared<Const>(std::to_string(res) + (is_long ? "L" : ""), is_long ? "long" : "int");
                        }
                        if (b->op == "/" && v2 != 0) {
                            long long res = is_long ? (v1 / v2) : (int32_t)((int32_t)v1 / (int32_t)v2);
                            return std::make_shared<Const>(std::to_string(res) + (is_long ? "L" : ""), is_long ? "long" : "int");
                        }
                        if (b->op == "%" && v2 != 0) {
                            long long res = is_long ? (v1 % v2) : (int32_t)((int32_t)v1 % (int32_t)v2);
                            return std::make_shared<Const>(std::to_string(res) + (is_long ? "L" : ""), is_long ? "long" : "int");
                        }
                        if (b->op == "&") {
                            long long res = is_long ? (v1 & v2) : (int32_t)((int32_t)v1 & (int32_t)v2);
                            return std::make_shared<Const>(std::to_string(res) + (is_long ? "L" : ""), is_long ? "long" : "int");
                        }
                        if (b->op == "|") {
                            long long res = is_long ? (v1 | v2) : (int32_t)((int32_t)v1 | (int32_t)v2);
                            return std::make_shared<Const>(std::to_string(res) + (is_long ? "L" : ""), is_long ? "long" : "int");
                        }
                        if (b->op == "^") {
                            long long res = is_long ? (v1 ^ v2) : (int32_t)((int32_t)v1 ^ (int32_t)v2);
                            return std::make_shared<Const>(std::to_string(res) + (is_long ? "L" : ""), is_long ? "long" : "int");
                        }
                        if (b->op == "<<") {
                            long long res = is_long ? (v1 << (v2 & 63)) : (int32_t)((int32_t)v1 << (v2 & 31));
                            return std::make_shared<Const>(std::to_string(res) + (is_long ? "L" : ""), is_long ? "long" : "int");
                        }
                        if (b->op == ">>") {
                            long long res = is_long ? (v1 >> (v2 & 63)) : (int32_t)((int32_t)v1 >> (v2 & 31));
                            return std::make_shared<Const>(std::to_string(res) + (is_long ? "L" : ""), is_long ? "long" : "int");
                        }
                        if (b->op == ">>>") {
                            long long res = is_long ? (long long)((uint64_t)v1 >> (v2 & 63)) : (int32_t)((uint32_t)v1 >> (v2 & 31));
                            return std::make_shared<Const>(std::to_string(res) + (is_long ? "L" : ""), is_long ? "long" : "int");
                        }
                    } catch (...) {}
                }
            }
            if (is_same_expr(b->left, b->right)) {
                if (b->op == "==") return std::make_shared<Const>("true", "boolean");
                if (b->op == "!=") return std::make_shared<Const>("false", "boolean");
                if (b->op == "<=" || b->op == ">=") return std::make_shared<Const>("true", "boolean");
                if (b->op == "<" || b->op == ">") return std::make_shared<Const>("false", "boolean");
                if (b->op == "^" || b->op == "-") return std::make_shared<Const>("0", b->type);
                if (b->op == "&" || b->op == "|") return b->left;
            }
            if (b->op == "^") {
                if (b->left && b->left->kind == ExprKind::BinOp) {
                    auto* bl = static_cast<BinOp*>(b->left.get());
                    if (bl->op == "^") {
                        if (is_same_expr(bl->right, b->right)) return bl->left;
                        if (is_same_expr(bl->left, b->right)) return bl->right;
                    }
                }
                if (b->right && b->right->kind == ExprKind::BinOp) {
                    auto* br = static_cast<BinOp*>(b->right.get());
                    if (br->op == "^") {
                        if (is_same_expr(br->right, b->left)) return br->left;
                        if (is_same_expr(br->left, b->left)) return br->right;
                    }
                }
            }
            if (b->op == "==") {
                if (b->right && b->right->kind == ExprKind::Const) {
                    auto* c = static_cast<Const*>(b->right.get());
                    if (c->literal == "true") return b->left;
                    if (c->literal == "false") return negate(b->left);
                    if (c->literal == "0" && (b->left->type == "boolean" || b->left->kind == ExprKind::BinOp)) return negate(b->left);
                }
                if (b->left && b->left->kind == ExprKind::Const) {
                    auto* c = static_cast<Const*>(b->left.get());
                    if (c->literal == "true") return b->right;
                    if (c->literal == "false") return negate(b->right);
                    if (c->literal == "0" && (b->right->type == "boolean" || b->right->kind == ExprKind::BinOp)) return negate(b->right);
                }
            }
            if (b->op == "!=") {
                if (b->right && b->right->kind == ExprKind::Const) {
                    auto* c = static_cast<Const*>(b->right.get());
                    if (c->literal == "true") return negate(b->left);
                    if (c->literal == "false") return b->left;
                    if (c->literal == "0" && (b->left->type == "boolean" || b->left->kind == ExprKind::BinOp)) return b->left;
                }
                if (b->left && b->left->kind == ExprKind::Const) {
                    auto* c = static_cast<Const*>(b->left.get());
                    if (c->literal == "true") return negate(b->right);
                    if (c->literal == "false") return b->right;
                    if (c->literal == "0" && (b->right->type == "boolean" || b->right->kind == ExprKind::BinOp)) return b->right;
                }
            }
            if (b->op == "&") {
                if (b->right && b->right->kind == ExprKind::Const) {
                    auto* c = static_cast<Const*>(b->right.get());
                    if (c->literal == "0") return std::make_shared<Const>("0", b->type);
                    if (c->literal == "-1") return b->left;
                    if (c->literal == "255") { c->literal = "0xFF"; c->value = "0xFF"; c->val = "0xFF"; }
                    else if (c->literal == "65535") { c->literal = "0xFFFF"; c->value = "0xFFFF"; c->val = "0xFFFF"; }
                    else if (c->literal == "16777215") { c->literal = "0xFFFFFF"; c->value = "0xFFFFFF"; c->val = "0xFFFFFF"; }
                    else if (c->literal == "65280") { c->literal = "0xFF00"; c->value = "0xFF00"; c->val = "0xFF00"; }
                    else if (c->literal == "4294967295L") { c->literal = "0xFFFFFFFFL"; c->value = "0xFFFFFFFFL"; c->val = "0xFFFFFFFFL"; }
                }
                if (b->left && b->left->kind == ExprKind::Const) {
                    auto* c = static_cast<Const*>(b->left.get());
                    if (c->literal == "0") return std::make_shared<Const>("0", b->type);
                    if (c->literal == "-1") return b->right;
                    if (c->literal == "255") { c->literal = "0xFF"; c->value = "0xFF"; c->val = "0xFF"; }
                    else if (c->literal == "65535") { c->literal = "0xFFFF"; c->value = "0xFFFF"; c->val = "0xFFFF"; }
                }
            }
            if (b->op == "|" || b->op == "^") {
                if (b->right && b->right->kind == ExprKind::Const) {
                    auto* c = static_cast<Const*>(b->right.get());
                    if (c->literal == "0") return b->left;
                    if (c->literal == "255") { c->literal = "0xFF"; c->value = "0xFF"; c->val = "0xFF"; }
                    else if (c->literal == "65535") { c->literal = "0xFFFF"; c->value = "0xFFFF"; c->val = "0xFFFF"; }
                }
                if (b->left && b->left->kind == ExprKind::Const) {
                    auto* c = static_cast<Const*>(b->left.get());
                    if (c->literal == "0") return b->right;
                    if (c->literal == "255") { c->literal = "0xFF"; c->value = "0xFF"; c->val = "0xFF"; }
                    else if (c->literal == "65535") { c->literal = "0xFFFF"; c->value = "0xFFFF"; c->val = "0xFFFF"; }
                }
            }
            if (b->op == "<<" || b->op == ">>" || b->op == ">>>") {
                if (b->right && b->right->kind == ExprKind::Const) {
                    auto* c = static_cast<Const*>(b->right.get());
                    if (c->literal == "0") return b->left;
                }
            }
            if (b->op == "+") {
                if (b->right && b->right->kind == ExprKind::Const && static_cast<Const*>(b->right.get())->literal == "0" && b->type != "String") {
                    return b->left;
                }
                if (b->left && b->left->kind == ExprKind::Const && static_cast<Const*>(b->left.get())->literal == "0" && b->type != "String") {
                    return b->right;
                }
            }
            if (b->op == "-") {
                if (b->right && b->right->kind == ExprKind::Const && static_cast<Const*>(b->right.get())->literal == "0") {
                    return b->left;
                }
            }
            if (b->op == "*") {
                if (b->right && b->right->kind == ExprKind::Const) {
                    auto* c = static_cast<Const*>(b->right.get());
                    if (c->literal == "1") return b->left;
                    if (c->literal == "0") return std::make_shared<Const>("0", b->type.empty() ? "int" : b->type);
                }
                if (b->left && b->left->kind == ExprKind::Const) {
                    auto* c = static_cast<Const*>(b->left.get());
                    if (c->literal == "1") return b->right;
                    if (c->literal == "0") return std::make_shared<Const>("0", b->type.empty() ? "int" : b->type);
                }
            }

            if (b->op == "/") {
                if (b->right && b->right->kind == ExprKind::Const && static_cast<Const*>(b->right.get())->literal == "1") return b->left;
            }
            if (b->op == "&&") {
                if (b->right && b->right->kind == ExprKind::Const) {
                    auto* c = static_cast<Const*>(b->right.get());
                    if (c->literal == "true") return b->left;
                    if (c->literal == "false") return std::make_shared<Const>("false", "boolean");
                }
                if (b->left && b->left->kind == ExprKind::Const) {
                    auto* c = static_cast<Const*>(b->left.get());
                    if (c->literal == "true") return b->right;
                    if (c->literal == "false") return std::make_shared<Const>("false", "boolean");
                }
            }
            if (b->op == "||") {
                if (b->right && b->right->kind == ExprKind::Const) {
                    auto* c = static_cast<Const*>(b->right.get());
                    if (c->literal == "false") return b->left;
                    if (c->literal == "true") return std::make_shared<Const>("true", "boolean");
                }
                if (b->left && b->left->kind == ExprKind::Const) {
                    auto* c = static_cast<Const*>(b->left.get());
                    if (c->literal == "false") return b->right;
                    if (c->literal == "true") return std::make_shared<Const>("true", "boolean");
                }
            }
            if (b->op == "==" || b->op == "!=") {
                bool is_eq = (b->op == "==");
                if ((b->left && b->left->kind == ExprKind::NewObject && b->right && b->right->kind == ExprKind::Const && static_cast<Const*>(b->right.get())->literal == "null") ||
                    (b->right && b->right->kind == ExprKind::NewObject && b->left && b->left->kind == ExprKind::Const && static_cast<Const*>(b->left.get())->literal == "null")) {
                    return std::make_shared<Const>(is_eq ? "false" : "true", "boolean");
                }
            }
            if (b->op == "&&") {
                if (b->left && b->left->kind == ExprKind::BinOp && b->right && b->right->kind == ExprKind::InstanceOf) {
                    auto* bl = static_cast<BinOp*>(b->left.get());
                    auto* io = static_cast<InstanceOf*>(b->right.get());
                    if (bl->op == "!=" && ((bl->right && bl->right->kind == ExprKind::Const && static_cast<Const*>(bl->right.get())->literal == "null" && is_same_expr(bl->left, io->expr)) ||
                                           (bl->left && bl->left->kind == ExprKind::Const && static_cast<Const*>(bl->left.get())->literal == "null" && is_same_expr(bl->right, io->expr)))) {
                        return b->right;
                    }
                }
                if (b->right && b->right->kind == ExprKind::BinOp && b->left && b->left->kind == ExprKind::InstanceOf) {
                    auto* br = static_cast<BinOp*>(b->right.get());
                    auto* io = static_cast<InstanceOf*>(b->left.get());
                    if (br->op == "!=" && ((br->right && br->right->kind == ExprKind::Const && static_cast<Const*>(br->right.get())->literal == "null" && is_same_expr(br->left, io->expr)) ||
                                           (br->left && br->left->kind == ExprKind::Const && static_cast<Const*>(br->left.get())->literal == "null" && is_same_expr(br->right, io->expr)))) {
                        return b->left;
                    }
                }
            }
            if (b->op == "+" && (b->type == "String" || (b->left && b->left->type == "String") || (b->right && b->right->type == "String"))) {
                if (b->right && b->right->kind == ExprKind::MethodCall) {
                    auto* r_mc = static_cast<MethodCall*>(b->right.get());
                    if (r_mc->name == "valueOf" && r_mc->args.size() == 1 &&
                        (!r_mc->owner.has_value() || *r_mc->owner == "String" || *r_mc->owner == "java.lang.String")) {
                        b->right = r_mc->args[0];
                    }
                }
                if (b->left && b->left->kind == ExprKind::MethodCall) {
                    auto* l_mc = static_cast<MethodCall*>(b->left.get());
                    if (l_mc->name == "valueOf" && l_mc->args.size() == 1 &&
                        (!l_mc->owner.has_value() || *l_mc->owner == "String" || *l_mc->owner == "java.lang.String")) {
                        b->left = l_mc->args[0];
                    }
                }
            }
            break;
        }
        case ExprKind::UnOp: {
            auto* u = static_cast<UnOp*>(e.get());
            if (u->expr) u->expr = simplify_expr(u->expr);
            if (u->op == "!" && u->expr && u->expr->kind == ExprKind::UnOp) {
                auto* inner_u = static_cast<UnOp*>(u->expr.get());
                if (inner_u->op == "!") return inner_u->expr;
            }
            if (u->op == "!" && u->expr && u->expr->kind == ExprKind::BinOp) {
                auto* b = static_cast<BinOp*>(u->expr.get());
                static const std::map<std::string, std::string> flip = {
                    {"==", "!="}, {"!=", "=="}, {"<", ">="}, {">=", "<"}, {">", "<="}, {"<=", ">"}
                };
                auto it = flip.find(b->op);
                if (it != flip.end()) {
                    return std::make_shared<BinOp>(it->second, b->left, b->right, "boolean");
                }
            }
            if (u->op == "~" && u->expr && u->expr->kind == ExprKind::UnOp) {
                auto* inner_u = static_cast<UnOp*>(u->expr.get());
                if (inner_u->op == "~") return inner_u->expr;
            }
            if (u->op == "-" && u->expr && u->expr->kind == ExprKind::UnOp) {
                auto* inner_u = static_cast<UnOp*>(u->expr.get());
                if (inner_u->op == "-") return inner_u->expr;
            }
            if (u->expr && u->expr->kind == ExprKind::Const) {
                auto* c = static_cast<Const*>(u->expr.get());
                if (u->op == "!") {
                    if (c->literal == "true") return std::make_shared<Const>("false", "boolean");
                    if (c->literal == "false") return std::make_shared<Const>("true", "boolean");
                } else if ((u->op == "~" || u->op == "-") && c->literal != "true" && c->literal != "false" &&
                           c->literal != "null" && (c->literal.empty() || c->literal.front() != '"')) {
                    try {
                        std::string lit = c->literal;
                        bool is_long = (!lit.empty() && (lit.back() == 'L' || lit.back() == 'l')) || u->type == "long" || c->type == "long";
                        if (!lit.empty() && (lit.back() == 'L' || lit.back() == 'l')) lit.pop_back();
                        uint64_t uval = (lit.rfind("0x", 0) == 0 || lit.rfind("0X", 0) == 0)
                                            ? std::stoull(lit, nullptr, 16)
                                            : static_cast<uint64_t>(std::stoll(lit, nullptr, 10));
                        if (u->op == "~") uval = ~uval;
                        else if (u->op == "-") uval = static_cast<uint64_t>(-static_cast<int64_t>(uval));
                        int64_t val = static_cast<int64_t>(uval);
                        if (is_long) {
                            return std::make_shared<Const>(std::to_string(val) + "L", "long");
                        } else {
                            return std::make_shared<Const>(std::to_string(static_cast<int32_t>(val)), "int");
                        }
                    } catch (...) {}
                }
            }
            break;
        }
        case ExprKind::Ternary: {
            auto* t = static_cast<Ternary*>(e.get());
            if (t->cond) t->cond = simplify_expr(t->cond);
            if (t->tval) t->tval = simplify_expr(t->tval);
            if (t->fval) t->fval = simplify_expr(t->fval);
            if (is_same_expr(t->tval, t->fval)) return t->tval;
            if (t->tval && t->tval->kind == ExprKind::Const && t->fval && t->fval->kind == ExprKind::Const) {
                auto* c1 = static_cast<Const*>(t->tval.get());
                auto* c2 = static_cast<Const*>(t->fval.get());
                if (c1->literal == "true" && c2->literal == "false") return t->cond;
                if (c1->literal == "false" && c2->literal == "true") return negate(t->cond);
                if (c1->literal == "true" && c2->literal == "true") return std::make_shared<Const>("true", "boolean");
                if (c1->literal == "false" && c2->literal == "false") return std::make_shared<Const>("false", "boolean");
            }
            if (t->cond && t->cond->kind == ExprKind::UnOp && static_cast<UnOp*>(t->cond.get())->op == "!") {
                auto* u = static_cast<UnOp*>(t->cond.get());
                return std::make_shared<Ternary>(u->expr, t->fval, t->tval, t->type);
            }
            if (t->cond && t->cond->kind == ExprKind::BinOp) {
                auto* bo = static_cast<BinOp*>(t->cond.get());
                if (bo->op == "==" && bo->right && bo->right->kind == ExprKind::Const &&
                    static_cast<Const*>(bo->right.get())->literal == "null") {
                    if (is_same_expr(bo->left, t->fval)) {
                        auto new_cond = std::make_shared<BinOp>("!=", bo->left, bo->right, "boolean");
                        return std::make_shared<Ternary>(new_cond, t->fval, t->tval, t->type);
                    }
                }
            }
            break;
        }
        case ExprKind::Assign: {
            auto* a = static_cast<Assign*>(e.get());
            if (a->target) a->target = simplify_expr(a->target);
            if (a->value) a->value = simplify_expr(a->value);
            if (a->op.empty() || a->op == "=") {
                if (a->value && a->value->kind == ExprKind::BinOp) {
                    auto* b = static_cast<BinOp*>(a->value.get());
                    if (is_same_expr(a->target, b->left)) {
                        if (b->op == "+" && b->right && b->right->kind == ExprKind::Const &&
                            static_cast<Const*>(b->right.get())->literal == "1") {
                            return std::make_shared<UnOp>("++", a->target, a->target->type.empty() ? "int" : a->target->type, true);
                        }
                        if (b->op == "-" && b->right && b->right->kind == ExprKind::Const &&
                            static_cast<Const*>(b->right.get())->literal == "1") {
                            return std::make_shared<UnOp>("--", a->target, a->target->type.empty() ? "int" : a->target->type, true);
                        }
                        static const std::set<std::string> comp_ops = {
                            "+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>", ">>>"
                        };
                        if (comp_ops.count(b->op)) {
                            a->op = b->op + "=";
                            a->value = b->right;
                            return e;
                        }
                    } else if (b->op == "+" && is_same_expr(a->target, b->right)) {
                        if (b->left && b->left->kind == ExprKind::Const &&
                            static_cast<Const*>(b->left.get())->literal == "1") {
                            return std::make_shared<UnOp>("++", a->target, a->target->type.empty() ? "int" : a->target->type, true);
                        }
                    }
                }
            }
            break;
        }
        default:
            break;
    }
    return e;
}

std::vector<StmtPtr> fold_boolean_materialization(const std::vector<StmtPtr>& stmts) {
    std::vector<StmtPtr> out;
    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == StmtKind::IfStmt) {
            auto* i = static_cast<IfStmt*>(s.get());
            i->then_body = fold_boolean_materialization(i->then_body);
            if (i->else_body) i->else_body = fold_boolean_materialization(*i->else_body);
        } else if (s->kind == StmtKind::WhileStmt) {
            auto* w = static_cast<WhileStmt*>(s.get());
            w->body = fold_boolean_materialization(w->body);
        } else if (s->kind == StmtKind::DoWhileStmt) {
            auto* dw = static_cast<DoWhileStmt*>(s.get());
            dw->body = fold_boolean_materialization(dw->body);
        } else if (s->kind == StmtKind::ForStmt) {
            auto* f = static_cast<ForStmt*>(s.get());
            f->body = fold_boolean_materialization(f->body);
        } else if (s->kind == StmtKind::BlockStmt) {
            auto* b = static_cast<BlockStmt*>(s.get());
            b->stmts = fold_boolean_materialization(b->stmts);
        } else if (s->kind == StmtKind::TryStmt) {
            auto* t = static_cast<TryStmt*>(s.get());
            t->body = fold_boolean_materialization(t->body);
            for (auto& c : t->catches) c.body = fold_boolean_materialization(c.body);
            if (t->finally_body) t->finally_body = fold_boolean_materialization(*t->finally_body);
        } else if (s->kind == StmtKind::SyncStmt) {
            auto* sy = static_cast<SyncStmt*>(s.get());
            sy->body = fold_boolean_materialization(sy->body);
        }
        if (s->kind == StmtKind::IfStmt) {
            auto* i = static_cast<IfStmt*>(s.get());
            if (!i->then_body.empty() && i->then_body.size() == 1 && i->else_body.has_value() && i->else_body->size() == 1) {
                if (!i->then_body[0] || !(*i->else_body)[0]) {
                    out.push_back(s);
                    continue;
                }
                ExprPtr v1, v2, tgt1, tgt2;
                auto a1 = as_assign(i->then_body[0]);
                auto a2 = as_assign((*i->else_body)[0]);
                if (a1.has_value() && a2.has_value()) {
                    tgt1 = a1->first; v1 = a1->second;
                    tgt2 = a2->first; v2 = a2->second;
                    if (!tgt1 || !tgt2 || !v1 || !v2) {
                        out.push_back(s);
                        continue;
                    }
                    if (same_target(tgt1, tgt2)) {
                        auto b1 = as_bool_const(v1), b2 = as_bool_const(v2);
                        if (b1.has_value() && b2.has_value() && ((*b1 == 0 && *b2 == 1) || (*b1 == 1 && *b2 == 0))) {
                            ExprPtr cond = (*b1 == 1) ? i->cond : negate(i->cond);
                            out.push_back(std::make_shared<ExprStmtNode>(std::make_shared<Assign>(tgt1, cond)));
                            continue;
                        }
                        // v2.0.5: Boolean short-circuit folding
                        if (b2.has_value() && *b2 == 0) {
                            ExprPtr folded = std::make_shared<BinOp>("&&", i->cond, v1, "boolean");
                            out.push_back(std::make_shared<ExprStmtNode>(std::make_shared<Assign>(tgt1, folded)));
                            continue;
                        } else if (b2.has_value() && *b2 == 1) {
                            ExprPtr folded = std::make_shared<BinOp>("||", negate(i->cond), v1, "boolean");
                            out.push_back(std::make_shared<ExprStmtNode>(std::make_shared<Assign>(tgt1, folded)));
                            continue;
                        } else if (b1.has_value() && *b1 == 0) {
                            ExprPtr folded = std::make_shared<BinOp>("&&", negate(i->cond), v2, "boolean");
                            out.push_back(std::make_shared<ExprStmtNode>(std::make_shared<Assign>(tgt1, folded)));
                            continue;
                        } else if (b1.has_value() && *b1 == 1) {
                            ExprPtr folded = std::make_shared<BinOp>("||", i->cond, v2, "boolean");
                            out.push_back(std::make_shared<ExprStmtNode>(std::make_shared<Assign>(tgt1, folded)));
                            continue;
                        }
                        std::string t1_type = v1->type;
                        std::string t2_type = v2->type;
                        if (t1_type == "int" && as_bool_const(v1).has_value() && t2_type == "boolean") {
                            auto* c1 = static_cast<Const*>(v1.get());
                            v1 = std::make_shared<Const>(c1->literal == "0" ? "false" : "true", "boolean");
                            t1_type = "boolean";
                        } else if (t2_type == "int" && as_bool_const(v2).has_value() && t1_type == "boolean") {
                            auto* c2 = static_cast<Const*>(v2.get());
                            v2 = std::make_shared<Const>(c2->literal == "0" ? "false" : "true", "boolean");
                            t2_type = "boolean";
                        }
                        std::string result_type;
                        if (!t1_type.empty() && !PSEUDO_TYPES.count(t1_type)) {
                            result_type = t1_type;
                        } else if (!t2_type.empty() && !PSEUDO_TYPES.count(t2_type)) {
                            result_type = t2_type;
                        } else {
                            std::string tgt_type = tgt1->type;
                            result_type = (!tgt_type.empty() && !PSEUDO_TYPES.count(tgt_type)) ? tgt_type : "Object";
                        }
                        out.push_back(std::make_shared<ExprStmtNode>(
                            std::make_shared<Assign>(tgt1, std::make_shared<Ternary>(i->cond, v1, v2, result_type))));
                        continue;
                    }
                }
            }
        }
        out.push_back(s);
    }
    return out;
}

std::vector<StmtPtr> collapse_temp_chains(std::vector<StmtPtr> stmts) {
    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == StmtKind::IfStmt) {
            auto* i = static_cast<IfStmt*>(s.get());
            i->then_body = collapse_temp_chains(i->then_body);
            if (i->else_body) i->else_body = collapse_temp_chains(*i->else_body);
        } else if (s->kind == StmtKind::WhileStmt) {
            auto* w = static_cast<WhileStmt*>(s.get());
            w->body = collapse_temp_chains(w->body);
        } else if (s->kind == StmtKind::DoWhileStmt) {
            auto* dw = static_cast<DoWhileStmt*>(s.get());
            dw->body = collapse_temp_chains(dw->body);
        } else if (s->kind == StmtKind::ForStmt) {
            auto* f = static_cast<ForStmt*>(s.get());
            f->body = collapse_temp_chains(f->body);
        } else if (s->kind == StmtKind::BlockStmt) {
            auto* b = static_cast<BlockStmt*>(s.get());
            b->stmts = collapse_temp_chains(b->stmts);
        } else if (s->kind == StmtKind::TryStmt) {
            auto* t = static_cast<TryStmt*>(s.get());
            t->body = collapse_temp_chains(t->body);
            for (auto& c : t->catches) c.body = collapse_temp_chains(c.body);
            if (t->finally_body) t->finally_body = collapse_temp_chains(*t->finally_body);
        } else if (s->kind == StmtKind::SyncStmt) {
            auto* sy = static_cast<SyncStmt*>(s.get());
            sy->body = collapse_temp_chains(sy->body);
        }
    }
    for (int iter = 0; iter < 4; ++iter) {
        bool changed = false;
        size_t n = stmts.size();
        for (size_t i = 0; i < n; ++i) {
            if (!stmts[i]) continue;
            auto a = as_assign(stmts[i]);
            if (!a.has_value()) continue;
            ExprPtr tgt = a->first, val = a->second;
            if (!tgt || !(tgt->kind == ExprKind::Local && is_synth_temp(static_cast<Local*>(tgt.get())->name))) continue;
            std::string tgt_name = static_cast<Local*>(tgt.get())->name;

            size_t max_j = (n > 64) ? std::min(n, i + 32) : n;
            std::vector<size_t> uses;
            for (size_t j = i + 1; j < max_j; ++j) {
                if (stmts[j] && contains_local_ref_stmt(stmts[j], tgt_name)) {
                    uses.push_back(j);
                    if (uses.size() > 1) break;
                }
            }
            if (uses.size() != 1) continue;
            size_t j = uses[0];
            if (!stmts[j]) continue;
            auto b = as_assign(stmts[j]);
            if (!b.has_value()) continue;
            ExprPtr tgt2 = b->first, val2 = b->second;
            if (!val2 || !(val2->kind == ExprKind::Local && static_cast<Local*>(val2.get())->name == tgt_name)) continue;
            if (j != i + 1 && has_side_effect(val)) continue;

            stmts[i] = nullptr;
            if (stmts[j]->kind == StmtKind::LocalDecl) {
                static_cast<LocalDecl*>(stmts[j].get())->init = val;
            } else {
                stmts[j] = std::make_shared<ExprStmtNode>(std::make_shared<Assign>(tgt2, val));
            }
            changed = true;
        }
        if (changed) {
            std::vector<StmtPtr> compacted;
            compacted.reserve(stmts.size());
            for (auto& s : stmts) {
                if (s) compacted.push_back(std::move(s));
            }
            stmts = std::move(compacted);
        } else {
            break;
        }
    }
    return stmts;
}

std::vector<StmtPtr> hoist_common_branch_tail(const std::vector<StmtPtr>& stmts) {
    std::vector<StmtPtr> out;
    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == StmtKind::IfStmt) {
            auto* i = static_cast<IfStmt*>(s.get());
            if (!i->then_body.empty() && i->else_body.has_value() && !i->else_body->empty()) {
                std::vector<StmtPtr> tb = i->then_body, eb = *i->else_body;
                std::vector<StmtPtr> tail;
                while (!tb.empty() && !eb.empty()) {
                    if (!tb.back() || !eb.back()) break;
                    auto a1 = as_assign(tb.back());
                    auto a2 = as_assign(eb.back());
                    if (!a1.has_value() || !a2.has_value()) break;
                    ExprPtr t1 = a1->first, v1 = a1->second;
                    ExprPtr t2 = a2->first, v2 = a2->second;
                    if (!t1 || !t2 || !v1 || !v2) break;
                    if ((t1->kind == ExprKind::Local && t2->kind == ExprKind::Local &&
                         static_cast<Local*>(t1.get())->name == static_cast<Local*>(t2.get())->name &&
                         is_same_expr(v1, v2)) ||
                        (same_target(t1, t2) && is_same_expr(v1, v2)) ||
                        false) {
                        tail.push_back(tb.back());
                        tb.pop_back();
                        eb.pop_back();
                        continue;
                    }
                    break;
                }
                // Hoist common synth temp assignments from anywhere in branches
                std::vector<StmtPtr> hoisted_common;
                for (size_t ti = 0; ti < tb.size(); ) {
                    auto a1 = as_assign(tb[ti]);
                    if (!a1.has_value() || !a1->first || a1->first->kind != ExprKind::Local ||
                        !is_synth_temp(static_cast<Local*>(a1->first.get())->name) || has_side_effect(a1->second)) {
                        ++ti;
                        continue;
                    }
                    std::string loc_name = static_cast<Local*>(a1->first.get())->name;
                    int match_idx = -1;
                    for (size_t ei = 0; ei < eb.size(); ++ei) {
                        auto a2 = as_assign(eb[ei]);
                        if (a2.has_value() && a2->first && a2->first->kind == ExprKind::Local &&
                            static_cast<Local*>(a2->first.get())->name == loc_name &&
                            is_same_expr(a1->second, a2->second)) {
                            match_idx = static_cast<int>(ei);
                            break;
                        }
                    }
                    if (match_idx >= 0) {
                        hoisted_common.push_back(tb[ti]);
                        tb.erase(tb.begin() + ti);
                        eb.erase(eb.begin() + match_idx);
                    } else {
                        ++ti;
                    }
                }

                if (!tail.empty() || !hoisted_common.empty()) {
                    for (auto& hc : hoisted_common) out.push_back(hc);
                    i->then_body = tb;
                    i->else_body = eb;
                    out.push_back(s);
                    for (auto it = tail.rbegin(); it != tail.rend(); ++it) out.push_back(*it);
                    continue;
                }
            }
        }
        out.push_back(s);
    }
    return out;
}

bool block_always_exits(const std::vector<StmtPtr>& block);

bool is_unconditional_exit(const StmtPtr& s) {
    if (!s) return false;
    if (s->kind == StmtKind::ReturnStmt || s->kind == StmtKind::ThrowStmt ||
        s->kind == StmtKind::BreakStmt || s->kind == StmtKind::ContinueStmt) return true;
    if (s->kind == StmtKind::TryStmt) {
        auto* t = static_cast<TryStmt*>(s.get());
        if (!block_always_exits(t->body)) return false;
        for (auto& cat : t->catches) {
            if (!block_always_exits(cat.body)) return false;
        }
        return true;
    }
    if (s->kind == StmtKind::IfStmt) {
        auto* i = static_cast<IfStmt*>(s.get());
        if (i->else_body.has_value() && block_always_exits(i->then_body) && block_always_exits(*i->else_body)) return true;
    }
    return false;
}

bool block_always_exits(const std::vector<StmtPtr>& block) {
    if (block.empty() || !block.back()) return false;
    return is_unconditional_exit(block.back());
}

std::vector<StmtPtr> eliminate_redundant_else_after_return(const std::vector<StmtPtr>& stmts) {
    std::vector<StmtPtr> out;
    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == StmtKind::IfStmt) {
            auto* i = static_cast<IfStmt*>(s.get());
            if (i->else_body.has_value() && !i->else_body->empty() && block_always_exits(i->then_body)) {
                std::vector<StmtPtr> eb = *i->else_body;
                i->else_body = std::nullopt;
                out.push_back(s);
                for (auto& es : eb) out.push_back(es);
                continue;
            }
        }
        out.push_back(s);
    }
    return out;
}

bool substitute_expr_or_root(ExprPtr& expr, const std::string& name, const ExprPtr& replacement) {
    if (!expr) return false;
    if (expr->kind == ExprKind::Local && static_cast<Local*>(expr.get())->name == name) {
        expr = replacement;
        return true;
    }
    return substitute_temp(expr, name, replacement);
}

bool substitute_temp_in_stmt(const StmtPtr& stmt, const std::string& name, const ExprPtr& replacement) {
    if (!stmt) return false;
    if (stmt->kind == StmtKind::ExprStmt) {
        auto* es = static_cast<ExprStmtNode*>(stmt.get());
        return substitute_expr_or_root(es->expr, name, replacement);
    }
    if (stmt->kind == StmtKind::ReturnStmt) {
        auto* r = static_cast<ReturnStmt*>(stmt.get());
        return substitute_expr_or_root(r->expr, name, replacement);
    }
    if (stmt->kind == StmtKind::ThrowStmt) {
        auto* t = static_cast<ThrowStmt*>(stmt.get());
        return substitute_expr_or_root(t->expr, name, replacement);
    }
    if (stmt->kind == StmtKind::IfStmt) {
        auto* i = static_cast<IfStmt*>(stmt.get());
        bool done = substitute_expr_or_root(i->cond, name, replacement);
        for (auto& s : i->then_body) if (substitute_temp_in_stmt(s, name, replacement)) done = true;
        if (i->else_body.has_value()) {
            for (auto& s : *i->else_body) if (substitute_temp_in_stmt(s, name, replacement)) done = true;
        }
        return done;
    }
    if (stmt->kind == StmtKind::WhileStmt) {
        auto* w = static_cast<WhileStmt*>(stmt.get());
        bool done = substitute_expr_or_root(w->cond, name, replacement);
        for (auto& s : w->body) if (substitute_temp_in_stmt(s, name, replacement)) done = true;
        return done;
    }
    if (stmt->kind == StmtKind::DoWhileStmt) {
        auto* dw = static_cast<DoWhileStmt*>(stmt.get());
        bool done = substitute_expr_or_root(dw->cond, name, replacement);
        for (auto& s : dw->body) if (substitute_temp_in_stmt(s, name, replacement)) done = true;
        return done;
    }
    if (stmt->kind == StmtKind::ForStmt) {
        auto* f = static_cast<ForStmt*>(stmt.get());
        bool done = false;
        if (f->init) done |= substitute_expr_or_root(f->init, name, replacement);
        if (f->cond) done |= substitute_expr_or_root(f->cond, name, replacement);
        if (f->update) done |= substitute_temp_in_stmt(f->update, name, replacement);
        for (auto& s : f->body) if (substitute_temp_in_stmt(s, name, replacement)) done = true;
        return done;
    }
    if (stmt->kind == StmtKind::BlockStmt) {
        auto* b = static_cast<BlockStmt*>(stmt.get());
        bool done = false;
        for (auto& s : b->stmts) if (substitute_temp_in_stmt(s, name, replacement)) done = true;
        return done;
    }
    if (stmt->kind == StmtKind::TryStmt) {
        auto* t = static_cast<TryStmt*>(stmt.get());
        bool done = false;
        for (auto& s : t->body) if (substitute_temp_in_stmt(s, name, replacement)) done = true;
        for (auto& c : t->catches) for (auto& s : c.body) if (substitute_temp_in_stmt(s, name, replacement)) done = true;
        if (t->finally_body.has_value()) {
            for (auto& s : *t->finally_body) if (substitute_temp_in_stmt(s, name, replacement)) done = true;
        }
        return done;
    }
    if (stmt->kind == StmtKind::LocalDecl) {
        auto* ld = static_cast<LocalDecl*>(stmt.get());
        return substitute_expr_or_root(ld->init, name, replacement);
    }
    if (stmt->kind == StmtKind::SwitchStmt) {
        auto* sw = static_cast<SwitchStmt*>(stmt.get());
        bool done = substitute_expr_or_root(sw->selector, name, replacement);
        for (auto& c : sw->cases) {
            for (auto& s : c.body) if (substitute_temp_in_stmt(s, name, replacement)) done = true;
        }
        return done;
    }
    if (stmt->kind == StmtKind::SyncStmt) {
        auto* sy = static_cast<SyncStmt*>(stmt.get());
        bool done = substitute_expr_or_root(sy->expr, name, replacement);
        for (auto& s : sy->body) if (substitute_temp_in_stmt(s, name, replacement)) done = true;
        return done;
    }
    return false;
}

std::vector<StmtPtr> inline_single_use_temps_anywhere(std::vector<StmtPtr> stmts) {
    bool changed = true;
    while (changed) {
        changed = false;
        size_t n = stmts.size();
        for (size_t i = 0; i < n; ++i) {
            auto a = as_assign(stmts[i]);
            if (!a.has_value()) continue;
            ExprPtr tgt = a->first, val = a->second;
            if (!(tgt->kind == ExprKind::Local && is_synth_temp(static_cast<Local*>(tgt.get())->name))) continue;
            std::string tgt_name = static_cast<Local*>(tgt.get())->name;
            std::vector<size_t> uses;
            for (size_t j = i + 1; j < n; ++j) {
                if (contains_local_ref_stmt(stmts[j], tgt_name)) uses.push_back(j);
            }
            if (uses.size() != 1) continue;
            size_t j = uses[0];
            StmtPtr target_stmt = stmts[j];
            if (!target_stmt) continue;
            // НЕЛЬЗЯ инлайнить переменную, вычисленную ДО цикла, В ТЕЛО цикла:
            // иначе выражение будет перевычисляться на каждой итерации вместо одного раза до старта цикла!
            if (target_stmt->kind == StmtKind::WhileStmt || target_stmt->kind == StmtKind::ForStmt || target_stmt->kind == StmtKind::DoWhileStmt) {
                continue;
            }
            // Если между определением и использованием есть другие операторы, запрещаем перенос выражений с побочными эффектами
            if (j != i + 1 && has_side_effect(val)) continue;
            if (substitute_temp_in_stmt(target_stmt, tgt_name, val)) {
                std::vector<StmtPtr> new_stmts;
                new_stmts.insert(new_stmts.end(), stmts.begin(), stmts.begin() + i);
                new_stmts.insert(new_stmts.end(), stmts.begin() + i + 1, stmts.end());
                stmts = std::move(new_stmts);
                changed = true;
                break;
            }
        }
    }
    return stmts;
}

StmtPtr simplify_while_true(const std::shared_ptr<WhileStmt>& s) {
    std::vector<StmtPtr> body = s->body;
    if (!body.empty() && body[0] && body[0]->kind == StmtKind::IfStmt) {
        auto* first = static_cast<IfStmt*>(body[0].get());
        if (!first->then_body.empty() && first->then_body.size() == 1 && is_plain_break(first->then_body[0])) {
            s->cond = negate(first->cond);
            std::vector<StmtPtr> new_body;
            if (first->else_body.has_value()) {
                new_body = *first->else_body;
            }
            new_body.insert(new_body.end(), body.begin() + 1, body.end());
            body = std::move(new_body);
        } else if (first->else_body.has_value() && first->else_body->size() == 1 && is_plain_break((*first->else_body)[0])) {
            s->cond = first->cond;
            std::vector<StmtPtr> new_body = first->then_body;
            new_body.insert(new_body.end(), body.begin() + 1, body.end());
            body = std::move(new_body);
        }
    }
    s->body = body;
    if (s->cond) s->cond = simplify_expr(s->cond);
    if (s->cond && s->cond->kind == ExprKind::Const && static_cast<Const*>(s->cond.get())->literal == "true" && !body.empty()) {
        StmtPtr last = body.back();
        if (last && last->kind == StmtKind::IfStmt) {
            auto* li = static_cast<IfStmt*>(last.get());
            auto& tb = li->then_body;
            auto& eb = li->else_body;
            if (!tb.empty() && tb.size() == 1 && is_plain_continue(tb[0]) && eb.has_value() && eb->size() == 1 &&
                is_plain_break((*eb)[0])) {
                return std::make_shared<DoWhileStmt>(li->cond, std::vector<StmtPtr>(body.begin(), body.end() - 1), s->label);
            }
            if (eb.has_value() && eb->size() == 1 && is_plain_continue((*eb)[0]) && !tb.empty() && tb.size() == 1 &&
                is_plain_break(tb[0])) {
                return std::make_shared<DoWhileStmt>(negate(li->cond), std::vector<StmtPtr>(body.begin(), body.end() - 1), s->label);
            }
            // Паттерн 3: if (cond) break; в конце тела while(true) без else
            if (!tb.empty() && tb.size() == 1 && is_plain_break(tb[0]) && (!eb.has_value() || eb->empty())) {
                return std::make_shared<DoWhileStmt>(negate(li->cond), std::vector<StmtPtr>(body.begin(), body.end() - 1), s->label);
            }
            // Паттерн 4: if (cond) break; в конце тела while(true) в ветке else (при пустом then)
            if (tb.empty() && eb.has_value() && eb->size() == 1 && is_plain_break((*eb)[0])) {
                return std::make_shared<DoWhileStmt>(li->cond, std::vector<StmtPtr>(body.begin(), body.end() - 1), s->label);
            }
        }
    }
    bool cond_is_true = s->cond && s->cond->kind == ExprKind::Const && static_cast<Const*>(s->cond.get())->literal == "true";
    if (!cond_is_true && !body.empty()) {
        StmtPtr last = body.back();
        if (looks_like_update(last)) {
            return std::make_shared<ForStmt>(nullptr, s->cond, last, std::vector<StmtPtr>(body.begin(), body.end() - 1), s->label);
        }
    }
    return s;
}

static StmtPtr simplify_stmt(StmtPtr s, int depth);

static std::vector<StmtPtr> simplify_stmt_list(const std::vector<StmtPtr>& stmts, int depth = 0) {
    static thread_local int s_simp_stmt_depth = 0;
    s_simp_stmt_depth++;
    struct SimpStmtGuard { ~SimpStmtGuard() { s_simp_stmt_depth--; } } s_stmt_guard;
    if (depth > 30 || s_simp_stmt_depth > 30) return stmts;
    std::vector<StmtPtr> res;
    res.reserve(stmts.size());
    for (auto& s : stmts) {
        if (s) res.push_back(simplify_stmt(s, depth));
    }
    return res;
}

static StmtPtr simplify_stmt(StmtPtr s, int depth) {
    if (!s) return s;
    
    if (depth > 60) return s;
    if (s->kind == StmtKind::ExprStmt) {
        auto* es = static_cast<ExprStmtNode*>(s.get());
        if (es->expr) es->expr = simplify_expr(es->expr);
        return s;
    }
    if (s->kind == StmtKind::ReturnStmt) {
        auto* r = static_cast<ReturnStmt*>(s.get());
        if (r->expr) r->expr = simplify_expr(r->expr);
        return s;
    }
    if (s->kind == StmtKind::ThrowStmt) {
        auto* t = static_cast<ThrowStmt*>(s.get());
        if (t->expr) t->expr = simplify_expr(t->expr);
        return s;
    }
    if (s->kind == StmtKind::LocalDecl) {
        auto* ld = static_cast<LocalDecl*>(s.get());
        if (ld->init) ld->init = simplify_expr(ld->init);
        return s;
    }
    if (s->kind == StmtKind::WhileStmt) {
        auto w = std::static_pointer_cast<WhileStmt>(s);
        if (w->cond) w->cond = simplify_expr(w->cond);
        w->body = simplify_stmt_list(w->body, depth + 1);
        while (!w->body.empty() && is_plain_continue(w->body.back())) w->body.pop_back();
        if (w->cond && w->cond->kind == ExprKind::Const && static_cast<Const*>(w->cond.get())->literal == "true") {
            return simplify_while_true(w);
        }
        if (!w->body.empty() && looks_like_update(w->body.back())) {
            StmtPtr last = w->body.back();
            return std::make_shared<ForStmt>(nullptr, w->cond, last, std::vector<StmtPtr>(w->body.begin(), w->body.end() - 1), w->label);
        }
        return w;
    }
    if (s->kind == StmtKind::DoWhileStmt) {
        auto* w = static_cast<DoWhileStmt*>(s.get());
        if (w->cond) w->cond = simplify_expr(w->cond);
        w->body = simplify_stmt_list(w->body, depth + 1);
        while (!w->body.empty() && is_plain_continue(w->body.back())) w->body.pop_back();
        if (w->cond && w->cond->kind == ExprKind::Const && static_cast<Const*>(w->cond.get())->literal == "false" && (!w->label.has_value() || w->label->empty())) {
            bool has_break = false;
            for (auto& bs : w->body) {
                if (bs && bs->kind == StmtKind::BreakStmt && (!static_cast<BreakStmt*>(bs.get())->label.has_value() || static_cast<BreakStmt*>(bs.get())->label->empty())) {
                    has_break = true;
                    break;
                }
            }
            if (!has_break && w->body.size() == 1) {
                return w->body[0];
            }
        }

        return s;
    }

    if (s->kind == StmtKind::ForStmt) {
        auto* f = static_cast<ForStmt*>(s.get());
        if (f->init) f->init = simplify_expr(f->init);
        if (f->cond) f->cond = simplify_expr(f->cond);
        if (f->update && f->update->kind == StmtKind::ExprStmt) {
            auto* es = static_cast<ExprStmtNode*>(f->update.get());
            if (es->expr) es->expr = simplify_expr(es->expr);
        }
        f->body = simplify_stmt_list(f->body, depth + 1);
        while (!f->body.empty() && is_plain_continue(f->body.back())) f->body.pop_back();
        return s;
    }
    if (s->kind == StmtKind::IfStmt) {
        auto* i = static_cast<IfStmt*>(s.get());
        if (i->cond) i->cond = simplify_expr(i->cond);
        if (!i->then_body.empty()) i->then_body = simplify_stmt_list(i->then_body, depth + 1);
        if (i->else_body.has_value() && !i->else_body->empty()) i->else_body = simplify_stmt_list(*i->else_body, depth + 1);
        if (i->then_body.empty() && i->else_body.has_value() && !i->else_body->empty()) {
            i->cond = negate(i->cond);
            i->then_body = *i->else_body;
            i->else_body = std::nullopt;
        } else if (i->else_body.has_value() && !i->else_body->empty()) {
            bool cond_is_negated = (i->cond && i->cond->kind == ExprKind::UnOp && static_cast<UnOp*>(i->cond.get())->op == "!");
            bool then_is_if = (!i->then_body.empty() && i->then_body[0]->kind == StmtKind::IfStmt);
            bool else_is_not_if = (i->else_body->size() != 1 || (*i->else_body)[0]->kind != StmtKind::IfStmt);
            if (cond_is_negated || (then_is_if && else_is_not_if)) {
                i->cond = negate(i->cond);
                std::swap(i->then_body, *i->else_body);
            }
        }
        return s;
    }
    if (s->kind == StmtKind::TryStmt) {
        auto* t = static_cast<TryStmt*>(s.get());
        t->body = simplify_stmt_list(t->body, depth + 1);
        for (auto& c : t->catches) c.body = simplify_stmt_list(c.body, depth + 1);
        if (t->finally_body.has_value()) t->finally_body = simplify_stmt_list(*t->finally_body, depth + 1);
        return s;
    }
    if (s->kind == StmtKind::SwitchStmt) {
        auto* sw = static_cast<SwitchStmt*>(s.get());
        if (sw->selector) sw->selector = simplify_expr(sw->selector);
        for (auto& c : sw->cases) c.body = simplify_stmt_list(c.body, depth + 1);
        return s;
    }
    if (s->kind == StmtKind::SyncStmt) {
        auto* sy = static_cast<SyncStmt*>(s.get());
        if (sy->expr) sy->expr = simplify_expr(sy->expr);
        sy->body = simplify_stmt_list(sy->body, depth + 1);
        return s;
    }
    return s;
}

static bool is_same_stmt(const StmtPtr& a, const StmtPtr& b);

static bool is_same_stmts(const std::vector<StmtPtr>& a, const std::vector<StmtPtr>& b) {
    if (a.size() != b.size()) return false;
    for (size_t k = 0; k < a.size(); ++k) {
        if (!is_same_stmt(a[k], b[k])) return false;
    }
    return true;
}

static bool is_same_stmt(const StmtPtr& a, const StmtPtr& b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    switch (a->kind) {
        case StmtKind::ExprStmt:
            return is_same_expr(static_cast<ExprStmtNode*>(a.get())->expr, static_cast<ExprStmtNode*>(b.get())->expr);
        case StmtKind::LocalDecl: {
            auto* l1 = static_cast<LocalDecl*>(a.get());
            auto* l2 = static_cast<LocalDecl*>(b.get());
            return l1->type == l2->type && l1->name == l2->name && is_same_expr(l1->init, l2->init);
        }
        case StmtKind::ReturnStmt:
            return is_same_expr(static_cast<ReturnStmt*>(a.get())->expr, static_cast<ReturnStmt*>(b.get())->expr);
        case StmtKind::ThrowStmt:
            return is_same_expr(static_cast<ThrowStmt*>(a.get())->expr, static_cast<ThrowStmt*>(b.get())->expr);
        case StmtKind::BreakStmt:
            return static_cast<BreakStmt*>(a.get())->label == static_cast<BreakStmt*>(b.get())->label;
        case StmtKind::ContinueStmt:
            return static_cast<ContinueStmt*>(a.get())->label == static_cast<ContinueStmt*>(b.get())->label;
        case StmtKind::GotoStmt:
            return static_cast<GotoStmt*>(a.get())->label == static_cast<GotoStmt*>(b.get())->label;
        case StmtKind::LabelStmt:
            return static_cast<LabelStmt*>(a.get())->label == static_cast<LabelStmt*>(b.get())->label;
        case StmtKind::RawStmt:
            return static_cast<RawStmt*>(a.get())->text == static_cast<RawStmt*>(b.get())->text;
        case StmtKind::BlockStmt:
            return is_same_stmts(static_cast<BlockStmt*>(a.get())->stmts, static_cast<BlockStmt*>(b.get())->stmts);
        case StmtKind::IfStmt: {
            auto* i1 = static_cast<IfStmt*>(a.get());
            auto* i2 = static_cast<IfStmt*>(b.get());
            if (!is_same_expr(i1->cond, i2->cond)) return false;
            if (!is_same_stmts(i1->then_body, i2->then_body)) return false;
            if (i1->else_body.has_value() != i2->else_body.has_value()) return false;
            if (i1->else_body.has_value()) {
                if (!is_same_stmts(*i1->else_body, *i2->else_body)) return false;
            }
            return true;
        }
        default:
            return false;
    }
}

static void unwrap_nested_blocks(std::vector<StmtPtr>& body) {
    if (body.size() == 1 && body[0] && body[0]->kind == StmtKind::BlockStmt) {
        auto* b = static_cast<BlockStmt*>(body[0].get());
        if (!b->label.has_value()) {
            body = b->stmts;
        }
    }
}

std::vector<StmtPtr> collapse_nested_if_conditions(std::vector<StmtPtr> stmts) {
    bool changed = true;
    int max_iters = 25;
    while (changed && --max_iters >= 0) {
        changed = false;
        std::vector<StmtPtr> next_stmts;
        next_stmts.reserve(stmts.size());

        for (size_t s_idx = 0; s_idx < stmts.size(); ++s_idx) {
            auto s = stmts[s_idx];
            if (!s) continue;

            if (s->kind == StmtKind::IfStmt) {
                auto* i = static_cast<IfStmt*>(s.get());
                unwrap_nested_blocks(i->then_body);
                if (i->else_body.has_value()) unwrap_nested_blocks(*i->else_body);

                // 1. Пустые ветки
                // if (C) {} else { B } => if (!C) { B }
                if (i->then_body.empty() && i->else_body.has_value() && !i->else_body->empty()) {
                    i->cond = invert_condition(i->cond);
                    i->then_body = *i->else_body;
                    i->else_body = std::nullopt;
                    changed = true;
                }
                // if (C) { A } else {} => if (C) { A }
                if (i->else_body.has_value() && i->else_body->empty()) {
                    i->else_body = std::nullopt;
                    changed = true;
                }
                // if (C) {} без else => удаляем
                if (i->then_body.empty() && !i->else_body.has_value()) {
                    changed = true;
                    continue;
                }

                // 2. Идентичные ветки
                // if (C) { A } else { A } => A
                if (i->else_body.has_value() && is_same_stmts(i->then_body, *i->else_body)) {
                    for (auto& bs : i->then_body) next_stmts.push_back(bs);
                    changed = true;
                    continue;
                }

                // 3. Схлопывание if (c1) { if (c2) { ... } } без else в if (c1 && c2)
                if (!i->else_body.has_value() && i->then_body.size() == 1 && i->then_body[0] && i->then_body[0]->kind == StmtKind::IfStmt) {
                    auto* inner = static_cast<IfStmt*>(i->then_body[0].get());
                    if (!inner->else_body.has_value() && inner->cond) {
                        auto new_then = inner->then_body;
                        if (!is_same_expr(i->cond, inner->cond)) {
                            i->cond = std::make_shared<BinOp>("&&", i->cond, inner->cond, "boolean");
                        }
                        i->then_body = std::move(new_then);
                        changed = true;
                    }
                }

                // 4. Схлопывание анти-декомпиляторных ловушек в then_body (AegisArmorTrap):
                // 4a. if (C) { if (C) { A } else { B } } else { B } => if (C) { A } else { B }
                if (i->else_body.has_value() && i->then_body.size() == 1 && i->then_body[0] && i->then_body[0]->kind == StmtKind::IfStmt) {
                    auto* inner = static_cast<IfStmt*>(i->then_body[0].get());
                    if (inner->else_body.has_value()) {
                        if (is_same_expr(i->cond, inner->cond) && is_same_stmts(*i->else_body, *inner->else_body)) {
                            i->then_body = inner->then_body;
                            changed = true;
                        } else if (is_opposite_expr(i->cond, inner->cond) && is_same_stmts(*i->else_body, inner->then_body)) {
                            // if (C) { if (!C) { B } else { A } } else { B } => if (C) { A } else { B }
                            i->then_body = *inner->else_body;
                            changed = true;
                        }
                    }
                }

                // 5. Схлопывание анти-декомпиляторных ловушек в else_body (симметричный AegisArmorTrap):
                // 5a. if (C) { A } else { if (C) { A } else { B } } => if (C) { A } else { B }
                if (i->else_body.has_value() && i->else_body->size() == 1 && (*i->else_body)[0] && (*i->else_body)[0]->kind == StmtKind::IfStmt) {
                    auto* inner = static_cast<IfStmt*>((*i->else_body)[0].get());
                    if (inner->else_body.has_value()) {
                        if (is_same_expr(i->cond, inner->cond) && is_same_stmts(i->then_body, inner->then_body)) {
                            i->else_body = inner->else_body;
                            changed = true;
                        } else if (is_opposite_expr(i->cond, inner->cond) && is_same_stmts(i->then_body, *inner->else_body)) {
                            // if (C) { A } else { if (!C) { B } else { A } } => if (C) { A } else { B }
                            i->else_body = inner->then_body;
                            changed = true;
                        } else if (is_opposite_expr(i->cond, inner->cond) && is_same_stmts(i->then_body, inner->then_body)) {
                            // if (!C) { B } else { if (C) { A } else { B } } => if (C) { A } else { B }
                            i->cond = inner->cond;
                            i->then_body = inner->then_body;
                            i->else_body = inner->else_body;
                            changed = true;
                        }
                    }
                }

                // Рекурсивный проход в тела
                i->then_body = collapse_nested_if_conditions(i->then_body);
                if (i->else_body.has_value()) {
                    i->else_body = collapse_nested_if_conditions(*i->else_body);
                }
                next_stmts.push_back(s);
            } else if (s->kind == StmtKind::WhileStmt) {
                auto* w = static_cast<WhileStmt*>(s.get());
                w->body = collapse_nested_if_conditions(w->body);
                next_stmts.push_back(s);
            } else if (s->kind == StmtKind::DoWhileStmt) {
                auto* dw = static_cast<DoWhileStmt*>(s.get());
                dw->body = collapse_nested_if_conditions(dw->body);
                next_stmts.push_back(s);
            } else if (s->kind == StmtKind::ForStmt) {
                auto* f = static_cast<ForStmt*>(s.get());
                f->body = collapse_nested_if_conditions(f->body);
                next_stmts.push_back(s);
            } else if (s->kind == StmtKind::BlockStmt) {
                auto* b = static_cast<BlockStmt*>(s.get());
                b->stmts = collapse_nested_if_conditions(b->stmts);
                next_stmts.push_back(s);
            } else if (s->kind == StmtKind::TryStmt) {
                auto* t = static_cast<TryStmt*>(s.get());
                t->body = collapse_nested_if_conditions(t->body);
                for (auto& c : t->catches) c.body = collapse_nested_if_conditions(c.body);
                if (t->finally_body.has_value()) t->finally_body = collapse_nested_if_conditions(*t->finally_body);
                next_stmts.push_back(s);
            } else if (s->kind == StmtKind::SwitchStmt) {
                auto* sw = static_cast<SwitchStmt*>(s.get());
                for (auto& c : sw->cases) c.body = collapse_nested_if_conditions(c.body);
                next_stmts.push_back(s);
            } else if (s->kind == StmtKind::SyncStmt) {
                auto* sy = static_cast<SyncStmt*>(s.get());
                sy->body = collapse_nested_if_conditions(sy->body);
                next_stmts.push_back(s);
            } else {
                next_stmts.push_back(s);
            }
        }
        stmts = std::move(next_stmts);
    }
    return stmts;
}

std::vector<StmtPtr> merge_sequential_short_circuit_ifs(std::vector<StmtPtr> stmts) {
    if (stmts.size() < 2) return stmts;
    std::vector<StmtPtr> out;
    size_t idx = 0;
    while (idx < stmts.size()) {
        if (idx + 1 < stmts.size() && stmts[idx] && stmts[idx + 1] && stmts[idx]->kind == StmtKind::IfStmt && stmts[idx + 1]->kind == StmtKind::IfStmt) {
            auto* i1 = static_cast<IfStmt*>(stmts[idx].get());
            auto* i2 = static_cast<IfStmt*>(stmts[idx + 1].get());
            if (!i1->else_body.has_value() && !i2->else_body.has_value() &&
                i1->then_body.size() == 1 && i2->then_body.size() == 1 &&
                is_same_stmt(i1->then_body[0], i2->then_body[0])) {
                auto merged_cond = std::make_shared<BinOp>("||", i1->cond, i2->cond, "boolean");
                auto merged_if = std::make_shared<IfStmt>(merged_cond, i1->then_body, std::nullopt);
                out.push_back(merged_if);
                idx += 2;
                continue;
            }
        }
        out.push_back(stmts[idx]);
        idx += 1;
    }
    return out;
}

static bool is_twr_close_expr(const ExprPtr& e, const std::string& res_name, const std::string& exc_name) {
    if (!e) return false;
    if (e->kind == ExprKind::MethodCall) {
        auto* mc = static_cast<MethodCall*>(e.get());
        if (mc->name == "close" && mc->target && mc->target->kind == ExprKind::Local &&
            static_cast<Local*>(mc->target.get())->name == res_name) {
            return true;
        }
        if (mc->name == "addSuppressed" && mc->target && mc->target->kind == ExprKind::Local &&
            !exc_name.empty() && static_cast<Local*>(mc->target.get())->name == exc_name) {
            return true;
        }
        if (mc->name == "$closeResource") {
            for (auto& a : mc->args) {
                if (a && a->kind == ExprKind::Local && static_cast<Local*>(a.get())->name == res_name) {
                    return true;
                }
            }
        }
    }
    return false;
}

static bool is_twr_close_or_cleanup_stmt(const StmtPtr& s, const std::string& res_name, const std::string& exc_name) {
    if (!s) return false;
    if (s->kind == StmtKind::ExprStmt) {
        return is_twr_close_expr(static_cast<ExprStmtNode*>(s.get())->expr, res_name, exc_name);
    }
    if (s->kind == StmtKind::IfStmt) {
        auto* ifs = static_cast<IfStmt*>(s.get());
        bool then_all = true;
        for (const auto& ts : ifs->then_body) {
            if (!is_twr_close_or_cleanup_stmt(ts, res_name, exc_name)) {
                then_all = false;
                break;
            }
        }
        if (!then_all) return false;
        if (ifs->else_body.has_value()) {
            for (const auto& es : *ifs->else_body) {
                if (!is_twr_close_or_cleanup_stmt(es, res_name, exc_name)) return false;
            }
        }
        return true;
    }
    if (s->kind == StmtKind::TryStmt) {
        auto* ts = static_cast<TryStmt*>(s.get());
        for (const auto& b : ts->body) {
            if (!is_twr_close_or_cleanup_stmt(b, res_name, exc_name)) return false;
        }
        for (const auto& c : ts->catches) {
            for (const auto& cb : c.body) {
                if (!is_twr_close_or_cleanup_stmt(cb, res_name, exc_name)) return false;
            }
        }
        if (ts->finally_body.has_value()) {
            for (const auto& fb : *ts->finally_body) {
                if (!is_twr_close_or_cleanup_stmt(fb, res_name, exc_name)) return false;
            }
        }
        return true;
    }
    return false;
}

static bool is_twr_synthetic_catch(const CatchClause& c, const std::string& exc_name) {
    if (c.type != "Throwable" && c.type != "java.lang.Throwable" &&
        c.type != "Exception" && c.type != "java.lang.Exception") {
        return false;
    }
    if (c.body.empty() || c.body.size() > 2) return false;
    bool has_throw = false;
    bool has_assign = false;
    for (const auto& s : c.body) {
        if (!s) continue;
        if (s->kind == StmtKind::ThrowStmt) {
            auto* ts = static_cast<ThrowStmt*>(s.get());
            if (ts->expr && ts->expr->kind == ExprKind::Local) {
                std::string ln = static_cast<Local*>(ts->expr.get())->name;
                if (ln == c.var_name || (!exc_name.empty() && ln == exc_name)) {
                    has_throw = true;
                }
            }
        } else if (s->kind == StmtKind::ExprStmt) {
            auto* es = static_cast<ExprStmtNode*>(s.get());
            if (es->expr && es->expr->kind == ExprKind::Assign) {
                auto* as = static_cast<Assign*>(es->expr.get());
                if (as->target && as->target->kind == ExprKind::Local) {
                    std::string tn = static_cast<Local*>(as->target.get())->name;
                    if (!exc_name.empty() && tn == exc_name) {
                        has_assign = true;
                    }
                }
            }
        }
    }
    return has_throw && (c.body.size() == 1 || has_assign);
}

static bool is_standalone_close_stmt(const StmtPtr& s, const std::string& res_name) {
    if (!s) return false;
    if (s->kind == StmtKind::ExprStmt) {
        auto* es = static_cast<ExprStmtNode*>(s.get());
        if (es->expr && es->expr->kind == ExprKind::MethodCall) {
            auto* mc = static_cast<MethodCall*>(es->expr.get());
            if (mc->name == "close" && mc->target && mc->target->kind == ExprKind::Local &&
                static_cast<Local*>(mc->target.get())->name == res_name) {
                return true;
            }
        }
    }
    if (s->kind == StmtKind::IfStmt) {
        auto* ifs = static_cast<IfStmt*>(s.get());
        if (ifs->then_body.size() == 1 && is_standalone_close_stmt(ifs->then_body[0], res_name)) {
            return true;
        }
    }
    return false;
}

static bool is_javac_modern_twr_catch(const CatchClause& c, const std::string& res_name) {
    if (c.type != "Throwable" && c.type != "java.lang.Throwable") return false;
    bool has_close = false;
    for (const auto& s : c.body) {
        if (contains_local_ref_stmt(s, res_name)) {
            has_close = true;
            break;
        }
    }
    return has_close;
}

std::vector<StmtPtr> fold_try_with_resources(std::vector<StmtPtr> stmts) {
    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == StmtKind::IfStmt) {
            auto* if_s = static_cast<IfStmt*>(s.get());
            if_s->then_body = fold_try_with_resources(if_s->then_body);
            if (if_s->else_body.has_value()) {
                if_s->else_body = fold_try_with_resources(*if_s->else_body);
            }
        } else if (s->kind == StmtKind::WhileStmt) {
            auto* w = static_cast<WhileStmt*>(s.get());
            w->body = fold_try_with_resources(w->body);
        } else if (s->kind == StmtKind::DoWhileStmt) {
            auto* d = static_cast<DoWhileStmt*>(s.get());
            d->body = fold_try_with_resources(d->body);
        } else if (s->kind == StmtKind::ForStmt) {
            auto* f = static_cast<ForStmt*>(s.get());
            f->body = fold_try_with_resources(f->body);
        } else if (s->kind == StmtKind::BlockStmt) {
            auto* b = static_cast<BlockStmt*>(s.get());
            b->stmts = fold_try_with_resources(b->stmts);
        } else if (s->kind == StmtKind::TryStmt) {
            auto* t = static_cast<TryStmt*>(s.get());
            t->body = fold_try_with_resources(t->body);
            for (auto& c : t->catches) c.body = fold_try_with_resources(c.body);
            if (t->finally_body.has_value()) t->finally_body = fold_try_with_resources(*t->finally_body);
        } else if (s->kind == StmtKind::SwitchStmt) {
            auto* sw = static_cast<SwitchStmt*>(s.get());
            for (auto& c : sw->cases) c.body = fold_try_with_resources(c.body);
        } else if (s->kind == StmtKind::SyncStmt) {
            auto* sy = static_cast<SyncStmt*>(s.get());
            sy->body = fold_try_with_resources(sy->body);
        }
    }

    if (stmts.size() < 2) return stmts;
    std::vector<StmtPtr> out;
    size_t i = 0;
    while (i < stmts.size()) {
        if (i + 1 < stmts.size() && stmts[i]->kind == StmtKind::LocalDecl) {
            auto* ld = static_cast<LocalDecl*>(stmts[i].get());
            std::string res_name = ld->name;
            std::string exc_name = "";
            size_t try_idx = i + 1;

            if (try_idx < stmts.size() && stmts[try_idx]->kind == StmtKind::LocalDecl) {
                auto* ld2 = static_cast<LocalDecl*>(stmts[try_idx].get());
                if ((ld2->type == "Throwable" || ld2->type == "java.lang.Throwable") &&
                    (!ld2->init || ld2->init->kind == ExprKind::Const)) {
                    exc_name = ld2->name;
                    try_idx = i + 2;
                }
            }

            if (try_idx < stmts.size() && stmts[try_idx]->kind == StmtKind::TryStmt) {
                auto* t = static_cast<TryStmt*>(stmts[try_idx].get());
                bool is_resource = false;
                bool skip_after_close = false;

                if (t->finally_body.has_value() && !t->finally_body->empty()) {
                    std::vector<StmtPtr> filtered_fb;
                    for (const auto& fs : *t->finally_body) {
                        if (is_twr_close_or_cleanup_stmt(fs, res_name, exc_name)) {
                            is_resource = true;
                        } else {
                            filtered_fb.push_back(fs);
                        }
                    }
                    if (is_resource) {
                        if (filtered_fb.empty()) {
                            t->finally_body = std::nullopt;
                        } else {
                            t->finally_body = filtered_fb;
                        }
                    }
                }

                if (!is_resource) {
                    for (const auto& c : t->catches) {
                        if (is_javac_modern_twr_catch(c, res_name)) {
                            is_resource = true;
                            break;
                        }
                    }
                    if (is_resource) {
                        if (try_idx + 1 < stmts.size() && is_standalone_close_stmt(stmts[try_idx + 1], res_name)) {
                            skip_after_close = true;
                        }
                    }
                }

                if (is_resource) {
                    std::vector<CatchClause> filtered_catches;
                    for (const auto& c : t->catches) {
                        if (!is_twr_synthetic_catch(c, exc_name) && !is_javac_modern_twr_catch(c, res_name)) {
                            filtered_catches.push_back(c);
                        }
                    }
                    t->catches = filtered_catches;
                    t->resources.push_back(stmts[i]);

                    if (!t->body.empty() && is_standalone_close_stmt(t->body.back(), res_name)) {
                        t->body.pop_back();
                    }

                    while (t->body.size() == 1 && t->body[0] && t->body[0]->kind == StmtKind::TryStmt) {
                        auto* inner_t = static_cast<TryStmt*>(t->body[0].get());
                        if (!inner_t->resources.empty() && !inner_t->finally_body.has_value() && inner_t->catches.empty()) {
                            t->resources.insert(t->resources.end(), inner_t->resources.begin(), inner_t->resources.end());
                            t->body = std::move(inner_t->body);
                        } else {
                            break;
                        }
                    }

                    out.push_back(stmts[try_idx]);
                    i = try_idx + 1 + (skip_after_close ? 1 : 0);
                    continue;
                }
            }
        }
        out.push_back(stmts[i]);
        i += 1;
    }
    return out;
}

std::vector<StmtPtr> fuse_for_initializers(std::vector<StmtPtr> stmts) {
    bool changed = true;
    while (changed) {
        changed = false;
        size_t n = stmts.size();
        for (size_t i = 0; i + 1 < n; ++i) {
            if (!stmts[i] || !stmts[i + 1]) continue;
            if (stmts[i + 1]->kind != StmtKind::ForStmt) continue;
            auto* f = static_cast<ForStmt*>(stmts[i + 1].get());
            if (f->init != nullptr) continue;

            std::string var_name;
            std::string var_type = "int";
            ExprPtr init_val = nullptr;
            bool is_decl = false;

            if (stmts[i]->kind == StmtKind::LocalDecl) {
                auto* ld = static_cast<LocalDecl*>(stmts[i].get());
                var_name = ld->name;
                var_type = ld->type;
                init_val = ld->init;
                is_decl = true;
            } else if (stmts[i]->kind == StmtKind::ExprStmt) {
                auto* es = static_cast<ExprStmtNode*>(stmts[i].get());
                if (es->expr && es->expr->kind == ExprKind::Assign) {
                    auto* as = static_cast<Assign*>(es->expr.get());
                    if (as->target && as->target->kind == ExprKind::Local) {
                        var_name = static_cast<Local*>(as->target.get())->name;
                        var_type = as->target->type;
                        init_val = as->value;
                        is_decl = false;
                    }
                }
            }

            if (var_name.empty()) continue;

            bool in_cond = f->cond && contains_local_ref_expr(f->cond, var_name);
            bool in_upd = f->update && contains_local_ref_stmt(f->update, var_name);
            if (!in_cond && !in_upd) continue;

            bool used_after = false;
            for (size_t j = i + 2; j < n; ++j) {
                if (contains_local_ref_stmt(stmts[j], var_name)) {
                    used_after = true;
                    break;
                }
            }

            if (is_decl && !used_after) {
                std::string init_val_str = init_val ? emit_expr(init_val) : "0";
                f->init = std::make_shared<Raw>(var_type + " " + var_name + " = " + init_val_str);
                stmts.erase(stmts.begin() + i);
                changed = true;
                break;
            } else if (!is_decl) {
                f->init = std::make_shared<Assign>(
                    std::make_shared<Local>(var_name, var_type),
                    init_val ? init_val : std::make_shared<Const>("0", "int")
                );
                stmts.erase(stmts.begin() + i);
                changed = true;
                break;
            }
        }
    }
    return stmts;
}

static bool expr_modifies_array(const ExprPtr& e, const std::string& name) {
    if (!e) return false;
    if (e->kind == ExprKind::Assign) {
        auto* as = static_cast<Assign*>(e.get());
        if (as->target && as->target->kind == ExprKind::Local && static_cast<Local*>(as->target.get())->name == name) return true;
        if (as->target && as->target->kind == ExprKind::ArrayAccess) {
            auto* aa = static_cast<ArrayAccess*>(as->target.get());
            if (aa->array && aa->array->kind == ExprKind::Local && static_cast<Local*>(aa->array.get())->name == name) return true;
        }
    }
    return false;
}

static ExprPtr fold_const_array_lookups_in_expr(ExprPtr e, const std::map<std::string, std::vector<ExprPtr>>& arrays) {
    if (!e) return e;
    if (e->kind == ExprKind::ArrayAccess) {
        auto* aa = static_cast<ArrayAccess*>(e.get());
        if (aa->array) aa->array = fold_const_array_lookups_in_expr(aa->array, arrays);
        if (aa->index) aa->index = fold_const_array_lookups_in_expr(aa->index, arrays);
        std::string array_ident = "";
        if (aa->array) {
            if (aa->array->kind == ExprKind::Local) {
                array_ident = static_cast<Local*>(aa->array.get())->name;
            } else if (aa->array->kind == ExprKind::FieldAccess) {
                array_ident = static_cast<FieldAccess*>(aa->array.get())->name;
            }
        }
        if (!array_ident.empty() && aa->index && aa->index->kind == ExprKind::Const) {
            auto it = arrays.find(array_ident);
            if (it != arrays.end()) {
                auto* idx_c = static_cast<Const*>(aa->index.get());
                try {
                    long long idx = std::stoll(idx_c->literal);
                    if (idx >= 0 && static_cast<size_t>(idx) < it->second.size()) {
                        ExprPtr val = it->second[static_cast<size_t>(idx)];
                        if (val) return val;
                    }
                } catch (...) {}
            }
        }
        return e;
    }
    if (e->kind == ExprKind::BinOp) {
        auto* b = static_cast<BinOp*>(e.get());
        if (b->left) b->left = fold_const_array_lookups_in_expr(b->left, arrays);
        if (b->right) b->right = fold_const_array_lookups_in_expr(b->right, arrays);
        return e;
    }
    if (e->kind == ExprKind::UnOp) {
        auto* u = static_cast<UnOp*>(e.get());
        if (u->expr) u->expr = fold_const_array_lookups_in_expr(u->expr, arrays);
        return e;
    }
    if (e->kind == ExprKind::Ternary) {
        auto* t = static_cast<Ternary*>(e.get());
        if (t->cond) t->cond = fold_const_array_lookups_in_expr(t->cond, arrays);
        if (t->tval) t->tval = fold_const_array_lookups_in_expr(t->tval, arrays);
        if (t->fval) t->fval = fold_const_array_lookups_in_expr(t->fval, arrays);
        return e;
    }
    if (e->kind == ExprKind::MethodCall) {
        auto* mc = static_cast<MethodCall*>(e.get());
        if (mc->target) mc->target = fold_const_array_lookups_in_expr(mc->target, arrays);
        for (auto& a : mc->args) if (a) a = fold_const_array_lookups_in_expr(a, arrays);
        return e;
    }
    if (e->kind == ExprKind::Cast) {
        auto* c = static_cast<Cast*>(e.get());
        if (c->expr) c->expr = fold_const_array_lookups_in_expr(c->expr, arrays);
        return e;
    }
    if (e->kind == ExprKind::Assign) {
        auto* as = static_cast<Assign*>(e.get());
        if (as->target) as->target = fold_const_array_lookups_in_expr(as->target, arrays);
        if (as->value) as->value = fold_const_array_lookups_in_expr(as->value, arrays);
        return e;
    }
    return e;
}

std::vector<StmtPtr> propagate_local_constant_arrays(std::vector<StmtPtr> stmts) {
    std::map<std::string, std::vector<ExprPtr>> candidate_arrays;
    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == StmtKind::LocalDecl) {
            auto* ld = static_cast<LocalDecl*>(s.get());
            if (ld->init && ld->init->kind == ExprKind::NewArray) {
                auto* na = static_cast<NewArray*>(ld->init.get());
                if (na->initializer.has_value() && !na->initializer->empty()) {
                    std::vector<ExprPtr> safe_items;
                    for (const auto& item : *na->initializer) {
                        if (item) safe_items.push_back(item);
                    }
                    if (!safe_items.empty()) candidate_arrays[ld->name] = std::move(safe_items);
                }
            }
        } else if (s->kind == StmtKind::ExprStmt) {
            auto* es = static_cast<ExprStmtNode*>(s.get());
            if (es->expr && es->expr->kind == ExprKind::Assign) {
                auto* as = static_cast<Assign*>(es->expr.get());
                if (as->target && as->value) {
                    if (as->target->kind == ExprKind::FieldAccess && as->value->kind == ExprKind::NewArray) {
                        auto* fa = static_cast<FieldAccess*>(as->target.get());
                        auto* na = static_cast<NewArray*>(as->value.get());
                        if (na->initializer.has_value() && !na->initializer->empty()) {
                            std::vector<ExprPtr> safe_items;
                            for (const auto& item : *na->initializer) {
                                if (item) safe_items.push_back(item);
                            }
                            if (!safe_items.empty()) candidate_arrays[fa->name] = std::move(safe_items);
                        }
                    } else if (as->target->kind == ExprKind::ArrayAccess && as->value->kind == ExprKind::Const) {
                        auto* aa = static_cast<ArrayAccess*>(as->target.get());
                        std::string target_name = "";
                        if (aa->array && aa->array->kind == ExprKind::FieldAccess) {
                            target_name = static_cast<FieldAccess*>(aa->array.get())->name;
                        } else if (aa->array && aa->array->kind == ExprKind::Local) {
                            target_name = static_cast<Local*>(aa->array.get())->name;
                        }
                        if (!target_name.empty() && candidate_arrays.count(target_name) && aa->index && aa->index->kind == ExprKind::Const) {
                            try {
                                long long idx = std::stoll(static_cast<Const*>(aa->index.get())->literal);
                                if (idx >= 0 && idx < 256) {
                                    auto& arr = candidate_arrays[target_name];
                                    if (arr.size() <= static_cast<size_t>(idx)) {
                                        arr.resize(static_cast<size_t>(idx) + 1, std::make_shared<Const>("0", "int"));
                                    }
                                    arr[static_cast<size_t>(idx)] = as->value;
                                }
                            } catch (...) {}
                        }
                    }
                }
            }
        }
    }
    if (candidate_arrays.empty()) return stmts;

    for (auto& s : stmts) {
        if (!s) continue;
        for (auto it = candidate_arrays.begin(); it != candidate_arrays.end(); ) {
            bool modified = false;
            if (s->kind == StmtKind::ExprStmt) {
                auto* es = static_cast<ExprStmtNode*>(s.get());
                if (es->expr && expr_modifies_array(es->expr, it->first)) modified = true;
            }
            if (modified) {
                it = candidate_arrays.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (candidate_arrays.empty()) return stmts;

    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == StmtKind::ExprStmt) {
            auto* es = static_cast<ExprStmtNode*>(s.get());
            if (es->expr) es->expr = fold_const_array_lookups_in_expr(es->expr, candidate_arrays);
        } else if (s->kind == StmtKind::ReturnStmt) {
            auto* r = static_cast<ReturnStmt*>(s.get());
            if (r->expr) r->expr = fold_const_array_lookups_in_expr(r->expr, candidate_arrays);
        } else if (s->kind == StmtKind::IfStmt) {
            auto* i = static_cast<IfStmt*>(s.get());
            if (i->cond) i->cond = fold_const_array_lookups_in_expr(i->cond, candidate_arrays);
        } else if (s->kind == StmtKind::WhileStmt) {
            auto* w = static_cast<WhileStmt*>(s.get());
            if (w->cond) w->cond = fold_const_array_lookups_in_expr(w->cond, candidate_arrays);
        } else if (s->kind == StmtKind::LocalDecl) {
            auto* ld = static_cast<LocalDecl*>(s.get());
            if (ld->init) ld->init = fold_const_array_lookups_in_expr(ld->init, candidate_arrays);
        }
    }
    return stmts;
}

std::vector<StmtPtr> fold_try_catches(std::vector<StmtPtr> stmts) {
    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == StmtKind::TryStmt) {
            auto* t = static_cast<TryStmt*>(s.get());
            t->body = fold_try_catches(t->body);
            for (auto& c : t->catches) c.body = fold_try_catches(c.body);
            if (t->finally_body.has_value()) t->finally_body = fold_try_catches(*t->finally_body);

            // 0. Neutralize fake exception trampolines: try { throw new E(); } catch (E e) { body; }
            if (t->body.size() == 1 && t->body[0] && t->body[0]->kind == StmtKind::ThrowStmt &&
                !t->finally_body.has_value() && t->resources.empty() && t->catches.size() == 1) {
                auto* ts = static_cast<ThrowStmt*>(t->body[0].get());
                if (ts->expr && ts->expr->kind == ExprKind::NewObject) {
                    auto* no = static_cast<NewObject*>(ts->expr.get());
                    const std::string& cat_t = t->catches[0].type;
                    if (no->type == cat_t || cat_t == "Throwable" || cat_t == "Exception" || cat_t == "RuntimeException" ||
                        no->type.find("Exception") != std::string::npos || no->type.find("Error") != std::string::npos) {
                        const std::string& vname = t->catches[0].var_name;
                        std::vector<StmtPtr> unwrapped = t->catches[0].body;
                        if (!vname.empty() && contains_local_ref_list(unwrapped, vname)) {
                            unwrapped.insert(unwrapped.begin(), std::make_shared<LocalDecl>(vname, cat_t, ts->expr));
                        }
                        s = std::make_shared<BlockStmt>(unwrapped);
                        continue;
                    }
                }
            }

            // 1. Flatten single nested try without finally/resources into outer try
            if (t->body.size() == 1 && t->body[0] && t->body[0]->kind == StmtKind::TryStmt) {
                auto inner_holder = t->body[0];
                auto* inner = static_cast<TryStmt*>(inner_holder.get());
                if (!inner->finally_body.has_value() && inner->resources.empty() && !t->finally_body.has_value()) {
                    std::vector<CatchClause> merged = inner->catches;
                    merged.insert(merged.end(), t->catches.begin(), t->catches.end());
                    auto new_body = inner->body;
                    t->body = std::move(new_body);
                    t->catches = std::move(merged);
                }
            }

            // 2. Multi-catch merging: combine adjacent catches with identical body
            for (size_t i = 0; i + 1 < t->catches.size(); ) {
                auto& c1 = t->catches[i];
                auto& c2 = t->catches[i + 1];
                bool same = false;
                if (c1.body.empty() && c2.body.empty()) {
                    same = true;
                } else {
                    auto b1_lines = emit_stmts(c1.body, 0);
                    auto b2_lines = emit_stmts(c2.body, 0);
                    if (c1.var_name == c2.var_name) {
                        same = (b1_lines == b2_lines);
                    } else if (!c1.var_name.empty() && !c2.var_name.empty()) {
                        auto replace_ident = [](std::string str, const std::string& from, const std::string& to) {
                            size_t pos = 0;
                            while ((pos = str.find(from, pos)) != std::string::npos) {
                                bool left_ok = (pos == 0 || (!std::isalnum(static_cast<unsigned char>(str[pos - 1])) && str[pos - 1] != '_'));
                                size_t end_pos = pos + from.size();
                                bool right_ok = (end_pos == str.size() || (!std::isalnum(static_cast<unsigned char>(str[end_pos])) && str[end_pos] != '_'));
                                if (left_ok && right_ok) {
                                    str.replace(pos, from.size(), to);
                                    pos += to.size();
                                } else {
                                    pos += from.size();
                                }
                            }
                            return str;
                        };
                        std::vector<std::string> b2_norm;
                        for (const auto& line : b2_lines) {
                            b2_norm.push_back(replace_ident(line, c2.var_name, c1.var_name));
                        }
                        same = (b1_lines == b2_norm);
                    }
                }
                if (same) {
                    c1.type = c1.type + "|" + c2.type;
                    t->catches.erase(t->catches.begin() + i + 1);
                } else {
                    ++i;
                }
            }
        } else if (s->kind == StmtKind::IfStmt) {
            auto* i = static_cast<IfStmt*>(s.get());
            i->then_body = fold_try_catches(i->then_body);
            if (i->else_body.has_value()) i->else_body = fold_try_catches(*i->else_body);
        } else if (s->kind == StmtKind::WhileStmt) {
            auto* w = static_cast<WhileStmt*>(s.get());
            w->body = fold_try_catches(w->body);
        } else if (s->kind == StmtKind::DoWhileStmt) {
            auto* d = static_cast<DoWhileStmt*>(s.get());
            d->body = fold_try_catches(d->body);
        } else if (s->kind == StmtKind::ForStmt) {
            auto* f = static_cast<ForStmt*>(s.get());
            f->body = fold_try_catches(f->body);
        } else if (s->kind == StmtKind::BlockStmt) {
            auto* b = static_cast<BlockStmt*>(s.get());
            b->stmts = fold_try_catches(b->stmts);
        }
    }
    return stmts;
}

std::vector<StmtPtr> prune_opaque_branches(std::vector<StmtPtr> stmts) {
    std::vector<StmtPtr> out;
    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == StmtKind::IfStmt) {
            auto* i = static_cast<IfStmt*>(s.get());
            if (i->cond) i->cond = simplify_expr(i->cond);
            i->then_body = prune_opaque_branches(i->then_body);
            if (i->else_body.has_value()) i->else_body = prune_opaque_branches(*i->else_body);
            if (i->cond && i->cond->kind == ExprKind::Const) {
                auto* c = static_cast<Const*>(i->cond.get());
                if (c->literal == "true") {
                    for (auto& bs : i->then_body) out.push_back(bs);
                    continue;
                }
                if (c->literal == "false") {
                    if (i->else_body.has_value()) {
                        for (auto& bs : *i->else_body) out.push_back(bs);
                    }
                    continue;
                }
            }
            if (i->else_body.has_value() && is_same_stmts(i->then_body, *i->else_body)) {
                for (auto& bs : i->then_body) out.push_back(bs);
                continue;
            }
            if (i->then_body.empty() && i->else_body.has_value() && !i->else_body->empty()) {
                i->cond = invert_condition(i->cond);
                i->then_body = *i->else_body;
                i->else_body = std::nullopt;
            } else if (i->else_body.has_value() && i->else_body->empty()) {
                i->else_body = std::nullopt;
            }
            if (i->then_body.empty() && !i->else_body.has_value()) {
                continue;
            }
            out.push_back(s);
        } else if (s->kind == StmtKind::WhileStmt) {
            auto* w = static_cast<WhileStmt*>(s.get());
            if (w->cond) w->cond = simplify_expr(w->cond);
            if (w->cond && w->cond->kind == ExprKind::Const && static_cast<Const*>(w->cond.get())->literal == "false") {
                continue;
            }
            w->body = prune_opaque_branches(w->body);
            out.push_back(s);
        } else if (s->kind == StmtKind::DoWhileStmt) {
            auto* dw = static_cast<DoWhileStmt*>(s.get());
            dw->body = prune_opaque_branches(dw->body);
            out.push_back(s);
        } else if (s->kind == StmtKind::ForStmt) {
            auto* f = static_cast<ForStmt*>(s.get());
            f->body = prune_opaque_branches(f->body);
            out.push_back(s);
        } else if (s->kind == StmtKind::BlockStmt) {
            auto* b = static_cast<BlockStmt*>(s.get());
            b->stmts = prune_opaque_branches(b->stmts);
            out.push_back(s);
        } else if (s->kind == StmtKind::TryStmt) {
            auto* t = static_cast<TryStmt*>(s.get());
            t->body = prune_opaque_branches(t->body);
            for (auto& c : t->catches) c.body = prune_opaque_branches(c.body);
            if (t->finally_body.has_value()) t->finally_body = prune_opaque_branches(*t->finally_body);
            out.push_back(s);
        } else if (s->kind == StmtKind::SwitchStmt) {
            auto* sw = static_cast<SwitchStmt*>(s.get());
            for (auto& c : sw->cases) c.body = prune_opaque_branches(c.body);
            out.push_back(s);
        } else if (s->kind == StmtKind::SyncStmt) {
            auto* sy = static_cast<SyncStmt*>(s.get());
            sy->body = prune_opaque_branches(sy->body);
            out.push_back(s);
        } else {
            out.push_back(s);
        }
    }
    return out;
}

std::vector<StmtPtr> restore_assertions_pass(std::vector<StmtPtr> stmts) {
    std::vector<StmtPtr> out;
    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == StmtKind::IfStmt) {
            auto* i = static_cast<IfStmt*>(s.get());
            i->then_body = restore_assertions_pass(i->then_body);
            if (i->else_body.has_value()) i->else_body = restore_assertions_pass(*i->else_body);
            
            // Check if then_body throws AssertionError
            if (i->then_body.size() == 1 && i->then_body[0]->kind == StmtKind::ThrowStmt) {
                auto* ts = static_cast<ThrowStmt*>(i->then_body[0].get());
                if (ts->expr && ts->expr->kind == ExprKind::NewObject) {
                    auto* no = static_cast<NewObject*>(ts->expr.get());
                    if (no->type.find("AssertionError") != std::string::npos) {
                        ExprPtr cond = i->cond;
                        ExprPtr actual_cond = nullptr;
                        ExprPtr detail_expr = no->args.empty() ? nullptr : no->args[0];
                        
                        if (cond && cond->kind == ExprKind::BinOp) {
                            auto* b = static_cast<BinOp*>(cond.get());
                            if (b->op == "&&") {
                                auto is_assert_disabled = [](const ExprPtr& e) {
                                    if (!e) return false;
                                    std::string str = emit_expr(e);
                                    return str.find("assertionsDisabled") != std::string::npos;
                                };
                                if (is_assert_disabled(b->left)) {
                                    actual_cond = negate(b->right);
                                } else if (is_assert_disabled(b->right)) {
                                    actual_cond = negate(b->left);
                                }
                            }
                        } else if (cond) {
                            std::string str = emit_expr(cond);
                            if (str.find("assertionsDisabled") != std::string::npos) {
                                actual_cond = std::make_shared<Const>("false", "boolean");
                            }
                        }
                        
                        if (actual_cond) {
                            std::string assert_code = "assert " + emit_expr(actual_cond);
                            if (detail_expr) {
                                assert_code += " : " + emit_expr(detail_expr);
                            }
                            assert_code += ";";
                            out.push_back(std::make_shared<RawStmt>(assert_code));
                            continue;
                        }
                    }
                }
            }
            out.push_back(s);
        } else if (s->kind == StmtKind::WhileStmt) {
            auto* w = static_cast<WhileStmt*>(s.get());
            w->body = restore_assertions_pass(w->body);
            out.push_back(s);
        } else if (s->kind == StmtKind::BlockStmt) {
            auto* b = static_cast<BlockStmt*>(s.get());
            b->stmts = restore_assertions_pass(b->stmts);
            out.push_back(s);
        } else {
            out.push_back(s);
        }
    }
    return out;
}

std::vector<StmtPtr> unflatten_switch_dispatchers(std::vector<StmtPtr> stmts) {
    if (stmts.size() < 2) return stmts;
    std::vector<StmtPtr> out;
    size_t i = 0;
    while (i < stmts.size()) {
        if (!stmts[i]) {
            i += 1;
            continue;
        }
        if (i + 1 < stmts.size() && stmts[i] && stmts[i + 1] && stmts[i]->kind == StmtKind::LocalDecl && stmts[i + 1]->kind == StmtKind::WhileStmt) {
            auto* ld = static_cast<LocalDecl*>(stmts[i].get());
            auto* w = static_cast<WhileStmt*>(stmts[i + 1].get());
            if (ld->init && ld->init->kind == ExprKind::Const && w->body.size() == 1 && w->body[0] && w->body[0]->kind == StmtKind::SwitchStmt) {
                std::string var_name = ld->name;
                std::string curr_state = static_cast<Const*>(ld->init.get())->literal;
                auto* sw = static_cast<SwitchStmt*>(w->body[0].get());
                if (sw->selector && sw->selector->kind == ExprKind::Local && static_cast<Local*>(sw->selector.get())->name == var_name) {
                    std::map<std::string, const SwitchCase*> case_map;
                    for (const auto& c : sw->cases) {
                        for (const auto& v : c.values) {
                            case_map[v] = &c;
                        }
                    }
                    std::vector<StmtPtr> unflattened;
                    std::set<std::string> visited;
                    bool success = false;
                    while (!curr_state.empty()) {
                        if (visited.count(curr_state)) break;
                        visited.insert(curr_state);
                        auto it = case_map.find(curr_state);
                        if (it == case_map.end()) {
                            success = true;
                            break;
                        }
                        const SwitchCase* sc = it->second;
                        std::vector<StmtPtr> c_body = sc->body;
                        while (!c_body.empty() && c_body.back()->kind == StmtKind::BreakStmt) {
                            c_body.pop_back();
                        }
                        if (c_body.empty()) break;
                        if (c_body.back()->kind == StmtKind::ReturnStmt) {
                            for (auto& s_in_c : c_body) unflattened.push_back(s_in_c);
                            success = true;
                            break;
                        }
                        std::string next_state = "";
                        if (c_body.back()->kind == StmtKind::ExprStmt) {
                            auto* es = static_cast<ExprStmtNode*>(c_body.back().get());
                            if (es->expr && es->expr->kind == ExprKind::Assign) {
                                auto* as = static_cast<Assign*>(es->expr.get());
                                if (as->target && as->target->kind == ExprKind::Local &&
                                    static_cast<Local*>(as->target.get())->name == var_name &&
                                    as->value && as->value->kind == ExprKind::Const) {
                                    next_state = static_cast<Const*>(as->value.get())->literal;
                                    c_body.pop_back();
                                }
                            }
                        }
                        if (next_state.empty()) break;
                        for (auto& s_in_c : c_body) unflattened.push_back(s_in_c);
                        curr_state = next_state;
                    }
                    if (success && !unflattened.empty()) {
                        for (auto& us : unflattened) out.push_back(us);
                        i += 2;
                        continue;
                    }
                }
            }
        }
        out.push_back(stmts[i]);
        i += 1;
    }
    return out;
}

void collect_used_labels(const std::vector<StmtPtr>& stmts, std::set<std::string>& used) {
    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == StmtKind::GotoStmt) {
            used.insert(static_cast<GotoStmt*>(s.get())->label);
        } else if (s->kind == StmtKind::BreakStmt) {
            auto* b = static_cast<BreakStmt*>(s.get());
            if (b->label.has_value()) used.insert(*b->label);
        } else if (s->kind == StmtKind::ContinueStmt) {
            auto* c = static_cast<ContinueStmt*>(s.get());
            if (c->label.has_value()) used.insert(*c->label);
        } else if (s->kind == StmtKind::IfStmt) {
            auto* i = static_cast<IfStmt*>(s.get());
            collect_used_labels(i->then_body, used);
            if (i->else_body.has_value()) collect_used_labels(*i->else_body, used);
        } else if (s->kind == StmtKind::WhileStmt) {
            collect_used_labels(static_cast<WhileStmt*>(s.get())->body, used);
        } else if (s->kind == StmtKind::DoWhileStmt) {
            collect_used_labels(static_cast<DoWhileStmt*>(s.get())->body, used);
        } else if (s->kind == StmtKind::ForStmt) {
            collect_used_labels(static_cast<ForStmt*>(s.get())->body, used);
        } else if (s->kind == StmtKind::BlockStmt) {
            collect_used_labels(static_cast<BlockStmt*>(s.get())->stmts, used);
        } else if (s->kind == StmtKind::SwitchStmt) {
            for (auto& c : static_cast<SwitchStmt*>(s.get())->cases) collect_used_labels(c.body, used);
        } else if (s->kind == StmtKind::TryStmt) {
            auto* t = static_cast<TryStmt*>(s.get());
            collect_used_labels(t->body, used);
            for (auto& c : t->catches) collect_used_labels(c.body, used);
            if (t->finally_body.has_value()) collect_used_labels(*t->finally_body, used);
        } else if (s->kind == StmtKind::SyncStmt) {
            collect_used_labels(static_cast<SyncStmt*>(s.get())->body, used);
        }
    }
}

std::vector<StmtPtr> eliminate_dead_code_and_labels(std::vector<StmtPtr> stmts, const std::set<std::string>& used_labels) {
    std::vector<StmtPtr> out;
    bool terminal_reached = false;

    for (size_t i = 0; i < stmts.size(); ++i) {
        auto& s = stmts[i];
        if (!s) continue;

        if (s->kind == StmtKind::LabelStmt) {
            auto* ls = static_cast<LabelStmt*>(s.get());
            if (used_labels.count(ls->label)) {
                terminal_reached = false;
                out.push_back(s);
            }
            continue;
        }

        if (terminal_reached) {
            continue;
        }

        if (s->kind == StmtKind::BlockStmt) {
            auto* b = static_cast<BlockStmt*>(s.get());
            if (!b->label.has_value()) {
                auto flattened = eliminate_dead_code_and_labels(b->stmts, used_labels);
                for (auto& fs : flattened) {
                    if (terminal_reached) break;
                    out.push_back(fs);
                    if (fs->kind == StmtKind::ReturnStmt || fs->kind == StmtKind::ThrowStmt ||
                        fs->kind == StmtKind::BreakStmt || fs->kind == StmtKind::ContinueStmt) {
                        terminal_reached = true;
                    }
                }
                continue;
            }
        }

        if (s->kind == StmtKind::IfStmt) {
            auto* if_s = static_cast<IfStmt*>(s.get());
            if_s->then_body = eliminate_dead_code_and_labels(if_s->then_body, used_labels);
            if (if_s->else_body.has_value()) {
                if_s->else_body = eliminate_dead_code_and_labels(*if_s->else_body, used_labels);
                if (if_s->else_body->empty()) if_s->else_body = std::nullopt;
            }
        } else if (s->kind == StmtKind::WhileStmt) {
            auto* w = static_cast<WhileStmt*>(s.get());
            w->body = eliminate_dead_code_and_labels(w->body, used_labels);
        } else if (s->kind == StmtKind::DoWhileStmt) {
            auto* dw = static_cast<DoWhileStmt*>(s.get());
            dw->body = eliminate_dead_code_and_labels(dw->body, used_labels);
        } else if (s->kind == StmtKind::ForStmt) {
            auto* f = static_cast<ForStmt*>(s.get());
            f->body = eliminate_dead_code_and_labels(f->body, used_labels);
        } else if (s->kind == StmtKind::TryStmt) {
            auto* t = static_cast<TryStmt*>(s.get());
            t->body = eliminate_dead_code_and_labels(t->body, used_labels);
            for (auto& c : t->catches) c.body = eliminate_dead_code_and_labels(c.body, used_labels);
            if (t->finally_body.has_value()) {
                t->finally_body = eliminate_dead_code_and_labels(*t->finally_body, used_labels);
            }
        } else if (s->kind == StmtKind::SyncStmt) {
            auto* sy = static_cast<SyncStmt*>(s.get());
            sy->body = eliminate_dead_code_and_labels(sy->body, used_labels);
        } else if (s->kind == StmtKind::SwitchStmt) {
            auto* sw = static_cast<SwitchStmt*>(s.get());
            for (auto& sc : sw->cases) sc.body = eliminate_dead_code_and_labels(sc.body, used_labels);
        }

        out.push_back(s);

        if (is_unconditional_exit(s)) {
            terminal_reached = true;
        }
    }
    return out;
}

std::vector<StmtPtr> desugar_assignment_ternaries(std::vector<StmtPtr> stmts) {
    std::vector<StmtPtr> out;
    size_t n = stmts.size();
    for (size_t i = 0; i < n; ++i) {
        auto& s = stmts[i];
        if (!s) continue;
        if (s->kind == StmtKind::IfStmt) {
            auto* if_s = static_cast<IfStmt*>(s.get());
            if_s->then_body = desugar_assignment_ternaries(if_s->then_body);
            if (if_s->else_body.has_value()) {
                if_s->else_body = desugar_assignment_ternaries(*if_s->else_body);
            }
            if (if_s->then_body.size() == 1 && if_s->then_body[0] && if_s->then_body[0]->kind == StmtKind::ExprStmt &&
                if_s->else_body.has_value() && if_s->else_body->size() == 1 && (*if_s->else_body)[0] && (*if_s->else_body)[0]->kind == StmtKind::ExprStmt) {
                auto* e1 = static_cast<ExprStmtNode*>(if_s->then_body[0].get());
                auto* e2 = static_cast<ExprStmtNode*>((*if_s->else_body)[0].get());
                if (e1->expr && e1->expr->kind == ExprKind::Assign && e2->expr && e2->expr->kind == ExprKind::Assign) {
                    auto* a1 = static_cast<Assign*>(e1->expr.get());
                    auto* a2 = static_cast<Assign*>(e2->expr.get());
                    if (a1->op == "=" && a2->op == "=" && a1->target && a2->target) {
                        std::string t1 = emit_expr(a1->target);
                        std::string t2 = emit_expr(a2->target);
                        if (!t1.empty() && t1 == t2) {
                            std::string res_type = !a1->value->type.empty() ? a1->value->type : a2->value->type;
                            auto tern = std::make_shared<Ternary>(if_s->cond, a1->value, a2->value, res_type);
                            out.push_back(std::make_shared<ExprStmtNode>(std::make_shared<Assign>(a1->target, simplify_expr(tern), "=")));
                            continue;
                        }
                    }
                }
            }
        }
        out.push_back(s);
    }
    return out;
}

std::vector<StmtPtr> desugar_return_ternaries(std::vector<StmtPtr> stmts) {
    std::vector<StmtPtr> out;
    size_t n = stmts.size();
    for (size_t i = 0; i < n; ++i) {
        auto& s = stmts[i];
        if (!s) continue;
        if (s->kind == StmtKind::IfStmt) {
            auto* if_s = static_cast<IfStmt*>(s.get());
            if_s->then_body = desugar_return_ternaries(if_s->then_body);
            if (if_s->else_body.has_value()) {
                if_s->else_body = desugar_return_ternaries(*if_s->else_body);
            }
            // Pattern 1: if (cond) return a; else return b;
            if (if_s->then_body.size() == 1 && if_s->then_body[0] && if_s->then_body[0]->kind == StmtKind::ReturnStmt &&
                if_s->else_body.has_value() && if_s->else_body->size() == 1 && (*if_s->else_body)[0] && (*if_s->else_body)[0]->kind == StmtKind::ReturnStmt) {
                auto* r1 = static_cast<ReturnStmt*>(if_s->then_body[0].get());
                auto* r2 = static_cast<ReturnStmt*>((*if_s->else_body)[0].get());
                if (r1->expr && r2->expr) {
                    std::string res_type = !r1->expr->type.empty() ? r1->expr->type : r2->expr->type;
                    auto tern = std::make_shared<Ternary>(if_s->cond, r1->expr, r2->expr, res_type);
                    out.push_back(std::make_shared<ReturnStmt>(simplify_expr(tern)));
                    continue;
                }
            }
            // Pattern 2: if (cond) return a; [followed by] return b;
            if (if_s->then_body.size() == 1 && if_s->then_body[0] && if_s->then_body[0]->kind == StmtKind::ReturnStmt &&
                (!if_s->else_body.has_value() || if_s->else_body->empty()) &&
                i + 1 < n && stmts[i + 1] && stmts[i + 1]->kind == StmtKind::ReturnStmt) {
                auto* r1 = static_cast<ReturnStmt*>(if_s->then_body[0].get());
                auto* r2 = static_cast<ReturnStmt*>(stmts[i + 1].get());
                if (r1->expr && r2->expr) {
                    std::string res_type = !r1->expr->type.empty() ? r1->expr->type : r2->expr->type;
                    auto tern = std::make_shared<Ternary>(if_s->cond, r1->expr, r2->expr, res_type);
                    out.push_back(std::make_shared<ReturnStmt>(simplify_expr(tern)));
                    ++i;
                    continue;
                }
            }
        }
        out.push_back(s);
    }
    return out;
}

std::vector<StmtPtr> propagate_single_use_flags(std::vector<StmtPtr> stmts) {
    size_t n = stmts.size();
    for (size_t i = 0; i + 1 < n; ++i) {
        if (!stmts[i] || !stmts[i + 1]) continue;
        std::string var_name;
        ExprPtr cond_expr = nullptr;
        if (stmts[i]->kind == StmtKind::LocalDecl) {
            auto* ld = static_cast<LocalDecl*>(stmts[i].get());
            if (ld->type == "boolean" && ld->init && (is_synth_temp(ld->name) || ld->name.rfind("flag", 0) == 0 || ld->name.rfind("b", 0) == 0)) {
                var_name = ld->name;
                cond_expr = ld->init;
            }
        } else if (stmts[i]->kind == StmtKind::ExprStmt) {
            auto* es = static_cast<ExprStmtNode*>(stmts[i].get());
            if (es->expr && es->expr->kind == ExprKind::Assign) {
                auto* as = static_cast<Assign*>(es->expr.get());
                if (as->target && as->target->kind == ExprKind::Local && as->target->type == "boolean") {
                    auto* loc = static_cast<Local*>(as->target.get());
                    if (is_synth_temp(loc->name) || loc->name.rfind("flag", 0) == 0) {
                        var_name = loc->name;
                        cond_expr = as->value;
                    }
                }
            }
        }
        if (var_name.empty() || !cond_expr) continue;

        bool can_propagate = false;
        if (stmts[i + 1]->kind == StmtKind::IfStmt) {
            auto* if_s = static_cast<IfStmt*>(stmts[i + 1].get());
            if (if_s->cond && if_s->cond->kind == ExprKind::Local && static_cast<Local*>(if_s->cond.get())->name == var_name) {
                can_propagate = true;
            }
        } else if (stmts[i + 1]->kind == StmtKind::ReturnStmt) {
            auto* ret_s = static_cast<ReturnStmt*>(stmts[i + 1].get());
            if (ret_s->expr && ret_s->expr->kind == ExprKind::Local && static_cast<Local*>(ret_s->expr.get())->name == var_name) {
                can_propagate = true;
            }
        }

        if (can_propagate) {
            int use_count = 0;
            size_t max_scan = (n > 64) ? std::min(n, i + 64) : n;
            for (size_t j = i + 1; j < max_scan; ++j) {
                if (contains_local_ref_stmt(stmts[j], var_name)) {
                    ++use_count;
                    if (use_count > 1) break;
                }
            }
            if (use_count == 1) {
                if (stmts[i + 1]->kind == StmtKind::IfStmt) {
                    static_cast<IfStmt*>(stmts[i + 1].get())->cond = cond_expr;
                } else if (stmts[i + 1]->kind == StmtKind::ReturnStmt) {
                    static_cast<ReturnStmt*>(stmts[i + 1].get())->expr = cond_expr;
                }
                stmts.erase(stmts.begin() + i);
                return propagate_single_use_flags(stmts);
            }
        }
    }
    return stmts;
}

std::vector<StmtPtr> group_multi_catch_blocks(std::vector<StmtPtr> stmts) {
    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == StmtKind::TryStmt) {
            auto* t = static_cast<TryStmt*>(s.get());
            t->body = group_multi_catch_blocks(t->body);
            if (t->catches.size() > 1) {
                std::vector<CatchClause> new_catches;
                for (size_t ci = 0; ci < t->catches.size(); ++ci) {
                    t->catches[ci].body = group_multi_catch_blocks(t->catches[ci].body);
                    bool merged = false;
                    for (auto& existing : new_catches) {
                        if (existing.var_name == t->catches[ci].var_name && existing.body.size() == t->catches[ci].body.size()) {
                            bool all_match = true;
                            for (size_t bi = 0; bi < existing.body.size(); ++bi) {
                                if (emit_stmt(existing.body[bi], 0) != emit_stmt(t->catches[ci].body[bi], 0)) {
                                    all_match = false;
                                    break;
                                }
                            }
                            if (all_match) {
                                existing.type = existing.type + " | " + t->catches[ci].type;
                                merged = true;
                                break;
                            }
                        }
                    }
                    if (!merged) {
                        new_catches.push_back(t->catches[ci]);
                    }
                }
                t->catches = std::move(new_catches);
            }
            if (t->finally_body.has_value()) {
                t->finally_body = group_multi_catch_blocks(*t->finally_body);
            }
        } else if (s->kind == StmtKind::IfStmt) {
            auto* i = static_cast<IfStmt*>(s.get());
            i->then_body = group_multi_catch_blocks(i->then_body);
            if (i->else_body.has_value()) i->else_body = group_multi_catch_blocks(*i->else_body);
        } else if (s->kind == StmtKind::WhileStmt) {
            static_cast<WhileStmt*>(s.get())->body = group_multi_catch_blocks(static_cast<WhileStmt*>(s.get())->body);
        } else if (s->kind == StmtKind::ForStmt) {
            static_cast<ForStmt*>(s.get())->body = group_multi_catch_blocks(static_cast<ForStmt*>(s.get())->body);
        }
    }
    return stmts;
}

std::vector<StmtPtr> desugar_enhanced_for_loops(std::vector<StmtPtr> stmts) {
    // 1. Iterator while loop desugaring:
    // Iterator it = container.iterator(); while (it.hasNext()) { Elem elem = (Elem) it.next(); ... }
    std::vector<StmtPtr> new_stmts;
    size_t s_len = stmts.size();
    size_t s_i = 0;
    while (s_i < s_len) {
        if (s_i + 1 < s_len && stmts[s_i] && stmts[s_i + 1] &&
            stmts[s_i]->kind == StmtKind::LocalDecl && stmts[s_i + 1]->kind == StmtKind::WhileStmt) {
            auto* ld = static_cast<LocalDecl*>(stmts[s_i].get());
            auto* w = static_cast<WhileStmt*>(stmts[s_i + 1].get());
            if (ld->init && ld->init->kind == ExprKind::MethodCall) {
                auto* mc = static_cast<MethodCall*>(ld->init.get());
                if (mc->name == "iterator" && mc->args.empty() && mc->target && w->cond && w->cond->kind == ExprKind::MethodCall) {
                    auto* hmc = static_cast<MethodCall*>(w->cond.get());
                    if (hmc->name == "hasNext" && hmc->args.empty() && hmc->target && hmc->target->kind == ExprKind::Local &&
                        static_cast<Local*>(hmc->target.get())->name == ld->name && !w->body.empty()) {
                        // Check first statement in body: Type elem = (Type) it.next();
                        StmtPtr first_b = w->body[0];
                        std::string elem_type, elem_name;
                        ExprPtr elem_next_target = nullptr;
                        if (first_b->kind == StmtKind::LocalDecl) {
                            auto* b_ld = static_cast<LocalDecl*>(first_b.get());
                            ExprPtr init_e = b_ld->init;
                            if (init_e && init_e->kind == ExprKind::Cast) {
                                init_e = static_cast<Cast*>(init_e.get())->expr;
                            }
                            if (init_e && init_e->kind == ExprKind::MethodCall) {
                                auto* nmc = static_cast<MethodCall*>(init_e.get());
                                if (nmc->name == "next" && nmc->args.empty() && nmc->target && nmc->target->kind == ExprKind::Local &&
                                    static_cast<Local*>(nmc->target.get())->name == ld->name) {
                                    elem_type = b_ld->type;
                                    elem_name = b_ld->name;
                                }
                            }
                        }
                        if (!elem_name.empty()) {
                            // Verify iterator is never referenced in rest of body
                            bool it_used_again = false;
                            for (size_t b_idx = 1; b_idx < w->body.size(); ++b_idx) {
                                if (contains_local_ref_stmt(w->body[b_idx], ld->name)) {
                                    it_used_again = true;
                                    break;
                                }
                            }
                            if (!it_used_again) {
                                std::string cont_str = emit_expr(mc->target);
                                std::vector<StmtPtr> rem_body(w->body.begin() + 1, w->body.end());
                                auto f_stmt = std::make_shared<ForStmt>(
                                    std::make_shared<Raw>(mark_type(elem_type) + " " + elem_name + " : " + cont_str),
                                    nullptr, nullptr, rem_body, w->label);
                                new_stmts.push_back(f_stmt);
                                s_i += 2;
                                continue;
                            }
                        }
                    }
                }
            }
        }
        new_stmts.push_back(stmts[s_i]);
        s_i++;
    }
    stmts = new_stmts;

    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == StmtKind::ForStmt) {
            auto* f = static_cast<ForStmt*>(s.get());
            f->body = desugar_enhanced_for_loops(f->body);
            if (f->init && f->cond && f->update && !f->body.empty()) {
                std::string idx_name;
                std::string init_str = emit_expr(f->init);
                if (init_str.rfind("int ", 0) == 0 && init_str.find(" = 0") != std::string::npos) {
                    size_t eq_pos = init_str.find(" = 0");
                    idx_name = init_str.substr(4, eq_pos - 4);
                    while (!idx_name.empty() && idx_name.back() == ' ') idx_name.pop_back();
                }
                if (!idx_name.empty()) {
                    if (f->body[0] && f->body[0]->kind == StmtKind::LocalDecl) {
                        auto* ld = static_cast<LocalDecl*>(f->body[0].get());
                        ExprPtr source_container = nullptr;
                        if (ld->init && ld->init->kind == ExprKind::ArrayAccess) {
                            auto* aa = static_cast<ArrayAccess*>(ld->init.get());
                            if (aa->index && aa->index->kind == ExprKind::Local &&
                                static_cast<Local*>(aa->index.get())->name == idx_name) {
                                source_container = aa->array;
                            }
                        } else if (ld->init && ld->init->kind == ExprKind::MethodCall) {
                            auto* mc = static_cast<MethodCall*>(ld->init.get());
                            if (mc->name == "get" && mc->args.size() == 1 && mc->args[0] &&
                                mc->args[0]->kind == ExprKind::Local &&
                                static_cast<Local*>(mc->args[0].get())->name == idx_name && mc->target) {
                                source_container = mc->target;
                            }
                        }
                        if (source_container) {
                            bool idx_used_again = false;
                            for (size_t b_idx = 1; b_idx < f->body.size(); ++b_idx) {
                                if (contains_local_ref_stmt(f->body[b_idx], idx_name)) {
                                    idx_used_again = true;
                                    break;
                                }
                            }
                            if (!idx_used_again) {
                                std::string cont_str = emit_expr(source_container);
                                f->init = std::make_shared<Raw>(mark_type(ld->type) + " " + ld->name + " : " + cont_str);
                                f->cond = nullptr;
                                f->update = nullptr;
                                f->body.erase(f->body.begin());
                            }
                        }
                    }
                }
            }
        } else if (s->kind == StmtKind::IfStmt) {
            auto* i = static_cast<IfStmt*>(s.get());
            i->then_body = desugar_enhanced_for_loops(i->then_body);
            if (i->else_body.has_value()) i->else_body = desugar_enhanced_for_loops(*i->else_body);
        } else if (s->kind == StmtKind::WhileStmt) {
            static_cast<WhileStmt*>(s.get())->body = desugar_enhanced_for_loops(static_cast<WhileStmt*>(s.get())->body);
        }
    }
    return stmts;
}

std::vector<StmtPtr> prune_empty_blocks_pass(std::vector<StmtPtr> stmts) {
    std::vector<StmtPtr> out;
    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == StmtKind::BlockStmt) {
            auto* b = static_cast<BlockStmt*>(s.get());
            if (b->stmts.empty() && (!b->label.has_value() || b->label->empty())) {
                continue;
            }
            b->stmts = prune_empty_blocks_pass(b->stmts);
        } else if (s->kind == StmtKind::IfStmt) {
            auto* i = static_cast<IfStmt*>(s.get());
            i->then_body = prune_empty_blocks_pass(i->then_body);
            if (i->else_body.has_value()) {
                i->else_body = prune_empty_blocks_pass(*i->else_body);
                if (i->else_body->empty()) i->else_body = std::nullopt;
            }
            if (i->then_body.empty() && (!i->else_body.has_value() || i->else_body->empty())) {
                if (has_side_effect(i->cond)) {
                    out.push_back(std::make_shared<ExprStmtNode>(i->cond));
                }
                continue;
            }
        } else if (s->kind == StmtKind::WhileStmt) {
            auto* w = static_cast<WhileStmt*>(s.get());
            w->body = prune_empty_blocks_pass(w->body);
        } else if (s->kind == StmtKind::DoWhileStmt) {
            auto* dw = static_cast<DoWhileStmt*>(s.get());
            dw->body = prune_empty_blocks_pass(dw->body);
        } else if (s->kind == StmtKind::ForStmt) {
            auto* f = static_cast<ForStmt*>(s.get());
            f->body = prune_empty_blocks_pass(f->body);
        } else if (s->kind == StmtKind::TryStmt) {
            auto* t = static_cast<TryStmt*>(s.get());
            t->body = prune_empty_blocks_pass(t->body);
            for (auto& c : t->catches) c.body = prune_empty_blocks_pass(c.body);
            if (t->finally_body.has_value()) {
                t->finally_body = prune_empty_blocks_pass(*t->finally_body);
                if (t->finally_body->empty()) t->finally_body = std::nullopt;
            }
        } else if (s->kind == StmtKind::ExprStmt) {
            auto* es = static_cast<ExprStmtNode*>(s.get());
            if (es->expr && (es->expr->kind == ExprKind::Const || es->expr->kind == ExprKind::Local || es->expr->kind == ExprKind::This)) {
                continue;
            }
        }
        out.push_back(s);
    }
    return out;
}

static const std::map<std::string, std::string> kBukkitTypeNames = {
    {"Player", "player"},
    {"OfflinePlayer", "offlinePlayer"},
    {"Location", "location"},
    {"World", "world"},
    {"ItemStack", "itemStack"},
    {"ItemMeta", "meta"},
    {"Block", "block"},
    {"CommandSender", "sender"},
    {"Command", "command"},
    {"Event", "event"},
    {"Plugin", "plugin"},
    {"Inventory", "inventory"},
    {"InventoryView", "view"},
    {"UUID", "uuid"},
    {"File", "file"},
    {"Path", "path"},
    {"FileConfiguration", "config"},
    {"ConfigurationSection", "section"},
    {"NamespacedKey", "key"},
    {"Vector", "vector"},
    {"Sound", "sound"},
    {"Material", "material"},
    {"Particle", "particle"},
    {"PotionEffect", "effect"},
    {"Enchantment", "enchantment"},
    {"Entity", "entity"},
    {"LivingEntity", "livingEntity"},
    {"Damageable", "damageable"},
    {"InputStream", "input"},
    {"OutputStream", "output"},
    {"BufferedReader", "reader"},
    {"StringBuilder", "sb"},
    {"Exception", "e"},
    {"Throwable", "t"},
    {"Random", "random"},
    {"Class", "clazz"},
    {"List", "list"},
    {"ArrayList", "list"},
    {"LinkedList", "list"},
    {"Map", "map"},
    {"HashMap", "map"},
    {"LinkedHashMap", "map"},
    {"TreeMap", "map"},
    {"Set", "set"},
    {"HashSet", "set"},
    {"LinkedHashSet", "set"},
    {"TreeSet", "set"},
    {"JsonObject", "json"},
    {"JsonArray", "jsonArray"},
    {"JsonElement", "jsonElement"},
    {"RegionQuery", "query"},
    {"RegionAssociable", "associable"},
    {"RegionManager", "regionManager"},
    {"ProtectedRegion", "region"},
    {"ApplicableRegionSet", "regions"},
    {"StateFlag", "flag"},
    {"StateFlag[]", "flags"},
    {"Flag", "flag"},
    {"EntityType", "entityType"},
    {"CommandContext", "context"},
    {"CommandSourceStack", "source"},
    {"WorldConfiguration", "worldConfig"},
    {"RegionPermissionModel", "permissionModel"},
    {"SessionManager", "sessionManager"},
    {"Session", "session"}
};

void rename_ident_in_expr(ExprPtr& e, const std::string& old_name, const std::string& new_name) {
    if (!e) return;
    if (e->kind == ExprKind::Local) {
        auto* l = static_cast<Local*>(e.get());
        if (l->name == old_name) l->name = new_name;
    } else if (e->kind == ExprKind::BinOp) {
        auto* b = static_cast<BinOp*>(e.get());
        rename_ident_in_expr(b->left, old_name, new_name);
        rename_ident_in_expr(b->right, old_name, new_name);
    } else if (e->kind == ExprKind::UnOp) {
        auto* u = static_cast<UnOp*>(e.get());
        rename_ident_in_expr(u->expr, old_name, new_name);
    } else if (e->kind == ExprKind::Ternary) {
        auto* t = static_cast<Ternary*>(e.get());
        rename_ident_in_expr(t->cond, old_name, new_name);
        rename_ident_in_expr(t->tval, old_name, new_name);
        rename_ident_in_expr(t->fval, old_name, new_name);
    } else if (e->kind == ExprKind::Assign) {
        auto* a = static_cast<Assign*>(e.get());
        rename_ident_in_expr(a->target, old_name, new_name);
        rename_ident_in_expr(a->value, old_name, new_name);
    } else if (e->kind == ExprKind::MethodCall) {
        auto* m = static_cast<MethodCall*>(e.get());
        rename_ident_in_expr(m->target, old_name, new_name);
        for (auto& arg : m->args) rename_ident_in_expr(arg, old_name, new_name);
    } else if (e->kind == ExprKind::ArrayAccess) {
        auto* a = static_cast<ArrayAccess*>(e.get());
        rename_ident_in_expr(a->array, old_name, new_name);
        rename_ident_in_expr(a->index, old_name, new_name);
    } else if (e->kind == ExprKind::Cast) {
        auto* c = static_cast<Cast*>(e.get());
        rename_ident_in_expr(c->expr, old_name, new_name);
    } else if (e->kind == ExprKind::InstanceOf) {
        auto* io = static_cast<InstanceOf*>(e.get());
        rename_ident_in_expr(io->expr, old_name, new_name);
    } else if (e->kind == ExprKind::NewObject) {
        auto* no = static_cast<NewObject*>(e.get());
        for (auto& arg : no->args) rename_ident_in_expr(arg, old_name, new_name);
    } else if (e->kind == ExprKind::NewArray) {
        auto* na = static_cast<NewArray*>(e.get());
        for (auto& d : na->dims) rename_ident_in_expr(d, old_name, new_name);
        if (na->initializer.has_value()) {
            for (auto& item : *na->initializer) rename_ident_in_expr(item, old_name, new_name);
        }
    } else if (e->kind == ExprKind::FieldAccess) {
        auto* fa = static_cast<FieldAccess*>(e.get());
        rename_ident_in_expr(fa->target, old_name, new_name);
    } else if (e->kind == ExprKind::Lambda) {
        auto* l = static_cast<Lambda*>(e.get());
        for (auto& p : l->params) rename_ident_in_expr(p, old_name, new_name);
        rename_ident_in_expr(l->body_method_ref, old_name, new_name);
    }
}

void rename_ident_in_stmts(std::vector<StmtPtr>& stmts, const std::string& old_name, const std::string& new_name) {
    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == StmtKind::LocalDecl) {
            auto* ld = static_cast<LocalDecl*>(s.get());
            if (ld->name == old_name) ld->name = new_name;
            rename_ident_in_expr(ld->init, old_name, new_name);
        } else if (s->kind == StmtKind::ExprStmt) {
            auto* es = static_cast<ExprStmtNode*>(s.get());
            rename_ident_in_expr(es->expr, old_name, new_name);
        } else if (s->kind == StmtKind::ReturnStmt) {
            auto* r = static_cast<ReturnStmt*>(s.get());
            rename_ident_in_expr(r->expr, old_name, new_name);
        } else if (s->kind == StmtKind::ThrowStmt) {
            auto* t = static_cast<ThrowStmt*>(s.get());
            rename_ident_in_expr(t->expr, old_name, new_name);
        } else if (s->kind == StmtKind::IfStmt) {
            auto* i = static_cast<IfStmt*>(s.get());
            rename_ident_in_expr(i->cond, old_name, new_name);
            rename_ident_in_stmts(i->then_body, old_name, new_name);
            if (i->else_body.has_value()) rename_ident_in_stmts(*i->else_body, old_name, new_name);
        } else if (s->kind == StmtKind::WhileStmt) {
            auto* w = static_cast<WhileStmt*>(s.get());
            rename_ident_in_expr(w->cond, old_name, new_name);
            rename_ident_in_stmts(w->body, old_name, new_name);
        } else if (s->kind == StmtKind::DoWhileStmt) {
            auto* dw = static_cast<DoWhileStmt*>(s.get());
            rename_ident_in_expr(dw->cond, old_name, new_name);
            rename_ident_in_stmts(dw->body, old_name, new_name);
        } else if (s->kind == StmtKind::ForStmt) {
            auto* f = static_cast<ForStmt*>(s.get());
            rename_ident_in_expr(f->init, old_name, new_name);
            rename_ident_in_expr(f->cond, old_name, new_name);
            if (f->update && f->update->kind == StmtKind::ExprStmt) {
                rename_ident_in_expr(static_cast<ExprStmtNode*>(f->update.get())->expr, old_name, new_name);
            }
            rename_ident_in_stmts(f->body, old_name, new_name);
        } else if (s->kind == StmtKind::BlockStmt) {
            auto* b = static_cast<BlockStmt*>(s.get());
            rename_ident_in_stmts(b->stmts, old_name, new_name);
        } else if (s->kind == StmtKind::TryStmt) {
            auto* t = static_cast<TryStmt*>(s.get());
            rename_ident_in_stmts(t->body, old_name, new_name);
            for (auto& c : t->catches) rename_ident_in_stmts(c.body, old_name, new_name);
            if (t->finally_body.has_value()) rename_ident_in_stmts(*t->finally_body, old_name, new_name);
        } else if (s->kind == StmtKind::SwitchStmt) {
            auto* sw = static_cast<SwitchStmt*>(s.get());
            rename_ident_in_expr(sw->selector, old_name, new_name);
            for (auto& c : sw->cases) rename_ident_in_stmts(c.body, old_name, new_name);
        }
    }
}

std::vector<StmtPtr> smart_rename_locals(std::vector<StmtPtr> stmts) {
    std::set<std::string> all_names;
    std::vector<std::pair<std::string, std::string>> candidates;

    std::function<void(const std::vector<StmtPtr>&)> scan = [&](const std::vector<StmtPtr>& list) {
        for (auto& s : list) {
            if (!s) continue;
            if (s->kind == StmtKind::LocalDecl) {
                auto* ld = static_cast<LocalDecl*>(s.get());
                all_names.insert(ld->name);
                bool is_generic = (ld->name.rfind("var", 0) == 0 || ld->name.rfind("loc", 0) == 0 ||
                                   ld->name.rfind("obj", 0) == 0 || (ld->name.size() == 1 && std::islower(static_cast<unsigned char>(ld->name[0]))));
                if (!is_generic && ld->name.rfind("n", 0) == 0 && ld->name.size() > 1 && std::isdigit(static_cast<unsigned char>(ld->name[1]))) {
                    is_generic = true;
                }
                if (is_generic) {
                    candidates.push_back({ld->name, ld->type});
                }
            } else if (s->kind == StmtKind::ExprStmt) {
                auto* es = static_cast<ExprStmtNode*>(s.get());
                if (es->expr && es->expr->kind == ExprKind::Assign) {
                    auto* a = static_cast<Assign*>(es->expr.get());
                    if (a->target && a->target->kind == ExprKind::Local) {
                        auto* loc = static_cast<Local*>(a->target.get());
                        all_names.insert(loc->name);
                        bool is_generic = (loc->name.rfind("var", 0) == 0 || loc->name.rfind("loc", 0) == 0 ||
                                           loc->name.rfind("obj", 0) == 0 || (loc->name.size() == 1 && std::islower(static_cast<unsigned char>(loc->name[0]))));
                        if (is_generic) {
                            std::string typ = !loc->type.empty() ? loc->type : (a->value ? a->value->type : "");
                            if (a->value && (a->value->type == "boolean" || a->value->kind == ExprKind::BinOp)) {
                                typ = "boolean";
                                loc->type = "boolean";
                            }
                            // Check if boolean expression has hints (isFire, isLava, testBuild)
                            std::string hint_name;
                            if (a->value && (typ == "boolean" || loc->type == "boolean")) {
                                std::string vstr = emit_expr(a->value);
                                if (vstr.find("isFire") != std::string::npos) hint_name = "isFire";
                                else if (vstr.find("isLava") != std::string::npos) hint_name = "isLava";
                                else if (vstr.find("testBuild") != std::string::npos) hint_name = "allowed";
                            }
                            if (!hint_name.empty()) {
                                candidates.push_back({loc->name, "@HINT:" + hint_name});
                            } else {
                                candidates.push_back({loc->name, typ});
                            }
                        }
                    }
                }
            } else if (s->kind == StmtKind::IfStmt) {
                auto* i = static_cast<IfStmt*>(s.get());
                scan(i->then_body);
                if (i->else_body) scan(*i->else_body);
            } else if (s->kind == StmtKind::WhileStmt) {
                scan(static_cast<WhileStmt*>(s.get())->body);
            } else if (s->kind == StmtKind::DoWhileStmt) {
                scan(static_cast<DoWhileStmt*>(s.get())->body);
            } else if (s->kind == StmtKind::ForStmt) {
                auto* f = static_cast<ForStmt*>(s.get());
                if (f->init && f->init->kind == ExprKind::Assign) {
                    auto* a = static_cast<Assign*>(f->init.get());
                    if (a->target && a->target->kind == ExprKind::Local) {
                        auto* loc = static_cast<Local*>(a->target.get());
                        all_names.insert(loc->name);
                        if (loc->name.rfind("var", 0) == 0 || loc->name.rfind("n", 0) == 0) {
                            candidates.push_back({loc->name, "@HINT:i"});
                        }
                    }
                }
                scan(f->body);
            } else if (s->kind == StmtKind::BlockStmt) {
                scan(static_cast<BlockStmt*>(s.get())->stmts);
            } else if (s->kind == StmtKind::TryStmt) {
                auto* t = static_cast<TryStmt*>(s.get());
                scan(t->body);
                for (auto& c : t->catches) scan(c.body);
                if (t->finally_body) scan(*t->finally_body);
            }
        }
    };
    scan(stmts);

    std::map<std::string, int> base_counts;
    for (const auto& [old_n, typ] : candidates) {
        std::string simple_t = typ;
        size_t dot_pos = simple_t.find_last_of('.');
        if (dot_pos != std::string::npos) simple_t = simple_t.substr(dot_pos + 1);

        std::string base_n;
        if (typ.rfind("@HINT:", 0) == 0) {
            base_n = typ.substr(6);
        } else {
            auto it = kBukkitTypeNames.find(simple_t);
            if (it != kBukkitTypeNames.end()) {
                base_n = it->second;
            } else if (simple_t.size() > 5 && simple_t.rfind("Event") == simple_t.size() - 5) {
                base_n = "event";
            } else if (simple_t.size() > 8 && simple_t.rfind("Listener") == simple_t.size() - 8) {
                base_n = "listener";
            } else if (simple_t.size() > 9 && simple_t.rfind("Exception") == simple_t.size() - 9) {
                base_n = "e";
            } else if (simple_t.size() > 7 && simple_t.rfind("Builder") == simple_t.size() - 7) {
                base_n = "builder";
            } else if (simple_t.size() > 7 && simple_t.rfind("Manager") == simple_t.size() - 7) {
                base_n = "manager";
            } else if (simple_t.size() > 6 && simple_t.rfind("Config") == simple_t.size() - 6) {
                base_n = "config";
            } else if (simple_t.size() >= 2 && std::isupper(static_cast<unsigned char>(simple_t[0]))) {
                base_n = simple_t;
                base_n[0] = std::tolower(static_cast<unsigned char>(base_n[0]));
            } else if (typ == "char") {
                base_n = "c";
            } else if (typ == "boolean") {
                base_n = "flag";
            } else if (typ == "int" || typ == "short" || typ == "byte") {
                base_n = "n";
            } else if (typ == "long") {
                base_n = "l";
            } else if (typ == "String") {
                base_n = "s";
            }
        }
        if (!base_n.empty()) {
            int idx = ++base_counts[base_n];
            std::string suggested = (idx == 1 && !all_names.count(base_n)) ? base_n : (base_n + std::to_string(idx));
            while (all_names.count(suggested)) {
                idx = ++base_counts[base_n];
                suggested = base_n + std::to_string(idx);
            }
            all_names.insert(suggested);
            rename_ident_in_stmts(stmts, old_n, suggested);
        }
    }
    return stmts;
}

std::vector<StmtPtr> clean_synthetic_null_checks(std::vector<StmtPtr> stmts) {
    std::vector<StmtPtr> out;
    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == StmtKind::ExprStmt) {
            auto* es = static_cast<ExprStmtNode*>(s.get());
            if (es->expr && es->expr->kind == ExprKind::MethodCall) {
                auto* mc = static_cast<MethodCall*>(es->expr.get());
                // x.getClass() as standalone statement is javac's synthetic null check
                if (mc->name == "getClass" && mc->args.empty() && mc->target &&
                    (mc->target->kind == ExprKind::Local || mc->target->kind == ExprKind::FieldAccess)) {
                    continue; // drop redundant synthetic null-check line
                }
            }
        } else if (s->kind == StmtKind::IfStmt) {
            auto* i = static_cast<IfStmt*>(s.get());
            i->then_body = clean_synthetic_null_checks(i->then_body);
            if (i->else_body) i->else_body = clean_synthetic_null_checks(*i->else_body);
        } else if (s->kind == StmtKind::WhileStmt) {
            auto* w = static_cast<WhileStmt*>(s.get());
            w->body = clean_synthetic_null_checks(w->body);
        } else if (s->kind == StmtKind::DoWhileStmt) {
            auto* dw = static_cast<DoWhileStmt*>(s.get());
            dw->body = clean_synthetic_null_checks(dw->body);
        } else if (s->kind == StmtKind::ForStmt) {
            auto* f = static_cast<ForStmt*>(s.get());
            f->body = clean_synthetic_null_checks(f->body);
        } else if (s->kind == StmtKind::BlockStmt) {
            auto* b = static_cast<BlockStmt*>(s.get());
            b->stmts = clean_synthetic_null_checks(b->stmts);
        } else if (s->kind == StmtKind::TryStmt) {
            auto* t = static_cast<TryStmt*>(s.get());
            t->body = clean_synthetic_null_checks(t->body);
            for (auto& c : t->catches) c.body = clean_synthetic_null_checks(c.body);
            if (t->finally_body) t->finally_body = clean_synthetic_null_checks(*t->finally_body);
        }
        out.push_back(s);
    }
    return out;
}

std::vector<StmtPtr> fold_compound_assignments(std::vector<StmtPtr> stmts) {
    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == StmtKind::ExprStmt) {
            auto* es = static_cast<ExprStmtNode*>(s.get());
            if (es->expr && es->expr->kind == ExprKind::Assign) {
                auto* a = static_cast<Assign*>(es->expr.get());
                if (a->op == "=" && a->value && a->value->kind == ExprKind::BinOp) {
                    auto* bo = static_cast<BinOp*>(a->value.get());
                    static const std::unordered_set<std::string> kCompoundOps = {
                        "+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>", ">>>"
                    };
                    if (kCompoundOps.count(bo->op) && is_same_expr(a->target, bo->left)) {
                        a->op = bo->op + "=";
                        a->value = bo->right;
                    }
                }
            }
        } else if (s->kind == StmtKind::IfStmt) {
            auto* i = static_cast<IfStmt*>(s.get());
            i->then_body = fold_compound_assignments(i->then_body);
            if (i->else_body) i->else_body = fold_compound_assignments(*i->else_body);
        } else if (s->kind == StmtKind::WhileStmt) {
            auto* w = static_cast<WhileStmt*>(s.get());
            w->body = fold_compound_assignments(w->body);
        } else if (s->kind == StmtKind::DoWhileStmt) {
            auto* dw = static_cast<DoWhileStmt*>(s.get());
            dw->body = fold_compound_assignments(dw->body);
        } else if (s->kind == StmtKind::ForStmt) {
            auto* f = static_cast<ForStmt*>(s.get());
            f->body = fold_compound_assignments(f->body);
        } else if (s->kind == StmtKind::BlockStmt) {
            auto* b = static_cast<BlockStmt*>(s.get());
            b->stmts = fold_compound_assignments(b->stmts);
        } else if (s->kind == StmtKind::TryStmt) {
            auto* t = static_cast<TryStmt*>(s.get());
            t->body = fold_compound_assignments(t->body);
            for (auto& c : t->catches) c.body = fold_compound_assignments(c.body);
            if (t->finally_body) t->finally_body = fold_compound_assignments(*t->finally_body);
        }
    }
    return stmts;
}

std::vector<StmtPtr> desugar_instanceof_pattern_matching(std::vector<StmtPtr> stmts) {
    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == StmtKind::IfStmt) {
            auto* i = static_cast<IfStmt*>(s.get());
            if (i->cond && i->cond->kind == ExprKind::InstanceOf && !i->then_body.empty()) {
                auto* io = static_cast<InstanceOf*>(i->cond.get());
                StmtPtr first = i->then_body[0];
                std::optional<std::string> pat_name;
                if (first->kind == StmtKind::LocalDecl) {
                    auto* ld = static_cast<LocalDecl*>(first.get());
                    if (ld->init && ld->init->kind == ExprKind::Cast) {
                        auto* c = static_cast<Cast*>(ld->init.get());
                        if (is_same_expr(c->expr, io->expr)) {
                            pat_name = ld->name;
                        }
                    }
                } else if (first->kind == StmtKind::ExprStmt) {
                    auto* es = static_cast<ExprStmtNode*>(first.get());
                    if (es->expr && es->expr->kind == ExprKind::Assign) {
                        auto* a = static_cast<Assign*>(es->expr.get());
                        if (a->op == "=" && a->target && a->target->kind == ExprKind::Local &&
                            a->value && a->value->kind == ExprKind::Cast) {
                            auto* c = static_cast<Cast*>(a->value.get());
                            if (is_same_expr(c->expr, io->expr)) {
                                pat_name = static_cast<Local*>(a->target.get())->name;
                            }
                        }
                    }
                }
                if (pat_name.has_value() && !pat_name->empty()) {
                    io->pattern_var = pat_name;
                    i->then_body.erase(i->then_body.begin());
                }
            }
            i->then_body = desugar_instanceof_pattern_matching(i->then_body);
            if (i->else_body) i->else_body = desugar_instanceof_pattern_matching(*i->else_body);
        } else if (s->kind == StmtKind::WhileStmt) {
            auto* w = static_cast<WhileStmt*>(s.get());
            w->body = desugar_instanceof_pattern_matching(w->body);
        } else if (s->kind == StmtKind::DoWhileStmt) {
            auto* dw = static_cast<DoWhileStmt*>(s.get());
            dw->body = desugar_instanceof_pattern_matching(dw->body);
        } else if (s->kind == StmtKind::ForStmt) {
            auto* f = static_cast<ForStmt*>(s.get());
            f->body = desugar_instanceof_pattern_matching(f->body);
        } else if (s->kind == StmtKind::BlockStmt) {
            auto* b = static_cast<BlockStmt*>(s.get());
            b->stmts = desugar_instanceof_pattern_matching(b->stmts);
        } else if (s->kind == StmtKind::TryStmt) {
            auto* t = static_cast<TryStmt*>(s.get());
            t->body = desugar_instanceof_pattern_matching(t->body);
            for (auto& c : t->catches) c.body = desugar_instanceof_pattern_matching(c.body);
            if (t->finally_body) t->finally_body = desugar_instanceof_pattern_matching(*t->finally_body);
        }
    }
    return stmts;
}

std::vector<StmtPtr> desugar_objects_require_non_null(std::vector<StmtPtr> stmts) {
    std::vector<StmtPtr> out;
    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == StmtKind::IfStmt) {
            auto* i = static_cast<IfStmt*>(s.get());
            // if (x == null) throw new NullPointerException("msg"); -> Objects.requireNonNull(x, "msg");
            if (i->then_body.size() == 1 && (!i->else_body.has_value() || i->else_body->empty()) &&
                i->then_body[0]->kind == StmtKind::ThrowStmt && i->cond && i->cond->kind == ExprKind::BinOp) {
                auto* bo = static_cast<BinOp*>(i->cond.get());
                if (bo->op == "==") {
                    ExprPtr null_target = nullptr;
                    if (bo->right && bo->right->kind == ExprKind::Const && static_cast<Const*>(bo->right.get())->val == "null") {
                        null_target = bo->left;
                    } else if (bo->left && bo->left->kind == ExprKind::Const && static_cast<Const*>(bo->left.get())->val == "null") {
                        null_target = bo->right;
                    }
                    if (null_target && (null_target->kind == ExprKind::Local || null_target->kind == ExprKind::FieldAccess)) {
                        auto* ts = static_cast<ThrowStmt*>(i->then_body[0].get());
                        if (ts->expr && ts->expr->kind == ExprKind::NewObject) {
                            auto* no = static_cast<NewObject*>(ts->expr.get());
                            if (no->type.find("NullPointerException") != std::string::npos) {
                                std::vector<ExprPtr> req_args = {null_target};
                                if (!no->args.empty()) req_args.push_back(no->args[0]);
                                auto req_call = std::make_shared<MethodCall>(
                                    std::make_shared<Local>("Objects", "java.util.Objects"),
                                    "requireNonNull",
                                    req_args,
                                    "Object"
                                );
                                out.push_back(std::make_shared<ExprStmtNode>(req_call));
                                continue;
                            }
                        }
                    }
                }
            }
            i->then_body = desugar_objects_require_non_null(i->then_body);
            if (i->else_body) i->else_body = desugar_objects_require_non_null(*i->else_body);
        } else if (s->kind == StmtKind::WhileStmt) {
            auto* w = static_cast<WhileStmt*>(s.get());
            w->body = desugar_objects_require_non_null(w->body);
        } else if (s->kind == StmtKind::DoWhileStmt) {
            auto* dw = static_cast<DoWhileStmt*>(s.get());
            dw->body = desugar_objects_require_non_null(dw->body);
        } else if (s->kind == StmtKind::ForStmt) {
            auto* f = static_cast<ForStmt*>(s.get());
            f->body = desugar_objects_require_non_null(f->body);
        } else if (s->kind == StmtKind::BlockStmt) {
            auto* b = static_cast<BlockStmt*>(s.get());
            b->stmts = desugar_objects_require_non_null(b->stmts);
        } else if (s->kind == StmtKind::TryStmt) {
            auto* t = static_cast<TryStmt*>(s.get());
            t->body = desugar_objects_require_non_null(t->body);
            for (auto& c : t->catches) c.body = desugar_objects_require_non_null(c.body);
            if (t->finally_body) t->finally_body = desugar_objects_require_non_null(*t->finally_body);
        }
        out.push_back(s);
    }
    return out;
}

std::vector<StmtPtr> prune_dead_code_pass(std::vector<StmtPtr> stmts) {
    std::set<std::string> used_labels;
    collect_used_labels(stmts, used_labels);
    return eliminate_dead_code_and_labels(stmts, used_labels);
}

}  // namespace


// ---------------- substitute_local_in_expr / substitute_local_in_stmt ----------------
// Полноценная подстановка выражения вместо локальной переменной (устранение tempX стековых мусорных переменных)

void substitute_local_in_expr(ExprPtr& e, const std::string& name, const ExprPtr& replacement);
void substitute_local_in_stmt(StmtPtr& s, const std::string& name, const ExprPtr& replacement);

void substitute_local_in_expr(ExprPtr& e, const std::string& name, const ExprPtr& replacement) {
    if (!e || !replacement) return;
    if (e->kind == ExprKind::Local) {
        if (static_cast<Local*>(e.get())->name == name) {
            e = replacement;
        }
        return;
    }
    switch (e->kind) {
        case ExprKind::FieldAccess:
            substitute_local_in_expr(static_cast<FieldAccess*>(e.get())->target, name, replacement);
            break;
        case ExprKind::ArrayAccess: {
            auto* a = static_cast<ArrayAccess*>(e.get());
            substitute_local_in_expr(a->array, name, replacement);
            substitute_local_in_expr(a->index, name, replacement);
            break;
        }
        case ExprKind::MethodCall: {
            auto* m = static_cast<MethodCall*>(e.get());
            substitute_local_in_expr(m->target, name, replacement);
            for (auto& a : m->args) substitute_local_in_expr(a, name, replacement);
            break;
        }
        case ExprKind::NewObject:
            for (auto& a : static_cast<NewObject*>(e.get())->args) substitute_local_in_expr(a, name, replacement);
            break;
        case ExprKind::NewArray: {
            auto* na = static_cast<NewArray*>(e.get());
            for (auto& d : na->dims) substitute_local_in_expr(d, name, replacement);
            if (na->initializer.has_value()) {
                for (auto& item : *na->initializer) substitute_local_in_expr(item, name, replacement);
            }
            break;
        }
        case ExprKind::Cast:
            substitute_local_in_expr(static_cast<Cast*>(e.get())->expr, name, replacement);
            break;
        case ExprKind::InstanceOf:
            substitute_local_in_expr(static_cast<InstanceOf*>(e.get())->expr, name, replacement);
            break;
        case ExprKind::BinOp: {
            auto* b = static_cast<BinOp*>(e.get());
            substitute_local_in_expr(b->left, name, replacement);
            substitute_local_in_expr(b->right, name, replacement);
            break;
        }
        case ExprKind::UnOp:
            substitute_local_in_expr(static_cast<UnOp*>(e.get())->expr, name, replacement);
            break;
        case ExprKind::Ternary: {
            auto* t = static_cast<Ternary*>(e.get());
            substitute_local_in_expr(t->cond, name, replacement);
            substitute_local_in_expr(t->tval, name, replacement);
            substitute_local_in_expr(t->fval, name, replacement);
            break;
        }
        case ExprKind::Assign: {
            auto* as = static_cast<Assign*>(e.get());
            substitute_local_in_expr(as->target, name, replacement);
            substitute_local_in_expr(as->value, name, replacement);
            break;
        }
        case ExprKind::Lambda: {
            auto* lam = static_cast<Lambda*>(e.get());
            substitute_local_in_expr(lam->body_method_ref, name, replacement);
            break;
        }
        default:
            break;
    }
}

void substitute_local_in_stmt(StmtPtr& s, const std::string& name, const ExprPtr& replacement) {
    if (!s || !replacement) return;
    switch (s->kind) {
        case StmtKind::ExprStmt:
            substitute_local_in_expr(static_cast<ExprStmtNode*>(s.get())->expr, name, replacement);
            break;
        case StmtKind::LocalDecl:
            substitute_local_in_expr(static_cast<LocalDecl*>(s.get())->init, name, replacement);
            break;
        case StmtKind::ReturnStmt:
            substitute_local_in_expr(static_cast<ReturnStmt*>(s.get())->expr, name, replacement);
            break;
        case StmtKind::ThrowStmt:
            substitute_local_in_expr(static_cast<ThrowStmt*>(s.get())->expr, name, replacement);
            break;
        case StmtKind::IfStmt: {
            auto* i = static_cast<IfStmt*>(s.get());
            substitute_local_in_expr(i->cond, name, replacement);
            for (auto& sub : i->then_body) substitute_local_in_stmt(sub, name, replacement);
            if (i->else_body) {
                for (auto& sub : *i->else_body) substitute_local_in_stmt(sub, name, replacement);
            }
            break;
        }
        case StmtKind::WhileStmt: {
            auto* w = static_cast<WhileStmt*>(s.get());
            substitute_local_in_expr(w->cond, name, replacement);
            for (auto& sub : w->body) substitute_local_in_stmt(sub, name, replacement);
            break;
        }
        case StmtKind::DoWhileStmt: {
            auto* dw = static_cast<DoWhileStmt*>(s.get());
            substitute_local_in_expr(dw->cond, name, replacement);
            for (auto& sub : dw->body) substitute_local_in_stmt(sub, name, replacement);
            break;
        }
        case StmtKind::ForStmt: {
            auto* f = static_cast<ForStmt*>(s.get());
            if (f->init) substitute_local_in_expr(f->init, name, replacement);
            substitute_local_in_expr(f->cond, name, replacement);
            if (f->update) substitute_local_in_stmt(f->update, name, replacement);
            for (auto& sub : f->body) substitute_local_in_stmt(sub, name, replacement);
            break;
        }
        case StmtKind::SwitchStmt: {
            auto* sw = static_cast<SwitchStmt*>(s.get());
            substitute_local_in_expr(sw->selector, name, replacement);
            for (auto& c : sw->cases) {
                for (auto& sub : c.body) substitute_local_in_stmt(sub, name, replacement);
            }
            break;
        }
        case StmtKind::TryStmt: {
            auto* t = static_cast<TryStmt*>(s.get());
            for (auto& sub : t->body) substitute_local_in_stmt(sub, name, replacement);
            for (auto& c : t->catches) {
                for (auto& sub : c.body) substitute_local_in_stmt(sub, name, replacement);
            }
            if (t->finally_body) {
                for (auto& sub : *t->finally_body) substitute_local_in_stmt(sub, name, replacement);
            }
            break;
        }
        case StmtKind::SyncStmt: {
            auto* sy = static_cast<SyncStmt*>(s.get());
            substitute_local_in_expr(sy->expr, name, replacement);
            for (auto& sub : sy->body) substitute_local_in_stmt(sub, name, replacement);
            break;
        }
        case StmtKind::BlockStmt:
            for (auto& sub : static_cast<BlockStmt*>(s.get())->stmts) substitute_local_in_stmt(sub, name, replacement);
            break;
        default:
            break;
    }
}

std::vector<StmtPtr> inline_single_use_synth_temps(std::vector<StmtPtr> stmts) {
    // Recurse into child blocks first
    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == StmtKind::IfStmt) {
            auto* i = static_cast<IfStmt*>(s.get());
            i->then_body = inline_single_use_synth_temps(i->then_body);
            if (i->else_body) i->else_body = inline_single_use_synth_temps(*i->else_body);
        } else if (s->kind == StmtKind::WhileStmt) {
            auto* w = static_cast<WhileStmt*>(s.get());
            w->body = inline_single_use_synth_temps(w->body);
        } else if (s->kind == StmtKind::DoWhileStmt) {
            auto* dw = static_cast<DoWhileStmt*>(s.get());
            dw->body = inline_single_use_synth_temps(dw->body);
        } else if (s->kind == StmtKind::ForStmt) {
            auto* f = static_cast<ForStmt*>(s.get());
            f->body = inline_single_use_synth_temps(f->body);
        } else if (s->kind == StmtKind::SwitchStmt) {
            auto* sw = static_cast<SwitchStmt*>(s.get());
            for (auto& c : sw->cases) c.body = inline_single_use_synth_temps(c.body);
        } else if (s->kind == StmtKind::TryStmt) {
            auto* t = static_cast<TryStmt*>(s.get());
            t->body = inline_single_use_synth_temps(t->body);
            for (auto& c : t->catches) c.body = inline_single_use_synth_temps(c.body);
            if (t->finally_body) t->finally_body = inline_single_use_synth_temps(*t->finally_body);
        } else if (s->kind == StmtKind::SyncStmt) {
            auto* sy = static_cast<SyncStmt*>(s.get());
            sy->body = inline_single_use_synth_temps(sy->body);
        }
    }
    for (int iter = 0; iter < 4; ++iter) {
        bool changed = false;
        size_t n = stmts.size();
        for (size_t i = 0; i < n; ++i) {
            if (!stmts[i]) continue;
            auto a = as_assign(stmts[i]);
            if (!a.has_value()) continue;
            ExprPtr tgt = a->first, val = a->second;
            if (!tgt || !(tgt->kind == ExprKind::Local && is_synth_temp(static_cast<Local*>(tgt.get())->name))) continue;
            std::string tgt_name = static_cast<Local*>(tgt.get())->name;

            size_t max_j = (n > 64) ? std::min(n, i + 32) : n;
            std::vector<size_t> uses;
            for (size_t j = i + 1; j < max_j; ++j) {
                if (stmts[j] && contains_local_ref_stmt(stmts[j], tgt_name)) {
                    uses.push_back(j);
                    if (uses.size() > 1) break;
                }
            }
            if (uses.size() != 1) continue;
            size_t j = uses[0];
            if (!stmts[j]) continue;

            if (j != i + 1 && has_side_effect(val)) continue;

            substitute_local_in_stmt(stmts[j], tgt_name, val);
            stmts[i] = nullptr;
            changed = true;
        }
        if (changed) {
            std::vector<StmtPtr> compacted;
            compacted.reserve(stmts.size());
            for (auto& s : stmts) {
                if (s) compacted.push_back(std::move(s));
            }
            stmts = std::move(compacted);
        } else {
            break;
        }
    }
    return stmts;
}


bool is_local_read_expr(const ExprPtr& e, const std::string& name);
bool is_local_read_stmt(const StmtPtr& s, const std::string& name);

bool is_local_read_expr(const ExprPtr& e, const std::string& name) {
    if (!e) return false;
    if (e->kind == ExprKind::Local) {
        return static_cast<const Local*>(e.get())->name == name;
    }
    if (e->kind == ExprKind::Assign) {
        auto* a = static_cast<const Assign*>(e.get());
        if (a->target) {
            if (a->target->kind == ExprKind::Local && static_cast<const Local*>(a->target.get())->name == name) {
                // target of direct assign is a write
            } else if (a->target->kind == ExprKind::ArrayAccess) {
                auto* aa = static_cast<const ArrayAccess*>(a->target.get());
                if (is_local_read_expr(aa->index, name)) return true;
                if (aa->array && aa->array->kind == ExprKind::Local && static_cast<const Local*>(aa->array.get())->name == name) {
                    // target array element set: write to name's element
                } else {
                    if (is_local_read_expr(aa->array, name)) return true;
                }
            } else {
                if (is_local_read_expr(a->target, name)) return true;
            }
        }
        return is_local_read_expr(a->value, name);
    }
    switch (e->kind) {
        case ExprKind::FieldAccess:
            return is_local_read_expr(static_cast<const FieldAccess*>(e.get())->target, name);
        case ExprKind::ArrayAccess: {
            auto* a = static_cast<const ArrayAccess*>(e.get());
            return is_local_read_expr(a->array, name) || is_local_read_expr(a->index, name);
        }
        case ExprKind::MethodCall: {
            auto* m = static_cast<const MethodCall*>(e.get());
            if (is_local_read_expr(m->target, name)) return true;
            for (auto& a : m->args) if (is_local_read_expr(a, name)) return true;
            return false;
        }
        case ExprKind::NewObject:
            for (auto& a : static_cast<const NewObject*>(e.get())->args) if (is_local_read_expr(a, name)) return true;
            return false;
        case ExprKind::NewArray: {
            auto* n = static_cast<const NewArray*>(e.get());
            for (auto& d : n->dims) if (is_local_read_expr(d, name)) return true;
            if (n->initializer.has_value()) {
                for (auto& item : *n->initializer) if (is_local_read_expr(item, name)) return true;
            }
            return false;
        }
        case ExprKind::Cast:
            return is_local_read_expr(static_cast<const Cast*>(e.get())->expr, name);
        case ExprKind::InstanceOf:
            return is_local_read_expr(static_cast<const InstanceOf*>(e.get())->expr, name);
        case ExprKind::BinOp: {
            auto* b = static_cast<const BinOp*>(e.get());
            return is_local_read_expr(b->left, name) || is_local_read_expr(b->right, name);
        }
        case ExprKind::UnOp:
            return is_local_read_expr(static_cast<const UnOp*>(e.get())->expr, name);
        case ExprKind::Ternary: {
            auto* t = static_cast<const Ternary*>(e.get());
            return is_local_read_expr(t->cond, name) || is_local_read_expr(t->tval, name) ||
                   is_local_read_expr(t->fval, name);
        }
        case ExprKind::Lambda: {
            auto* l = static_cast<const Lambda*>(e.get());
            for (auto& p : l->params) if (is_local_read_expr(p, name)) return true;
            return is_local_read_expr(l->body_method_ref, name);
        }
        default:
            return false;
    }
}

bool is_local_read_stmt(const StmtPtr& s, const std::string& name) {
    if (!s) return false;
    if (s->kind == StmtKind::ExprStmt) {
        return is_local_read_expr(static_cast<const ExprStmtNode*>(s.get())->expr, name);
    } else if (s->kind == StmtKind::ThrowStmt) {
        return is_local_read_expr(static_cast<const ThrowStmt*>(s.get())->expr, name);
    } else if (s->kind == StmtKind::SyncStmt) {
        return is_local_read_expr(static_cast<const SyncStmt*>(s.get())->expr, name);
    } else if (s->kind == StmtKind::ReturnStmt) {
        return is_local_read_expr(static_cast<const ReturnStmt*>(s.get())->expr, name);
    } else if (s->kind == StmtKind::IfStmt) {
        auto* i = static_cast<const IfStmt*>(s.get());
        if (is_local_read_expr(i->cond, name)) return true;
        for (auto& sub : i->then_body) if (is_local_read_stmt(sub, name)) return true;
        if (i->else_body) for (auto& sub : *i->else_body) if (is_local_read_stmt(sub, name)) return true;
        return false;
    } else if (s->kind == StmtKind::WhileStmt) {
        auto* w = static_cast<const WhileStmt*>(s.get());
        if (is_local_read_expr(w->cond, name)) return true;
        for (auto& sub : w->body) if (is_local_read_stmt(sub, name)) return true;
        return false;
    } else if (s->kind == StmtKind::DoWhileStmt) {
        auto* dw = static_cast<const DoWhileStmt*>(s.get());
        if (is_local_read_expr(dw->cond, name)) return true;
        for (auto& sub : dw->body) if (is_local_read_stmt(sub, name)) return true;
        return false;
    } else if (s->kind == StmtKind::ForStmt) {
        auto* f = static_cast<const ForStmt*>(s.get());
        if (f->init && is_local_read_expr(f->init, name)) return true;
        if (is_local_read_expr(f->cond, name)) return true;
        if (f->update && is_local_read_stmt(f->update, name)) return true;
        for (auto& sub : f->body) if (is_local_read_stmt(sub, name)) return true;
        return false;
    } else if (s->kind == StmtKind::LocalDecl) {
        return is_local_read_expr(static_cast<const LocalDecl*>(s.get())->init, name);
    } else if (s->kind == StmtKind::SwitchStmt) {
        auto* sw = static_cast<const SwitchStmt*>(s.get());
        if (is_local_read_expr(sw->selector, name)) return true;
        for (auto& c : sw->cases) {
            for (auto& sub : c.body) if (is_local_read_stmt(sub, name)) return true;
        }
        return false;
    } else if (s->kind == StmtKind::TryStmt) {
        auto* t = static_cast<const TryStmt*>(s.get());
        for (auto& sub : t->body) if (is_local_read_stmt(sub, name)) return true;
        for (auto& c : t->catches) for (auto& sub : c.body) if (is_local_read_stmt(sub, name)) return true;
        if (t->finally_body) for (auto& sub : *t->finally_body) if (is_local_read_stmt(sub, name)) return true;
        return false;
    } else if (s->kind == StmtKind::BlockStmt) {
        for (auto& sub : static_cast<const BlockStmt*>(s.get())->stmts) if (is_local_read_stmt(sub, name)) return true;
        return false;
    }
    return false;
}

std::vector<StmtPtr> prune_dead_synth_locals(std::vector<StmtPtr> stmts) {
    for (auto& s : stmts) {
        if (!s) continue;
        if (s->kind == StmtKind::IfStmt) {
            auto* i = static_cast<IfStmt*>(s.get());
            i->then_body = prune_dead_synth_locals(i->then_body);
            if (i->else_body) i->else_body = prune_dead_synth_locals(*i->else_body);
        } else if (s->kind == StmtKind::WhileStmt) {
            auto* w = static_cast<WhileStmt*>(s.get());
            w->body = prune_dead_synth_locals(w->body);
        } else if (s->kind == StmtKind::DoWhileStmt) {
            auto* dw = static_cast<DoWhileStmt*>(s.get());
            dw->body = prune_dead_synth_locals(dw->body);
        } else if (s->kind == StmtKind::ForStmt) {
            auto* f = static_cast<ForStmt*>(s.get());
            f->body = prune_dead_synth_locals(f->body);
        } else if (s->kind == StmtKind::SwitchStmt) {
            auto* sw = static_cast<SwitchStmt*>(s.get());
            for (auto& c : sw->cases) c.body = prune_dead_synth_locals(c.body);
        } else if (s->kind == StmtKind::TryStmt) {
            auto* t = static_cast<TryStmt*>(s.get());
            t->body = prune_dead_synth_locals(t->body);
            for (auto& c : t->catches) c.body = prune_dead_synth_locals(c.body);
            if (t->finally_body) t->finally_body = prune_dead_synth_locals(*t->finally_body);
        } else if (s->kind == StmtKind::SyncStmt) {
            auto* sy = static_cast<SyncStmt*>(s.get());
            sy->body = prune_dead_synth_locals(sy->body);
        } else if (s->kind == StmtKind::BlockStmt) {
            auto* b = static_cast<BlockStmt*>(s.get());
            b->stmts = prune_dead_synth_locals(b->stmts);
        }
    }
    size_t n = stmts.size();
    for (size_t i = 0; i < n; ++i) {
        if (!stmts[i] || stmts[i]->kind != StmtKind::LocalDecl) continue;
        auto* ld = static_cast<LocalDecl*>(stmts[i].get());
        if (is_synth_temp(ld->name)) {
            bool is_read = false;
            for (size_t j = 0; j < n; ++j) {
                if (i == j || !stmts[j]) continue;
                if (is_local_read_stmt(stmts[j], ld->name)) {
                    is_read = true;
                    break;
                }
            }
            if (!is_read) {
                // The synth local is write-only or completely unused!
                // 1. Prune the LocalDecl itself if init has no unremovable side effects
                if (!ld->init || !has_side_effect(ld->init) || ld->init->kind == ExprKind::NewArray || ld->init->kind == ExprKind::NewObject) {
                    stmts[i] = nullptr;
                    // 2. Prune all dead writes/array-sets to this synth local
                    for (size_t j = 0; j < n; ++j) {
                        if (!stmts[j] || stmts[j]->kind != StmtKind::ExprStmt) continue;
                        auto* es = static_cast<ExprStmtNode*>(stmts[j].get());
                        if (es->expr && es->expr->kind == ExprKind::Assign) {
                            auto* a = static_cast<Assign*>(es->expr.get());
                            bool is_write_to_ld = false;
                            if (a->target && a->target->kind == ExprKind::Local && static_cast<Local*>(a->target.get())->name == ld->name) {
                                is_write_to_ld = true;
                            } else if (a->target && a->target->kind == ExprKind::ArrayAccess) {
                                auto* aa = static_cast<ArrayAccess*>(a->target.get());
                                if (aa->array && aa->array->kind == ExprKind::Local && static_cast<Local*>(aa->array.get())->name == ld->name) {
                                    is_write_to_ld = true;
                                }
                            }
                            if (is_write_to_ld) {
                                stmts[j] = nullptr;
                            }
                        }
                    }
                }
            }
        }
    }
    std::vector<StmtPtr> compacted;
    for (auto& s : stmts) {
        if (s) compacted.push_back(std::move(s));
    }
    return compacted;
}



std::vector<StmtPtr> simplify_stmts(const std::vector<StmtPtr>& stmts) {
    
    nd::g_crash_pass_name = "simplify_stmt_initial";
    std::vector<StmtPtr> out = simplify_stmt_list(stmts, 0);
    for (int iter = 0; iter < 4; ++iter) {
        out = fold_boolean_materialization(out);
        out = collapse_temp_chains(out);
        out = inline_single_use_synth_temps(out);
        out = prune_dead_synth_locals(out);
        out = hoist_common_branch_tail(out);
        out = eliminate_redundant_else_after_return(out);
        out = desugar_return_ternaries(out);
        out = desugar_assignment_ternaries(out);
        out = propagate_single_use_flags(out);
        out = collapse_nested_if_conditions(out);
        out = merge_sequential_short_circuit_ifs(out);
        out = group_multi_catch_blocks(out);
        out = desugar_enhanced_for_loops(out);
        out = fold_try_with_resources(out);
        collapse_string_switches(out);
        out = fuse_for_initializers(out);
        out = propagate_local_constant_arrays(out);
        out = fold_try_catches(out);
        out = prune_opaque_branches(out);
        out = unflatten_switch_dispatchers(out);
        out = inline_single_use_synth_temps(out);
        out = prune_dead_synth_locals(out);
        out = restore_assertions_pass(out);
        out = prune_empty_blocks_pass(out);
        out = prune_dead_code_pass(out);
    }
    nd::g_crash_pass_name = "clean_synthetic_null_checks";
    out = clean_synthetic_null_checks(out);
    nd::g_crash_pass_name = "desugar_objects_require_non_null";
    out = desugar_objects_require_non_null(out);
    nd::g_crash_pass_name = "fold_compound_assignments";
    out = fold_compound_assignments(out);
    nd::g_crash_pass_name = "desugar_instanceof_pattern_matching";
    out = desugar_instanceof_pattern_matching(out);
    nd::g_crash_pass_name = "smart_rename_locals";
    out = smart_rename_locals(out);
    nd::g_crash_pass_name = "simplify_stmts_done";
    return out;
}

}  // namespace nd
