/*
 * RocketVM
 * Copyright (c) 2011 Max McGuire
 *
 * See copyright notice in lua.h
 */
#include "Lexer.h"
#include "String.h"
#include "State.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static const char* tokenName[] =
    {
        // Reserved words.
        "and",
        "break",
        "do",
        "else",
        "elseif",
        "end",
        "false",
        "for",
        "function",
        "if",
        "in",
        "local",
        "nil",
        "not",
        "or",
        "repeat",
        "return",
        "then",
        "true",
        "until",
        "while",
        // Symbols.
        "..",
        "=",
        ">=",
        "<=",
        "~=",
        // Specials.
        "number",
        "name",
        "string",
        "end of stream",
    };

void Lexer_Initialize(Lexer* lexer, lua_State* L, Input* input)
{
    lexer->L                = L;
    lexer->input            = input;
    lexer->lineNumber       = 1;
    lexer->token.string     = NULL;
    lexer->haveToken        = false;
    lexer->numRestoreTokens = 0;
    Lexer_NextToken(lexer);
}

static inline bool Lexer_IsSpace(int c)
{
    return c == ' ' || c == '\t';
}

static inline bool Lexer_IsNewLine(int c)
{
    return c == '\n' || c == '\r';
}

static inline bool Lexer_IsDigit(int c)
{
    return c >= '0' && c <= '9';
}

static void Lexer_ReadComment(Lexer* lexer)
{
    int c;
    do
    {
        c = Input_ReadByte(lexer->input);
    }
    while (c != END_OF_STREAM && !Lexer_IsNewLine(c));
    ++lexer->lineNumber;
}

const char* Token_GetString(TokenType token)
{
    return tokenName[token - TokenType_First];
}

void Lexer_NextToken(Lexer* lexer)
{

    if (lexer->haveToken)
    {
        return;
    }

    if (lexer->numRestoreTokens > 0)
    {
        --lexer->numRestoreTokens;
        lexer->token     = lexer->restoreToken[lexer->numRestoreTokens];
        lexer->haveToken = true;
        return;
    }

    lexer->haveToken = true;

    while (1)
    {

        int c = Input_ReadByte(lexer->input);

        switch (c)
        {
        case '\n':
        case '\r':
            ++lexer->lineNumber;
            break;
        case ' ':
        case '\t':
            break;
        case END_OF_STREAM:
            lexer->token.type = TokenType_EndOfStream;
            return;
        case '~':
            if (Input_PeekByte(lexer->input) == '=')
            {
                Input_ReadByte(lexer->input);
                lexer->token.type = TokenType_Ne;
            }
            else
            {
                lexer->token.type = '~';
            }
            return;
        case '=':
            if (Input_PeekByte(lexer->input) == '=')
            {
                Input_ReadByte(lexer->input);
                lexer->token.type = TokenType_Eq;
            }
            else
            {
                lexer->token.type = '=';
            }
            return;
        case '<':
            if (Input_PeekByte(lexer->input) == '=')
            {
                Input_ReadByte(lexer->input);
                lexer->token.type = TokenType_Le;
            }
            else
            {
                lexer->token.type = '<';
            }
            return;
        case '>':
            if (Input_PeekByte(lexer->input) == '=')
            {
                Input_ReadByte(lexer->input);
                lexer->token.type = TokenType_Ge;
            }
            else
            {
                lexer->token.type = '>';
            }
            return;
        case '-':
            if (Input_PeekByte(lexer->input) != '-')
            {
                lexer->token.type = '-';
                return;
            }
            Lexer_ReadComment(lexer);
            break;
        case '+':
        case '*':
        case '/':
        case '(':
        case ')':
        case '[':
        case ']':
        case '{':
        case '}':
        case ',':
        case ':':
        case '#':
            lexer->token.type = c;
            return;
        case '.':
            if (Input_PeekByte(lexer->input) == '.')
            {
                Input_ReadByte(lexer->input);
                lexer->token.type = TokenType_Concat;
                return;
            }
            lexer->token.type = c;
            return;
        case '"':
        case '\'':
            {
                int end = c;
                // Read the string literal.
                char buffer[1024];
                size_t length = 0;
                while (length < 1024)
                {
                    c = Input_ReadByte(lexer->input);
                    if (c == '\n')
                    {
                        State_Error(lexer->L);
                    }
                    if (c == end)
                    {
                        break;
                    }
                    if (c == '\\')
                    {
                        // Handle escape sequences.
                        c = Input_ReadByte(lexer->input);
                        switch (c)
                        {
                        case 'a': c = '\a'; break;
                        case 'b': c = '\b'; break;
                        case 'f': c = '\f'; break;
                        case 'n': c = '\n'; break;
                        case 'r': c = '\r'; break;
                        case 't': c = '\t'; break;
                        case 'v': c = '\v'; break;
                        case '\\':  c = '\\'; break;
                        case '\"':  c = '\"'; break;
                        case '\'':  c = '\''; break;
                        default:
                            State_Error(lexer->L);
                        }
                    }
                    buffer[length] = c;
                    ++length;
                }
                lexer->token.type = TokenType_String;
                lexer->token.string = String_Create(lexer->L, buffer, length);
            }
            return;
        default:
            if (Lexer_IsDigit(c))
            {
                lexer->token.number = 0.0f;
                while (1)
                {
                    lua_Number digit = static_cast<lua_Number>(c - '0');
                    lexer->token.number = lexer->token.number * 10.0f + digit;
                    c = Input_PeekByte(lexer->input);
                    if (Lexer_IsDigit(c))
                    {
                        Input_ReadByte(lexer->input);
                    }
                    else
                    {
                        break;
                    }
                }
                lexer->token.type = TokenType_Number;
            }
            else
            {
                char buffer[LUA_MAXNAME];
                size_t bufferLength = 1;
                buffer[0] = c;
                while (bufferLength < LUA_MAXNAME)
                {
                    c = Input_PeekByte(lexer->input);
                    if (c < 128 && (isalpha(c) || c == '_' || isdigit(c)))
                    {
                        Input_ReadByte(lexer->input);
                        buffer[bufferLength] = c;
                        ++bufferLength;
                    }
                    else
                    {
                        break;
                    }
                }

                // Check to see if this is one of the reserved words.
                const int numReserved = TokenType_LastReserved - TokenType_First + 1;
                for (int i = 0; i < numReserved; ++i)
                {
                    size_t length = strlen(tokenName[i]);
                    if (length == bufferLength && strncmp(buffer, tokenName[i], bufferLength) == 0)
                    {
                        lexer->token.type = TokenType_And + i;
                        return;
                    }
                }

                lexer->token.string = String_Create(lexer->L, buffer, bufferLength);
                lexer->token.type = TokenType_Name;

            }
            return;
        }
    }

}

int Lexer_GetTokenType(Lexer* lexer)
{
    return lexer->token.type;
}

void Lexer_CaptureToken(Lexer* lexer, Token* token)
{
    *token = lexer->token;
}

void Lexer_RestoreTokens(Lexer* lexer, const Token token[], int numTokens)
{

    // If we have a token queued we need to store that as well since otherwise
    // it will be lost.
    if (lexer->haveToken)
    {
        assert( lexer->numRestoreTokens + 1 <= Lexer_maxRestoreTokens );
        lexer->restoreToken[ lexer->numRestoreTokens ] = lexer->token;
        ++lexer->numRestoreTokens;
        lexer->haveToken = false;
    }

    assert( lexer->numRestoreTokens + numTokens <= Lexer_maxRestoreTokens );
    memcpy( lexer->restoreToken + lexer->numRestoreTokens, token, numTokens * sizeof(Token) );
    lexer->numRestoreTokens += numTokens;    

}