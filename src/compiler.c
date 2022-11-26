#include "compiler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "debug.h"
#include "gc.h"
#include "memory.h"
#include "scanner.h"

bool debugPrintCode = false;

typedef struct {
  Token name;
  int depth;
  bool isCaptured;
} Local;

typedef struct {
  uint8_t index;
  bool isLocal;
} Upvalue;

typedef enum {
  TYPE_FUNCTION,
  TYPE_INITIALIZER,
  TYPE_METHOD,
  TYPE_SCRIPT,
} FunctionType;

typedef struct Compiler {
  struct Compiler* enclosing;
  ObjFunction* function;
  FunctionType type;

  Local locals[UINT8_COUNT];
  int localCount;
  Upvalue upvalues[UINT8_COUNT];
  int scopeDepth;
} Compiler;

typedef struct ClassCompiler {
  struct ClassCompiler* enclosing;
  bool hasSuperclass;
} ClassCompiler;

typedef struct {
  FILE* fout;
  FILE* ferr;
  GC* gc;
  void (*prevMarkRoots)(GC*, void*);
  void* prevMarkRootsArg;
  void (*prevFixWeak)(void*);
  void* prevFixWeakArg;
  Table* strings;
  Scanner scanner;
  Compiler* currentCompiler;
  ClassCompiler* currentClass;
  Token current;
  Token previous;
  bool hadError;
  bool panicMode;
} Parser;

// clang-format off
typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT, // =
  PREC_OR,         // or
  PREC_AND,        // and
  PREC_EQUALITY,   // == !=
  PREC_COMPARISON, // < > <= >=
  PREC_TERM,       // + -
  PREC_FACTOR,     // * /
  PREC_UNARY,      // ! -
  PREC_CALL,       // . ()
  PREC_PRIMARY,
} Precedence;
// clang-format on

typedef void (*ParseFn)(Parser* parser, bool canAssign);

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

static Chunk* currentChunk(Parser* parser) {
  return &parser->currentCompiler->function->chunk;
}

static void errorAt(Parser* parser, Token* token, const char* message) {
  if (parser->panicMode) {
    return;
  }
  parser->panicMode = true;
  fprintf(parser->ferr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(parser->ferr, " at end");
  } else if (token->type == TOKEN_ERROR) {
    // Nothing.
  } else {
    fprintf(parser->ferr, " at '%.*s'", token->length, token->start);
  }

  fprintf(parser->ferr, ": %s\n", message);
  parser->hadError = true;
}

static void error(Parser* parser, const char* message) {
  errorAt(parser, &parser->previous, message);
}

static void errorAtCurrent(Parser* parser, const char* message) {
  errorAt(parser, &parser->current, message);
}

static void advance(Parser* parser) {
  parser->previous = parser->current;

  for (;;) {
    parser->current = scanToken(&parser->scanner);
    if (parser->current.type != TOKEN_ERROR) {
      break;
    }

    errorAtCurrent(parser, parser->current.start);
  }
}

static void consume(
    Parser* parser, TokenType type, const char* message) {
  if (parser->current.type == type) {
    advance(parser);
    return;
  }

  errorAtCurrent(parser, message);
}

static bool check(Parser* parser, TokenType type) {
  return parser->current.type == type;
}

static bool match(Parser* parser, TokenType type) {
  if (!check(parser, type)) {
    return false;
  }
  advance(parser);
  return true;
}

static void emitByte(Parser* parser, uint8_t byte) {
  writeChunk(
      parser->gc, currentChunk(parser), byte, parser->previous.line);
}

static void emitBytes(Parser* parser, uint8_t byte1, uint8_t byte2) {
  emitByte(parser, byte1);
  emitByte(parser, byte2);
}

static void emitLoop(Parser* parser, int loopStart) {
  emitByte(parser, OP_LOOP);

  int offset = currentChunk(parser)->count - loopStart + 2;
  // GCOV_EXCL_START
  if (offset > UINT16_MAX) {
    error(parser, "Loop body too large.");
  }
  // GCOV_EXCL_STOP

  emitByte(parser, (offset >> 8) & 0xff);
  emitByte(parser, offset & 0xff);
}

static int emitJump(Parser* parser, uint8_t instruction) {
  emitByte(parser, instruction);
  emitByte(parser, 0xff);
  emitByte(parser, 0xff);
  return currentChunk(parser)->count - 2;
}

static void emitReturn(Parser* parser) {
  if (parser->currentCompiler->type == TYPE_INITIALIZER) {
    emitBytes(parser, OP_GET_LOCAL, 0);
  } else {
    emitByte(parser, OP_NIL);
  }

  emitByte(parser, OP_RETURN);
}

static uint8_t makeConstant(Parser* parser, Value value) {
  int constant = addConstant(parser->gc, currentChunk(parser), value);
  // GCOV_EXCL_START
  if (constant > UINT8_MAX) {
    error(parser, "Too many constants in one chunk.");
    return 0;
  }
  // GCOV_EXCL_STOP

  return (uint8_t)constant;
}

static void emitConstant(Parser* parser, Value value) {
  emitBytes(parser, OP_CONSTANT, makeConstant(parser, value));
}

static void patchJump(Parser* parser, int offset) {
  // -2 to adjust for the bytecode for the jump offset itself.
  int jump = currentChunk(parser)->count - offset - 2;

  // GCOV_EXCL_START
  if (jump > UINT16_MAX) {
    error(parser, "Too much code to jump over.");
  }
  // GCOV_EXCL_STOP

  currentChunk(parser)->code[offset] = (jump >> 8) & 0xff;
  currentChunk(parser)->code[offset + 1] = jump & 0xff;
}

static void initCompiler(
    Parser* parser, Compiler* compiler, FunctionType type) {
  compiler->enclosing = parser->currentCompiler;
  compiler->function = NULL;
  compiler->type = type;
  compiler->localCount = 0;
  compiler->scopeDepth = 0;
  compiler->function = newFunction(parser->gc);
  parser->currentCompiler = compiler;
  if (type != TYPE_SCRIPT) {
    parser->currentCompiler->function->name =
        copyString(parser->gc, parser->strings, parser->previous.start,
            parser->previous.length);
  }

  Local* local = &parser->currentCompiler
                      ->locals[parser->currentCompiler->localCount++];
  local->depth = 0;
  local->isCaptured = false;
  if (type != TYPE_FUNCTION) {
    local->name.start = "this";
    local->name.length = 4;
  } else {
    local->name.start = "";
    local->name.length = 0;
  }
}

static ObjFunction* endCompiler(Parser* parser) {
  emitReturn(parser);
  ObjFunction* function = parser->currentCompiler->function;

  // GCOV_EXCL_START
  if (debugPrintCode && !parser->hadError) {
    disassembleChunk(parser->ferr, currentChunk(parser),
        !IS_NIL(function->name) ? strChars(&function->name)
                                : "<script>");
  }
  // GCOV_EXCL_STOP

  parser->currentCompiler = parser->currentCompiler->enclosing;
  return function;
}

static void beginScope(Parser* parser) {
  parser->currentCompiler->scopeDepth++;
}

static void endScope(Parser* parser) {
  parser->currentCompiler->scopeDepth--;

  while (parser->currentCompiler->localCount > 0 &&
      parser->currentCompiler
              ->locals[parser->currentCompiler->localCount - 1]
              .depth > parser->currentCompiler->scopeDepth) {
    if (parser->currentCompiler
            ->locals[parser->currentCompiler->localCount - 1]
            .isCaptured) {
      emitByte(parser, OP_CLOSE_UPVALUE);
    } else {
      emitByte(parser, OP_POP);
    }
    parser->currentCompiler->localCount--;
  }
}

static void parsePrecedence(Parser* parser, Precedence precedence);
static ParseRule* getRule(TokenType type);
static void expression(Parser* parser);
static void declaration(Parser* parser);
static void statement(Parser* parser);

static uint8_t identifierConstant(Parser* parser, Token* name) {
  return makeConstant(parser,
      copyString(
          parser->gc, parser->strings, name->start, name->length));
}

static bool identifiersEqual(Token* a, Token* b) {
  if (a->length != b->length) {
    return false;
  }
  return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(
    Parser* parser, Compiler* compiler, Token* name) {
  for (int i = compiler->localCount - 1; i >= 0; i--) {
    Local* local = &compiler->locals[i];
    if (identifiersEqual(name, &local->name)) {
      if (local->depth == -1) {
        error(parser,
            "Can't read local variable in its own initializer.");
      }
      return i;
    }
  }

  return -1;
}

static int addUpvalue(
    Parser* parser, Compiler* compiler, uint8_t index, bool isLocal) {
  int upvalueCount = compiler->function->upvalueCount;

  for (int i = 0; i < upvalueCount; i++) {
    Upvalue* upvalue = &compiler->upvalues[i];
    if (upvalue->index == index && upvalue->isLocal == isLocal) {
      return i;
    }
  }

  // GCOV_EXCL_START
  if (upvalueCount == UINT8_COUNT) {
    error(parser, "Too many closure variables in function.");
    return 0;
  }
  // GCOV_EXCL_STOP

  compiler->upvalues[upvalueCount].isLocal = isLocal;
  compiler->upvalues[upvalueCount].index = index;
  return compiler->function->upvalueCount++;
}

static int resolveUpvalue(
    Parser* parser, Compiler* compiler, Token* name) {
  if (compiler->enclosing == NULL) {
    return -1;
  }

  int local = resolveLocal(parser, compiler->enclosing, name);
  if (local != -1) {
    compiler->enclosing->locals[local].isCaptured = true;
    return addUpvalue(parser, compiler, (uint8_t)local, true);
  }

  int upvalue = resolveUpvalue(parser, compiler->enclosing, name);
  if (upvalue != -1) {
    return addUpvalue(parser, compiler, (uint8_t)upvalue, false);
  }

  return -1;
}

static void addLocal(Parser* parser, Token name) {
  // GCOV_EXCL_START
  if (parser->currentCompiler->localCount == UINT8_COUNT) {
    error(parser, "Too many local variables in function.");
    return;
  }
  // GCOV_EXCL_STOP

  Local* local = &parser->currentCompiler
                      ->locals[parser->currentCompiler->localCount++];
  local->name = name;
  local->depth = -1;
  local->isCaptured = false;
}

static void declareVariable(Parser* parser) {
  if (parser->currentCompiler->scopeDepth == 0) {
    return;
  }

  Token* name = &parser->previous;
  for (int i = parser->currentCompiler->localCount - 1; i >= 0; i--) {
    Local* local = &parser->currentCompiler->locals[i];
    if (local->depth != -1 &&
        local->depth < parser->currentCompiler->scopeDepth) {
      break;
    }

    if (identifiersEqual(name, &local->name)) {
      error(parser, "Already a variable with this name in this scope.");
    }
  }

  addLocal(parser, *name);
}

static uint8_t parseVariable(Parser* parser, const char* errorMessage) {
  consume(parser, TOKEN_IDENTIFIER, errorMessage);

  declareVariable(parser);
  if (parser->currentCompiler->scopeDepth > 0) {
    return 0;
  }

  return identifierConstant(parser, &parser->previous);
}

static void markInitialized(Parser* parser) {
  if (parser->currentCompiler->scopeDepth == 0) {
    return;
  }
  parser->currentCompiler
      ->locals[parser->currentCompiler->localCount - 1]
      .depth = parser->currentCompiler->scopeDepth;
}

static void defineVariable(Parser* parser, uint8_t global) {
  if (parser->currentCompiler->scopeDepth > 0) {
    markInitialized(parser);
    return;
  }

  emitBytes(parser, OP_DEFINE_GLOBAL, global);
}

static uint8_t argumentList(Parser* parser) {
  uint8_t argCount = 0;
  if (!check(parser, TOKEN_RIGHT_PAREN)) {
    do {
      expression(parser);
      // GCOV_EXCL_START
      if (argCount == 255) {
        error(parser, "Can't have more than 255 arguments.");
      }
      // GCOV_EXCL_STOP
      argCount++;
    } while (match(parser, TOKEN_COMMA));
  }
  consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
  return argCount;
}

static void and_(Parser* parser, bool canAssign) {
  (void)canAssign;

  int endJump = emitJump(parser, OP_JUMP_IF_FALSE);

  emitByte(parser, OP_POP);
  parsePrecedence(parser, PREC_AND);

  patchJump(parser, endJump);
}

static void binary(Parser* parser, bool canAssign) {
  (void)canAssign;

  TokenType operatorType = parser->previous.type;
  ParseRule* rule = getRule(operatorType);
  parsePrecedence(parser, (Precedence)(rule->precedence + 1));

  switch (operatorType) {
    case TOKEN_BANG_EQUAL: emitBytes(parser, OP_EQUAL, OP_NOT); break;
    case TOKEN_EQUAL_EQUAL: emitByte(parser, OP_EQUAL); break;
    case TOKEN_GREATER: emitByte(parser, OP_GREATER); break;
    case TOKEN_GREATER_EQUAL: emitBytes(parser, OP_LESS, OP_NOT); break;
    case TOKEN_LESS: emitByte(parser, OP_LESS); break;
    case TOKEN_LESS_EQUAL: emitBytes(parser, OP_GREATER, OP_NOT); break;
    case TOKEN_PLUS: emitByte(parser, OP_ADD); break;
    case TOKEN_MINUS: emitByte(parser, OP_SUBTRACT); break;
    case TOKEN_STAR: emitByte(parser, OP_MULTIPLY); break;
    case TOKEN_SLASH: emitByte(parser, OP_DIVIDE); break;
    default: return; // GCOV_EXCL_LINE: Unreachable.
  }
}

static void call(Parser* parser, bool canAssign) {
  (void)canAssign;

  uint8_t argCount = argumentList(parser);
  emitBytes(parser, OP_CALL, argCount);
}

static void dot(Parser* parser, bool canAssign) {
  consume(parser, TOKEN_IDENTIFIER, "Expect property name after '.'.");
  uint8_t name = identifierConstant(parser, &parser->previous);

  if (canAssign && match(parser, TOKEN_EQUAL)) {
    expression(parser);
    emitBytes(parser, OP_SET_PROPERTY, name);
  } else if (match(parser, TOKEN_LEFT_PAREN)) {
    uint8_t argCount = argumentList(parser);
    emitBytes(parser, OP_INVOKE, name);
    emitByte(parser, argCount);
  } else {
    emitBytes(parser, OP_GET_PROPERTY, name);
  }
}

static void literal(Parser* parser, bool canAssign) {
  (void)canAssign;

  switch (parser->previous.type) {
    case TOKEN_FALSE: emitByte(parser, OP_FALSE); break;
    case TOKEN_NIL: emitByte(parser, OP_NIL); break;
    case TOKEN_TRUE: emitByte(parser, OP_TRUE); break;
    default: return; // GCOV_EXCL_LINE: Unreachable.
  }
}

static void grouping(Parser* parser, bool canAssign) {
  (void)canAssign;

  expression(parser);
  consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(Parser* parser, bool canAssign) {
  (void)canAssign;

  double value = strtod(parser->previous.start, NULL);
  emitConstant(parser, NUMBER_VAL(value));
}

static void or_(Parser* parser, bool canAssign) {
  (void)canAssign;

  int elseJump = emitJump(parser, OP_JUMP_IF_FALSE);
  int endJump = emitJump(parser, OP_JUMP);

  patchJump(parser, elseJump);
  emitByte(parser, OP_POP);

  parsePrecedence(parser, PREC_OR);
  patchJump(parser, endJump);
}

static void string(Parser* parser, bool canAssign) {
  (void)canAssign;

  emitConstant(parser,
      copyString(parser->gc, parser->strings,
          parser->previous.start + 1, parser->previous.length - 2));
}

static void namedVariable(Parser* parser, Token name, bool canAssign) {
  uint8_t getOp, setOp;
  int arg = resolveLocal(parser, parser->currentCompiler, &name);
  if (arg != -1) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  } else if ((arg = resolveUpvalue(
                  parser, parser->currentCompiler, &name)) != -1) {
    getOp = OP_GET_UPVALUE;
    setOp = OP_SET_UPVALUE;
  } else {
    arg = identifierConstant(parser, &name);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  }

  if (canAssign && match(parser, TOKEN_EQUAL)) {
    expression(parser);
    emitBytes(parser, setOp, (uint8_t)arg);
  } else {
    emitBytes(parser, getOp, (uint8_t)arg);
  }
}

static void variable(Parser* parser, bool canAssign) {
  namedVariable(parser, parser->previous, canAssign);
}

static Token syntheticToken(const char* text) {
  Token token;
  token.start = text;
  token.length = (int)strlen(text);
  return token;
}

static void super_(Parser* parser, bool canAssign) {
  (void)canAssign;

  if (parser->currentClass == NULL) {
    error(parser, "Can't use 'super' outside of a class.");
  } else if (!parser->currentClass->hasSuperclass) {
    error(parser, "Can't use 'super' in a class with no superclass.");
  }

  consume(parser, TOKEN_DOT, "Expect '.' after 'super'.");
  consume(parser, TOKEN_IDENTIFIER, "Expect superclass method name.");
  uint8_t name = identifierConstant(parser, &parser->previous);

  namedVariable(parser, syntheticToken("this"), false);
  if (match(parser, TOKEN_LEFT_PAREN)) {
    uint8_t argCount = argumentList(parser);
    namedVariable(parser, syntheticToken("super"), false);
    emitBytes(parser, OP_SUPER_INVOKE, name);
    emitByte(parser, argCount);
  } else {
    namedVariable(parser, syntheticToken("super"), false);
    emitBytes(parser, OP_GET_SUPER, name);
  }
}

static void this_(Parser* parser, bool canAssign) {
  (void)canAssign;

  if (parser->currentClass == NULL) {
    error(parser, "Can't use 'this' outside of a class.");
    return;
  }

  variable(parser, false);
}

static void unary(Parser* parser, bool canAssign) {
  (void)canAssign;

  TokenType operatorType = parser->previous.type;

  // Compile the operand.
  parsePrecedence(parser, PREC_UNARY);

  // Emit the operator instruction.
  switch (operatorType) {
    case TOKEN_BANG: emitByte(parser, OP_NOT); break;
    case TOKEN_MINUS: emitByte(parser, OP_NEGATE); break;
    default: return; // GCOV_EXCL_LINE: Unreachable.
  }
}

// clang-format off
ParseRule rules[] = {
  [TOKEN_LEFT_PAREN]    = { grouping, call,   PREC_CALL },
  [TOKEN_RIGHT_PAREN]   = { NULL,     NULL,   PREC_NONE },
  [TOKEN_LEFT_BRACE]    = { NULL,     NULL,   PREC_NONE },
  [TOKEN_RIGHT_BRACE]   = { NULL,     NULL,   PREC_NONE },
  [TOKEN_COMMA]         = { NULL,     NULL,   PREC_NONE },
  [TOKEN_DOT]           = { NULL,     dot,    PREC_CALL },
  [TOKEN_MINUS]         = { unary,    binary, PREC_TERM },
  [TOKEN_PLUS]          = { NULL,     binary, PREC_TERM },
  [TOKEN_SEMICOLON]     = { NULL,     NULL,   PREC_NONE },
  [TOKEN_SLASH]         = { NULL,     binary, PREC_FACTOR },
  [TOKEN_STAR]          = { NULL,     binary, PREC_FACTOR },
  [TOKEN_BANG]          = { unary,    NULL,   PREC_NONE },
  [TOKEN_BANG_EQUAL]    = { NULL,     binary, PREC_EQUALITY },
  [TOKEN_EQUAL]         = { NULL,     NULL,   PREC_NONE },
  [TOKEN_EQUAL_EQUAL]   = { NULL,     binary, PREC_EQUALITY },
  [TOKEN_GREATER]       = { NULL,     binary, PREC_COMPARISON },
  [TOKEN_GREATER_EQUAL] = { NULL,     binary, PREC_COMPARISON },
  [TOKEN_LESS]          = { NULL,     binary, PREC_COMPARISON },
  [TOKEN_LESS_EQUAL]    = { NULL,     binary, PREC_COMPARISON },
  [TOKEN_IDENTIFIER]    = { variable, NULL,   PREC_NONE },
  [TOKEN_STRING]        = { string,   NULL,   PREC_NONE },
  [TOKEN_NUMBER]        = { number,   NULL,   PREC_NONE },
  [TOKEN_AND]           = { NULL,     and_,   PREC_AND },
  [TOKEN_CLASS]         = { NULL,     NULL,   PREC_NONE },
  [TOKEN_ELSE]          = { NULL,     NULL,   PREC_NONE },
  [TOKEN_FALSE]         = { literal,  NULL,   PREC_NONE },
  [TOKEN_FOR]           = { NULL,     NULL,   PREC_NONE },
  [TOKEN_FUN]           = { NULL,     NULL,   PREC_NONE },
  [TOKEN_IF]            = { NULL,     NULL,   PREC_NONE },
  [TOKEN_NIL]           = { literal,  NULL,   PREC_NONE },
  [TOKEN_OR]            = { NULL,     or_,    PREC_OR },
  [TOKEN_PRINT]         = { NULL,     NULL,   PREC_NONE },
  [TOKEN_RETURN]        = { NULL,     NULL,   PREC_NONE },
  [TOKEN_SUPER]         = { super_,   NULL,   PREC_NONE },
  [TOKEN_THIS]          = { this_,    NULL,   PREC_NONE },
  [TOKEN_TRUE]          = { literal,  NULL,   PREC_NONE },
  [TOKEN_VAR]           = { NULL,     NULL,   PREC_NONE },
  [TOKEN_WHILE]         = { NULL,     NULL,   PREC_NONE },
  [TOKEN_ERROR]         = { NULL,     NULL,   PREC_NONE },
  [TOKEN_EOF]           = { NULL,     NULL,   PREC_NONE },
};
// clang-format on

static void parsePrecedence(Parser* parser, Precedence precedence) {
  advance(parser);
  ParseFn prefixRule = getRule(parser->previous.type)->prefix;
  if (prefixRule == NULL) {
    error(parser, "Expect expression.");
    return;
  }

  bool canAssign = precedence <= PREC_ASSIGNMENT;
  prefixRule(parser, canAssign);

  while (precedence <= getRule(parser->current.type)->precedence) {
    advance(parser);
    ParseFn infixRule = getRule(parser->previous.type)->infix;
    infixRule(parser, canAssign);
  }

  if (canAssign && match(parser, TOKEN_EQUAL)) {
    error(parser, "Invalid assignment target.");
  }
}

static ParseRule* getRule(TokenType type) {
  return &rules[type];
}

static void expression(Parser* parser) {
  parsePrecedence(parser, PREC_ASSIGNMENT);
}

static void block(Parser* parser) {
  while (
      !check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
    declaration(parser);
  }

  consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(Parser* parser, FunctionType type) {
  Compiler compiler;
  initCompiler(parser, &compiler, type);
  beginScope(parser);

  consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after function name.");
  if (!check(parser, TOKEN_RIGHT_PAREN)) {
    do {
      parser->currentCompiler->function->arity++;
      // GCOV_EXCL_START
      if (parser->currentCompiler->function->arity > 255) {
        errorAtCurrent(parser, "Can't have more than 255 parameters.");
      }
      // GCOV_EXCL_STOP
      uint8_t constant =
          parseVariable(parser, "Expect parameter name.");
      defineVariable(parser, constant);
    } while (match(parser, TOKEN_COMMA));
  }
  consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
  consume(parser, TOKEN_LEFT_BRACE, "Expect '{' before function body.");
  block(parser);

  ObjFunction* function = endCompiler(parser);
  emitBytes(
      parser, OP_CLOSURE, makeConstant(parser, OBJ_VAL(function)));

  for (int i = 0; i < function->upvalueCount; i++) {
    emitByte(parser, compiler.upvalues[i].isLocal ? 1 : 0);
    emitByte(parser, compiler.upvalues[i].index);
  }
}

static void method(Parser* parser) {
  consume(parser, TOKEN_IDENTIFIER, "Expect method name.");
  uint8_t constant = identifierConstant(parser, &parser->previous);

  FunctionType type = TYPE_METHOD;
  if (parser->previous.length == 4 &&
      memcmp(parser->previous.start, "init", 4) == 0) {
    type = TYPE_INITIALIZER;
  }
  function(parser, type);
  emitBytes(parser, OP_METHOD, constant);
}

static void classDeclaration(Parser* parser) {
  consume(parser, TOKEN_IDENTIFIER, "Expect class name.");
  Token className = parser->previous;
  uint8_t nameConstant = identifierConstant(parser, &parser->previous);
  declareVariable(parser);

  emitBytes(parser, OP_CLASS, nameConstant);
  defineVariable(parser, nameConstant);

  ClassCompiler classCompiler;
  classCompiler.hasSuperclass = false;
  classCompiler.enclosing = parser->currentClass;
  parser->currentClass = &classCompiler;

  if (match(parser, TOKEN_LESS)) {
    consume(parser, TOKEN_IDENTIFIER, "Expect superclass name.");
    variable(parser, false);

    if (identifiersEqual(&className, &parser->previous)) {
      error(parser, "A class can't inherit from itself.");
    }

    beginScope(parser);
    addLocal(parser, syntheticToken("super"));
    defineVariable(parser, 0);

    namedVariable(parser, className, false);
    emitByte(parser, OP_INHERIT);
    classCompiler.hasSuperclass = true;
  }

  namedVariable(parser, className, false);
  consume(parser, TOKEN_LEFT_BRACE, "Expect '{' before class body.");
  while (
      !check(parser, TOKEN_RIGHT_BRACE) && !check(parser, TOKEN_EOF)) {
    method(parser);
  }
  consume(parser, TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
  emitByte(parser, OP_POP);

  if (classCompiler.hasSuperclass) {
    endScope(parser);
  }

  parser->currentClass = parser->currentClass->enclosing;
}

static void funDeclaration(Parser* parser) {
  uint8_t global = parseVariable(parser, "Expect function name.");
  markInitialized(parser);
  function(parser, TYPE_FUNCTION);
  defineVariable(parser, global);
}

static void varDeclaration(Parser* parser) {
  uint8_t global = parseVariable(parser, "Expect variable name.");

  if (match(parser, TOKEN_EQUAL)) {
    expression(parser);
  } else {
    emitByte(parser, OP_NIL);
  }
  consume(parser, TOKEN_SEMICOLON,
      "Expect ';' after variable declaration.");

  defineVariable(parser, global);
}

static void expressionStatement(Parser* parser) {
  expression(parser);
  consume(parser, TOKEN_SEMICOLON, "Expect ';' after expression.");
  emitByte(parser, OP_POP);
}

static void forStatement(Parser* parser) {
  beginScope(parser);
  consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
  if (match(parser, TOKEN_SEMICOLON)) {
    // No initializer.
  } else if (match(parser, TOKEN_VAR)) {
    varDeclaration(parser);
  } else {
    expressionStatement(parser);
  }

  int loopStart = currentChunk(parser)->count;
  int exitJump = -1;
  if (!match(parser, TOKEN_SEMICOLON)) {
    expression(parser);
    consume(
        parser, TOKEN_SEMICOLON, "Expect ';' after loop condition.");

    // Jump out of the loop if the condition is false.
    exitJump = emitJump(parser, OP_JUMP_IF_FALSE);
    emitByte(parser, OP_POP); // Condition.
  }

  if (!match(parser, TOKEN_RIGHT_PAREN)) {
    int bodyJump = emitJump(parser, OP_JUMP);
    int incrementStart = currentChunk(parser)->count;
    expression(parser);
    emitByte(parser, OP_POP);
    consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

    emitLoop(parser, loopStart);
    loopStart = incrementStart;
    patchJump(parser, bodyJump);
  }

  statement(parser);
  emitLoop(parser, loopStart);

  if (exitJump != -1) {
    patchJump(parser, exitJump);
    emitByte(parser, OP_POP); // Condition.
  }

  endScope(parser);
}

static void ifStatement(Parser* parser) {
  consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
  expression(parser);
  consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  int thenJump = emitJump(parser, OP_JUMP_IF_FALSE);
  emitByte(parser, OP_POP);
  statement(parser);

  int elseJump = emitJump(parser, OP_JUMP);

  patchJump(parser, thenJump);
  emitByte(parser, OP_POP);

  if (match(parser, TOKEN_ELSE)) {
    statement(parser);
  }
  patchJump(parser, elseJump);
}

static void printStatement(Parser* parser) {
  expression(parser);
  consume(parser, TOKEN_SEMICOLON, "Expect ';' after value.");
  emitByte(parser, OP_PRINT);
}

static void returnStatement(Parser* parser) {
  if (parser->currentCompiler->type == TYPE_SCRIPT) {
    error(parser, "Can't return from top-level code.");
  }

  if (match(parser, TOKEN_SEMICOLON)) {
    emitReturn(parser);
  } else {
    if (parser->currentCompiler->type == TYPE_INITIALIZER) {
      error(parser, "Can't return a value from an initializer.");
    }

    expression(parser);
    consume(parser, TOKEN_SEMICOLON, "Expect ';' after return value.");
    emitByte(parser, OP_RETURN);
  }
}

static void whileStatement(Parser* parser) {
  int loopStart = currentChunk(parser)->count;
  consume(parser, TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
  expression(parser);
  consume(parser, TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  int exitJump = emitJump(parser, OP_JUMP_IF_FALSE);
  emitByte(parser, OP_POP);
  statement(parser);
  emitLoop(parser, loopStart);

  patchJump(parser, exitJump);
  emitByte(parser, OP_POP);
}

static void synchronize(Parser* parser) {
  parser->panicMode = false;

  while (parser->current.type != TOKEN_EOF) {
    if (parser->previous.type == TOKEN_SEMICOLON) {
      return;
    }
    switch (parser->current.type) {
      case TOKEN_CLASS:
      case TOKEN_FUN:
      case TOKEN_VAR:
      case TOKEN_FOR:
      case TOKEN_IF:
      case TOKEN_WHILE:
      case TOKEN_PRINT:
      case TOKEN_RETURN: return;

      default:; // Do nothing.
    }

    advance(parser);
  }
}

static void declaration(Parser* parser) {
  if (match(parser, TOKEN_CLASS)) {
    classDeclaration(parser);
  } else if (match(parser, TOKEN_FUN)) {
    funDeclaration(parser);
  } else if (match(parser, TOKEN_VAR)) {
    varDeclaration(parser);
  } else {
    statement(parser);
  }

  if (parser->panicMode) {
    synchronize(parser);
  }
}

static void statement(Parser* parser) {
  if (match(parser, TOKEN_PRINT)) {
    printStatement(parser);
  } else if (match(parser, TOKEN_FOR)) {
    forStatement(parser);
  } else if (match(parser, TOKEN_IF)) {
    ifStatement(parser);
  } else if (match(parser, TOKEN_RETURN)) {
    returnStatement(parser);
  } else if (match(parser, TOKEN_WHILE)) {
    whileStatement(parser);
  } else if (match(parser, TOKEN_LEFT_BRACE)) {
    beginScope(parser);
    block(parser);
    endScope(parser);
  } else {
    expressionStatement(parser);
  }
}

static void compilerMarkRoots(struct GC* gc, void* arg) {
  Parser* parser = (Parser*)arg;
  Compiler* compiler = parser->currentCompiler;
  while (compiler != NULL) {
    markObject(gc, (Obj*)compiler->function);
    compiler = compiler->enclosing;
  }

  if (parser->prevMarkRoots) {
    parser->prevMarkRoots(gc, parser->prevMarkRootsArg);
  }
}

static void compilerFixWeak(void* arg) {
  Parser* parser = (Parser*)arg;
  tableRemoveWhite(parser->strings);

  if (parser->prevFixWeak) {
    parser->prevFixWeak(parser->prevFixWeakArg);
  }
}

static void setupGC(Parser* parser, GC* gc, Table* strings) {
  parser->gc = gc;
  parser->prevMarkRoots = gc->markRoots;
  parser->prevMarkRootsArg = gc->markRootsArg;
  parser->prevFixWeak = gc->fixWeak;
  parser->prevFixWeakArg = gc->fixWeakArg;

  gc->markRoots = compilerMarkRoots;
  gc->markRootsArg = parser;
  if (gc->fixWeak != (void (*)(void*))tableRemoveWhite ||
      gc->fixWeakArg != strings) {
    gc->fixWeak = compilerFixWeak;
    gc->fixWeakArg = parser;
  }
}

static void restoreGC(Parser* parser) {
  GC* gc = parser->gc;
  gc->markRoots = parser->prevMarkRoots;
  gc->markRootsArg = parser->prevMarkRootsArg;
  gc->fixWeak = parser->prevFixWeak;
  gc->fixWeakArg = parser->prevFixWeakArg;
}

ObjFunction* compile(FILE* fout, FILE* ferr, const char* source, GC* gc,
    Table* strings) {
  Parser parser;
  parser.fout = fout;
  parser.ferr = ferr;
  setupGC(&parser, gc, strings);
  parser.strings = strings;
  parser.currentCompiler = NULL;
  parser.currentClass = NULL;
  parser.hadError = false;
  parser.panicMode = false;

  initScanner(&parser.scanner, source);
  Compiler compiler;
  initCompiler(&parser, &compiler, TYPE_SCRIPT);
  advance(&parser);

  while (!match(&parser, TOKEN_EOF)) {
    declaration(&parser);
  }

  ObjFunction* function = endCompiler(&parser);
  restoreGC(&parser);
  return parser.hadError ? NULL : function;
}
