// Copyright 2017-2018 VMware, Inc.
// SPDX-License-Identifier: BSD-2-Clause
//
// The BSD-2 license (the License) set forth below applies to all parts of the
// Cascade project.  You may not use this file except in compliance with the
// License.
//
// BSD-2 License
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS AS IS AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "src/target/core/sw/sw_logic.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include "src/target/core/sw/monitor.h"
#include "src/target/input.h"
#include "src/target/interface.h"
#include "src/verilog/analyze/evaluate.h"
#include "src/verilog/analyze/module_info.h"
#include "src/verilog/analyze/printf.h"
#include "src/verilog/analyze/resolve.h"
#include "src/verilog/ast/ast.h"
#include "src/verilog/print/text/text_printer.h"

using namespace std;

namespace cascade {

SwLogic::SwLogic(Interface* interface, ModuleDeclaration* md) : Logic(interface), Visitor() { 
  // Record pointer to source code
  src_ = md;
  // Initialize monitors
  for (auto mi : *src_->get_items()) {
    Monitor().init(mi);
  }
  // Initial provision for update_pool_:
  update_pool_.resize(1);
}

SwLogic::~SwLogic() {
  delete src_;
}

SwLogic& SwLogic::set_read(const Identifier* id, VId vid) {
  if (vid >= reads_.size()) {
    reads_.resize(vid+1, nullptr);
  }
  reads_[vid] = id;
  return *this;
}

SwLogic& SwLogic::set_write(const Identifier* id, VId vid) {
  writes_.push_back(make_pair(id, vid));
  return *this;
}

SwLogic& SwLogic::set_state(const Identifier* id, VId vid) {
  state_.insert(make_pair(vid, id));
  return *this;
}

State* SwLogic::get_state() {
  auto s = new State();
  for (const auto& sv : state_) {
    s->insert(sv.first, Evaluate().get_array_value(sv.second));
  }
  return s;
}

void SwLogic::set_state(const State* s) {
  for (const auto& sv : state_) {
    const auto itr = s->find(sv.first);
    if (itr != s->end()) {
      Evaluate().assign_array_value(sv.second, itr->second);
    }
  }
}

Input* SwLogic::get_input() {
  auto i = new Input();
  for (size_t v = 0, ve = reads_.size(); v < ve; ++v) {
    const auto id = reads_[v];
    if (id == nullptr) {
      continue;
    }
    i->insert(v, Evaluate().get_value(id));
  }
  return i;
}

void SwLogic::set_input(const Input* i) {
  for (size_t v = 0, ve = reads_.size(); v < ve; ++v) {
    const auto id = reads_[v];
    if (id == nullptr) {
      continue;
    }
    const auto itr = i->find(v);
    if (itr != i->end()) {
      Evaluate().assign_value(id, itr->second);
    }
  }
}

void SwLogic::resync() {
  // Schedule always constructs and continuous assigns.
  for (auto mi : *src_->get_items()) {
    if (dynamic_cast<const AlwaysConstruct*>(mi)) {
      schedule_now(mi);
    } else if (auto ca = dynamic_cast<const ContinuousAssign*>(mi)) {
      schedule_now(ca);
    }
  }
  for (auto l : ModuleInfo(src_).inputs()) {
    notify(l);
  } 

  // Turn on silent mode and drain the active queue
  silent_ = true;
  while (!active_.empty()) {
    auto e = active_.back();
    active_.pop_back();
    const_cast<Node*>(e)->active_ = false;
    schedule_now(e);
  }
  silent_ = false;

  // Now that signals have been propagated, schedule initial constructs
  for (auto mi : *src_->get_items()) {
    if (auto ic = dynamic_cast<const InitialConstruct*>(mi)) {
      schedule_now(ic);
    }
  }
}

void SwLogic::read(VId vid, const Bits* b) {
  const auto id = reads_[vid];
  Evaluate().assign_value(id, *b);
  notify(id);
}

void SwLogic::evaluate() {
  // This is a while loop. Active events can generate new active events.
  there_were_tasks_ = false;
  while (!active_.empty()) {
    auto e = active_.back();
    active_.pop_back();
    const_cast<Node*>(e)->active_ = false;
    schedule_now(e);
  }
  for (auto& o : writes_) {
    interface()->write(o.second, &Evaluate().get_value(o.first));
  }
}

bool SwLogic::there_are_updates() const {
  return !updates_.empty();
}

void SwLogic::update() {
  // This is a for loop. Updates happen simultaneously
  for (size_t i = 0, ie = updates_.size(); i < ie; ++i) {
    const auto& val = update_pool_[i];
    Evaluate().assign_value(get<0>(updates_[i]), get<1>(updates_[i]), get<2>(updates_[i]), get<3>(updates_[i]), val);
    notify(get<0>(updates_[i]));
  }
  updates_.clear();

  // This is while loop. Active events can generate new active events.
  there_were_tasks_ = false;
  while (!active_.empty()) {
    auto e = active_.back();
    active_.pop_back();
    const_cast<Node*>(e)->active_ = false;
    schedule_now(e);
  }

  for (auto& o : writes_) {
    interface()->write(o.second, &Evaluate().get_value(o.first));
  }
}

bool SwLogic::there_were_tasks() const {
  return there_were_tasks_;
}

void SwLogic::schedule_now(const Node* n) {
  n->accept(this);
}

void SwLogic::schedule_active(const Node* n) {
  if (!n->active_) {
    active_.push_back(n);
    const_cast<Node*>(n)->active_ = true;
  }
}

void SwLogic::notify(const Node* n) {
  for (auto m : n->monitor_) {
    schedule_active(m);
  }
}

size_t& SwLogic::get_state(const Node* n) {
  return const_cast<Node*>(n)->ctrl_;
}

void SwLogic::visit(const Event* e) {
  // TODO: Support for complex expressions here
  const auto id = dynamic_cast<const Identifier*>(e->get_expr());
  assert(id != nullptr);
  const auto r = Resolve().get_resolution(id);

  if (e->get_type() != Event::NEGEDGE && Evaluate().get_value(r).to_bool()) {
    notify(e);
  } else if (e->get_type() != Event::POSEDGE && !Evaluate().get_value(r).to_bool()) {
    notify(e);
  }
}

void SwLogic::visit(const AlwaysConstruct* ac) {
  schedule_now(ac->get_stmt());
}

void SwLogic::visit(const InitialConstruct* ic) {
  const auto ign = ic->get_attrs()->get<String>("__ignore");
  if (ign == nullptr || !ign->eq("true")) {
    schedule_active(ic->get_stmt());
  }
}

void SwLogic::visit(const ContinuousAssign* ca) {
  // TODO: Support for timing control
  assert(ca->get_ctrl()->null());
  schedule_now(ca->get_assign());
}

void SwLogic::visit(const BlockingAssign* ba) {
  // TODO: Support for timing control
  assert(ba->get_ctrl()->null());

  schedule_now(ba->get_assign());
  notify(ba);
}

void SwLogic::visit(const NonblockingAssign* na) {
  // TODO: Support for timing control
  assert(na->get_ctrl()->null());
  
  if (!silent_) {
    const auto r = Resolve().get_resolution(na->get_assign()->get_lhs());
    assert(r != nullptr);
    const auto target = Evaluate().dereference(r, na->get_assign()->get_lhs());
    const auto& res = Evaluate().get_value(na->get_assign()->get_rhs());

    const auto idx = updates_.size();
    if (idx >= update_pool_.size()) {
      update_pool_.resize(2*update_pool_.size());
    } 

    updates_.push_back(make_tuple(r, get<0>(target), get<1>(target), get<2>(target)));
    update_pool_[idx] = res;
  }
  notify(na);
}

void SwLogic::visit(const ParBlock* pb) {
  auto& state = get_state(pb);
  switch (state) {
    case 0:
      state = pb->get_stmts()->size();
      for (auto s : *pb->get_stmts()) {
        schedule_now(s);
      }
      break;
    default:
      if (--state == 0) {
        notify(pb);
      }
      break;
  }
}

void SwLogic::visit(const SeqBlock* sb) { 
  auto& state = get_state(sb);
  if (state < sb->get_stmts()->size()) {
    auto item = sb->get_stmts()->get(state++);
    schedule_now(item);
  } else {
    state = 0;
    notify(sb);
  }
}

void SwLogic::visit(const CaseStatement* cs) {
  auto& state = get_state(cs);
  if (state == 0) {
    state = 1;
    const auto s = Evaluate().get_value(cs->get_cond()).to_int();
    for (auto ci : *cs->get_items()) {
      for (auto e : *ci->get_exprs()) {
        const auto c = Evaluate().get_value(e).to_int();
        if (s == c) {
          schedule_now(ci->get_stmt());
          return;
        }
      } 
      if (ci->get_exprs()->empty()) {
        schedule_now(ci->get_stmt());
        return;
      }
    }
    // Control should never reach here
    assert(false);
  } else {
    state = 0;
    notify(cs);
  }
}

void SwLogic::visit(const ConditionalStatement* cs) {
  auto& state = get_state(cs);
  if (state == 0) {
    state = 1;
    if (Evaluate().get_value(cs->get_if()).to_bool()) {
      schedule_now(cs->get_then());
    } else {
      schedule_now(cs->get_else());
    }
  } else {
    state = 0;
    notify(cs);
  }
}

void SwLogic::visit(const ForStatement* fs) {
  auto& state = get_state(fs);
  switch (state) {
    case 0:
      ++state;
      schedule_now(fs->get_init());
      // Fallthrough
    case 1:
      if (!Evaluate().get_value(fs->get_cond()).to_bool()) {
        state = 0;
        notify(fs);
        return;
      }
      state = 2;
      schedule_now(fs->get_stmt());
      break;
    default:
      state = 1;
      schedule_now(fs->get_update());
      schedule_now(fs);
      break;
  }
}

void SwLogic::visit(const RepeatStatement* rs) {
  auto& state = get_state(rs);
  switch (state) {
    case 0:
      state = Evaluate().get_value(rs->get_cond()).to_int() + 1;
      // Fallthrough ...
    default: 
      if (--state == 0) {
        notify(rs);
      } else {
        schedule_now(rs->get_stmt());
      }
      break;
  }
}

void SwLogic::visit(const WhileStatement* ws) {
  if (!Evaluate().get_value(ws->get_cond()).to_bool()) {
    notify(ws);
    return;
  }
  schedule_now(ws->get_stmt());
}


void SwLogic::visit(const TimingControlStatement* tcs) {
  auto& state = get_state(tcs);
  switch (state) {
    case 0:
      state = 1;
      // Wait on control 
      break;
    case 1:
      state = 2;
      schedule_now(tcs->get_stmt());
      break;
    default:
      state = 0;
      notify(tcs);
      break;
  }
}

void SwLogic::visit(const DisplayStatement* ds) {
  if (!silent_) {
    interface()->display(Printf().format(ds->get_args()));
    there_were_tasks_ = true;
  }
  notify(ds);
}

void SwLogic::visit(const FinishStatement* fs) {
  if (!silent_) {
    interface()->finish(Evaluate().get_value(fs->get_arg()).to_int());
    there_were_tasks_ = true;
  }
  notify(fs);
}

void SwLogic::visit(const WriteStatement* ws) {
  if (!silent_) {
    interface()->write(Printf().format(ws->get_args()));
    there_were_tasks_ = true;
  }
  notify(ws);
}

void SwLogic::visit(const WaitStatement* ws) {
  auto& state = get_state(ws);
  if (state == 0) {
    if (!Evaluate().get_value(ws->get_cond()).to_bool()) {
      return;
    }
    state = 1;
    schedule_now(ws->get_stmt());
  } else {
    state = 0;
    notify(ws);
  }
}

void SwLogic::visit(const DelayControl* dc) {
  // NOTE: Unsynthesizable verilog
  assert(false);
  (void) dc;
}

void SwLogic::visit(const EventControl* ec) {
  notify(ec);
}

void SwLogic::visit(const VariableAssign* va) {
  const auto& res = Evaluate().get_value(va->get_rhs());
  Evaluate().assign_value(va->get_lhs(), res);
  notify(Resolve().get_resolution(va->get_lhs()));
}

void SwLogic::log(const string& op, const Node* n) {
  TextPrinter(cout) << "[" << src_->get_id() << "] " << op << " " << n << "\n";
}

} // namespace cascade
