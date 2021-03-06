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

#ifndef CASCADE_SRC_VERILOG_AST_EXPRESSION_H
#define CASCADE_SRC_VERILOG_AST_EXPRESSION_H

#include <vector>
#include "src/base/bits/bits.h"
#include "src/verilog/ast/types/macro.h"
#include "src/verilog/ast/types/node.h"

namespace cascade {

class Expression : public Node {
  public:
    // Constructors:
    Expression();
    ~Expression() override = default;

    // Node Interface:
    Expression* clone() const override = 0;
    void accept(Visitor* v) const override = 0;
    void accept(Editor* e) override = 0;
    Expression* accept(Builder* b) const override = 0;
    Expression* accept(Rewriter* r) override = 0;

  protected:
    // Decorations used by Evaluate
    friend class Evaluate;
    // A vector of bitstring values, a variable array being the most general
    // instance of an expression
    DECORATION(std::vector<Bits>, bit_val);
    // Does this expression need to be recomputed (generally because the value
    // of one of its subexpressions has changed)?
    DECORATION(bool, needs_update);

    friend class Resolve;
    DECORATION(std::vector<Identifier*>, dependencies);
};

inline Expression::Expression() : Node() {
  needs_update_ = true;
}

} // namespace cascade 

#endif
