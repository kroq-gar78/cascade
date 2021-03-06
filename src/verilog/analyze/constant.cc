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

#include "src/verilog/analyze/constant.h"

#include "src/verilog/analyze/resolve.h"

namespace cascade {

Constant::Constant() : Visitor() { }

bool Constant::is_constant(const Expression* e) {
  res_ = true;
  genvar_ok_ = false;
  e->accept(this);
  return res_;
}

bool Constant::is_constant_genvar(const Expression* e) {
  res_ = true;
  genvar_ok_ = true;
  e->accept(this);
  return res_;
}

void Constant::visit(const Identifier* i) {
  Visitor::visit(i);

  const auto r = Resolve().get_resolution(i);
  if (r == nullptr) {
    res_ = false;
    return;
  } 

  const auto is_genvar = dynamic_cast<const GenvarDeclaration*>(r->get_parent()) != nullptr;
  const auto is_param = dynamic_cast<const ParameterDeclaration*>(r->get_parent()) != nullptr;
  const auto is_localparam = dynamic_cast<const LocalparamDeclaration*>(r->get_parent()) != nullptr;
  if (is_param || is_localparam || (is_genvar && genvar_ok_)) { 
    return;
  }

  res_ = false;
}

} // namespace cascade
