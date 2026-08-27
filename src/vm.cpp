#include "vm.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "builtins.hpp"
#include "compiler.hpp"
#include "debug.hpp"
#include "object.hpp"

namespace rill {

VM vm;

void VM::resetStack() {
  stackTop_ = stack_;
  frameCount_ = 0;
}

void VM::init() {
  resetStack();
  defineBuiltins(*this);
}

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

  // Innermost frame first, the way a stack trace reads.
  for (int i = frameCount_ - 1; i >= 0; i--) {
    CallFrame* frame = &frames_[i];
    ObjFunction* function = frame->function;
    size_t instruction =
        static_cast<size_t>(frame->ip - function->chunk.code.data()) - 1;
    std::fprintf(stderr, "[line %d] in ", function->chunk.lineAt(instruction));
    if (function->name == nullptr) {
      std::fprintf(stderr, "script\n");
    } else {
      std::fprintf(stderr, "fn '%s'\n", function->name->chars);
    }
  }
  resetStack();
}

bool VM::call(ObjFunction* function, int argCount) {
  if (argCount != function->arity) {
    runtimeError("expected %d arguments but got %d", function->arity, argCount);
    return false;
  }
  if (frameCount_ == kFramesMax) {
    runtimeError("stack overflow");
    return false;
  }

  CallFrame* frame = &frames_[frameCount_++];
  frame->function = function;
  frame->ip = function->chunk.code.data();
  // The callee sits just below its arguments and becomes slot 0.
  frame->slots = stackTop_ - argCount - 1;
  return true;
}

bool VM::callValue(Value callee, int argCount) {
  if (isObj(callee)) {
    switch (asObj(callee)->type) {
      case ObjType::Function:
        return call(asFunction(callee), argCount);

      case ObjType::Native: {
        ObjNative* native = asNative(callee);
        if (native->arity >= 0 && native->arity != argCount) {
          runtimeError("expected %d arguments but got %d", native->arity,
                       argCount);
          return false;
        }
        Value result = nilValue();
        if (!native->function(argCount, stackTop_ - argCount, &result)) {
          runtimeError("%s", isString(result) ? asCString(result)
                                              : "error in native function");
          return false;
        }
        stackTop_ -= argCount + 1;
        push(result);
        return true;
      }

      default:
        break;
    }
  }
  runtimeError("can only call functions");
  return false;
}

void VM::defineNative(const char* name, NativeFn function, int arity) {
  ObjString* key = copyString(name, static_cast<int>(std::strlen(name)));
  globals_.set(key, objValue(newNative(function, name, arity)));
}

namespace {

// Rill has no string type coercion: `+` concatenates only when both operands
// are strings, and adds only when both are numbers.
bool bothStrings(Value a, Value b) { return isString(a) && isString(b); }

}  // namespace

InterpretResult VM::run() {
  CallFrame* frame = &frames_[frameCount_ - 1];
  // `ip` is cached in a local so the hot loop does not chase a pointer through
  // the frame on every instruction. It MUST be written back to the frame
  // before any call and reloaded after, or a callee will resume at the wrong
  // instruction.
  uint8_t* ip = frame->ip;

#define READ_BYTE() (*ip++)
#define READ_CONSTANT() (frame->function->chunk.constants[READ_BYTE()])
#define READ_SHORT() \
  (ip += 2, static_cast<uint16_t>((ip[-2] << 8) | ip[-1]))
#define STORE_IP() (frame->ip = ip)
#define LOAD_FRAME()                     \
  do {                                   \
    frame = &frames_[frameCount_ - 1];   \
    ip = frame->ip;                      \
  } while (false)

#define RUNTIME_ERROR(...)               \
  do {                                   \
    STORE_IP();                          \
    runtimeError(__VA_ARGS__);           \
    return InterpretResult::RuntimeError; \
  } while (false)

#define BINARY_NUMERIC(makeValue, op)               \
  do {                                              \
    if (!isNumber(peek(0)) || !isNumber(peek(1))) {  \
      RUNTIME_ERROR("operands must be numbers");     \
    }                                               \
    double b = asNumber(pop());                     \
    double a = asNumber(pop());                     \
    push(makeValue(a op b));                        \
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
    disassembleInstruction(
        frame->function->chunk,
        static_cast<int>(ip - frame->function->chunk.code.data()));
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
          RUNTIME_ERROR("undefined variable '%s'", name->chars);
        }
        push(value);
        break;
      }

      case OpCode::SetGlobal: {
        ObjString* name = asString(READ_CONSTANT());
        // Assignment never creates a binding, so a miss is an error and the
        // entry it just created must be undone.
        if (globals_.set(name, peek(0))) {
          globals_.remove(name);
          RUNTIME_ERROR("undefined variable '%s'", name->chars);
        }
        break;
      }

      case OpCode::GetLocal: {
        uint8_t slot = READ_BYTE();
        push(frame->slots[slot]);
        break;
      }

      case OpCode::SetLocal: {
        uint8_t slot = READ_BYTE();
        // Assignment is an expression, so the value stays on the stack.
        frame->slots[slot] = peek(0);
        break;
      }

      case OpCode::PopN: {
        uint8_t count = READ_BYTE();
        stackTop_ -= count;
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

      case OpCode::Jump: {
        uint16_t offset = READ_SHORT();
        ip += offset;
        break;
      }

      case OpCode::JumpIfFalse: {
        uint16_t offset = READ_SHORT();
        // Deliberately does not pop: the compiler emits an explicit Pop on
        // each path, which is what lets `and`/`or` yield an operand.
        if (isFalsey(peek(0))) ip += offset;
        break;
      }

      case OpCode::Loop: {
        uint16_t offset = READ_SHORT();
        ip -= offset;
        break;
      }

      case OpCode::Not: push(boolValue(isFalsey(pop()))); break;

      case OpCode::Negate:
        if (!isNumber(peek(0))) {
          RUNTIME_ERROR("operand must be a number");
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
          RUNTIME_ERROR("operands must be two numbers or two strings");
        }
        break;
      }

      case OpCode::Subtract: BINARY_NUMERIC(numberValue, -); break;
      case OpCode::Multiply: BINARY_NUMERIC(numberValue, *); break;
      case OpCode::Divide:   BINARY_NUMERIC(numberValue, /); break;

      case OpCode::Modulo: {
        if (!isNumber(peek(0)) || !isNumber(peek(1))) {
          RUNTIME_ERROR("operands must be numbers");
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

      case OpCode::Call: {
        int argCount = READ_BYTE();
        STORE_IP();
        if (!callValue(peek(argCount), argCount)) {
          return InterpretResult::RuntimeError;
        }
        LOAD_FRAME();
        break;
      }

      case OpCode::Return: {
        Value result = pop();
        frameCount_--;
        if (frameCount_ == 0) {
          pop();  // The script function itself.
          return InterpretResult::Ok;
        }
        // Discard the callee's whole frame, then hand back the result.
        stackTop_ = frame->slots;
        push(result);
        LOAD_FRAME();
        break;
      }
    }
  }

#undef BINARY_NUMERIC
#undef RUNTIME_ERROR
#undef LOAD_FRAME
#undef STORE_IP
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_BYTE
}

InterpretResult VM::interpret(const char* source) {
  ObjFunction* function = compile(source);
  if (function == nullptr) return InterpretResult::CompileError;

  push(objValue(function));
  call(function, 0);
  return run();
}

}  // namespace rill
