#ifndef rain_chunk_h
#define rain_chunk_h

#include "common.h"
#include "value.h"

typedef enum
{
    OP_CONSTANT, // push a constant value onto the stack
    OP_NIL,      // push nil onto stack
    OP_TRUE,     // push true onto stack
    OP_FALSE,    // push false onto stack
    OP_POP,
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_JUMP_IF_FALSE,
    OP_LOOP,
    OP_JUMP,
    OP_DEFINE_GLOBAL,
    OP_SET_GLOBAL,
    OP_GET_GLOBAL,
    OP_GET_UPVALUE,
    OP_SET_UPVALUE,
    OP_CLOSE_UPVALUE,
    OP_GET_PROPERTY,
    OP_SET_PROPERTY,
    OP_GET_SUPER,
    OP_EQUAL,                  // pop two values, push true if they are equal
    OP_GREATER,                // pop two values, push true if left > right
    OP_LESS,                   // pop two values, push true if left < right
    OP_ADD,                    // pop two values, push sum
    OP_SUBTRACT,               // pop two values, push difference
    OP_MULTIPLY,               // pop two values, push product
    OP_DIVIDE,                 // pop two values, push quotient
    OP_NOT,                    // pop value, push logical not
    OP_NEGATE,                 // pop two values, push negated value
    OP_PUSH_EXCEPTION_HANDLER, // sets up catch block address
    OP_POP_EXCEPTION_HANDLER,  // removes handler when try succeeds
    OP_THROW,                  // throws an error value
    OP_PRINT,
    OP_CALL,
    OP_CLOSURE,
    OP_SUPER_INVOKE,
    OP_RETURN, // done , return from the current function
    OP_CLASS,
    OP_METHOD,
    OP_INVOKE,
    OP_INHERIT,
    OP_ARRAY_BUILD,
    OP_MAP_BUILD,
    OP_MAP_NEXT,
    OP_INDEX_GET,
    OP_INDEX_SET,
    OP_GET_PROPERTY_SAFE,
    OP_GET_MODULE
} OpCode;

typedef struct
{
    int count;            // how many bytes are currently used
    int capacity;         // how many bytes are allocated
    uint8_t *code;        // the actual byte array
    int *lines;           // source line number for each byte
    ValueArray constants; // the constant pool <- lives inside Chunk
} Chunk;

void initChunk(Chunk *chunk);                          // initialize a new empty chunk
void writeChunk(Chunk *chunk, uint8_t byte, int line); // append a byte to the chunk, grow if needed
void freeChunk(Chunk *chunk);                          // free all memory owned by the chunk
int addConstant(Chunk *chunk, Value value);            // adds constant to the pool

#endif
