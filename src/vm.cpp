#include "vm.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "compiler.hpp"
#include "debug.hpp"
#include "object.hpp"

namespace rill {

VM vm;

void VM::resetStack() { stackTop_ = stack_; }

void VM::init() { resetStack(); }

void VM::free() { freeObjects(); }

void VM::push(Value value) { *stackTop_++ = value; }

Value VM::pop() { return *--stackTop_; }

Value VM::peek(int distance) const { return stackTop_[-1 - distance]; }

void VM::runtimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  va_end(args);
  std::fputs("\n", stderr);

  size_t instruction = static_cast<size_t>(ip_ - chunk_->code.data()) - 1;
  std::fprintf(stderr, "[line %d] in script\n", chunk_->lineAt(instruction));
  resetStack();
}

namespace {

// Rill has no string type coercion: `+` concatenates only when both operands
// are strings, and adds only when both are numbers.
bool bothStrings(Value a, Value b) { return isString(a) && isString(b); }

}  // namespace

InterpretResult VM::run() {
#define READ_BYTE() (*ip_++)
#define READ_CONSTANT() (chunk_->constants[READ_BYTE()])

#define BINARY_NUMERIC(makeValue, op)                          \
  do {                                                         \
    if (!isNumber(peek(0)) || !isNumber(peek(1))) {             \
      runtimeError("operands must be numbers");                 \
      return InterpretResult::RuntimeError;                     \
    }                                                          \
    double b = asNumber(pop());                                 \
    double a = asNumber(pop());                                 \
    push(makeValue(a op b));                                    \
  } while (false)

  for (;;) {
#ifdef RILL_TRACE
    std::printf("          ");
    for (Value* slot = stack_; slot < stackTop_; slot++) {
      std::printf("[ ");
      printValue(*slot);
      std::printf(" ]");
    }
    std::printf("\n");
    disassembleInstruction(*chunk_,
                           static_cast<int>(ip_ - chunk_->code.data()));
#endif

    auto instruction = static_cast<OpCode>(READ_BYTE());
    switch (instruction) {
      case OpCode::Constant: push(READ_CONSTANT()); break;
      case OpCode::Nil:      push(nilValue()); break;
      case OpCode::True:     push(boolValue(true)); break;
      case OpCode::False:    push(boolValue(false)); break;
      case OpCode::Pop:      pop(); break;

      case OpCode::DefineGlobal: {
        ObjString* name = asString(READ_CONSTANT());
        globals_.set(name, peek(0));
        pop();
        break;
      }

      case OpCode::GetGlobal: {
        ObjString* name = asString(READ_CONSTANT());
        Value value;
        if (!globals_.get(name, &value)) {
          runtimeError("undefined variable '%s'", name->chars);
          return InterpretResult::RuntimeError;
        }
        push(value);
        break;
      }

      case OpCode::SetGlobal: {
        ObjString* name = asString(READ_CONSTANT());
        // Assignment never creates a binding, so a miss is an error and the
        // table entry it just created must be undone.
        if (globals_.set(name, peek(0))) {
          globals_.remove(name);
          runtimeError("undefined variable '%s'", name->chars);
          return InterpretResult::RuntimeError;
        }
        break;
      }

      case OpCode::GetLocal: {
        uint8_t slot = READ_BYTE();
        push(stack_[slot]);
        break;
      }

      case OpCode::SetLocal: {
        uint8_t slot = READ_BYTE();
        // Assignment is an expression, so the value stays on the stack.
        stack_[slot] = peek(0);
        break;
      }

      case OpCode::CloseScope: {
        uint8_t count = READ_BYTE();
        // Remove the scope's locals from under the block's result value.
        Value result = pop();
        stackTop_ -= count;
        push(result);
        break;
      }

      case OpCode::Not: push(boolValue(isFalsey(pop()))); break;

      case OpCode::Negate:
        if (!isNumber(peek(0))) {
          runtimeError("operand must be a number");
          return InterpretResult::RuntimeError;
        }
        push(numberValue(-asNumber(pop())));
        break;

      case OpCode::Add: {
        if (bothStrings(peek(0), peek(1))) {
          ObjString* b = asString(peek(0));
          ObjString* a = asString(peek(1));
          int length = a->length + b->length;
          auto* chars =
              static_cast<char*>(std::malloc(static_cast<size_t>(length) + 1));
          std::memcpy(chars, a->chars, static_cast<size_t>(a->length));
          std::memcpy(chars + a->length, b->chars,
                      static_cast<size_t>(b->length));
          chars[length] = '\0';
          ObjString* result = takeString(chars, length);
          pop();
          pop();
          push(objValue(result));
        } else if (isNumber(peek(0)) && isNumber(peek(1))) {
          double b = asNumber(pop());
          double a = asNumber(pop());
          push(numberValue(a + b));
        } else {
          runtimeError("operands must be two numbers or two strings");
          return InterpretResult::RuntimeError;
        }
        break;
      }

      case OpCode::Subtract: BINARY_NUMERIC(numberValue, -); break;
      case OpCode::Multiply: BINARY_NUMERIC(numberValue, *); break;
      case OpCode::Divide:   BINARY_NUMERIC(numberValue, /); break;

      case OpCode::Modulo: {
        if (!isNumber(peek(0)) || !isNumber(peek(1))) {
          runtimeError("operands must be numbers");
          return InterpretResult::RuntimeError;
        }
        double b = asNumber(pop());
        double a = asNumber(pop());
        push(numberValue(std::fmod(a, b)));
        break;
      }

      case OpCode::Equal: {
        Value b = pop();
        Value a = pop();
        push(boolValue(valuesEqual(a, b)));
        break;
      }
      case OpCode::Greater: BINARY_NUMERIC(boolValue, >); break;
      case OpCode::Less:    BINARY_NUMERIC(boolValue, <); break;

      case OpCode::Print:
        printValue(pop());
        std::printf("\n");
        // `print(x)` is an expression like any other and must leave exactly
        // one value behind. It becomes a native returning nil in a later
        // task; this keeps the invariant true in the meantime.
        push(nilValue());
        break;

      case OpCode::Return:
        return InterpretResult::Ok;
    }
  }

#undef BINARY_NUMERIC
#undef READ_CONSTANT
#undef READ_BYTE
}

InterpretResult VM::interpret(const char* source) {
  Chunk chunk;
  if (!compile(source, &chunk)) return InterpretResult::CompileError;

  chunk_ = &chunk;
  ip_ = chunk_->code.data();
  InterpretResult result = run();
  chunk_ = nullptr;
  ip_ = nullptr;
  return result;
}

}  // namespace rill
