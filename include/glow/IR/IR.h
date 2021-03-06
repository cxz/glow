/**
 * Copyright (c) 2017-present, Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef GLOW_IR_IR_H
#define GLOW_IR_IR_H

#include "glow/Base/Traits.h"
#include "glow/Base/Type.h"
#include "glow/IR/UseDef.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <list>
#include <unordered_map>
#include <vector>

namespace glow {
class Instruction;
class IRFunction;
class Function;
class Value;
class InstructionNumbering;

enum class OperandKind : unsigned char {
  In,
  InOut,
  Out,
};

inline const char *getOperandKindStr(OperandKind CC) {
  const char *names[] = {"@in", "@inout", "@out", nullptr};
  return names[(int)CC];
}

using InstructionOperand = std::pair<Value *, OperandKind>;
using ConstInstructionOperand = const std::pair<const Value *, OperandKind>;

/// A 'Use' is a use-list representation of an instruction operand. It maps to a
/// specific operand in an instruction.
struct Use {
  /// The instruction.
  Instruction *use_;
  /// The index of the operand.
  unsigned idx_;

  bool operator==(const Use &other) const {
    return idx_ == other.idx_ && use_ == other.use_;
  }

  Use(unsigned idx, Instruction *use) : use_(use), idx_(idx) {}

  /// \returns the instruction that the use refers to.
  Instruction *get() const { return use_; }
  /// \returns true if this Use is for the instruction \p other.
  bool isSame(Instruction *other) const { return use_ == other; }
  /// Sets the operand to a new value.
  void setOperand(Value *other);
  /// \returns the operand of the user instruction.
  InstructionOperand getOperand();
  ConstInstructionOperand getOperand() const;
};

class Value : public Named,
              public UseDef<Instruction, Value, Use>,
              public Typed,
              public Kinded {
public:
  Value(llvm::StringRef name, TypeRef T, Kinded::Kind k)
      : Named(name), Typed(T), Kinded(k) {}

  void verifyUseList(const InstructionNumbering &InstrNumbering) const;

  /// Verify the correctness of the instruction parameters.
  void verify(const IRFunction &M) const;

  /// Print value.
  void dump(llvm::raw_ostream &out) const;

  /// Print value using a default output stream.
  void dump() const;

  /// Print value in context.
  void dumpInContext(llvm::raw_ostream &out) const;

  /// Print value in context using a default output stream.
  void dumpInContext() const;
};

/// This represents an instruction in our IR.
class Instruction : public Value {
public:
  using Operand = InstructionOperand;

private:
  /// Parent function.
  IRFunction *F_;
  /// If a predicate is set this index points to the non-zero index of the
  /// predicate in the instruction list.
  unsigned predicateIndex_{0};

  /// A list of operands that the instruction has. This is typically a very
  /// short list.
  llvm::SmallVector<Operand, 6> ops_{};

  // Define/disallow default ctor, copy ctor and assignment operator.
  Instruction(const Instruction &I) = delete;
  Instruction &operator=(const Instruction &I) = delete;

protected:
  /// Prevent the destruction of a derived object via a base-class pointer.
  /// Use IRFunction::destroyInstruction instead.
  ~Instruction() {
    for (unsigned idx = 0, e = ops_.size(); idx < e; ++idx) {
      setOperand(idx, nullptr);
    }
  }

public:
  /// \returns the nullable predicate of the current node.
  Value *getPredicate() const;
  /// Assigns a nullable predicate to the current node.
  void setPredicate(Value *p);
  /// Checks if a predicate is assigned to the current node.
  bool hasPredicate() const;

  /// Adds a new operand \p op at the end of the operand list.
  void pushOperand(Operand op);

  Instruction(IRFunction *M, llvm::StringRef name, Kinded::Kind k, TypeRef Ty)
      : Value(name, Ty, k), F_(M) {}

  Instruction(IRFunction *M, llvm::StringRef name, Kinded::Kind k, TypeRef Ty,
              llvm::ArrayRef<Operand> ops)
      : Value(name, Ty, k), F_(M) {
    for (auto &op : ops) {
      pushOperand(op);
    }
  }

  /// \returns True if this instruction may reuse the memory buffer read by
  /// operand \p srcIdx for writing the result of the operand at \p dstIdx.
  bool isInplaceOp(unsigned dstIdx, unsigned srcIdx) const { return false; }

  /// \returns True if this instruction is data parallel.
  bool isDataParallel() const;

  /// Sets the ith operand at index \p idx to the value \p v.
  void setOperand(unsigned idx, Value *v);

  /// \returns the ith operand.
  Operand getOperand(unsigned idx) const;

  /// \returns the number of operands.
  unsigned getNumOperands() const { return ops_.size(); }

  /// \returns the operands of the instruction.
  llvm::ArrayRef<Operand> getOperands() const { return ops_; }

  /// Check the correctness of the use-list.
  void verifyUseList(const InstructionNumbering &InstrNumbering) const;

  /// Verify the correctness of the instruction parameters.
  void verify() const;

  /// The static dispatch version of isInplaceOp.
  static bool isInplaceOp(const Instruction *I, unsigned dstIdx,
                          unsigned srcIdx);

  /// \returns parent of current instruction.
  IRFunction *getParent() const { return F_; }

  /// Sets a parent for the current instruction.
  void setParent(IRFunction *Mod) { F_ = Mod; }

  /// Erases instruction from its parent and destroy it.
  void eraseFromParent();

  /// Removes instruction from its parent, but does not destroy it.
  /// The instruction can be inserted elsewhere afterwards.
  void removeFromParent();

  static bool classof(const Value *V);

  static bool classof(const Instruction *I) { return true; }

protected:
  /// Dump the operands of the instruction into the stream \p os.
  void dumpOperands(llvm::raw_ostream &os) const;
};

class WeightVar;
class Value;
class Node;

/// A function that represents the compilation unit.
class IRFunction final {
public:
  using VariableMap = std::unordered_map<const Node *, Value *>;
  using InstListTy = std::list<Instruction *>;
  using InstrIterator = InstListTy::iterator;
  using InstrConstIterator = InstListTy::const_iterator;
  using WeightVarListTy = std::list<WeightVar *>;

private:
  /// A pointer to the graph structure. The function does not own the graph.
  Function *G_{};

  /// A list of weights. Weights are shared between all execution context.
  WeightVarListTy weights_{};

  /// A list of instruction that represent the network.
  InstListTy instrs_{};

  /// Maps Variable nodes in the original graph to the weight values that
  /// represent them in the lower IR.
  VariableMap variableMap_{};

  /// Assign the instructions in the function a unique name.
  void nameInstructions();

  /// Perform scheduling on the graph.
  /// \returns computed schedule in the \p Schedule parameter.
  void scheduleGraph(std::list<Node *> &Schedule);

public:
  /// Add an instruction to the instr stream.
  void pushInstr(Instruction *I) { instrs_.push_back(I); }

  explicit IRFunction(Function *G = nullptr);

  ~IRFunction();

  /// Generate IR from the graph nodes. If the compilation mode is 'training'
  /// then this procedure will also generate the code for the backward pass.
  void generateIR();

  /// Wipe out the content of the function. This allows the function to be used
  /// again for another round of code generation.
  void clear();

  /// \returns a reference to the high-level graph.
  Function *getGraph() { return G_; }

  /// \returns a reference to the high-level graph.
  void setGraph(Function *F) { G_ = F; }

  /// Verify the correctness of the function.
  void verify() const;

  /// Dump a textual representation of the function.
  void dump();

  /// Dump a textual representation of the function into provided output stream.
  void dump(llvm::raw_ostream &OS);

  /// Dump a dotty graph that depicts the function.
  void dumpDAG(const char *dotFilename);

  /// Dump a dotty graph that depicts the function.
  void dumpDAG();

  /// \returns the variable map.
  VariableMap &getVariableMap() { return variableMap_; }

  /// \returns the weight that the variable \p v is lowered into, or null if the
  /// variable is unknown.
  Value *getWeightForNode(const Node *V) const;

  /// \returns the list of instructions.
  InstListTy &getInstrs() { return instrs_; }
  /// \returns the list of instructions.
  const InstListTy &getInstrs() const { return instrs_; }

  /// \returns the list of weights.
  WeightVarListTy &getWeights() { return weights_; }

  /// Erase the instruction from the function.
  void eraseInstruction(Instruction *I);

  /// Erase the instruction from the function.
  InstrIterator eraseInstruction(InstrIterator it);

  /// Remove the instruction from the function.
  void removeInstruction(Instruction *I);

  /// Remove the instruction from the function.
  InstrIterator removeInstruction(InstrIterator it);

  /// Destroy an instruction.
  void destroyInstruction(Instruction *I);

  /// Inserts an instruction at the place described by \where.
  InstrIterator insertInstruction(InstrIterator where, Instruction *I);

  /// Moves an instruction belonging to a function before the place described by
  /// \where.
  InstrIterator moveInstruction(InstrIterator where, Instruction *I);

  /// Moves an instruction belonging to a function before the place described by
  /// \where.
  InstrIterator moveInstruction(InstrIterator where, InstrIterator I);

  /// Moves an instruction belonging to a function before the place described by
  /// \where.
  InstrIterator moveInstruction(const Instruction *where, Instruction *I);

  /// Inserts an instruction at the end of the instructions list.
  void insertInstruction(Instruction *I);

  /// \returns instruction's list iterator corresponding to the instruction.
  InstrIterator getInstrIterator(const Instruction *I);

  /// \returns instruction's list iterator corresponding to the instruction.
  InstrConstIterator getInstrIterator(const Instruction *I) const;
};

/// Iterator over inteructions.
using InstrIterator = IRFunction::InstrIterator;
using InstrConstIterator = IRFunction::InstrConstIterator;

/// A helper class used for instructions numbering.
class InstructionNumbering {
  using NumberedInstructionMap = std::vector<InstrConstIterator>;
  using InstructionNumbersMap = std::unordered_map<Instruction *, size_t>;
  /// Maps the number to an instruction.
  NumberedInstructionMap numToInstr_;
  /// Maps an instruction to its number.
  InstructionNumbersMap instrToNum_;

public:
  InstructionNumbering(const IRFunction &M);

  /// Return the instruction with a given number or
  /// M.getInstrs().end() if this instruction is not assigned any number.
  InstrConstIterator getInstr(size_t InstrNumber) const;

  /// Return the number of an instruction or a negative value if no number
  /// was assigned to this instruction.
  int64_t getInstrNumber(InstrConstIterator IT) const;

  /// Return the number of an instruction or a negative value if no number
  /// was assigned to this instruction.
  int64_t getInstrNumber(Instruction *I) const;
};

/// Get the allocation corrsponding to th value \p V. It can look through
/// tensorview instructions. \returns found allocation or nullptr.
Value *getAllocationOrigin(Value *V);

/// \returns peels off the layers of tensorviews from a value \p V.
Value *getOrigin(Value *V);

} // namespace glow

#endif // GLOW_IR_IR_H
