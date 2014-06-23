/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sw=4 et tw=0 ft=c:
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_Opcodes_h
#define vm_Opcodes_h

#include "mozilla/Attributes.h"

#include <stddef.h>

/*
 * JavaScript operation bytecodes.  Add a new bytecode by claiming one of the
 * JSOP_UNUSED* here or by extracting the first unused opcode from
 * FOR_EACH_TRAILING_UNUSED_OPCODE and updating js::detail::LastDefinedOpcode
 * below.
 *
 * When changing the bytecode, don't forget to update XDR_BYTECODE_VERSION in
 * vm/Xdr.h!
 *
 * Includers must define a macro with the following form:
 *
 * #define MACRO(op,val,name,image,length,nuses,ndefs,format) ...
 *
 * Then simply use FOR_EACH_OPCODE(MACRO) to invoke MACRO for every opcode.
 * Selected arguments can be expanded in initializers.
 *
 * Field        Description
 * op           Bytecode name, which is the JSOp enumerator name
 * value        Bytecode value, which is the JSOp enumerator value
 * name         C string containing name for disassembler
 * image        C string containing "image" for pretty-printer, null if ugly
 * length       Number of bytes including any immediate operands
 * nuses        Number of stack slots consumed by bytecode, -1 if variadic
 * ndefs        Number of stack slots produced by bytecode, -1 if variadic
 * format       Bytecode plus immediate operand encoding format
 *
 * This file is best viewed with 128 columns:
12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678
 */

/*
 * SpiderMonkey bytecode categorization (as used in generated documentation):
 *
 * [Index]
 *   [Statements]
 *     Jumps
 *     Switch Statement
 *     For-In Statement
 *     With Statement
 *     Exception Handling
 *     Function
 *     Generator
 *     Debugger
 *   [Variables and Scopes]
 *     Variables
 *     Free Variables
 *     Local Variables
 *     Aliased Variables
 *     Intrinsics
 *     Block-local Scope
 *     This
 *     Arguments
 *   [Operator]
 *     Comparison Operators
 *     Arithmetic Operators
 *     Bitwise Logical Operators
 *     Bitwise Shift Operators
 *     Logical Operators
 *     Special Operators
 *     Stack Operations
 *   [Literals]
 *     Constants
 *     Object
 *     Array
 *     RegExp
 *   [Other]
 */

#define FOR_EACH_OPCODE(macro) \
    /* legend:  op      val   name          image       len use def  format */ \
    /*
     * No operation is performed.
     *   Category: Other
     *   Operands:
     *   Stack: =>
     */ \
    macro(JSOP_NOP,       0,  "nop",        NULL,         1,  0,  0, JOF_BYTE) \
    \
    /* Long-standing JavaScript bytecodes. */ \
    /*
     * Pushes 'undefined' onto the stack.
     *   Category: Literals
     *   Type: Constants
     *   Operands:
     *   Stack: => undefined
     */ \
    macro(JSOP_UNDEFINED, 1,  js_undefined_str, "",       1,  0,  1, JOF_BYTE) \
    macro(JSOP_UNUSED2,   2,  "unused2",    NULL,         1,  1,  0, JOF_BYTE) \
    /*
     * Pops the top of stack value, converts it to an object, and adds a
     * 'DynamicWithObject' wrapping that object to the scope chain.
     *
     * There is a matching JSOP_LEAVEWITH instruction later. All name
     * lookups between the two that may need to consult the With object
     * are deoptimized.
     *   Category: Statements
     *   Type: With Statement
     *   Operands: uint32_t staticWithIndex
     *   Stack: val =>
     */ \
    macro(JSOP_ENTERWITH, 3,  "enterwith",  NULL,         5,  1,  0, JOF_OBJECT) \
    /*
     * Pops the scope chain object pushed by JSOP_ENTERWITH.
     *   Category: Statements
     *   Type: With Statement
     *   Operands:
     *   Stack: =>
     */ \
    macro(JSOP_LEAVEWITH, 4,  "leavewith",  NULL,         1,  0,  0, JOF_BYTE) \
    /*
     * Pops the top of stack value as 'rval', stops interpretation of current
     * script and returns 'rval'.
     *   Category: Statements
     *   Type: Function
     *   Operands:
     *   Stack: rval =>
     */ \
    macro(JSOP_RETURN,    5,  "return",     NULL,         1,  1,  0, JOF_BYTE) \
    /*
     * Jumps to a 32-bit offset from the current bytecode.
     *   Category: Statements
     *   Type: Jumps
     *   Operands: int32_t offset
     *   Stack: =>
     */ \
    macro(JSOP_GOTO,      6,  "goto",       NULL,         5,  0,  0, JOF_JUMP) \
    /*
     * Pops the top of stack value, converts it into a boolean, if the result is
     * 'false', jumps to a 32-bit offset from the current bytecode.
     *
     * The idea is that a sequence like
     * JSOP_ZERO; JSOP_ZERO; JSOP_EQ; JSOP_IFEQ; JSOP_RETURN;
     * reads like a nice linear sequence that will execute the return.
     *   Category: Statements
     *   Type: Jumps
     *   Operands: int32_t offset
     *   Stack: cond =>
     */ \
    macro(JSOP_IFEQ,      7,  "ifeq",       NULL,         5,  1,  0, JOF_JUMP|JOF_DETECTING) \
    /*
     * Pops the top of stack value, converts it into a boolean, if the result is
     * 'true', jumps to a 32-bit offset from the current bytecode.
     *   Category: Statements
     *   Type: Jumps
     *   Operands: int32_t offset
     *   Stack: cond =>
     */ \
    macro(JSOP_IFNE,      8,  "ifne",       NULL,         5,  1,  0, JOF_JUMP) \
    \
    /*
     * Pushes the 'arguments' object for the current function activation.
     *
     * If 'JSScript' is not marked 'needsArgsObj', then a
     * JS_OPTIMIZED_ARGUMENTS magic value is pushed. Otherwise, a proper
     * arguments object is constructed and pushed.
     *
     * This opcode requires that the function does not have rest parameter.
     *   Category: Variables and Scopes
     *   Type: Arguments
     *   Operands:
     *   Stack: => arguments
     */ \
    macro(JSOP_ARGUMENTS, 9,  "arguments",  NULL,         1,  0,  1, JOF_BYTE) \
    \
    /*
     * Swaps the top two values on the stack. This is useful for things like
     * post-increment/decrement.
     *   Category: Operator
     *   Type: Stack Operations
     *   Operands:
     *   Stack: v1, v2 => v2, v1
     */ \
    macro(JSOP_SWAP,      10, "swap",       NULL,         1,  2,  2, JOF_BYTE) \
    /*
     * Pops the top 'n' values from the stack.
     *   Category: Operator
     *   Type: Stack Operations
     *   Operands: uint16_t n
     *   Stack: v[n-1], ..., v[1], v[0] =>
     *   nuses: n
     */ \
    macro(JSOP_POPN,      11, "popn",       NULL,         3, -1,  0, JOF_UINT16) \
    \
    /* More long-standing bytecodes. */ \
    /*
     * Pushes a copy of the top value on the stack.
     *   Category: Operator
     *   Type: Stack Operations
     *   Operands:
     *   Stack: v => v, v
     */ \
    macro(JSOP_DUP,       12, "dup",        NULL,         1,  1,  2, JOF_BYTE) \
    /*
     * Duplicates the top two values on the stack.
     *   Category: Operator
     *   Type: Stack Operations
     *   Operands:
     *   Stack: v1, v2 => v1, v2, v1, v2
     */ \
    macro(JSOP_DUP2,      13, "dup2",       NULL,         1,  2,  4, JOF_BYTE) \
    /*
     * Defines a readonly property on the frame's current variables-object (the
     * scope object on the scope chain designated to receive new variables).
     *   Category: Variables and Scopes
     *   Type: Variables
     *   Operands: uint32_t nameIndex
     *   Stack: val => val
     */ \
    macro(JSOP_SETCONST,  14, "setconst",   NULL,         5,  1,  1, JOF_ATOM|JOF_NAME|JOF_SET) \
    /*
     * Pops the top two values 'lval' and 'rval' from the stack, then pushes
     * the result of the operation applied to the two operands, converting
     * both to 32-bit signed integers if necessary.
     *   Category: Operator
     *   Type: Bitwise Logical Operators
     *   Operands:
     *   Stack: lval, rval => (lval OP rval)
     */ \
    macro(JSOP_BITOR,     15, "bitor",      "|",          1,  2,  1, JOF_BYTE|JOF_LEFTASSOC|JOF_ARITH) \
    macro(JSOP_BITXOR,    16, "bitxor",     "^",          1,  2,  1, JOF_BYTE|JOF_LEFTASSOC|JOF_ARITH) \
    macro(JSOP_BITAND,    17, "bitand",     "&",          1,  2,  1, JOF_BYTE|JOF_LEFTASSOC|JOF_ARITH) \
    /*
     * Pops the top two values from the stack and pushes the result of
     * comparing them.
     *   Category: Operator
     *   Type: Comparison Operators
     *   Operands:
     *   Stack: lval, rval => (lval OP rval)
     */ \
    macro(JSOP_EQ,        18, "eq",         "==",         1,  2,  1, JOF_BYTE|JOF_LEFTASSOC|JOF_ARITH|JOF_DETECTING) \
    macro(JSOP_NE,        19, "ne",         "!=",         1,  2,  1, JOF_BYTE|JOF_LEFTASSOC|JOF_ARITH|JOF_DETECTING) \
    macro(JSOP_LT,        20, "lt",         "<",          1,  2,  1, JOF_BYTE|JOF_LEFTASSOC|JOF_ARITH) \
    macro(JSOP_LE,        21, "le",         "<=",         1,  2,  1, JOF_BYTE|JOF_LEFTASSOC|JOF_ARITH) \
    macro(JSOP_GT,        22, "gt",         ">",          1,  2,  1, JOF_BYTE|JOF_LEFTASSOC|JOF_ARITH) \
    macro(JSOP_GE,        23, "ge",         ">=",         1,  2,  1, JOF_BYTE|JOF_LEFTASSOC|JOF_ARITH) \
    /*
     * Pops the top two values 'lval' and 'rval' from the stack, then pushes
     * the result of the operation applied to the operands.
     *   Category: Operator
     *   Type: Bitwise Shift Operators
     *   Operands:
     *   Stack: lval, rval => (lval OP rval)
     */ \
    macro(JSOP_LSH,       24, "lsh",        "<<",         1,  2,  1, JOF_BYTE|JOF_LEFTASSOC|JOF_ARITH) \
    macro(JSOP_RSH,       25, "rsh",        ">>",         1,  2,  1, JOF_BYTE|JOF_LEFTASSOC|JOF_ARITH) \
    /*
     * Pops the top two values 'lval' and 'rval' from the stack, then pushes
     * 'lval >>> rval'.
     *   Category: Operator
     *   Type: Bitwise Shift Operators
     *   Operands:
     *   Stack: lval, rval => (lval >>> rval)
     */ \
    macro(JSOP_URSH,      26, "ursh",       ">>>",        1,  2,  1, JOF_BYTE|JOF_LEFTASSOC|JOF_ARITH) \
    /*
     * Pops the top two values 'lval' and 'rval' from the stack, then pushes
     * the result of 'lval + rval'.
     *   Category: Operator
     *   Type: Arithmetic Operators
     *   Operands:
     *   Stack: lval, rval => (lval + rval)
     */ \
    macro(JSOP_ADD,       27, "add",        "+",          1,  2,  1, JOF_BYTE|JOF_LEFTASSOC|JOF_ARITH) \
    /*
     * Pops the top two values 'lval' and 'rval' from the stack, then pushes
     * the result of applying the arithmetic operation to them.
     *   Category: Operator
     *   Type: Arithmetic Operators
     *   Operands:
     *   Stack: lval, rval => (lval OP rval)
     */ \
    macro(JSOP_SUB,       28, "sub",        "-",          1,  2,  1, JOF_BYTE|JOF_LEFTASSOC|JOF_ARITH) \
    macro(JSOP_MUL,       29, "mul",        "*",          1,  2,  1, JOF_BYTE|JOF_LEFTASSOC|JOF_ARITH) \
    macro(JSOP_DIV,       30, "div",        "/",          1,  2,  1, JOF_BYTE|JOF_LEFTASSOC|JOF_ARITH) \
    macro(JSOP_MOD,       31, "mod",        "%",          1,  2,  1, JOF_BYTE|JOF_LEFTASSOC|JOF_ARITH) \
    /*
     * Pops the value 'val' from the stack, then pushes '!val'.
     *   Category: Operator
     *   Type: Logical Operators
     *   Operands:
     *   Stack: val => (!val)
     */ \
    macro(JSOP_NOT,       32, "not",        "!",          1,  1,  1, JOF_BYTE|JOF_ARITH|JOF_DETECTING) \
    /*
     * Pops the value 'val' from the stack, then pushes '~val'.
     *   Category: Operator
     *   Type: Bitwise Logical Operators
     *   Operands:
     *   Stack: val => (~val)
     */ \
    macro(JSOP_BITNOT,    33, "bitnot",     "~",          1,  1,  1, JOF_BYTE|JOF_ARITH) \
    /*
     * Pops the value 'val' from the stack, then pushes '-val'.
     *   Category: Operator
     *   Type: Arithmetic Operators
     *   Operands:
     *   Stack: val => (-val)
     */ \
    macro(JSOP_NEG,       34, "neg",        "- ",         1,  1,  1, JOF_BYTE|JOF_ARITH) \
    /*
     * Pops the value 'val' from the stack, then pushes '+val'.
     * ('+val' is the value converted to a number.)
     *   Category: Operator
     *   Type: Arithmetic Operators
     *   Operands:
     *   Stack: val => (+val)
     */ \
    macro(JSOP_POS,       35, "pos",        "+ ",         1,  1,  1, JOF_BYTE|JOF_ARITH) \
    /*
     * Looks up name on the scope chain and deletes it, pushes 'true' onto the
     * stack if succeeded (if the property was present and deleted or if the
     * property wasn't present in the first place), 'false' if not.
     *
     * Strict mode code should never contain this opcode.
     *   Category: Variables and Scopes
     *   Type: Variables
     *   Operands: uint32_t nameIndex
     *   Stack: => succeeded
     */ \
    macro(JSOP_DELNAME,   36, "delname",    NULL,         5,  0,  1, JOF_ATOM|JOF_NAME) \
    /*
     * Pops the top of stack value, deletes property from it, pushes 'true' onto
     * the stack if succeeded, 'false' if not.
     *   Category: Literals
     *   Type: Object
     *   Operands: uint32_t nameIndex
     *   Stack: obj => succeeded
     */ \
    macro(JSOP_DELPROP,   37, "delprop",    NULL,         5,  1,  1, JOF_ATOM|JOF_PROP) \
    /*
     * Pops the top two values on the stack as 'propval' and 'obj',
     * deletes 'propval' property from 'obj', pushes 'true'  onto the stack if
     * succeeded, 'false' if not.
     *   Category: Literals
     *   Type: Object
     *   Operands:
     *   Stack: obj, propval => succeeded
     */ \
    macro(JSOP_DELELEM,   38, "delelem",    NULL,         1,  2,  1, JOF_BYTE |JOF_ELEM) \
    /*
     * Pops the value 'val' from the stack, then pushes 'typeof val'.
     *   Category: Operator
     *   Type: Special Operators
     *   Operands:
     *   Stack: val => (typeof val)
     */ \
    macro(JSOP_TYPEOF,    39, js_typeof_str,NULL,         1,  1,  1, JOF_BYTE|JOF_DETECTING) \
    /*
     * Pops the top value on the stack and pushes 'undefined'.
     *   Category: Operator
     *   Type: Special Operators
     *   Operands:
     *   Stack: val => undefined
     */ \
    macro(JSOP_VOID,      40, js_void_str,  NULL,         1,  1,  1, JOF_BYTE) \
    \
    /*
     * spreadcall variant of JSOP_CALL.
     *
     * Invokes 'callee' with 'this' and 'args', pushes the return value onto
     * the stack.
     *
     * 'args' is an Array object which contains actual arguments.
     *   Category: Statements
     *   Type: Function
     *   Operands:
     *   Stack: callee, this, args => rval
     */ \
    macro(JSOP_SPREADCALL,41, "spreadcall", NULL,         1,  3,  1, JOF_BYTE|JOF_INVOKE|JOF_TYPESET) \
    /*
     * spreadcall variant of JSOP_NEW
     *
     * Invokes 'callee' as a constructor with 'this' and 'args', pushes the
     * return value onto the stack.
     *   Category: Statements
     *   Type: Function
     *   Operands:
     *   Stack: callee, this, args => rval
     */ \
    macro(JSOP_SPREADNEW, 42, "spreadnew",  NULL,         1,  3,  1, JOF_BYTE|JOF_INVOKE|JOF_TYPESET) \
    /*
     * spreadcall variant of JSOP_EVAL
     *
     * Invokes 'eval' with 'args' and pushes the return value onto the stack.
     *
     * If 'eval' in global scope is not original one, invokes the function
     * with 'this' and 'args', and pushes return value onto the stack.
     *   Category: Statements
     *   Type: Function
     *   Operands:
     *   Stack: callee, this, args => rval
     */ \
    macro(JSOP_SPREADEVAL,43, "spreadeval", NULL,         1,  3,  1, JOF_BYTE|JOF_INVOKE|JOF_TYPESET) \
    \
    /*
     * Duplicates the Nth value from the top onto the stack.
     *   Category: Operator
     *   Type: Stack Operations
     *   Operands: uint24_t n
     *   Stack: v[n], v[n-1], ..., v[1], v[0] =>
     *          v[n], v[n-1], ..., v[1], v[0], v[n]
     */ \
    macro(JSOP_DUPAT,     44, "dupat",      NULL,         4,  0,  1,  JOF_UINT24) \
    \
    macro(JSOP_UNUSED45,  45, "unused45",   NULL,         1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED46,  46, "unused46",   NULL,         1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED47,  47, "unused47",   NULL,         1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED48,  48, "unused48",   NULL,         1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED49,  49, "unused49",   NULL,         1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED50,  50, "unused50",   NULL,         1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED51,  51, "unused51",   NULL,         1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED52,  52, "unused52",   NULL,         1,  0,  0,  JOF_BYTE) \
    \
    /*
     * Pops the top of stack value, pushes property of it onto the stack.
     *   Category: Literals
     *   Type: Object
     *   Operands: uint32_t nameIndex
     *   Stack: obj => obj[name]
     */ \
    macro(JSOP_GETPROP,   53, "getprop",    NULL,         5,  1,  1, JOF_ATOM|JOF_PROP|JOF_TYPESET|JOF_TMPSLOT3) \
    /*
     * Pops the top two values on the stack as 'val' and 'obj', sets property of
     * 'obj' as 'val', pushes 'obj' onto the stack.
     *   Category: Literals
     *   Type: Object
     *   Operands: uint32_t nameIndex
     *   Stack: obj, val => val
     */ \
    macro(JSOP_SETPROP,   54, "setprop",    NULL,         5,  2,  1, JOF_ATOM|JOF_PROP|JOF_SET|JOF_DETECTING) \
    /*
     * Pops the top two values on the stack as 'propval' and 'obj', pushes
     * 'propval' property of 'obj' onto the stack.
     *   Category: Literals
     *   Type: Object
     *   Operands:
     *   Stack: obj, propval => obj[propval]
     */ \
    macro(JSOP_GETELEM,   55, "getelem",    NULL,         1,  2,  1, JOF_BYTE |JOF_ELEM|JOF_TYPESET|JOF_LEFTASSOC) \
    /*
     * Pops the top three values on the stack as 'val', 'propval' and 'obj',
     * sets 'propval' property of 'obj' as 'val', pushes 'obj' onto the
     * stack.
     *   Category: Literals
     *   Type: Object
     *   Operands:
     *   Stack: obj, propval, val => val
     */ \
    macro(JSOP_SETELEM,   56, "setelem",    NULL,         1,  3,  1, JOF_BYTE |JOF_ELEM|JOF_SET|JOF_DETECTING) \
    macro(JSOP_UNUSED57,  57, "unused57",   NULL,         1,  0,  0, JOF_BYTE) \
    /*
     * Invokes 'callee' with 'this' and 'args', pushes return value onto the
     * stack.
     *   Category: Statements
     *   Type: Function
     *   Operands: uint16_t argc
     *   Stack: callee, this, args[0], ..., args[argc-1] => rval
     *   nuses: (argc+2)
     */ \
    macro(JSOP_CALL,      58, "call",       NULL,         3, -1,  1, JOF_UINT16|JOF_INVOKE|JOF_TYPESET) \
    /*
     * Looks up name on the scope chain and pushes its value onto the stack.
     *   Category: Variables and Scopes
     *   Type: Variables
     *   Operands: uint32_t nameIndex
     *   Stack: => val
     */ \
    macro(JSOP_NAME,      59, "name",       NULL,         5,  0,  1, JOF_ATOM|JOF_NAME|JOF_TYPESET) \
    /*
     * Pushes numeric constant onto the stack.
     *   Category: Literals
     *   Type: Constants
     *   Operands: uint32_t constIndex
     *   Stack: => val
     */ \
    macro(JSOP_DOUBLE,    60, "double",     NULL,         5,  0,  1, JOF_DOUBLE) \
    /*
     * Pushes string constant onto the stack.
     *   Category: Literals
     *   Type: Constants
     *   Operands: uint32_t atomIndex
     *   Stack: => string
     */ \
    macro(JSOP_STRING,    61, "string",     NULL,         5,  0,  1, JOF_ATOM) \
    /*
     * Pushes '0' onto the stack.
     *   Category: Literals
     *   Type: Constants
     *   Operands:
     *   Stack: => 0
     */ \
    macro(JSOP_ZERO,      62, "zero",       "0",          1,  0,  1, JOF_BYTE) \
    /*
     * Pushes '1' onto the stack.
     *   Category: Literals
     *   Type: Constants
     *   Operands:
     *   Stack: => 1
     */ \
    macro(JSOP_ONE,       63, "one",        "1",          1,  0,  1, JOF_BYTE) \
    /*
     * Pushes 'null' onto the stack.
     *   Category: Literals
     *   Type: Constants
     *   Operands:
     *   Stack: => null
     */ \
    macro(JSOP_NULL,      64, js_null_str,  js_null_str,  1,  0,  1, JOF_BYTE) \
    /*
     * Pushes 'this' value for current stack frame onto the stack.
     *   Category: Variables and Scopes
     *   Type: This
     *   Operands:
     *   Stack: => this
     */ \
    macro(JSOP_THIS,      65, js_this_str,  js_this_str,  1,  0,  1, JOF_BYTE) \
    /*
     * Pushes boolean value onto the stack.
     *   Category: Literals
     *   Type: Constants
     *   Operands:
     *   Stack: => true/false
     */ \
    macro(JSOP_FALSE,     66, js_false_str, js_false_str, 1,  0,  1, JOF_BYTE) \
    macro(JSOP_TRUE,      67, js_true_str,  js_true_str,  1,  0,  1, JOF_BYTE) \
    /*
     * Converts the top of stack value into a boolean, if the result is 'true',
     * jumps to a 32-bit offset from the current bytecode.
     *   Category: Statements
     *   Type: Jumps
     *   Operands: int32_t offset
     *   Stack: cond => cond
     */ \
    macro(JSOP_OR,        68, "or",         NULL,         5,  1,  1, JOF_JUMP|JOF_DETECTING|JOF_LEFTASSOC) \
    /*
     * Converts the top of stack value into a boolean, if the result is 'false',
     * jumps to a 32-bit offset from the current bytecode.
     *   Category: Statements
     *   Type: Jumps
     *   Operands: int32_t offset
     *   Stack: cond => cond
     */ \
    macro(JSOP_AND,       69, "and",        NULL,         5,  1,  1, JOF_JUMP|JOF_DETECTING|JOF_LEFTASSOC) \
    \
    /*
     * Pops the top of stack value as 'i', if 'low <= i <= high',
     * jumps to a 32-bit offset: 'offset[i - low]' from the current bytecode,
     * jumps to a 32-bit offset: 'len' from the current bytecode if not.
     *
     * This opcode has variable length.
     *   Category: Statements
     *   Type: Switch Statement
     *   Operands: int32_t len, int32_t low, int32_t high,
     *             int32_t offset[0], ..., int32_t offset[high-low]
     *   Stack: i =>
     *   len: len
     */ \
    macro(JSOP_TABLESWITCH, 70, "tableswitch", NULL,     -1,  1,  0,  JOF_TABLESWITCH|JOF_DETECTING) \
    \
    /*
     * Prologue emitted in scripts expected to run once, which deoptimizes code
     * if it executes multiple times.
     *   Category: Statements
     *   Type: Function
     *   Operands:
     *   Stack: =>
     */ \
    macro(JSOP_RUNONCE,   71, "runonce",    NULL,         1,  0,  0,  JOF_BYTE) \
    \
    /* New, infallible/transitive identity ops. */ \
    /*
     * Pops the top two values from the stack, then pushes the result of
     * applying the operator to the two values.
     *   Category: Operator
     *   Type: Comparison Operators
     *   Operands:
     *   Stack: lval, rval => (lval OP rval)
     */ \
    macro(JSOP_STRICTEQ,  72, "stricteq",   "===",        1,  2,  1, JOF_BYTE|JOF_DETECTING|JOF_LEFTASSOC|JOF_ARITH) \
    macro(JSOP_STRICTNE,  73, "strictne",   "!==",        1,  2,  1, JOF_BYTE|JOF_DETECTING|JOF_LEFTASSOC|JOF_ARITH) \
    \
    /*
     * Sometimes web pages do 'o.Item(i) = j'. This is not an early SyntaxError,
     * for web compatibility. Instead we emit JSOP_SETCALL after the function
     * call, an opcode that always throws.
     *   Category: Statements
     *   Type: Function
     *   Operands:
     *   Stack: =>
     */ \
    macro(JSOP_SETCALL,   74, "setcall",    NULL,         1,  0,  0, JOF_BYTE) \
    \
    /*
     * Sets up a for-in or for-each-in loop using the JSITER_* flag bits in
     * this op's uint8_t immediate operand. It pops the top of stack value as
     * 'val' and pushes 'iter' which is an iterator for 'val'.
     *   Category: Statements
     *   Type: For-In Statement
     *   Operands: uint8_t flags
     *   Stack: val => iter
     */ \
    macro(JSOP_ITER,      75, "iter",       NULL,         2,  1,  1,  JOF_UINT8) \
    /*
     * Stores the next iterated value into 'cx->iterValue' and pushes 'true'
     * onto the stack if another value is available, and 'false' otherwise.
     * It is followed immediately by JSOP_IFNE.
     *
     * This opcode increments iterator cursor if current iteration has
     * JSITER_FOREACH flag.
     *   Category: Statements
     *   Type: For-In Statement
     *   Operands:
     *   Stack: iter => iter, cond
     */ \
    macro(JSOP_MOREITER,  76, "moreiter",   NULL,         1,  1,  2,  JOF_BYTE) \
    /*
     * Pushes the value produced by the preceding JSOP_MOREITER operation
     * ('cx->iterValue') onto the stack
     *
     * This opcode increments iterator cursor if current iteration does not have
     * JSITER_FOREACH flag.
     *   Category: Statements
     *   Type: For-In Statement
     *   Operands:
     *   Stack: iter => iter, val
     */ \
    macro(JSOP_ITERNEXT,  77, "iternext",   "<next>",     1,  0,  1,  JOF_BYTE) \
    /*
     * Exits a for-in loop by popping the iterator object from the stack and
     * closing it.
     *   Category: Statements
     *   Type: For-In Statement
     *   Operands:
     *   Stack: iter =>
     */ \
    macro(JSOP_ENDITER,   78, "enditer",    NULL,         1,  1,  0,  JOF_BYTE) \
    \
    /*
     * Invokes 'callee' with 'this' and 'args', pushes return value onto the
     * stack.
     *
     * This is for 'f.apply'.
     *   Category: Statements
     *   Type: Function
     *   Operands: uint16_t argc
     *   Stack: callee, this, args[0], ..., args[argc-1] => rval
     *   nuses: (argc+2)
     */ \
    macro(JSOP_FUNAPPLY,  79, "funapply",   NULL,         3, -1,  1,  JOF_UINT16|JOF_INVOKE|JOF_TYPESET) \
    \
    /*
     * Pushes deep-cloned object literal or singleton onto the stack.
     *   Category: Literals
     *   Type: Object
     *   Operands: uint32_t objectIndex
     *   Stack: => obj
     */ \
    macro(JSOP_OBJECT,    80, "object",     NULL,         5,  0,  1,  JOF_OBJECT) \
    \
    /*
     * Pops the top value off the stack.
     *   Category: Operator
     *   Type: Stack Operations
     *   Operands:
     *   Stack: v =>
     */ \
    macro(JSOP_POP,       81, "pop",        NULL,         1,  1,  0,  JOF_BYTE) \
    \
    /*
     * Invokes 'callee' as a constructor with 'this' and 'args', pushes return
     * value onto the stack.
     *   Category: Statements
     *   Type: Function
     *   Operands: uint16_t argc
     *   Stack: callee, this, args[0], ..., args[argc-1] => rval
     *   nuses: (argc+2)
     */ \
    macro(JSOP_NEW,       82, js_new_str,   NULL,         3, -1,  1,  JOF_UINT16|JOF_INVOKE|JOF_TYPESET) \
    /*
     * Pops the top three values on the stack as 'iterator', 'index' and 'obj',
     * iterates over 'iterator' and stores the iteration values as 'index + i'
     * elements of 'obj', pushes 'obj' and 'index + iteration count' onto the
     * stack.
     *
     * This opcode is used in Array literals with spread and spreadcall
     * arguments as well as in destructing assignment with rest element.
     *   Category: Literals
     *   Type: Array
     *   Operands:
     *   Stack: obj, index, iterator => obj, (index + iteration count)
     */ \
    macro(JSOP_SPREAD,    83, "spread",     NULL,         1,  3,  2,  JOF_BYTE|JOF_ELEM|JOF_SET) \
    \
    /*
     * Fast get op for function arguments and local variables.
     *
     * Pushes 'arguments[argno]' onto the stack.
     *   Category: Variables and Scopes
     *   Type: Arguments
     *   Operands: uint16_t argno
     *   Stack: => arguments[argno]
     */ \
    macro(JSOP_GETARG,    84, "getarg",     NULL,         3,  0,  1,  JOF_QARG |JOF_NAME) \
    /*
     * Fast set op for function arguments and local variables.
     *
     * Sets 'arguments[argno]' as the top of stack value.
     *   Category: Variables and Scopes
     *   Type: Arguments
     *   Operands: uint16_t argno
     *   Stack: v => v
     */ \
    macro(JSOP_SETARG,    85, "setarg",     NULL,         3,  1,  1,  JOF_QARG |JOF_NAME|JOF_SET) \
    /*
     * Pushes the value of local variable onto the stack.
     *   Category: Variables and Scopes
     *   Type: Local Variables
     *   Operands: uint32_t localno
     *   Stack: => val
     */ \
    macro(JSOP_GETLOCAL,  86,"getlocal",    NULL,         4,  0,  1,  JOF_LOCAL|JOF_NAME) \
    /*
     * Stores the top stack value to the given local.
     *   Category: Variables and Scopes
     *   Type: Local Variables
     *   Operands: uint32_t localno
     *   Stack: v => v
     */ \
    macro(JSOP_SETLOCAL,  87,"setlocal",    NULL,         4,  1,  1,  JOF_LOCAL|JOF_NAME|JOF_SET|JOF_DETECTING) \
    \
    /*
     * Pushes unsigned 16-bit int immediate integer operand onto the stack.
     *   Category: Literals
     *   Type: Constants
     *   Operands: uint16_t val
     *   Stack: => val
     */ \
    macro(JSOP_UINT16,    88, "uint16",     NULL,         3,  0,  1,  JOF_UINT16) \
    \
    /* Object and array literal support. */ \
    /*
     * Pushes newly created object onto the stack.
     *
     * This opcode takes the kind of initializer (JSProto_Array or
     * JSProto_Object).
     *
     * This opcode has an extra byte so it can be exchanged with JSOP_NEWOBJECT
     * during emit.
     *   Category: Literals
     *   Type: Object
     *   Operands: uint8_t kind (, uint24_t extra)
     *   Stack: => obj
     */ \
    macro(JSOP_NEWINIT,   89, "newinit",    NULL,         5,  0,  1, JOF_UINT8) \
    /*
     * Pushes newly created array onto the stack.
     *
     * This opcode takes the final length, which is preallocated.
     *   Category: Literals
     *   Type: Array
     *   Operands: uint24_t length
     *   Stack: => obj
     */ \
    macro(JSOP_NEWARRAY,  90, "newarray",   NULL,         4,  0,  1, JOF_UINT24) \
    /*
     * Pushes newly created object onto the stack.
     *
     * This opcode takes an object with the final shape, which can be set at
     * the start and slots then filled in directly.
     *   Category: Literals
     *   Type: Object
     *   Operands: uint32_t baseobjIndex
     *   Stack: => obj
     */ \
    macro(JSOP_NEWOBJECT, 91, "newobject",  NULL,         5,  0,  1, JOF_OBJECT) \
    /*
     * A no-operation bytecode.
     *
     * Indicates the end of object/array initialization, and used for
     * Type-Inference, decompile, etc.
     *   Category: Literals
     *   Type: Object
     *   Operands:
     *   Stack: =>
     */ \
    macro(JSOP_ENDINIT,   92, "endinit",    NULL,         1,  0,  0, JOF_BYTE) \
    /*
     * Initialize a named property in an object literal, like '{a: x}'.
     *
     * Pops the top two values on the stack as 'val' and 'obj', defines
     * 'nameIndex' property of 'obj' as 'val', pushes 'obj' onto the stack.
     *   Category: Literals
     *   Type: Object
     *   Operands: uint32_t nameIndex
     *   Stack: obj, val => obj
     */ \
    macro(JSOP_INITPROP,  93, "initprop",   NULL,         5,  2,  1, JOF_ATOM|JOF_PROP|JOF_SET|JOF_DETECTING) \
    \
    /*
     * Initialize a numeric property in an object literal, like '{1: x}'.
     *
     * Pops the top three values on the stack as 'val', 'id' and 'obj', defines
     * 'id' property of 'obj' as 'val', pushes 'obj' onto the stack.
     *   Category: Literals
     *   Type: Object
     *   Operands:
     *   Stack: obj, id, val => obj
     */ \
    macro(JSOP_INITELEM,  94, "initelem",   NULL,         1,  3,  1, JOF_BYTE|JOF_ELEM|JOF_SET|JOF_DETECTING) \
    \
    /*
     * Pops the top three values on the stack as 'val', 'index' and 'obj', sets
     * 'index' property of 'obj' as 'val', pushes 'obj' and 'index + 1' onto
     * the stack.
     *
     * This opcode is used in Array literals with spread and spreadcall
     * arguments.
     *   Category: Literals
     *   Type: Array
     *   Operands:
     *   Stack: obj, index, val => obj, (index + 1)
     */ \
    macro(JSOP_INITELEM_INC,95, "initelem_inc", NULL,     1,  3,  2, JOF_BYTE|JOF_ELEM|JOF_SET) \
    \
    /*
     * Initialize an array element.
     *
     * Pops the top two values on the stack as 'val' and 'obj', sets 'index'
     * property of 'obj' as 'val', pushes 'obj' onto the stack.
     *   Category: Literals
     *   Type: Array
     *   Operands: uint24_t index
     *   Stack: obj, val => obj
     */ \
    macro(JSOP_INITELEM_ARRAY,96, "initelem_array", NULL, 4,  2,  1,  JOF_UINT24|JOF_ELEM|JOF_SET|JOF_DETECTING) \
    \
    /*
     * Initialize a getter in an object literal.
     *
     * Pops the top two values on the stack as 'val' and 'obj', defines getter
     * of 'obj' as 'val', pushes 'obj' onto the stack.
     *   Category: Literals
     *   Type: Object
     *   Operands: uint32_t nameIndex
     *   Stack: obj, val => obj
     */ \
    macro(JSOP_INITPROP_GETTER,  97, "initprop_getter",   NULL, 5,  2,  1, JOF_ATOM|JOF_PROP|JOF_SET|JOF_DETECTING) \
    /*
     * Initialize a setter in an object literal.
     *
     * Pops the top two values on the stack as 'val' and 'obj', defines setter
     * of 'obj' as 'val', pushes 'obj' onto the stack.
     *   Category: Literals
     *   Type: Object
     *   Operands: uint32_t nameIndex
     *   Stack: obj, val => obj
     */ \
    macro(JSOP_INITPROP_SETTER,  98, "initprop_setter",   NULL, 5,  2,  1, JOF_ATOM|JOF_PROP|JOF_SET|JOF_DETECTING) \
    /*
     * Initialize a numeric getter in an object literal like
     * '{get 2() {}}'.
     *
     * Pops the top three values on the stack as 'val', 'id' and 'obj', defines
     * 'id' getter of 'obj' as 'val', pushes 'obj' onto the stack.
     *   Category: Literals
     *   Type: Object
     *   Operands:
     *   Stack: obj, id, val => obj
     */ \
    macro(JSOP_INITELEM_GETTER,  99, "initelem_getter",   NULL, 1,  3,  1, JOF_BYTE|JOF_ELEM|JOF_SET|JOF_DETECTING) \
    /*
     * Initialize a numeric setter in an object literal like
     * '{set 2(v) {}}'.
     *
     * Pops the top three values on the stack as 'val', 'id' and 'obj', defines
     * 'id' setter of 'obj' as 'val', pushes 'obj' onto the stack.
     *   Category: Literals
     *   Type: Object
     *   Operands:
     *   Stack: obj, id, val => obj
     */ \
    macro(JSOP_INITELEM_SETTER, 100, "initelem_setter",   NULL, 1,  3,  1, JOF_BYTE|JOF_ELEM|JOF_SET|JOF_DETECTING) \
    \
    macro(JSOP_UNUSED101,  101, "unused101",   NULL,         1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED102,  102, "unused102",   NULL,         1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED103,  103, "unused103",   NULL,         1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED104,  104, "unused104",   NULL,         1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED105,  105, "unused105",   NULL,         1,  0,  0,  JOF_BYTE) \
    \
    /*
     * This opcode precedes every labeled statement. It's a no-op.
     *
     * 'offset' is the offset to the next instruction after this statement,
     * the one 'break LABEL;' would jump to. IonMonkey uses this.
     *   Category: Statements
     *   Type: Jumps
     *   Operands: int32_t offset
     *   Stack: =>
     */ \
    macro(JSOP_LABEL,     106,"label",     NULL,          5,  0,  0,  JOF_JUMP) \
    \
    macro(JSOP_UNUSED107, 107,"unused107",  NULL,         1,  0,  0,  JOF_BYTE) \
    \
    /*
     * Invokes 'callee' with 'this' and 'args', pushes return value onto the
     * stack.
     *
     * If 'callee' is determined to be the canonical 'Function.prototype.call'
     * function, then this operation is optimized to directly call 'callee'
     * with 'args[0]' as 'this', and the remaining arguments as formal args
     * to 'callee'.
     *
     * Like JSOP_FUNAPPLY but for 'f.call' instead of 'f.apply'.
     *   Category: Statements
     *   Type: Function
     *   Operands: uint16_t argc
     *   Stack: callee, this, args[0], ..., args[argc-1] => rval
     *   nuses: (argc+2)
     */ \
    macro(JSOP_FUNCALL,   108,"funcall",    NULL,         3, -1,  1, JOF_UINT16|JOF_INVOKE|JOF_TYPESET) \
    \
    /*
     * Another no-op.
     *
     * This opcode is the target of the backwards jump for some loop.
     *   Category: Statements
     *   Type: Jumps
     *   Operands:
     *   Stack: =>
     */ \
    macro(JSOP_LOOPHEAD,  109,"loophead",   NULL,         1,  0,  0,  JOF_BYTE) \
    \
    /* ECMA-compliant assignment ops. */ \
    /*
     * Looks up name on the scope chain and pushes the scope which contains
     * the name onto the stack. If not found, pushes global scope onto the
     * stack.
     *   Category: Variables and Scopes
     *   Type: Variables
     *   Operands: uint32_t nameIndex
     *   Stack: => scope
     */ \
    macro(JSOP_BINDNAME,  110,"bindname",   NULL,         5,  0,  1,  JOF_ATOM|JOF_NAME|JOF_SET) \
    /*
     * Pops a scope and value from the stack, assigns value to the given name,
     * and pushes the value back on the stack
     *   Category: Variables and Scopes
     *   Type: Variables
     *   Operands: uint32_t nameIndex
     *   Stack: scope, val => val
     */ \
    macro(JSOP_SETNAME,   111,"setname",    NULL,         5,  2,  1,  JOF_ATOM|JOF_NAME|JOF_SET|JOF_DETECTING) \
    \
    /* Exception handling ops. */ \
    /*
     * Pops the top of stack value as 'v', sets pending exception as 'v', then
     * raises error.
     *   Category: Statements
     *   Type: Exception Handling
     *   Operands:
     *   Stack: v =>
     */ \
    macro(JSOP_THROW,     112,js_throw_str, NULL,         1,  1,  0,  JOF_BYTE) \
    \
    /*
     * Pops the top two values 'id' and 'obj' from the stack, then pushes
     * 'id in obj'.  This will throw a 'TypeError' if 'obj' is not an object.
     *
     * Note that 'obj' is the top value.
     *   Category: Operator
     *   Type: Special Operators
     *   Operands:
     *   Stack: id, obj => (id in obj)
     */ \
    macro(JSOP_IN,        113,js_in_str,    js_in_str,    1,  2,  1, JOF_BYTE|JOF_LEFTASSOC) \
    /*
     * Pops the top two values 'obj' and 'ctor' from the stack, then pushes
     * 'obj instanceof ctor'.  This will throw a 'TypeError' if 'obj' is not an
     * object.
     *   Category: Operator
     *   Type: Special Operators
     *   Operands:
     *   Stack: obj, ctor => (obj instanceof ctor)
     */ \
    macro(JSOP_INSTANCEOF,114,js_instanceof_str,js_instanceof_str,1,2,1,JOF_BYTE|JOF_LEFTASSOC|JOF_TMPSLOT) \
    \
    /*
     * Invokes debugger.
     *   Category: Statements
     *   Type: Debugger
     *   Operands:
     *   Stack: =>
     */ \
    macro(JSOP_DEBUGGER,  115,"debugger",   NULL,         1,  0,  0, JOF_BYTE) \
    \
    /*
     * Pushes 'false' and next bytecode's PC onto the stack, and jumps to
     * a 32-bit offset from the current bytecode.
     *
     * This opcode is used for entering 'finally' block.
     *   Category: Statements
     *   Type: Exception Handling
     *   Operands: int32_t offset
     *   Stack: => false, (next bytecode's PC)
     */ \
    macro(JSOP_GOSUB,     116,"gosub",      NULL,         5,  0,  0,  JOF_JUMP) \
    /*
     * Pops the top two values on the stack as 'rval' and 'lval', converts
     * 'lval' into a boolean, raises error if the result is 'true',
     * jumps to a 32-bit absolute PC: 'rval' if 'false'.
     *
     * This opcode is used for returning from 'finally' block.
     *   Category: Statements
     *   Type: Exception Handling
     *   Operands:
     *   Stack: lval, rval =>
     */ \
    macro(JSOP_RETSUB,    117,"retsub",     NULL,         1,  2,  0,  JOF_BYTE) \
    \
    /* More exception handling ops. */ \
    /*
     * Pushes the current pending exception onto the stack and clears the
     * pending exception. This is only emitted at the beginning of code for a
     * catch-block, so it is known that an exception is pending. It is used to
     * implement catch-blocks and 'yield*'.
     *   Category: Statements
     *   Type: Exception Handling
     *   Operands:
     *   Stack: => exception
     */ \
    macro(JSOP_EXCEPTION, 118,"exception",  NULL,         1,  0,  1,  JOF_BYTE) \
    \
    /*
     * Embedded lineno to speedup 'pc->line' mapping.
     *   Category: Other
     *   Operands: uint32_t lineno
     *   Stack: =>
     */ \
    macro(JSOP_LINENO,    119,"lineno",     NULL,         3,  0,  0,  JOF_UINT16) \
    \
    /*
     * This no-op appears after the bytecode for EXPR in 'switch (EXPR) {...}'
     * if the switch cannot be optimized using JSOP_TABLESWITCH.
     * For a non-optimized switch statement like this:
     *
     *     switch (EXPR) {
     *       case V0:
     *         C0;
     *       ...
     *       default:
     *         D;
     *     }
     *
     * the bytecode looks like this:
     *
     *     (EXPR)
     *     condswitch
     *     (V0)
     *     case ->C0
     *     ...
     *     default ->D
     *     (C0)
     *     ...
     *     (D)
     *
     * Note that code for all case-labels is emitted first, then code for
     * the body of each case clause.
     *   Category: Statements
     *   Type: Switch Statement
     *   Operands:
     *   Stack: =>
     */ \
    macro(JSOP_CONDSWITCH,120,"condswitch", NULL,         1,  0,  0,  JOF_BYTE) \
    /*
     * Pops the top two values on the stack as 'rval' and 'lval', compare them
     * with '===', if the result is 'true', jumps to a 32-bit offset from the
     * current bytecode, re-pushes 'lval' onto the stack if 'false'.
     *   Category: Statements
     *   Type: Switch Statement
     *   Operands: int32_t offset
     *   Stack: lval, rval => lval(if lval !== rval)
     */ \
    macro(JSOP_CASE,      121,"case",       NULL,         5,  2,  1,  JOF_JUMP) \
    /*
     * This appears after all cases in a JSOP_CONDSWITCH, whether there is a
     * 'default:' label in the switch statement or not. Pop the switch operand
     * from the stack and jump to a 32-bit offset from the current bytecode.
     * offset from the current bytecode.
     *   Category: Statements
     *   Type: Switch Statement
     *   Operands: int32_t offset
     *   Stack: lval =>
     */ \
    macro(JSOP_DEFAULT,   122,"default",    NULL,         5,  1,  0,  JOF_JUMP) \
    \
    /* ECMA-compliant call to eval op. */ \
    /*
     * Invokes 'eval' with 'args' and pushes return value onto the stack.
     *
     * If 'eval' in global scope is not original one, invokes the function
     * with 'this' and 'args', and pushes return value onto the stack.
     *   Category: Statements
     *   Type: Function
     *   Operands: uint16_t argc
     *   Stack: callee, this, args[0], ..., args[argc-1] => rval
     *   nuses: (argc+2)
     */ \
    macro(JSOP_EVAL,      123,"eval",       NULL,         3, -1,  1, JOF_UINT16|JOF_INVOKE|JOF_TYPESET) \
    \
    macro(JSOP_UNUSED124,  124, "unused124", NULL,      1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED125,  125, "unused125", NULL,      1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED126,  126, "unused126", NULL,      1,  0,  0,  JOF_BYTE) \
    \
    /*
     * Defines the given function on the current scope.
     *
     * This is used for global scripts and also in some cases for function
     * scripts where use of dynamic scoping inhibits optimization.
     *   Category: Variables and Scopes
     *   Type: Variables
     *   Operands: uint32_t funcIndex
     *   Stack: =>
     */ \
    macro(JSOP_DEFFUN,    127,"deffun",     NULL,         5,  0,  0,  JOF_OBJECT) \
    /*
     * Defines the new binding on the frame's current variables-object (the
     * scope object on the scope chain designated to receive new variables) with
     * 'READONLY' attribute. The binding is *not* JSPROP_PERMANENT. See bug
     * 1019181 for the reason.
     *
     * This is used for global scripts and also in some cases for function
     * scripts where use of dynamic scoping inhibits optimization.
     *   Category: Variables and Scopes
     *   Type: Variables
     *   Operands: uint32_t nameIndex
     *   Stack: =>
     */ \
    macro(JSOP_DEFCONST,  128,"defconst",   NULL,         5,  0,  0,  JOF_ATOM) \
    /*
     * Defines the new binding on the frame's current variables-object (the
     * scope object on the scope chain designated to receive new variables).
     *
     * This is used for global scripts and also in some cases for function
     * scripts where use of dynamic scoping inhibits optimization.
     *   Category: Variables and Scopes
     *   Type: Variables
     *   Operands: uint32_t nameIndex
     *   Stack: =>
     */ \
    macro(JSOP_DEFVAR,    129,"defvar",     NULL,         5,  0,  0,  JOF_ATOM) \
    \
    /*
     * Pushes a closure for a named or anonymous function expression onto the
     * stack.
     *   Category: Statements
     *   Type: Function
     *   Operands: uint32_t funcIndex
     *   Stack: => obj
     */ \
    macro(JSOP_LAMBDA,    130, "lambda",    NULL,         5,  0,  1, JOF_OBJECT) \
    /*
     * Pops the top of stack value as 'this', pushes an arrow function with
     * 'this' onto the stack.
     *   Category: Statements
     *   Type: Function
     *   Operands: uint32_t funcIndex
     *   Stack: this => obj
     */ \
    macro(JSOP_LAMBDA_ARROW, 131, "lambda_arrow", NULL,   5,  1,  1, JOF_OBJECT) \
    \
    /*
     * Pushes current callee onto the stack.
     *
     * Used for named function expression self-naming, if lightweight.
     *   Category: Variables and Scopes
     *   Type: Arguments
     *   Operands:
     *   Stack: => callee
     */ \
    macro(JSOP_CALLEE,    132, "callee",    NULL,         1,  0,  1, JOF_BYTE) \
    \
    /*
     * Picks the nth element from the stack and moves it to the top of the
     * stack.
     *   Category: Operator
     *   Type: Stack Operations
     *   Operands: uint8_t n
     *   Stack: v[n], v[n-1], ..., v[1], v[0] => v[n-1], ..., v[1], v[0], v[n]
     */ \
    macro(JSOP_PICK,        133, "pick",      NULL,       2,  0,  0,  JOF_UINT8|JOF_TMPSLOT2) \
    \
    /*
     * This no-op appears at the top of the bytecode for a 'TryStatement'.
     *
     * Location information for catch/finally blocks is stored in a
     * side table, 'script->trynotes()'.
     *   Category: Statements
     *   Type: Exception Handling
     *   Operands:
     *   Stack: =>
     */ \
    macro(JSOP_TRY,         134,"try",        NULL,       1,  0,  0,  JOF_BYTE) \
    /*
     * This opcode has a def count of 2, but these values are already on the
     * stack (they're pushed by JSOP_GOSUB).
     *   Category: Statements
     *   Type: Exception Handling
     *   Operands:
     *   Stack: => false, (next bytecode's PC)
     */ \
    macro(JSOP_FINALLY,     135,"finally",    NULL,       1,  0,  2,  JOF_BYTE) \
    \
    /*
     * Pushes aliased variable onto the stack.
     *
     * An "aliased variable" is a var, let, or formal arg that is aliased.
     * Sources of aliasing include: nested functions accessing the vars of an
     * enclosing function, function statements that are conditionally executed,
     * 'eval', 'with', and 'arguments'. All of these cases require creating a
     * CallObject to own the aliased variable.
     *
     * An ALIASEDVAR opcode contains the following immediates:
     *  uint8 hops:  the number of scope objects to skip to find the ScopeObject
     *               containing the variable being accessed
     *  uint24 slot: the slot containing the variable in the ScopeObject (this
     *               'slot' does not include RESERVED_SLOTS).
     *   Category: Variables and Scopes
     *   Type: Aliased Variables
     *   Operands: uint8_t hops, uint24_t slot
     *   Stack: => aliasedVar
     */ \
    macro(JSOP_GETALIASEDVAR, 136,"getaliasedvar",NULL,      5,  0,  1,  JOF_SCOPECOORD|JOF_NAME|JOF_TYPESET) \
    /*
     * Sets aliased variable as the top of stack value.
     *   Category: Variables and Scopes
     *   Type: Aliased Variables
     *   Operands: uint8_t hops, uint24_t slot
     *   Stack: v => v
     */ \
    macro(JSOP_SETALIASEDVAR, 137,"setaliasedvar",NULL,      5,  1,  1,  JOF_SCOPECOORD|JOF_NAME|JOF_SET|JOF_DETECTING) \
    \
    macro(JSOP_UNUSED138,  138, "unused138",   NULL,         1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED139,  139, "unused139",   NULL,         1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED140,  140, "unused140",   NULL,         1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED141,  141, "unused141",   NULL,         1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED142,  142, "unused142",   NULL,         1,  0,  0,  JOF_BYTE) \
    \
    /*
     * Pushes the value of the intrinsic onto the stack.
     *
     * Intrinsic names are emitted instead of JSOP_*NAME ops when the
     * 'CompileOptions' flag 'selfHostingMode' is set.
     *
     * They are used in self-hosted code to access other self-hosted values and
     * intrinsic functions the runtime doesn't give client JS code access to.
     *   Category: Variables and Scopes
     *   Type: Intrinsics
     *   Operands: uint32_t nameIndex
     *   Stack: => intrinsic[name]
     */ \
    macro(JSOP_GETINTRINSIC,  143, "getintrinsic",  NULL, 5,  0,  1, JOF_ATOM|JOF_NAME|JOF_TYPESET) \
    /*
     * Pops the top two values on the stack as 'val' and 'scope', sets intrinsic
     * as 'val', and pushes 'val' onto the stack.
     *
     * 'scope' is not used.
     *   Category: Variables and Scopes
     *   Type: Intrinsics
     *   Operands: uint32_t nameIndex
     *   Stack: scope, val => val
     */ \
    macro(JSOP_SETINTRINSIC,  144, "setintrinsic",  NULL, 5,  2,  1, JOF_ATOM|JOF_NAME|JOF_SET|JOF_DETECTING) \
    /*
     * Pushes 'intrinsicHolder' onto the stack.
     *   Category: Variables and Scopes
     *   Type: Intrinsics
     *   Operands: uint32_t nameIndex
     *   Stack: => intrinsicHolder
     */ \
    macro(JSOP_BINDINTRINSIC, 145, "bindintrinsic", NULL, 5,  0,  1, JOF_ATOM|JOF_NAME|JOF_SET) \
    \
    /* Unused. */ \
    macro(JSOP_UNUSED146,     146,"unused146", NULL,      1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED147,     147,"unused147", NULL,      1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED148,     148,"unused148", NULL,      1,  0,  0,  JOF_BYTE) \
    \
    /*
     * Placeholder opcode used during bytecode generation. This never
     * appears in a finished script. FIXME: bug 473671.
     *   Category: Statements
     *   Type: Jumps
     *   Operands: int32_t offset
     *   Stack: =>
     */ \
    macro(JSOP_BACKPATCH,     149,"backpatch", NULL,      5,  0,  0,  JOF_JUMP) \
    macro(JSOP_UNUSED150,     150,"unused150", NULL,      1,  0,  0,  JOF_BYTE) \
    \
    /*
     * Pops the top of stack value as 'v', sets pending exception as 'v',
     * to trigger rethrow.
     *
     * This opcode is used in conditional catch clauses.
     *   Category: Statements
     *   Type: Exception Handling
     *   Operands:
     *   Stack: v =>
     */ \
    macro(JSOP_THROWING,      151,"throwing", NULL,       1,  1,  0,  JOF_BYTE) \
    \
    /*
     * Pops the top of stack value as 'rval', sets the return value in stack
     * frame as 'rval'.
     *   Category: Statements
     *   Type: Function
     *   Operands:
     *   Stack: rval =>
     */ \
    macro(JSOP_SETRVAL,       152,"setrval",    NULL,       1,  1,  0,  JOF_BYTE) \
    /*
     * Stops interpretation and returns value set by JSOP_SETRVAL. When not set,
     * returns 'undefined'.
     *
     * Also emitted at end of script so interpreter don't need to check if
     * opcode is still in script range.
     *   Category: Statements
     *   Type: Function
     *   Operands:
     *   Stack: =>
     */ \
    macro(JSOP_RETRVAL,       153,"retrval",    NULL,       1,  0,  0,  JOF_BYTE) \
    \
    /*
     * Looks up name on global scope and pushes its value onto the stack.
     *
     * Free variable references that must either be found on the global or a
     * ReferenceError.
     *   Category: Variables and Scopes
     *   Type: Free Variables
     *   Operands: uint32_t nameIndex
     *   Stack: => val
     */ \
    macro(JSOP_GETGNAME,      154,"getgname",  NULL,       5,  0,  1, JOF_ATOM|JOF_NAME|JOF_TYPESET|JOF_GNAME) \
    /*
     * Pops the top two values on the stack as 'val' and 'scope', sets property
     * of 'scope' as 'val' and pushes 'val' back on the stack.
     *
     * 'scope' should be the global scope.
     *   Category: Variables and Scopes
     *   Type: Free Variables
     *   Operands: uint32_t nameIndex
     *   Stack: scope, val => val
     */ \
    macro(JSOP_SETGNAME,      155,"setgname",  NULL,       5,  2,  1, JOF_ATOM|JOF_NAME|JOF_SET|JOF_DETECTING|JOF_GNAME) \
    \
    macro(JSOP_UNUSED156,  156, "unused156",   NULL,         1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED157,  157, "unused157",   NULL,         1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED158,  158, "unused158",   NULL,         1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED159,  159, "unused159",   NULL,         1,  0,  0,  JOF_BYTE) \
    \
    /*
     * Pushes a regular expression literal onto the stack.
     * It requires special "clone on exec" handling.
     *   Category: Literals
     *   Type: RegExp
     *   Operands: uint32_t regexpIndex
     *   Stack: => regexp
     */ \
    macro(JSOP_REGEXP,        160,"regexp",   NULL,       5,  0,  1, JOF_REGEXP) \
    \
    macro(JSOP_UNUSED161,     161,"unused161",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED162,     162,"unused162",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED163,     163,"unused163",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED164,     164,"unused164",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED165,     165,"unused165",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED166,     166,"unused166",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED167,     167,"unused167",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED168,     168,"unused168",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED169,     169,"unused169",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED170,     170,"unused170",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED171,     171,"unused171",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED172,     172,"unused172",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED173,     173,"unused173",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED174,     174,"unused174",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED175,     175,"unused175",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED176,     176,"unused176",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED177,     177,"unused177",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED178,     178,"unused178",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED179,     179,"unused179",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED180,     180,"unused180",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED181,     181,"unused181",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED182,     182,"unused182",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED183,     183,"unused183",  NULL,     1,  0,  0,  JOF_BYTE) \
    \
    /*
     * Pops the top of stack value, pushes property of it onto the stack.
     *
     * Like JSOP_GETPROP but for call context.
     *   Category: Literals
     *   Type: Object
     *   Operands: uint32_t nameIndex
     *   Stack: obj => obj[name]
     */ \
    macro(JSOP_CALLPROP,      184,"callprop",   NULL,     5,  1,  1, JOF_ATOM|JOF_PROP|JOF_TYPESET|JOF_TMPSLOT3) \
    \
    macro(JSOP_UNUSED185,     185,"unused185",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED186,     186,"unused186",  NULL,     1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED187,     187,"unused187",  NULL,     1,  0,  0,  JOF_BYTE) \
    \
    /*
     * Pushes unsigned 24-bit int immediate integer operand onto the stack.
     *   Category: Literals
     *   Type: Constants
     *   Operands: uint24_t val
     *   Stack: => val
     */ \
    macro(JSOP_UINT24,        188,"uint24",     NULL,     4,  0,  1, JOF_UINT24) \
    \
    macro(JSOP_UNUSED189,     189,"unused189",   NULL,    1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED190,     190,"unused190",   NULL,    1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED191,     191,"unused191",   NULL,    1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED192,     192,"unused192",   NULL,    1,  0,  0,  JOF_BYTE) \
    \
    /*
     * Pops the top two values on the stack as 'propval' and 'obj', pushes
     * 'propval' property of 'obj' onto the stack.
     *
     * Like JSOP_GETELEM but for call context.
     *   Category: Literals
     *   Type: Object
     *   Operands:
     *   Stack: obj, propval => obj[propval]
     */ \
    macro(JSOP_CALLELEM,      193, "callelem",   NULL,    1,  2,  1, JOF_BYTE |JOF_ELEM|JOF_TYPESET|JOF_LEFTASSOC) \
    \
    /*
     * '__proto__: v' inside an object initializer.
     *
     * Pops the top two values on the stack as 'newProto' and 'obj', sets
     * prototype of 'obj' as 'newProto', pushes 'true' onto the stack if
     * succeeded, 'false' if not.
     *   Category: Literals
     *   Type: Object
     *   Operands:
     *   Stack: obj, newProto => succeeded
     */ \
    macro(JSOP_MUTATEPROTO,   194, "mutateproto",NULL,    1,  2,  1, JOF_BYTE) \
    \
    /*
     * Pops the top of stack value, gets an extant property value of it,
     * throwing ReferenceError if the identified property does not exist.
     *   Category: Literals
     *   Type: Object
     *   Operands: uint32_t nameIndex
     *   Stack: obj => obj[name]
     */ \
    macro(JSOP_GETXPROP,      195,"getxprop",    NULL,    5,  1,  1, JOF_ATOM|JOF_PROP|JOF_TYPESET) \
    \
    macro(JSOP_UNUSED196,     196,"unused196",   NULL,    1,  0,  0, JOF_BYTE) \
    \
    /*
     * Pops the top stack value as 'val' and pushes 'typeof val'.  Note that
     * this opcode isn't used when, in the original source code, 'val' is a
     * name -- see 'JSOP_TYPEOF' for that.
     * (This is because 'typeof undefinedName === "undefined"'.)
     *   Category: Operator
     *   Type: Special Operators
     *   Operands:
     *   Stack: val => (typeof val)
     */ \
    macro(JSOP_TYPEOFEXPR,    197,"typeofexpr",  NULL,    1,  1,  1, JOF_BYTE|JOF_DETECTING) \
    \
    /* Block-local scope support. */ \
    /*
     * Pushes block onto the scope chain.
     *   Category: Variables and Scopes
     *   Type: Block-local Scope
     *   Operands: uint32_t staticBlockObjectIndex
     *   Stack: =>
     */ \
    macro(JSOP_PUSHBLOCKSCOPE,198,"pushblockscope", NULL, 5,  0,  0,  JOF_OBJECT) \
    /*
     * Pops block from the scope chain.
     *   Category: Variables and Scopes
     *   Type: Block-local Scope
     *   Operands:
     *   Stack: =>
     */ \
    macro(JSOP_POPBLOCKSCOPE, 199,"popblockscope", NULL,  1,  0,  0,  JOF_BYTE) \
    /*
     * The opcode to assist the debugger.
     *   Category: Statements
     *   Type: Debugger
     *   Operands:
     *   Stack: =>
     */ \
    macro(JSOP_DEBUGLEAVEBLOCK, 200,"debugleaveblock", NULL, 1,  0,  0,  JOF_BYTE) \
    \
    macro(JSOP_UNUSED201,     201,"unused201",  NULL,     1,  0,  0,  JOF_BYTE) \
    \
    /*
     * Initializes generator frame, creates a generator, sets 'YIELDING' flag,
     * stops interpretation and returns the generator.
     *   Category: Statements
     *   Type: Generator
     *   Operands:
     *   Stack: =>
     */ \
    macro(JSOP_GENERATOR,     202,"generator",   NULL,    1,  0,  0,  JOF_BYTE) \
    /*
     * Pops the top of stack value as 'rval1', sets 'YIELDING' flag,
     * stops interpretation and returns 'rval1', pushes sent value from
     * 'send()' onto the stack.
     *   Category: Statements
     *   Type: Generator
     *   Operands:
     *   Stack: rval1 => rval2
     */ \
    macro(JSOP_YIELD,         203,"yield",       NULL,    1,  1,  1,  JOF_BYTE) \
    /*
     * Pops the top two values on the stack as 'obj' and 'v', pushes 'v' to
     * 'obj'.
     *
     * This opcode is used for Array Comprehension.
     *   Category: Literals
     *   Type: Array
     *   Operands:
     *   Stack: v, obj =>
     */ \
    macro(JSOP_ARRAYPUSH,     204,"arraypush",   NULL,    1,  2,  0,  JOF_BYTE) \
    \
    macro(JSOP_UNUSED205,     205, "unused205",    NULL,  1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED206,     206, "unused206",    NULL,  1,  0,  0,  JOF_BYTE) \
    \
    macro(JSOP_UNUSED207,     207, "unused207",    NULL,  1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED208,     208, "unused208",    NULL,  1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED209,     209, "unused209",    NULL,  1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED210,     210, "unused210",    NULL,  1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED211,     211, "unused211",    NULL,  1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED212,     212, "unused212",    NULL,  1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED213,     213, "unused213",    NULL,  1,  0,  0,  JOF_BYTE) \
    /*
     * Pushes the global scope onto the stack.
     *
     * 'nameIndex' is not used.
     *   Category: Variables and Scopes
     *   Type: Free Variables
     *   Operands: uint32_t nameIndex
     *   Stack: => global
     */ \
    macro(JSOP_BINDGNAME,     214, "bindgname",    NULL,  5,  0,  1,  JOF_ATOM|JOF_NAME|JOF_SET|JOF_GNAME) \
    \
    /*
     * Pushes 8-bit int immediate integer operand onto the stack.
     *   Category: Literals
     *   Type: Constants
     *   Operands: int8_t val
     *   Stack: => val
     */ \
    macro(JSOP_INT8,          215, "int8",         NULL,  2,  0,  1, JOF_INT8) \
    /*
     * Pushes 32-bit int immediate integer operand onto the stack.
     *   Category: Literals
     *   Type: Constants
     *   Operands: int32_t val
     *   Stack: => val
     */ \
    macro(JSOP_INT32,         216, "int32",        NULL,  5,  0,  1, JOF_INT32) \
    \
    /*
     * Pops the top of stack value, pushes the 'length' property of it onto the
     * stack.
     *   Category: Literals
     *   Type: Array
     *   Operands: uint32_t nameIndex
     *   Stack: obj => obj['length']
     */ \
    macro(JSOP_LENGTH,        217, "length",       NULL,  5,  1,  1, JOF_ATOM|JOF_PROP|JOF_TYPESET|JOF_TMPSLOT3) \
    \
    /*
     * Pushes a JS_ELEMENTS_HOLE value onto the stack, representing an omitted
     * property in an array literal (e.g. property 0 in the array '[, 1]').
     *
     * This opcode is used with the JSOP_NEWARRAY opcode.
     *   Category: Literals
     *   Type: Array
     *   Operands:
     *   Stack: => hole
     */ \
    macro(JSOP_HOLE,          218, "hole",         NULL,  1,  0,  1,  JOF_BYTE) \
    \
    macro(JSOP_UNUSED219,     219,"unused219",     NULL,  1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED220,     220,"unused220",     NULL,  1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED221,     221,"unused221",     NULL,  1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED222,     222,"unused222",     NULL,  1,  0,  0,  JOF_BYTE) \
    macro(JSOP_UNUSED223,     223,"unused223",     NULL,  1,  0,  0,  JOF_BYTE) \
    \
    /*
     * Creates rest parameter array for current function call, and pushes it
     * onto the stack.
     *   Category: Variables and Scopes
     *   Type: Arguments
     *   Operands:
     *   Stack: => rest
     */ \
    macro(JSOP_REST,          224, "rest",         NULL,  1,  0,  1,  JOF_BYTE|JOF_TYPESET) \
    \
    /*
     * Pops the top of stack value, converts it into a jsid (int or string), and
     * pushes it onto the stack.
     *   Category: Literals
     *   Type: Object
     *   Operands:
     *   Stack: obj, id => obj, (jsid of id)
     */ \
    macro(JSOP_TOID,          225, "toid",         NULL,  1,  1,  1,  JOF_BYTE) \
    \
    /*
     * Pushes the implicit 'this' value for calls to the associated name onto
     * the stack.
     *   Category: Variables and Scopes
     *   Type: This
     *   Operands: uint32_t nameIndex
     *   Stack: => this
     */                                                                 \
    macro(JSOP_IMPLICITTHIS,  226, "implicitthis", "",    5,  0,  1,  JOF_ATOM) \
    \
    /*
     * This opcode is the target of the entry jump for some loop. The uint8
     * argument is a bitfield. The lower 7 bits of the argument indicate the
     * loop depth. This value starts at 1 and is just a hint: deeply nested
     * loops all have the same value.  The upper bit is set if Ion should be
     * able to OSR at this point, which is true unless there is non-loop state
     * on the stack.
     *   Category: Statements
     *   Type: Jumps
     *   Operands: uint8_t BITFIELD
     *   Stack: =>
     */ \
    macro(JSOP_LOOPENTRY,     227, "loopentry",    NULL,  2,  0,  0,  JOF_UINT8)

/*
 * In certain circumstances it may be useful to "pad out" the opcode space to
 * a power of two.  Use this macro to do so.
 */
#define FOR_EACH_TRAILING_UNUSED_OPCODE(macro) \
    macro(228) \
    macro(229) \
    macro(230) \
    macro(231) \
    macro(232) \
    macro(233) \
    macro(234) \
    macro(235) \
    macro(236) \
    macro(237) \
    macro(238) \
    macro(239) \
    macro(240) \
    macro(241) \
    macro(242) \
    macro(243) \
    macro(244) \
    macro(245) \
    macro(246) \
    macro(247) \
    macro(248) \
    macro(249) \
    macro(250) \
    macro(251) \
    macro(252) \
    macro(253) \
    macro(254) \
    macro(255)

namespace js {

// Sanity check that opcode values and trailing unused opcodes completely cover
// the [0, 256) range.  Avert your eyes!  You don't want to know how the
// sausage gets made.

#define VALUE_AND_VALUE_PLUS_ONE(op, val, ...) \
    val) && (val + 1 ==
#define TRAILING_VALUE_AND_VALUE_PLUS_ONE(val) \
    val) && (val + 1 ==
static_assert((0 ==
               FOR_EACH_OPCODE(VALUE_AND_VALUE_PLUS_ONE)
               FOR_EACH_TRAILING_UNUSED_OPCODE(TRAILING_VALUE_AND_VALUE_PLUS_ONE)
               256),
              "opcode values and trailing unused opcode values monotonically "
              "increase from zero to 255");
#undef TRAILING_VALUE_AND_VALUE_PLUS_ONE
#undef VALUE_AND_VALUE_PLUS_ONE

// Define JSOP_*_LENGTH constants for all ops.
#define DEFINE_LENGTH_CONSTANT(op, val, name, image, len, ...) \
    MOZ_CONSTEXPR_VAR size_t op##_LENGTH = len;
FOR_EACH_OPCODE(DEFINE_LENGTH_CONSTANT)
#undef DEFINE_LENGTH_CONSTANT

} // namespace js

#endif // vm_Opcodes_h
