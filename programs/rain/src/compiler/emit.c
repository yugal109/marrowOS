#include "compiler_internal.h"

Chunk *currentChunk(void)
{
    return &current->function->chunk;
}

void emitByte(uint8_t byte)
{
    writeChunk(currentChunk(), byte, parser.previous.line);
}

void emitBytes(uint8_t byte1, uint8_t byte2)
{
    emitByte(byte1);
    emitByte(byte2);
}

int emitJump(uint8_t instruction)
{
    emitByte(instruction);
    emitByte(0xff);
    emitByte(0xff);
    return currentChunk()->count - 2;
}

void emitReturn(void)
{
    if (current->type == TYPE_INITIALIZER)
    {
        emitBytes(OP_GET_LOCAL, 0);
    }
    else
    {
        emitByte(OP_NIL);
    }
    emitByte(OP_RETURN);
}

void emitLoop(int loopStart)
{
    emitByte(OP_LOOP);

    int offset = currentChunk()->count - loopStart + 2;
    if (offset > UINT16_MAX)
        error("Loop body too large.");

    emitByte((offset >> 8) & 0xff);
    emitByte(offset & 0xff);
}

void emitIndex(uint16_t index)
{
    emitByte((index >> 8) & 0xff);
    emitByte(index & 0xff);
}

uint16_t makeConstant(Value value)
{
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT16_MAX)
    {
        error("Too many constants in one chunk");
        return 0;
    }
    return (uint16_t)constant;
}

void emitConstant(Value value)
{
    emitByte(OP_CONSTANT);
    emitIndex(makeConstant(value));
}

void patchJump(int offset)
{
    int jump = currentChunk()->count - offset - 2;
    if (jump > UINT16_MAX)
    {
        error("too much code to jump over.");
    }

    currentChunk()->code[offset] = (jump >> 8) & 0xff;
    currentChunk()->code[offset + 1] = jump & 0xff;
}
